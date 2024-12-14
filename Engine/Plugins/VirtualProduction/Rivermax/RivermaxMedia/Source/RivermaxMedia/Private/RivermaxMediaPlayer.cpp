// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxMediaPlayer.h"

#include "Engine/Engine.h"
#include "IMediaEventSink.h"
#include "IRivermaxCoreModule.h"
#include "IRivermaxManager.h"
#include "MediaIOCoreEncodeTime.h"
#include "MediaIOCoreFileWriter.h"
#include "MediaIOCoreSamples.h"
#include "Misc/ScopeLock.h"
#include "RenderCommandFence.h"
#include "RenderGraphUtils.h"
#include "RivermaxMediaLog.h"
#include "RivermaxMediaOutput.h"
#include "RivermaxMediaSourceOptions.h"
#include "RivermaxMediaTextureSample.h"
#include "RivermaxMediaUtils.h"
#include "RivermaxPTPUtils.h"
#include "RivermaxShaders.h"
#include "RivermaxTracingUtils.h"
#include "RivermaxTimecodeProvider.h"
#include "RivermaxTypes.h"
#include "Stats/Stats2.h"
#include "Tasks/Task.h"

#if WITH_EDITOR
#include "EngineAnalytics.h"
#endif


#define LOCTEXT_NAMESPACE "FRivermaxMediaPlayer"

// Sample number with frame delay taken into account
#define TO_EXPECTED_SAMPLE_FRAME_NUM(CURRENTFrameNum) GetFrameNumberWithAcountedLatency(FrameDelay, CURRENTFrameNum)

// Sample index with frame delay taken into account
#define TO_EXPECTED_SAMPLE_INDEX(CURRENTFrameNum) ConvertFrameNumToSampleIndex(FrameDelay, CURRENTFrameNum, kMaxNumVideoFrameBuffer)

// Sample index to access sample in framelocked array
#define FRAME_NUM_TO_INDEX(CURRENTFrameNum) (CURRENTFrameNum) % kMaxNumVideoFrameBuffer

// Identifies if the player is in framelocking mode.
#define IS_FRAMELOCKED() (EvaluationType == EMediaIOSampleEvaluationType::Timecode && bFramelock)

DECLARE_GPU_STAT_NAMED(RivermaxMedia_SampleUsageFence, TEXT("RivermaxMedia_SampleUsageFence"));
DECLARE_GPU_STAT_NAMED(Rmax_WaitForPixels, TEXT("Rmax_WaitForPixels"));
DECLARE_GPU_STAT(RivermaxSource_SampleConversion);

namespace UE::RivermaxMedia
{
	static TAutoConsoleVariable<int32> CVarRivermaxForcedFramelockLatency(
		TEXT("Rivermax.Player.Latency"),
		-1,
		TEXT("Override latency in framelock mode. 0 for 0 frame of latency and 1 for 1 frame of latency."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarRivermaxDefaultThreadBlockTimeoutSec(
		TEXT("Rivermax.Player.DefaultThreadBlockTimeoutSec"),
		0.5,
		TEXT("Overrides the wait time for the samples to be written to. In seconds. "),
		ECVF_Default);

	uint64 GetFrameNumberWithAcountedLatency(int64 InFrameDelay, uint64 FrameNumber)
	{
		const int64 ForcedLatency = CVarRivermaxForcedFramelockLatency.GetValueOnAnyThread();
		return (FrameNumber - (uint64)FMath::Clamp(InFrameDelay + ForcedLatency, 0, 1));
	}

	uint8 ConvertFrameNumToSampleIndex(int64 InFrameDelay, uint64 FrameNumber, uint8 InMaxNumVideoFrameBuffer)
	{
		return (GetFrameNumberWithAcountedLatency(InFrameDelay, FrameNumber)) % InMaxNumVideoFrameBuffer;
	}

	/** Returns current time. Adjusted to UTC and rolled over at 24 hours. */
	FTimespan GetCurrentPTPTimeOfDay()
	{
		FTimespan CurrentTimespan;

		const int64 NumberOfTicksPerDay = 60 * 60 * 24 * ETimespan::TicksPerSecond;

		IRivermaxCoreModule* RivermaxModule = FModuleManager::GetModulePtr<IRivermaxCoreModule>("RivermaxCore");
		if (RivermaxModule && RivermaxModule->GetRivermaxManager())
		{
			// Converting from nanoseconds to ticks.
			CurrentTimespan = FTimespan(RivermaxModule->GetRivermaxManager()->GetTime() / ETimespan::NanosecondsPerTick);
			UTimecodeProvider* Provider = GEngine->GetTimecodeProvider();
			
			// Convert from TAI PTP Time to UTC
			if (Provider && Provider->GetName().Contains("RivermaxTimecodeProvider"))
			{
				URivermaxTimecodeProvider* RmaxTimecodeProvider = static_cast<URivermaxTimecodeProvider*>(Provider);
				CurrentTimespan -= FTimespan(0, 0, RmaxTimecodeProvider->UTCSecondsOffset);
			}
			else
			{
				UE_CALL_ONCE([&] { UE_LOG(LogRivermaxMedia, Warning, TEXT("Rivermax Timecode provider is required for accurate playback.")); });
			}

			// Rollover 24 hours.
			CurrentTimespan = FTimespan(CurrentTimespan.GetTicks() % NumberOfTicksPerDay);
		}

		return CurrentTimespan;
	}
	/* FRivermaxVideoPlayer structors
	 *****************************************************************************/

	FRivermaxMediaPlayer::FRivermaxMediaPlayer(IMediaEventSink& InEventSink)
		: Super(InEventSink)
		, RivermaxThreadNewState(EMediaState::Closed)
		, VideoTextureSamplePool(MakeUnique<FRivermaxMediaTextureSamplePool>())
	{
	}

	FRivermaxMediaPlayer::~FRivermaxMediaPlayer()
	{
		Close();
	}

	/* IMediaPlayer interface
	 *****************************************************************************/

	 /**
	  * @EventName MediaFramework.RivermaxSourceOpened
	  * @Trigger Triggered when an Rivermax media source is opened through a media player.
	  * @Type Client
	  * @Owner MediaIO Team
	  */
	bool FRivermaxMediaPlayer::Open(const FString& Url, const IMediaOptions* Options)
	{
		if (!Super::Open(Url, Options))
		{
			RivermaxThreadNewState = EMediaState::Error;
			return false;
		}

		//Video related options
		{
			DesiredPixelFormat = (ERivermaxMediaSourcePixelFormat)Options->GetMediaOption(RivermaxMediaOption::PixelFormat, (int64)ERivermaxMediaSourcePixelFormat::RGB_8bit);
			const bool bOverrideResolution = Options->GetMediaOption(RivermaxMediaOption::OverrideResolution, false);
			bFollowsStreamResolution = !bOverrideResolution;
		}

		IRivermaxCoreModule* Module = FModuleManager::GetModulePtr<IRivermaxCoreModule>("RivermaxCore");
		if (Module && ConfigureStream(Options))
		{
			InputStream = Module->CreateInputStream();
		}

		// If we are not following the stream resolution, make it the video track format and then reset to go through a format change once
		if (!bFollowsStreamResolution)
		{
			StreamResolution = StreamOptions.EnforcedResolution;
		}

		VideoTrackFormat.Dim = FIntPoint::ZeroValue;

		CurrentState = EMediaState::Preparing;
		RivermaxThreadNewState = EMediaState::Preparing;

		if (InputStream == nullptr || !InputStream->Initialize(StreamOptions, *this))
		{
			UE_LOG(LogRivermaxMedia, Warning, TEXT("Failed to initialize Rivermax input stream."));
			RivermaxThreadNewState = EMediaState::Error;
			InputStream.Reset();
			return false;
		}

		// Setup our different supported channels based on source settings
		SetupSampleChannels();

#if WITH_EDITOR
		if (FEngineAnalytics::IsAvailable())
		{
			TArray<FAnalyticsEventAttribute> EventAttributes;

			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("ResolutionWidth"), FString::Printf(TEXT("%d"), VideoTrackFormat.Dim.X)));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("ResolutionHeight"), FString::Printf(TEXT("%d"), VideoTrackFormat.Dim.Y)));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("FrameRate"), *VideoFrameRate.ToPrettyText().ToString()));

			FEngineAnalytics::GetProvider().RecordEvent(TEXT("MediaFramework.RivermaxSourceOpened"), EventAttributes);
		}
#endif

		return true;
	}

	void FRivermaxMediaPlayer::Close()
	{
		RivermaxThreadNewState = EMediaState::Closed;

		WaitForPendingTasks();

		if (InputStream)
		{
			InputStream->Uninitialize(); // this may block, until the completion of a callback from IRivermaxChannelCallbackInterface
			InputStream.Reset();
		}

		Samples->FlushSamples();
		VideoTextureSamplePool.Reset();

		Super::Close();
	}

	FGuid FRivermaxMediaPlayer::GetPlayerPluginGUID() const
	{
		static FGuid PlayerPluginGUID(0xF537595A, 0x8E8D452B, 0xB8C05707, 0x6B334234);
		return PlayerPluginGUID;
	}


#if WITH_EDITOR
	const FSlateBrush* FRivermaxMediaPlayer::GetDisplayIcon() const
	{
		//todo for tdm
		return nullptr;
	}
#endif //WITH_EDITOR

	TSharedPtr<IRivermaxVideoSample> FRivermaxMediaPlayer::OnVideoFrameRequested(const FRivermaxInputVideoFrameDescriptor& FrameInfo)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Rmax::FrameRequested %u"), (FrameInfo.FrameNumber)));

		// If video is not playing, no need to provide samples when requested
		if (!IsReadyToPlay())
		{
			return nullptr;
		}

		if (FrameInfo.VideoBufferSize > 0)
		{
			TSharedPtr<FRivermaxMediaTextureSample> Sample;
			if (IS_FRAMELOCKED())
			{
				// If input stream has somehow completely lost a frame (not even a single packet received) and we are 2 frames ahead of rendered frame.
				if (FrameInfo.FrameNumber > LastFrameToAttemptReception + 1)
				{
					//Invalidate previous samples in case one of them is still being rendered. 
					for (uint64 PreviousFrameNumber = LastFrameToAttemptReception; PreviousFrameNumber < FrameInfo.FrameNumber; PreviousFrameNumber++)
					{
						TSharedPtr<FRivermaxMediaTextureSample> PreviousSample = FrameLockedSamples[FRAME_NUM_TO_INDEX(PreviousFrameNumber)];
						if (PreviousSample->IsBeingRendered())
						{
							// This will stop the waiting thread from waiting for the start of the reception. 
							PreviousSample->SetFrameNumber(PreviousFrameNumber);

							// this will stop the waiting thread from copying invalid data and waiting for the copy.
							PreviousSample->SetReceptionState(IRivermaxVideoSample::ESampleState::ReceptionError);

							// this will stop the waiting thread from waiting for the sample reception.
							PreviousSample->GetSampleReceivedEvent()->Trigger();
						}
					}
				}

				Sample = FrameLockedSamples[FRAME_NUM_TO_INDEX(FrameInfo.FrameNumber)];
				Sample->GetSampleReceivedEvent()->Reset();

			}
			else
			{
				Sample = VideoTextureSamplePool->AcquireShared(false /*NoAllocation*/);
			}

			// (More of a sanity check. Shouldn't be in this state.).
			if (!Sample.IsValid())
			{
				UE_LOG(LogRivermaxMedia, Warning, TEXT("Failed to provide a frame for incoming frame %u with timestamp %u"), FrameInfo.FrameNumber, FrameInfo.Timestamp);
				return nullptr;
			}

			UE_LOG(LogRivermaxMedia, Verbose, TEXT("Starting to receive frame '%u' with timestamp %u"), FrameInfo.FrameNumber, FrameInfo.Timestamp);

			// Until PTP Timecode is available sample records frame reception start time for sample picking.
			Sample->FrameReceptionStart = GetCurrentPTPTimeOfDay();

			// the following will be restored once we have true PTP timecode support.
			//const double MediaFrameTimeSecs = UE::RivermaxCore::ConvertRTPTimeStampToSeconds(FrameInfo.Timestamp);
			//Sample->SetTime(FTimespan::FromSeconds(MediaFrameTimeSecs));
			Sample->SetFrameNumber(FrameInfo.FrameNumber);
			Sample->SetReceptionState(IRivermaxVideoSample::ESampleState::ReadyForReception);
			LastFrameToAttemptReception = FrameInfo.FrameNumber;
			return Sample;
		}

		return nullptr;
	}


	void FRivermaxMediaPlayer::OnVideoFrameReceived(TSharedPtr<IRivermaxVideoSample> InReceivedVideoFrame)
	{
		if (!IsReadyToPlay())
		{
			return;
		}

		TSharedPtr<FRivermaxMediaTextureSample> Sample = StaticCastSharedPtr<FRivermaxMediaTextureSample>(InReceivedVideoFrame);
		check(Sample.IsValid());

		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Rmax::OnVideoFrameReceived %u"), (Sample->GetFrameNumber())));
		if (!IS_FRAMELOCKED())
		{
			Samples->AddVideo(Sample.ToSharedRef());
		}
		else
		{
			Sample->GetSampleReceivedEvent()->Trigger();
		}

		// Until PTP Timecode is available sample records frame reception start time for sample picking.
		Sample->FrameReceptionEnd = GetCurrentPTPTimeOfDay();
		
	}

	void FRivermaxMediaPlayer::TickInput(FTimespan DeltaTime, FTimespan Timecode)
	{
		// update player state
		EMediaState NewState = RivermaxThreadNewState;

		if (NewState != CurrentState)
		{
			CurrentState = NewState;
			if (CurrentState == EMediaState::Playing)
			{
				EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);
				EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpened);
				EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackResumed);
			}
			else if (NewState == EMediaState::Error)
			{
				EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
				Close();
			}
		}

		if (CurrentState != EMediaState::Playing)
		{
			return;
		}

		// Cache current stream detection, it could change while we are applying it
		FIntPoint CachedStreamResolution;
		{
			FScopeLock Lock(&StreamResolutionCriticalSection);
			CachedStreamResolution = StreamResolution;
		}

		if (VideoTrackFormat.Dim != CachedStreamResolution)
		{
			UE_LOG(LogRivermaxMedia, Log, TEXT("Player needs to apply newly detected stream resolution : %dx%d"), CachedStreamResolution.X, CachedStreamResolution.Y);

			{
				WaitForPendingTasks();

				AllocateBuffers(CachedStreamResolution);

				VideoTrackFormat.Dim = CachedStreamResolution;
			}

		}

		TickTimeManagement();
	}

	/* FRivermaxMediaPlayer implementation
	 *****************************************************************************/
	bool FRivermaxMediaPlayer::IsHardwareReady() const
	{
		return (RivermaxThreadNewState == EMediaState::Playing) || (RivermaxThreadNewState == EMediaState::Paused);
	}

	void FRivermaxMediaPlayer::SetupSampleChannels()
	{
		FMediaIOSamplingSettings VideoSettings = BaseSettings;
		VideoSettings.BufferSize = kMaxNumVideoFrameBuffer;

		// TODO: Initialize Audio, Anc buffers
		Samples->InitializeVideoBuffer(VideoSettings);
	}

	TSharedPtr<FMediaIOCoreTextureSampleConverter> FRivermaxMediaPlayer::CreateTextureSampleConverter() const
	{
		return MakeShared<FRivermaxMediaTextureSampleConverter>();
	}

	TSharedPtr<FMediaIOCoreTextureSampleBase> FRivermaxMediaPlayer::AcquireTextureSample_AnyThread() const
	{

		if ((Samples->NumVideoSamples() > 0 || IsJustInTimeRenderingEnabled()) && ProxySampleDummy.IsValid())
		{
			FScopeLock Lock(&ProxySampleAccessCriticalSection);
			// Create a copy of the proxy sample as Media Texture uses raw pointers for converters on Render thread and converter is set on game thread.
			TSharedPtr<FRivermaxMediaTextureSample> SampleToReturn = MakeShared<FRivermaxMediaTextureSample>();
			SampleToReturn->CopyConfiguration(ProxySampleDummy);
			return SampleToReturn;
		}
		else
		{
			return nullptr;
		}
	}

	void FRivermaxMediaPlayer::OnInitializationCompleted(const FRivermaxInputInitializationResult& Result)
	{
		RivermaxThreadNewState = Result.bHasSucceed ? EMediaState::Playing : EMediaState::Error;
		bStreamSupportsGPUDirect = Result.bIsGPUDirectSupported;
	}

	bool FRivermaxMediaPlayer::ConfigureStream(const IMediaOptions* Options)
	{
		using namespace UE::RivermaxCore;

		// Resolve interface address
		IRivermaxCoreModule* Module = FModuleManager::GetModulePtr<IRivermaxCoreModule>("RivermaxCore");
		if (Module == nullptr)
		{
			return false;
		}

		const FString DesiredInterface = Options->GetMediaOption(RivermaxMediaOption::InterfaceAddress, FString());
		const bool bFoundDevice = Module->GetRivermaxManager()->GetMatchingDevice(DesiredInterface, StreamOptions.InterfaceAddress);
		if (bFoundDevice == false)
		{
			UE_LOG(LogRivermaxMedia, Error, TEXT("Could not find a matching interface for IP '%s'"), *DesiredInterface);
			return false;
		}

		StreamOptions.StreamAddress = Options->GetMediaOption(RivermaxMediaOption::StreamAddress, FString());
		StreamOptions.Port = Options->GetMediaOption(RivermaxMediaOption::Port, (int64)0);
		StreamOptions.bUseGPUDirect = Options->GetMediaOption(RivermaxMediaOption::UseGPUDirect, false);
		StreamOptions.FrameRate = VideoFrameRate;
		StreamOptions.PixelFormat = UE::RivermaxMediaUtils::Private::MediaSourcePixelFormatToRivermaxSamplingType(DesiredPixelFormat);
		const FVideoFormatInfo FormatInfo = FStandardVideoFormat::GetVideoFormatInfo(StreamOptions.PixelFormat);
		const uint32 PixelAlignment = FormatInfo.PixelGroupCoverage;
		const uint32 AlignedHorizontalResolution = (VideoTrackFormat.Dim.X % PixelAlignment) ? VideoTrackFormat.Dim.X + (PixelAlignment - (VideoTrackFormat.Dim.X % PixelAlignment)) : VideoTrackFormat.Dim.X;
		StreamOptions.EnforcedResolution = FIntPoint(AlignedHorizontalResolution, VideoTrackFormat.Dim.Y);
		StreamOptions.bEnforceVideoFormat = !bFollowsStreamResolution;

		return true;
	}

	void FRivermaxMediaPlayer::AllocateBuffers(const FIntPoint& InResolution)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FRivermaxMediaPlayer::AllocateBuffers);
		using namespace UE::RivermaxCore;
		using namespace UE::RivermaxMediaUtils::Private;

		if (VideoTextureSamplePool.IsValid())
		{
			VideoTextureSamplePool->Reset();
		}

		VideoTextureSamplePool = MakeUnique<FRivermaxMediaTextureSamplePool>();
		Samples->FlushSamples();

		// Only need to store latest frame and the number of frames delayed by.
		Samples->SetSampleBufferSize(FrameDelay + 1);


		TSharedPtr<FRivermaxMediaTextureSample> NewSample;
		// Allocate our pool of samples where incoming ones will be written and chosen from
		for (int32 Index = 0; Index < kMaxNumVideoFrameBuffer; Index++)
		{
			NewSample = VideoTextureSamplePool->AcquireShared(true /*Allocate New*/);
			NewSample->InitializeGPUBuffer(InResolution, DesiredPixelFormat, bStreamSupportsGPUDirect);
			NewSample->SampleConversionFence = RHICreateGPUFence(*FString::Printf(TEXT("RmaxConversionDoneFence_%02d"), Index));
			NewSample->SetReceptionState(IRivermaxSample::ESampleState::ReadyForReception);
			NewSample->SetInputFormat(DesiredPixelFormat);
			if (IS_FRAMELOCKED())
			{
				FrameLockedSamples[Index] = NewSample;
			}
		}

		// Create the proxy sample that is going to be used for color conversion.
		ENQUEUE_RENDER_COMMAND(FRivermaxMediaTextureSample)(
			[NewSample, Resolution = InResolution, this](FRHICommandListImmediate& RHICmdList)
			{
				FScopeLock Lock(&ProxySampleAccessCriticalSection);

				ProxySampleDummy = MakeShared<FRivermaxMediaTextureSample>();
				ProxySampleDummy->SetTexture(CreateIntermediateRenderTarget(RHICmdList, Resolution, NewSample->GetPixelFormat(), NewSample->IsOutputSrgb()));
				UE::MediaIOCore::FColorFormatArgs ColorArgs;
				ColorArgs.ColorSpaceType = bOverrideSourceColorSpace ? (UE::Color::EColorSpace)OverrideSourceColorSpace : UE::Color::EColorSpace::None;
				ColorArgs.Encoding = bOverrideSourceEncoding ? (UE::Color::EEncoding)OverrideSourceEncoding : UE::Color::EEncoding::None;

				ProxySampleDummy->SetProperties(NewSample->GetStride(), VideoTrackFormat.Dim.X, VideoTrackFormat.Dim.X, NewSample->GetFormat(), FTimespan(0), FFrameRate(), FTimecode(), ColorArgs);
			});


		// Allocation is done on render thread so let's make sure it's completed before pursuing
		FRenderCommandFence RenderFence;
		RenderFence.BeginFence();
		RenderFence.Wait();
		VideoTextureSamplePool->Tick();

	}

	void FRivermaxMediaPlayer::OnStreamError()
	{
		// If the stream ends up in error, stop the player
		UE_LOG(LogRivermaxMedia, Error, TEXT("Stream caught an error. Player will stop."));
		RivermaxThreadNewState = EMediaState::Error;
	}

	TRefCountPtr<FRHITexture> FRivermaxMediaPlayer::CreateIntermediateRenderTarget(FRHICommandListImmediate& RHICmdList, const FIntPoint& InDim, EPixelFormat InPixelFormat, bool bInSRGB)
	{
		TRefCountPtr<FRHITexture> TextureToReturn;
		// create output render target if necessary
		ETextureCreateFlags OutputCreateFlags = TexCreate_Dynamic | (bInSRGB ? TexCreate_SRGB : TexCreate_None) | TexCreate_UAV;
		OutputCreateFlags |= TexCreate_UAV;
		OutputCreateFlags |= ETextureCreateFlags::RenderTargetable;


		const static FLazyName ClassName(TEXT("FRivermaxMediaTextureSample"));
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create2D(TEXT("FRivermaxMediaTextureOutput"))
			.SetExtent(InDim)
			.SetFormat(InPixelFormat)
			//.SetNumMips(1)
			.SetFlags(OutputCreateFlags | ETextureCreateFlags::ShaderResource)
			.SetInitialState(ERHIAccess::SRVMask)
			.SetClearValue(FClearValueBinding(FLinearColor::Red))
			.SetClassName(ClassName)
			.SetOwnerName(*GetMediaName().ToString());

		TextureToReturn = RHICmdList.CreateTexture(Desc);

		TextureToReturn->SetName(TEXT("RivermaxMediaTexture"));
		TextureToReturn->SetOwnerName(*GetMediaName().ToString());
		return MoveTemp(TextureToReturn);
	}

	bool FRivermaxMediaPlayer::JustInTimeSampleRender_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRHIRef& InDestinationTexture, TSharedPtr<FMediaIOCoreTextureSampleBase>& InJITRProxySample)
	{
		// Player renders into an intermediate render target that is then used to be converted into the right color encoding if needed.
		InDestinationTexture = ProxySampleDummy->GetTexture();
		TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxPlayerLateUpdate);
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Rmax::StartingRender %u"), (GFrameCounterRenderThread)));

		// Sanity check to make sure that we are not rendering this sample twice per frame.
		check(LastEngineRTFrameThatUpdatedJustInTime != GFrameCounterRenderThread);
		LastEngineRTFrameThatUpdatedJustInTime = GFrameCounterRenderThread;

		FFrameInfo FrameInformation;
		FrameInformation.RequestedTimecode = InJITRProxySample->GetTimecode().Get(FTimecode());
		FrameInformation.SampleTimespan = InJITRProxySample->GetTime().Time;
		FrameInformation.EvaluationOffset = InJITRProxySample->GetEvaluationOffsetInSeconds();
		FrameInformation.FrameNumber = GFrameCounterRenderThread;

		TSharedPtr<FRivermaxMediaTextureSample> SamplePtr = StaticCastSharedPtr<FRivermaxMediaTextureSample>(PickSampleToRender_RenderThread(FrameInformation));
		if (!SamplePtr.IsValid())
		{
			UE_LOG(LogRivermaxMedia, Verbose, TEXT("Couldn't find a sample to render for frame %u."), FrameInformation.FrameNumber);
			return false;
		}

		if (!InDestinationTexture.IsValid())
		{
			UE_LOG(LogRivermaxMedia, Warning, TEXT("Couldn't find texture to render into for sample %u."), SamplePtr->GetFrameNumber());
			return false;
		}

		// Verify if the frame we will use for rendering is still being rendered for the previous one.
		if (!SamplePtr->TryLockForRendering())
		{
			if (IS_FRAMELOCKED())
			{
				UE_LOG(LogRivermaxMedia, Warning, TEXT("Framelocked sample %u was still rendering when we expected to reuse its location."), SamplePtr->GetFrameNumber());
				return false;
			}
			else
			{
				UE_LOG(LogRivermaxMedia, Verbose, TEXT("Sample %u was either already rendered or is already being rendered."), SamplePtr->GetFrameNumber());
				return false;
			}
		}

		FSampleConverterOperationSetup ConverterSetup;
		SampleUploadSetupTaskThreadMode(SamplePtr, ConverterSetup);

		// If no input data was provided, no need to render
		if (ConverterSetup.GetGPUBufferFunc == nullptr && ConverterSetup.GetSystemBufferFunc == nullptr)
		{
			ensureMsgf(false, TEXT("Rivermax player late update succeeded but didn't provide any source data."));
			return false;
		}

		FRDGBuilder GraphBuilder((FRHICommandListExecutor::GetImmediateCommandList()));
		if (ConverterSetup.PreConvertFunc)
		{
			ConverterSetup.PreConvertFunc(GraphBuilder);
		}

		using namespace UE::RivermaxShaders;
		using namespace UE::RivermaxMediaUtils::Private;

		const FSourceBufferDesc SourceBufferDesc = GetBufferDescription(VideoTrackFormat.Dim, SamplePtr->GetInputFormat());
		{
			FRDGBufferRef InputBuffer;

			RDG_GPU_STAT_SCOPE(GraphBuilder, RivermaxSource_SampleConversion);
			SCOPED_DRAW_EVENT(GraphBuilder.RHICmdList, Rivermax_SampleConverter);

			FRDGTextureRef OutputResource = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(InDestinationTexture, TEXT("RivermaxMediaTextureOutputResource")));

			// If we have a valid GPUBuffer, i.e GPUDirect is involved, use that one. Otherwise, take the system buffer and upload it in a new structured buffer.
			if (ConverterSetup.GetGPUBufferFunc)
			{
				InputBuffer = GraphBuilder.RegisterExternalBuffer(ConverterSetup.GetGPUBufferFunc(), TEXT("RMaxGPUBuffer"));
			}
			else if (ConverterSetup.GetSystemBufferFunc)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::CreateStructuredBuffer);
				FRDGBufferNumElementsCallback NumElementCallback = [NumElements = SourceBufferDesc.NumberOfElements]()
					{
						return NumElements;
					};

				FRDGBufferInitialDataCallback GetDataCallback = ConverterSetup.GetSystemBufferFunc;
				FRDGBufferInitialDataSizeCallback TotalSizeCallback = [TotalSize = SourceBufferDesc.BytesPerElement * SourceBufferDesc.NumberOfElements]()
					{
						return TotalSize;
					};

				InputBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("RivermaxInputBuffer"), SourceBufferDesc.BytesPerElement, MoveTemp(NumElementCallback), MoveTemp(GetDataCallback), MoveTemp(TotalSizeCallback));
			}
			else
			{
				return false;
			}

			const FIntPoint ProcessedOutputDimension = { (int32)SourceBufferDesc.NumberOfElements, 1 };
			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(SourceBufferDesc.NumberOfElements, 64);
			FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

			//Configure shader and add conversion pass based on desired pixel format
			switch (SamplePtr->GetInputFormat())
			{
			case ERivermaxMediaSourcePixelFormat::YUV422_8bit:
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::YUV8ShaderSetup);

				const FMatrix YUVToRGBMatrix = SamplePtr->GetYUVToRGBMatrix();
				const FVector YUVOffset(MediaShaders::YUVOffset8bits);
				TShaderMapRef<FYUV8Bit422ToRGBACS> ComputeShader(GlobalShaderMap);
				FYUV8Bit422ToRGBACS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, YUVToRGBMatrix, YUVOffset, ProcessedOutputDimension.X, ProcessedOutputDimension.Y);

				FComputeShaderUtils::AddPass(
					GraphBuilder
					, RDG_EVENT_NAME("YUV8Bit422ToRGBA")
					, ComputeShader
					, Parameters
					, GroupCount);
				break;
			}
			case ERivermaxMediaSourcePixelFormat::YUV422_10bit:
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::YUV10ShaderSetup);

				const FMatrix YUVToRGBMatrix = SamplePtr->GetYUVToRGBMatrix();
				const FVector YUVOffset(MediaShaders::YUVOffset10bits);
				TShaderMapRef<FYUV10Bit422ToRGBACS> ComputeShader(GlobalShaderMap);
				FYUV10Bit422ToRGBACS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, YUVToRGBMatrix, YUVOffset, ProcessedOutputDimension.X, ProcessedOutputDimension.Y);

				FComputeShaderUtils::AddPass(
					GraphBuilder
					, RDG_EVENT_NAME("YUV10Bit422ToRGBA")
					, ComputeShader
					, Parameters
					, GroupCount);
				break;
			}
			case ERivermaxMediaSourcePixelFormat::RGB_8bit:
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::RGB8ShaderSetup);

				TShaderMapRef<FRGB8BitToRGBA8CS> ComputeShader(GlobalShaderMap);
				FRGB8BitToRGBA8CS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, ProcessedOutputDimension.X, ProcessedOutputDimension.Y);

				FComputeShaderUtils::AddPass(
					GraphBuilder
					, RDG_EVENT_NAME("RGB8BitToRGBA8")
					, ComputeShader
					, Parameters
					, GroupCount);
				break;
			}
			case ERivermaxMediaSourcePixelFormat::RGB_10bit:
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::RGB10ShaderSetup);

				TShaderMapRef<FRGB10BitToRGBA10CS> ComputeShader(GlobalShaderMap);
				FRGB10BitToRGBA10CS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, ProcessedOutputDimension.X, ProcessedOutputDimension.Y);

				FComputeShaderUtils::AddPass(
					GraphBuilder
					, RDG_EVENT_NAME("RGB10BitToRGBA")
					, ComputeShader
					, Parameters
					, GroupCount);
				break;
			}
			case ERivermaxMediaSourcePixelFormat::RGB_12bit:
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::RGB12ShaderSetup);

				TShaderMapRef<FRGB12BitToRGBA12CS> ComputeShader(GlobalShaderMap);
				FRGB12BitToRGBA12CS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, ProcessedOutputDimension.X, ProcessedOutputDimension.Y);

				FComputeShaderUtils::AddPass(
					GraphBuilder
					, RDG_EVENT_NAME("RGB12BitToRGBA")
					, ComputeShader
					, Parameters
					, GroupCount);
				break;
			}
			case ERivermaxMediaSourcePixelFormat::RGB_16bit_Float:
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::RGB16FloatShaderSetup);

				TShaderMapRef<FRGB16fBitToRGBA16fCS> ComputeShader(GlobalShaderMap);
				FRGB16fBitToRGBA16fCS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, ProcessedOutputDimension.X, ProcessedOutputDimension.Y);

				FComputeShaderUtils::AddPass(
					GraphBuilder
					, RDG_EVENT_NAME("RGB16fBitToRGBA")
					, ComputeShader
					, Parameters
					, GroupCount);
				break;
			}
			default:
			{
				ensureMsgf(false, TEXT("Unhandled pixel format (%d) given to Rivermax MediaSample converter"), SamplePtr->GetInputFormat());
				return false;
			}
			}
		}

		if (ConverterSetup.PostConvertFunc)
		{
			ConverterSetup.PostConvertFunc(GraphBuilder);
		}

		GraphBuilder.Execute();
		return true;
	}

	TSharedPtr<FMediaIOCoreTextureSampleBase> FRivermaxMediaPlayer::PickSampleToRenderFramelocked_RenderThread
	(const FFrameInfo& InFrameInformation)
	{
		TSharedPtr<FRivermaxMediaTextureSample> Sample = FrameLockedSamples[TO_EXPECTED_SAMPLE_INDEX(InFrameInformation.FrameNumber)];

		return Sample;
	}

	TSharedPtr<FMediaIOCoreTextureSampleBase> FRivermaxMediaPlayer::PickSampleToRenderForTimeSynchronized_RenderThread(const FFrameInfo& InFrameInformation)
	{
		// Reference time based on evaluation type
		FTimespan TargetSampleTimespan;

		// Get base uncorrected reference point

		if (EvaluationType == EMediaIOSampleEvaluationType::Timecode && InFrameInformation.RequestedTimecode != FTimecode())
		{
			// We'll use timecode data to find a proper sample
			TargetSampleTimespan = InFrameInformation.RequestedTimecode.ToTimespan(VideoFrameRate);
		}
		else
		{
			// We'll use platform time to find a proper sample
			TargetSampleTimespan = InFrameInformation.SampleTimespan;
		}

		// Apply time correction to the target time
		const FTimespan RequestedOffsetTimespan = FTimespan::FromSeconds(InFrameInformation.EvaluationOffset);

		// Latency adjusted VSync
		const FTimespan TargetTimespanCorrected = TargetSampleTimespan - RequestedOffsetTimespan;

		// Go over the sample pool and find a sample closest to the target time
		int32 ClosestIndex = -1;
		int64 SmallestInterval(TNumericLimits<int64>::Max());

		// Get all available video samples
		const TArray<TSharedPtr<IMediaTextureSample>> TextureSamples = Samples->GetVideoSamples();
		if (TextureSamples.Num() == 0)
		{
			return nullptr;
		}

		for (int32 Index = 0; Index < TextureSamples.Num(); ++Index)
		{
			TSharedPtr<FRivermaxMediaTextureSample> Sample = StaticCastSharedPtr<FRivermaxMediaTextureSample>(TextureSamples[Index]);

			// Either closest positive or closest negative
			const int64 TestInterval = FMath::Abs((Sample->FrameReceptionStart - TargetTimespanCorrected).GetTicks());

			// if VSync is within the time frame of sample's start and end of the reception it is the sample that is returned
			// otherwise find the sample which began the reception closest to the required VSync.
			if (TargetTimespanCorrected >= Sample->FrameReceptionStart && TargetTimespanCorrected < Sample->FrameReceptionEnd)
			{
				ClosestIndex = Index;
				break;
			}

			// '<=' instead of '<' is used here intentionally. Turns out we might have
			// some samples with the same timecode. To avoid early termination of the search '<=' is used.
			if (TestInterval <= SmallestInterval)
			{
				ClosestIndex = Index;
				SmallestInterval = TestInterval;
			}
			else
			{
				// Since our samples are stored in chronological order, it makes no sense
				// to continue searching. The interval will continue increasing.
				break;
			}
		}

		checkSlow(ClosestIndex >= 0 && ClosestIndex < TextureSamples.Num());

		// Finally, return the closest sample we found
		return StaticCastSharedPtr<FMediaIOCoreTextureSampleBase, IMediaTextureSample, ESPMode::ThreadSafe>(TextureSamples[ClosestIndex]);
	}

	void FRivermaxMediaPlayer::PostSampleUsage(FRDGBuilder& GraphBuilder, TSharedPtr<FRivermaxMediaTextureSample> Sample)
	{
		GraphBuilder.AddPass(RDG_EVENT_NAME("RivermaxPostSampleUsage"),
			ERDGPassFlags::NeverCull,
			[SamplePtr = Sample, this](FRHICommandList& RHICmdList)
			{
				RHI_BREADCRUMB_EVENT_STAT(RHICmdList, RivermaxMedia_SampleUsageFence, "RivermaxMedia_SampleUsageFence");
				SCOPED_GPU_STAT(RHICmdList, RivermaxMedia_SampleUsageFence);
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Rmax::ReleasingSample %u"), (SamplePtr->GetFrameNumber())));

				// Write a fence in the post sample usage pass to be able to know when we can reuse it
				RHICmdList.WriteGPUFence(SamplePtr->SampleConversionFence);

				++TasksInFlight;

				// This async task is waiting for the GPU to be finished with Sample's resources and then release them.
				UE::Tasks::Launch(UE_SOURCE_LOCATION,
					[SamplePtr, this]()
					{
						ON_SCOPE_EXIT
						{
							--TasksInFlight;
						};

						TRACE_CPUPROFILER_EVENT_SCOPE(RmaxWaitForShader);
						do
						{
							const bool bHasValidFence = SamplePtr->SampleConversionFence.IsValid();
							const bool bHasFenceCompleted = bHasValidFence ? SamplePtr->SampleConversionFence->Poll() : false;
							if (bHasValidFence == false || bHasFenceCompleted)
							{
								break;
							}

							FPlatformProcess::SleepNoStats(SleepTimeSeconds);

						} while (true);

						// We clear the sample states, mark this sample as ready for reuse and that rendering is completed.
						SamplePtr->ShutdownPoolable();
					});
			});
	}

	void FRivermaxMediaPlayer::OnVideoFrameReceptionError(TSharedPtr<IRivermaxVideoSample> InVideoFrameSample)
	{
		TSharedPtr<FRivermaxMediaTextureSample> Sample = StaticCastSharedPtr<FRivermaxMediaTextureSample>(InVideoFrameSample);
		if (!Sample.IsValid())
		{
			return;
		}

		UE_LOG(LogRivermaxMedia, Warning, TEXT("Issue receiving frame number %u."), Sample->GetFrameNumber());
		if (Sample->IsBeingRendered())
		{
			Sample->SetReceptionState(FRivermaxMediaTextureSample::ESampleState::ReceptionError);
			Sample->GetSampleReceivedEvent()->Trigger();
		}
		else
		{
			Sample->SetReceptionState(FRivermaxMediaTextureSample::ESampleState::ReadyForReception);
		}
	}

	void FRivermaxMediaPlayer::OnVideoFormatChanged(const FRivermaxInputVideoFormatChangedInfo& NewFormatInfo)
	{
		const ERivermaxMediaSourcePixelFormat NewFormat = UE::RivermaxMediaUtils::Private::RivermaxPixelFormatToMediaSourcePixelFormat(NewFormatInfo.PixelFormat);
		const FIntPoint NewResolution = { (int32)NewFormatInfo.Width, (int32)NewFormatInfo.Height };
		bool bNeedReinitializing = (NewFormatInfo.PixelFormat != StreamOptions.PixelFormat);
		bNeedReinitializing |= (NewFormatInfo.Width != VideoTrackFormat.Dim.X || NewFormatInfo.Height != VideoTrackFormat.Dim.Y);

		UE_LOG(LogRivermaxMedia, Log, TEXT("New video format detected: %dx%d with pixel format '%s'"), NewResolution.X, NewResolution.Y, *UEnum::GetValueAsString(NewFormat));

		if (bNeedReinitializing && bFollowsStreamResolution)
		{
			FScopeLock Lock(&StreamResolutionCriticalSection);
			StreamResolution = NewResolution;
		}
	}

	bool FRivermaxMediaPlayer::IsReadyToPlay() const
	{
		if (RivermaxThreadNewState == EMediaState::Playing)
		{
			FScopeLock Lock(&StreamResolutionCriticalSection);
			return StreamResolution == VideoTrackFormat.Dim;
		}

		return false;
	}

	void FRivermaxMediaPlayer::WaitForPendingTasks()
	{
		// Flush any rendering activity to be sure we can move on with clearing resources. 
		FlushRenderingCommands();

		// Wait for all pending tasks to complete. They should all complete at some point but add a timeout as a last resort. 
		constexpr double TimeoutSeconds = 2.0;
		const double StartTimeSeconds = FPlatformTime::Seconds();
		while (TasksInFlight > 0)
		{
			FPlatformProcess::SleepNoStats(SleepTimeSeconds);
			if ((FPlatformTime::Seconds() - StartTimeSeconds) > TimeoutSeconds)
			{
				UE_LOG(LogRivermaxMedia, Warning, TEXT("Timed out waiting for pendings tasks to finish."));
				break;
			}
		}
	}

	bool FRivermaxMediaPlayer::WaitForSample(TSharedPtr<FRivermaxMediaTextureSample> Sample, FWaitConditionFunc WaitConditionFunction, bool bCanTimeout)
	{
		const double StartTimeSeconds = FPlatformTime::Seconds();
		const double TimeoutSeconds = CVarRivermaxDefaultThreadBlockTimeoutSec.GetValueOnAnyThread();

		while (true)
		{
			if (!IsReadyToPlay())
			{
				return false;
			}

			// Our goal here is to wait until the expected frame is available to be used (received) unless there is a timeout
			if (WaitConditionFunction(Sample))
			{
				return true;
			}

			{
				FPlatformProcess::SleepNoStats(SleepTimeSeconds);
				if (bCanTimeout && ((FPlatformTime::Seconds() - StartTimeSeconds) > TimeoutSeconds))
				{
					UE_LOG(LogRivermaxMedia, Error, TEXT("Timed out waiting for frame %u."), Sample->GetFrameNumber());
					return false;
				}
			}
		}
	}

	void FRivermaxMediaPlayer::SampleUploadSetupTaskThreadMode(TSharedPtr<FRivermaxMediaTextureSample> Sample, FSampleConverterOperationSetup& OutConverterSetup)
	{
		// We will always be providing a buffer already located on the GPU even when not using gpudirect
		// Once a frame has arrived on system, we will upload it to the allocated gpu buffer.
		OutConverterSetup.GetGPUBufferFunc = [SamplePtr = Sample]() { return SamplePtr->GetGPUBuffer(); };

		const uint64 NextFrameExpectations = (IS_FRAMELOCKED()) ? TO_EXPECTED_SAMPLE_FRAME_NUM(GFrameCounterRenderThread) : Sample->GetFrameNumber();

		OutConverterSetup.PreConvertFunc = [SamplePtr = Sample, NextFrameExpectations, this](const FRDGBuilder& GraphBuilder)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Rmax::01_PreConvertFunction %u"), (SamplePtr->GetFrameNumber())));
				// When GPUDirect is not involved, we have an extra step to do. We need to wait for the sample to be received
				// but also initiate the memcopy to gpu memory for it to be rendered
				if (bStreamSupportsGPUDirect == false)
				{
					constexpr uint32 Offset = 0;
					const uint32 Size = SamplePtr->GetGPUBuffer()->GetSize();

					SamplePtr->SetAwaitingForGPUTransfer(true);

					// Todo: Find a way to keep the buffer permanently locked.
					if (SamplePtr->LockedMemory == nullptr)
					{
						SamplePtr->LockedMemory = GraphBuilder.RHICmdList.LockBuffer(SamplePtr->GetGPUBuffer()->GetRHI(), Offset, Size, EResourceLockMode::RLM_WriteOnly_NoOverwrite);
					}

					++TasksInFlight;
					UE::Tasks::Launch(UE_SOURCE_LOCATION,
						[Size, SamplePtr, NextFrameExpectations, this]()
						{
							ON_SCOPE_EXIT
							{
								--TasksInFlight;
							};
							TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Rmax::01_CopyPixels %u"), (NextFrameExpectations)));

							// Indicates if we sample reception started on the input thread.
							bool bStreamReceptionStarted = true;

							// In frame locked mode the packets potentially haven't started being received yet. 
							// This waits for frame locked samples to start writing packets and then for the completion of the reception.
							if (IS_FRAMELOCKED())
							{
								TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Rmax::01_WaitForReception %u"), (NextFrameExpectations)));

								// Waiting to start receiving.
								const double TimeoutSeconds = CVarRivermaxDefaultThreadBlockTimeoutSec.GetValueOnAnyThread();
								const double StartTimeSeconds = FPlatformTime::Seconds();
								uint64 CurrentSampleFrameNumber = SamplePtr->GetFrameNumber();

								while (CurrentSampleFrameNumber < NextFrameExpectations)
								{
									FPlatformProcess::SleepNoStats(SleepTimeSeconds);
									if ((FPlatformTime::Seconds() - StartTimeSeconds) > TimeoutSeconds)
									{
										UE_LOG(LogRivermaxMedia, Warning, TEXT("Timed out waiting for frame #%u to start being received."), NextFrameExpectations);
										bStreamReceptionStarted = false;
										break;
									}
									CurrentSampleFrameNumber = SamplePtr->GetFrameNumber();
								}

								// At this point it is known that the sample reception has started or not.
								if (bStreamReceptionStarted)
								{
									TRACE_CPUPROFILER_EVENT_SCOPE(RmaxWaitSampleReception);
									TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Rmax::02_WaitSampleReceived %u"), (NextFrameExpectations)));
									// We wait for 2 Media Player frames to timeout. Reason: Due to 2110 standard anything more than 1 frame is going to be discarded.
									// Since we are guaranteed to have started receiving the buffer we shouldn't wait longer than that.
									const double TwoFrameTime = 2. / GetFrameRate().AsDecimal();
									SamplePtr->GetSampleReceivedEvent()->Wait(FTimespan::FromSeconds(TwoFrameTime));
								}
							}

							if (SamplePtr->GetFrameNumber() != NextFrameExpectations)
							{
								UE_LOG(LogRivermaxMedia, Warning, TEXT("Rendering unexpected frame %u, when frame %u was expected."), SamplePtr->GetFrameNumber(), NextFrameExpectations);
							}

							// In case reception failed mid way.
							if (bStreamReceptionStarted && SamplePtr->GetReceptionState() != IRivermaxSample::ESampleState::ReceptionError)
							{
								TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Rmax::03_CopyBufferToGPU %u"), (SamplePtr->GetFrameNumber())));
								FMemory::Memcpy(SamplePtr->LockedMemory, SamplePtr->GetOrRequestBuffer(Size), Size);
							}
							else
							{
								UE_LOG(LogRivermaxMedia, Warning, TEXT("Incomplete or failed pixels will be rendered for frame %u"), NextFrameExpectations);
							}

							// Signals the RHI thread that the GPU transfer has completed.
							SamplePtr->SetAwaitingForGPUTransfer(false);
						}
					);
				}


				RHI_BREADCRUMB_EVENT_STAT(GraphBuilder.RHICmdList, Rmax_WaitForPixels, "Rmax::WaitForPixels");
				SCOPED_GPU_STAT(GraphBuilder.RHICmdList, Rmax_WaitForPixels);
				// Since we are going to enqueue a lambda that can potentially sleep in the RHI thread if the pixels haven't arrived,
				// we dispatch the existing commands (including the draw event start timing in the SCOPED_DRAW_EVENT above) before any potential sleep.
				GraphBuilder.RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

				// Setup requirements for sample to be ready to be rendered
				GraphBuilder.RHICmdList.EnqueueLambda(
					[SamplePtr, NextFrameExpectations, this](FRHICommandListImmediate&)
					{
						FWaitConditionFunc ReadyToRenderConditionFunc = [](const TSharedPtr<FRivermaxMediaTextureSample>& Sample)
							{
								return Sample->IsAwaitingForGPUTransfer() == false;
							};

						TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Rmax::04_WaitForUploadToFinish %u"), (NextFrameExpectations)));

						// Only accept timeouts here if we're going through the system memory in order to avoid timing out while doing
						// the memcopy (buffer upload)
						bool bCanTimeout = bStreamSupportsGPUDirect;
						WaitForSample(SamplePtr, MoveTemp(ReadyToRenderConditionFunc), bCanTimeout);
					}
				);

				// Final step, if the memory was locked (non gpu direct), enqueue unlock after the wait for sample in order to render it
				if (SamplePtr->LockedMemory && ensure(!bStreamSupportsGPUDirect))
				{
					GraphBuilder.RHICmdList.UnlockBuffer(SamplePtr->GetGPUBuffer()->GetRHI());
				}
			};

		// Setup post sample usage pass 
		OutConverterSetup.PostConvertFunc = [SamplePtr = Sample, this](FRDGBuilder& GraphBuilder)
			{
				PostSampleUsage(GraphBuilder, SamplePtr);
			};
	}

}

#undef LOCTEXT_NAMESPACE
