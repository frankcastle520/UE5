// Copyright Epic Games, Inc. All Rights Reserved.

#include "NDIMediaCapture.h"

#include "NDIMediaAPI.h"
#include "NDIMediaModule.h"
#include "NDIMediaOutput.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Slate/SceneViewport.h"

DEFINE_LOG_CATEGORY(LogNDIMedia);

class UNDIMediaCapture::FNDICaptureInstance
{
public:
	FNDICaptureInstance(const TSharedPtr<FNDIMediaRuntimeLibrary>& InNDILib, const UNDIMediaOutput* InMediaOutput)
		: NDILibHandle(InNDILib)
		, NDILib(InNDILib ? InNDILib->Lib : nullptr)
	{
		if (NDILib != nullptr)
		{
			NDIlib_send_create_t SendDesc;

			SendDesc.p_ndi_name = TCHAR_TO_UTF8(*InMediaOutput->SourceName);
			SendDesc.p_groups = (!InMediaOutput->GroupName.IsEmpty()) ? TCHAR_TO_UTF8(*InMediaOutput->GroupName) : nullptr;

			// Don't clock audio, normally, if audio and video is
			SendDesc.clock_audio = false;

			// Clocked video
			SendDesc.clock_video = true;

			Sender = NDILib->send_create(&SendDesc);
		}

		if (!Sender)
		{
			UE_LOG(LogNDIMedia, Error, TEXT("Failed to create NDI capture."));
		}

		// Keep track of specified frame rate.
		FrameRateDenominator = InMediaOutput->FrameRate.Denominator;
		FrameRateNumerator = InMediaOutput->FrameRate.Numerator;
		OutputType = InMediaOutput->OutputType;

		// Caution: logic inversion, on purpose, because for this class, async 
		// enables more work, while sync disables, and I prefer having my inverted
		// logic in one place, here instead of all over the place in this class.
		// bWaitForSyncEvent logic in Media Output is inverted to match with BlackMedia
		// and AJA Media Output's properties, in the hope that it makes it easier to
		// generically manage those objects.
		bAsyncSend = !InMediaOutput->bWaitForSyncEvent;

		if (bAsyncSend)
		{
			// Prepare our video frame buffers for async send.

			// Documentation and samples indicate only 2 buffers should be 
			// necessary. But, considering potential difference in frame rates,
			// ranging from 30 to 240, better be safe. We could even expose 
			// this in case issues pop up.
			static constexpr int32 NumVideoFrameBuffers = 3;	// Experimental.

			VideoFrameBuffers.SetNum(NumVideoFrameBuffers);
		}
	}

	~FNDICaptureInstance()
	{
		if (Sender)
		{
			// Force sync in case some data is still used by the ndi encoder.
			NDILib->send_send_video_v2(Sender, nullptr);

			// Destroy the NDI sender
			NDILib->send_destroy(Sender);

			Sender = nullptr;
		}
	}

	struct FVideoFrameBuffer
	{
		int32 Height;
		int32 BytesPerRow;
		TArray<uint8> Data;

		FVideoFrameBuffer(int32 InHeight, int32 InBytesPerRow)
			: Height(InHeight)
			, BytesPerRow(InBytesPerRow)
		{
			Data.SetNumUninitialized(InHeight * InBytesPerRow, EAllowShrinking::Yes);
		}

		FVideoFrameBuffer* EnsureSize(int32 InHeight, int32 InBytesPerRow)
		{
			if (Height != InHeight || BytesPerRow != InBytesPerRow)
			{
				Height = InHeight;
				BytesPerRow = InBytesPerRow;
				Data.SetNumUninitialized(InHeight * InBytesPerRow, EAllowShrinking::Yes);
			}
			return this;
		}

		uint8* GetData()
		{
			return Data.GetData();
		}

	};

	FVideoFrameBuffer* GetNextVideoFrameBuffer(int InHeight, int InBytesPerRow)
	{
		// Move to next video frame buffer in the circular array.
		VideoFrameBufferCurrentIndex++;
		if (VideoFrameBufferCurrentIndex >= VideoFrameBuffers.Num())
		{
			VideoFrameBufferCurrentIndex = 0;
		}

		// Lazy allocation
		if (!VideoFrameBuffers[VideoFrameBufferCurrentIndex].IsValid())
		{
			VideoFrameBuffers[VideoFrameBufferCurrentIndex] = MakeUnique<FVideoFrameBuffer>(InHeight, InBytesPerRow);
		}

		// Ensure video frame buffer is of proper size.
		return VideoFrameBuffers[VideoFrameBufferCurrentIndex]->EnsureSize(InHeight, InBytesPerRow);
	}

public:
	TSharedPtr<FNDIMediaRuntimeLibrary> NDILibHandle;
	const NDIlib_v5* NDILib = nullptr;
	
	NDIlib_send_instance_t Sender = nullptr;
	int32 FrameRateNumerator = 30000;
	int32 FrameRateDenominator = 1001;
	EMediaIOOutputType OutputType = EMediaIOOutputType::Fill;

	// By default send async because it is the recommended way in the SDK.
	bool bAsyncSend = true;

	// Circular buffer of Video Frames.
	TArray<TUniquePtr<FVideoFrameBuffer>> VideoFrameBuffers;
	int32 VideoFrameBufferCurrentIndex = 0;
};

UNDIMediaCapture::~UNDIMediaCapture()
{
	delete CaptureInstance;
}

inline int64_t ConvertToNDITimeCode(const FTimecode& InTimecode, const FFrameRate& InFrameRate)
{
	// Handling drop frame logic is too troublesome. Using engine types to do it.
	if (InTimecode.bDropFrameFormat)
	{
		// Remark: Potential overflow conditions. 
		// 1- converts to frames stored as int32. Overflow frequency at 60 fps: ~414 days.
		// 2- converts frames to seconds as double, which can only keep nano-second precision for a week. (source: https://randomascii.wordpress.com/2012/02/13/dont-store-that-in-a-float/)
		const FTimespan TimeSpan = InTimecode.ToTimespan(InFrameRate);

		// Ticks are defined as 100 ns so it matches with NDI's timecode tick.
		static_assert(ETimespan::NanosecondsPerTick == 100);
		return TimeSpan.GetTicks();
	}
	else
	{
		// Our own implementation.
		// Doesn't depend on engine types to avoid issues with change of ticks definitions.
		static const int64_t NanosecondsPerTick = 100;		// NDI tick is 100 ns.
		static const int64_t TicksPerSecond = 1000000000 / NanosecondsPerTick;
		static const int64_t TicksPerMinute = TicksPerSecond * 60;
		static const int64_t TicksPerHour = TicksPerMinute * 60;

		const double FramesPerSecond = InFrameRate.AsDecimal();
		const int64_t TicksPerFrame = static_cast<int64_t>(static_cast<double>(TicksPerSecond) / FramesPerSecond);

		return InTimecode.Frames * TicksPerFrame
			+ InTimecode.Seconds * TicksPerSecond
			+ InTimecode.Minutes * TicksPerMinute
			+ InTimecode.Hours * TicksPerHour;
	}
}

void UNDIMediaCapture::OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height, int32 BytesPerRow)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UNDIMediaCapture::OnFrameCaptured_RenderingThread);

	FScopeLock ScopeLock(&CaptureInstanceCriticalSection);

	if (CaptureInstance && CaptureInstance->Sender)
	{
		NDIlib_video_frame_v2_t NDI_video_frame;

		// The logic for now is that if we have a Fill and Key, the format is RGBA because we don't support
		// the conversion to the semi planar format YUVA for now.
		const bool bIsRGBA = CaptureInstance->OutputType == EMediaIOOutputType::FillAndKey ? true : false;

		// HACK fix bug until media capture is fixed.
		if (BytesPerRow == 0)
		{
			BytesPerRow = Width * 4;
		}

		// Note: for YUV format (422), width has been divided by 2.
		NDI_video_frame.xres = bIsRGBA ? Width : Width * 2;
		NDI_video_frame.yres = Height;
		NDI_video_frame.FourCC = bIsRGBA ? NDIlib_FourCC_type_BGRA : NDIlib_FourCC_type_UYVY;
		NDI_video_frame.p_data = static_cast<uint8_t*>(InBuffer);
		NDI_video_frame.line_stride_in_bytes = BytesPerRow;
		NDI_video_frame.frame_rate_D = CaptureInstance->FrameRateDenominator;
		NDI_video_frame.frame_rate_N = CaptureInstance->FrameRateNumerator;
		NDI_video_frame.timecode = ConvertToNDITimeCode(InBaseData.SourceFrameTimecode, InBaseData.SourceFrameTimecodeFramerate);

		if (CaptureInstance->bAsyncSend)
		{
			// For async send, the memory buffer needs to remain valid until the next call.
			// 
			// Since the incoming buffer (InBuffer) is a mapped memory region from a texture that gets unmapped
			// right after this call returns, we need to make a copy.
			//
			FNDICaptureInstance::FVideoFrameBuffer* VideoFrameBuffer = CaptureInstance->GetNextVideoFrameBuffer(Height, BytesPerRow);
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(UNDIMediaCapture::CopyVideoFrameBuffer);
				FMemory::Memcpy(VideoFrameBuffer->GetData(), InBuffer, Height * BytesPerRow);
			}
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(NDIlib_send_send_video_async_v2);
				NDI_video_frame.p_data = VideoFrameBuffer->GetData();
				CaptureInstance->NDILib->send_send_video_async_v2(CaptureInstance->Sender, &NDI_video_frame);
			}
		}
		else
		{
			// send the video synchroneously.
			TRACE_CPUPROFILER_EVENT_SCOPE(NDIlib_send_send_video_v2);
			NDI_video_frame.p_data = static_cast<uint8_t*>(InBuffer);
			CaptureInstance->NDILib->send_send_video_v2(CaptureInstance->Sender, &NDI_video_frame);
		}
	}
}

bool UNDIMediaCapture::InitializeCapture()
{
	return true;
}

bool UNDIMediaCapture::PostInitializeCaptureViewport(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	const bool bSuccess = StartNewCapture();
	if (bSuccess)
	{
		UE_LOG(LogNDIMedia, Log, TEXT("Media Capture Started: Scene Viewport (%d x %d)."), 
			InSceneViewport->GetSize().X, InSceneViewport->GetSize().Y);
	}
	return bSuccess;
}

bool UNDIMediaCapture::PostInitializeCaptureRenderTarget(UTextureRenderTarget2D* InRenderTarget)
{
	const bool bSuccess = StartNewCapture();
	if (bSuccess)
	{
		UE_LOG(LogNDIMedia, Log, TEXT("Media Capture Started: Render Target (%d x %d)."), 
			InRenderTarget->SizeX, InRenderTarget->SizeY);
	}
	return bSuccess;
}

void UNDIMediaCapture::StopCaptureImpl(bool /*bAllowPendingFrameToBeProcess*/)
{
	TRACE_BOOKMARK(TEXT("NDIMediaCapture::StopCapture"));

	FScopeLock ScopeLock(&CaptureInstanceCriticalSection);

	delete CaptureInstance;
	CaptureInstance = nullptr;
}

bool UNDIMediaCapture::StartNewCapture()
{
	TRACE_BOOKMARK(TEXT("NDIMediaCapture::StartNewCapture"));
	{
		FScopeLock ScopeLock(&CaptureInstanceCriticalSection);

		delete CaptureInstance;
		CaptureInstance = nullptr;

		if (const UNDIMediaOutput* NDIMediaOutput = Cast<UNDIMediaOutput>(MediaOutput))
		{
			CaptureInstance = new FNDICaptureInstance(FNDIMediaModule::GetNDIRuntimeLibrary(), NDIMediaOutput);
		}
		else
		{
			UE_LOG(LogNDIMedia, Error, TEXT("Internal Error: Media Capture's associated Media Output is not of type \"UNDIMediaOutput\"."));
		}
	}

	SetState(EMediaCaptureState::Capturing);
	return true;
}
