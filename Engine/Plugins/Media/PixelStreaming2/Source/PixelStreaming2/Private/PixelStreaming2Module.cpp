// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2Module.h"

#include "Blueprints/PixelStreaming2InputComponent.h"
#include "CoderUtils.h"
#include "CoreMinimal.h"
#include "CoreUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "EpicRtcThread.h"
#include "IPixelStreaming2InputModule.h"
#include "Logging.h"
#include "Modules/ModuleManager.h"
#include "PixelStreaming2Delegates.h"
#include "PixelStreaming2Utils.h"
#include "PixelStreaming2PluginSettings.h"
#include "Slate/SceneViewport.h"
#include "Streamer.h"
#include "UObject/UObjectIterator.h"
#include "UtilsCommon.h"

#if PLATFORM_LINUX
	#include "CudaModule.h"
#endif

#if PLATFORM_WINDOWS
	#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
	#include <VersionHelpers.h>
THIRD_PARTY_INCLUDES_END
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "AudioDeviceManager.h"
#include "RenderingThread.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RendererInterface.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/GameModeBase.h"
#include "Dom/JsonObject.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "WebSocketsModule.h"

#include "Video/Encoders/Configs/VideoEncoderConfigH264.h"
#include "Video/Encoders/Configs/VideoEncoderConfigAV1.h"

#if !UE_BUILD_SHIPPING
	#include "DrawDebugHelpers.h"
#endif

#include "VideoProducerBackBuffer.h"
#include "VideoProducerMediaCapture.h"
#include "Engine/GameEngine.h"
#include "Stats.h"
#include "UtilsString.h"

#include "EpicRtcAllocator.h"
#include "EpicRtcAudioMixingCapturer.h"
#include "EpicRtcLogging.h"
#include "EpicRtcVideoEncoderInitializer.h"
#include "EpicRtcVideoDecoderInitializer.h"
#include "EpicRtcWebsocketFactory.h"

namespace UE::PixelStreaming2
{
	constexpr EpicRtcLogLevel UnrealLogToEpicRtcCategoryMap[] = {
		EpicRtcLogLevel::Off,
		EpicRtcLogLevel::Critical,
		EpicRtcLogLevel::Error,
		EpicRtcLogLevel::Warning,
		EpicRtcLogLevel::Info,
		EpicRtcLogLevel::Info,
		EpicRtcLogLevel::Debug,
		EpicRtcLogLevel::Trace,
		EpicRtcLogLevel::Trace
	};

	static_assert(EpicRtcLogLevel::Off == UnrealLogToEpicRtcCategoryMap[ELogVerbosity::Type::NoLogging]);
	static_assert(EpicRtcLogLevel::Critical == UnrealLogToEpicRtcCategoryMap[ELogVerbosity::Type::Fatal]);
	static_assert(EpicRtcLogLevel::Error == UnrealLogToEpicRtcCategoryMap[ELogVerbosity::Type::Error]);
	static_assert(EpicRtcLogLevel::Warning == UnrealLogToEpicRtcCategoryMap[ELogVerbosity::Type::Warning]);
	static_assert(EpicRtcLogLevel::Info == UnrealLogToEpicRtcCategoryMap[ELogVerbosity::Type::Display]);
	static_assert(EpicRtcLogLevel::Info == UnrealLogToEpicRtcCategoryMap[ELogVerbosity::Type::Log]);
	static_assert(EpicRtcLogLevel::Debug == UnrealLogToEpicRtcCategoryMap[ELogVerbosity::Type::Verbose]);
	static_assert(EpicRtcLogLevel::Trace == UnrealLogToEpicRtcCategoryMap[ELogVerbosity::Type::VeryVerbose]);
	static_assert(EpicRtcLogLevel::Trace == UnrealLogToEpicRtcCategoryMap[ELogVerbosity::Type::All]);

	FPixelStreaming2Module* FPixelStreaming2Module::PixelStreaming2Module = nullptr;

	FUtf8String FPixelStreaming2Module::EpicRtcConferenceName("pixel_streaming_conference_instance");

	/**
	 * Stats logger - as turned on/off by CVarPixelStreaming2LogStats
	 */
	void ConsumeStat(FString PlayerId, FName StatName, float StatValue)
	{
		UE_LOGFMT(LogPixelStreaming2, Log, "[{0}]({1}) = {2}", PlayerId, StatName.ToString(), StatValue);
	}

	/**
	 * IModuleInterface implementation
	 */
	void FPixelStreaming2Module::StartupModule()
	{
#if UE_SERVER
		// Hack to no-op the rest of the module so Blueprints can still work
		return;
#endif
		if (!IsStreamingSupported())
		{
			return;
		}

		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		const ERHIInterfaceType RHIType = GDynamicRHI ? RHIGetInterfaceType() : ERHIInterfaceType::Hidden;
		// only D3D11/D3D12/Vulkan is supported
		if (!(RHIType == ERHIInterfaceType::D3D11 || RHIType == ERHIInterfaceType::D3D12 || RHIType == ERHIInterfaceType::Vulkan || RHIType == ERHIInterfaceType::Metal))
		{
#if !WITH_DEV_AUTOMATION_TESTS
			UE_LOG(LogPixelStreaming2, Warning, TEXT("Only D3D11/D3D12/Vulkan/Metal Dynamic RHI is supported. Detected %s"), GDynamicRHI != nullptr ? GDynamicRHI->GetName() : TEXT("[null]"));
#endif
			return;
		}

		// Initialize EpicRtc thread. Handles tasks like audio pushing and conference ticking
		EpicRtcThread = MakeShared<FEpicRtcThread>();

		// By calling InitDefaultStreamer post engine init we can use pixel streaming in standalone editor mode
		FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddLambda([this, RHIType]() {
			// Need to initialize after other modules have initialized such as NVCodec.
			if (!InitializeEpicRtc())
			{
				return;
			}

			// Check to see if we can use the Pixel Streaming plugin on this platform.
			// If not then we avoid setting up our delegates to prevent access to the plugin.
			if (!IsPlatformCompatible())
			{
				return;
			}

			if (!ensure(GEngine != nullptr))
			{
				return;
			}

			FApp::SetUnfocusedVolumeMultiplier(1.0f);

			// Ensure we have ImageWrapper loaded, used in Freezeframes
			verify(FModuleManager::Get().LoadModule(FName("ImageWrapper")));

			// HACK (Eden.Harris): Until or if we ever find a workaround for fencing, we need to ensure capture always uses a fence.
			// If we don't then we get frequent and intermittent stuttering as textures are rendered to while being encoded.
			// From testing NVENC + CUDA pathway seems acceptable without a fence in most cases so we use the faster, unsafer path there.
			if (IsRHIDeviceAMD())
			{
				if (!UPixelStreaming2PluginSettings::CVarCaptureUseFence.GetValueOnAnyThread())
				{
					UE_LOGFMT(LogPixelStreaming2, Warning, "AMD GPU Device detected, setting PixelStreaming2.CaptureUseFence to true to avoid screen tearing in stream.");
				}

				UPixelStreaming2PluginSettings::CVarCaptureUseFence.AsVariable()->Set(true);
			}

			// We don't want to start immediately streaming in editor
			if (!GIsEditor)
			{
				InitDefaultStreamer();
				StartStreaming();
			}

			bModuleReady = true;
			ReadyEvent.Broadcast(*this);
		});

		FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

		// Call these to initialize their singletons
		FStats::Get();

		// Extra Initialisations post loading console commands
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("PixelStreaming.StartStreaming"),
			TEXT("Start all streaming sessions"),
			FConsoleCommandDelegate::CreateLambda([]() {
				IPixelStreaming2Module::Get().StartStreaming();
			}));

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("PixelStreaming.StopStreaming"),
			TEXT("End any existing streaming sessions."),
			FConsoleCommandDelegate::CreateLambda([]() {
				IPixelStreaming2Module::Get().StopStreaming();
			}));

		if (UPixelStreaming2PluginSettings::FDelegates* Delegates = UPixelStreaming2PluginSettings::Delegates())
		{
			Delegates->OnLogStatsChanged.AddLambda([this](IConsoleVariable* Var) {
				bool					   bLogStats = Var->GetBool();
				UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get();
				if (!Delegates)
				{
					return;
				}
				if (bLogStats)
				{
					LogStatsHandle = Delegates->OnStatChangedNative.AddStatic(&ConsumeStat);
				}
				else
				{
					Delegates->OnStatChangedNative.Remove(LogStatsHandle);
				}
			});

			Delegates->OnWebRTCFpsChanged.AddLambda([](IConsoleVariable*) {
				IPixelStreaming2Module::Get().ForEachStreamer([](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
					Streamer->RefreshStreamBitrate();
				});
			});

			Delegates->OnWebRTCBitrateChanged.AddLambda([](IConsoleVariable*) {
				IPixelStreaming2Module::Get().ForEachStreamer([](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
					Streamer->RefreshStreamBitrate();
				});
			});
		}

		bStartupCompleted = true;
	}

	void FPixelStreaming2Module::ShutdownModule()
	{
		if (!IsStreamingSupported())
		{
			return;
		}

		if (!bStartupCompleted)
		{
			return;
		}

		// We explicitly call release on streamer so WebRTC gets shutdown before our module is deleted
		// additionally the streamer does a bunch of delegate calls and unbinds which seem to have issues
		// when called during engine destruction rather than here.
		Streamers.Empty();
		DefaultStreamer.Reset();

		// Reset thread must be called before tasks to ensure it does not attempt to run any partially destroyed tasks from the AudioMixingCapturer
		EpicRtcThread.Reset();
		AudioMixingCapturer.Reset();

		TickConferenceTask.Reset();

		if (!EpicRtcPlatform)
		{
			UE_LOGFMT(LogPixelStreaming2, Error, "EpicRtcPlatform does not exist during shutdown when it is expected to exist");
		}
		else
		{
			EpicRtcPlatform->ReleaseConference(ToEpicRtcStringView(EpicRtcConferenceName));
		}

		bStartupCompleted = false;
	}

	/**
	 * End IModuleInterface implementation
	 */

	FPixelStreaming2Module* FPixelStreaming2Module::GetModule()
	{
		if (!PixelStreaming2Module)
		{
			PixelStreaming2Module = FModuleManager::Get().LoadModulePtr<FPixelStreaming2Module>("PixelStreaming2");
		}

		return PixelStreaming2Module;
	}

	/**
	 * IPixelStreaming2Module implementation
	 */
	IPixelStreaming2Module::FReadyEvent& FPixelStreaming2Module::OnReady()
	{
		return ReadyEvent;
	}

	bool FPixelStreaming2Module::IsReady()
	{
		return bModuleReady;
	}

	bool FPixelStreaming2Module::StartStreaming()
	{
		if (DefaultStreamer.IsValid())
		{
			DefaultStreamer->StartStreaming();
			return true;
		}

		return false;
	}

	void FPixelStreaming2Module::StopStreaming()
	{
		if (DefaultStreamer.IsValid())
		{
			DefaultStreamer->StopStreaming();
		}
	}

	TSharedPtr<IPixelStreaming2Streamer> FPixelStreaming2Module::CreateStreamer(const FString& StreamerId)
	{
		TSharedPtr<IPixelStreaming2Streamer> ExistingStreamer = FindStreamer(StreamerId);
		if (ExistingStreamer)
		{
			return ExistingStreamer;
		}

		TSharedPtr<FStreamer> NewStreamer = FStreamer::Create(StreamerId, EpicRtcConference);
		{
			FScopeLock Lock(&StreamersCS);
			Streamers.Add(StreamerId, NewStreamer);
		}

		// Any time we create a new streamer, populate it's signalling server URL with whatever is in the ini, console or command line
		NewStreamer->SetSignallingServerURL(UPixelStreaming2PluginSettings::CVarSignallingURL.GetValueOnAnyThread());

		// Ensure that this new streamer is able to handle pixel streaming relevant input
		RegisterCustomHandlers(NewStreamer);

		return NewStreamer;
	}

	TSharedPtr<IPixelStreaming2AudioProducer> FPixelStreaming2Module::CreateAudioProducer()
	{
		return GetAudioCapturer()->CreateAudioProducer();
	}

	TSharedPtr<IPixelStreaming2VideoProducer> FPixelStreaming2Module::CreateVideoProducer()
	{
		return FVideoProducer::Create();
	}

	TArray<FString> FPixelStreaming2Module::GetStreamerIds()
	{
		TArray<FString> StreamerKeys;
		FScopeLock		Lock(&StreamersCS);
		Streamers.GenerateKeyArray(StreamerKeys);
		return StreamerKeys;
	}

	TSharedPtr<IPixelStreaming2Streamer> FPixelStreaming2Module::FindStreamer(const FString& StreamerId)
	{
		FScopeLock Lock(&StreamersCS);
		if (Streamers.Contains(StreamerId))
		{
			return Streamers[StreamerId].Pin();
		}
		return nullptr;
	}

	TSharedPtr<IPixelStreaming2Streamer> FPixelStreaming2Module::DeleteStreamer(const FString& StreamerId)
	{
		TSharedPtr<IPixelStreaming2Streamer> ToBeDeleted;
		FScopeLock							 Lock(&StreamersCS);
		if (Streamers.Contains(StreamerId))
		{
			ToBeDeleted = Streamers[StreamerId].Pin();
			Streamers.Remove(StreamerId);
		}
		return ToBeDeleted;
	}

	void FPixelStreaming2Module::DeleteStreamer(TSharedPtr<IPixelStreaming2Streamer> ToBeDeleted)
	{
		FScopeLock Lock(&StreamersCS);
		for (auto& [Id, Streamer] : Streamers)
		{
			if (Streamer == ToBeDeleted)
			{
				Streamers.Remove(Id);
				break;
			}
		}
	}

	void FPixelStreaming2Module::AddInputComponent(UPixelStreaming2Input* InInputComponent)
	{
		InputComponents.Add(InInputComponent);
	}

	void FPixelStreaming2Module::RemoveInputComponent(UPixelStreaming2Input* InInputComponent)
	{
		InputComponents.Remove(InInputComponent);
	}

	const TArray<UPixelStreaming2Input*> FPixelStreaming2Module::GetInputComponents()
	{
		return InputComponents;
	}

	FString FPixelStreaming2Module::GetDefaultStreamerID()
	{
		return UPixelStreaming2PluginSettings::CVarDefaultStreamerID.GetValueOnAnyThread();
	}

	FString FPixelStreaming2Module::GetDefaultSignallingURL()
	{
		return UPixelStreaming2PluginSettings::CVarSignallingURL.GetValueOnAnyThread();
	}

	void FPixelStreaming2Module::ForEachStreamer(const TFunction<void(TSharedPtr<IPixelStreaming2Streamer>)>& Func)
	{
		TSet<FString> KeySet;
		{
			FScopeLock Lock(&StreamersCS);
			Streamers.GetKeys(KeySet);
		}
		for (auto&& StreamerId : KeySet)
		{
			if (TSharedPtr<IPixelStreaming2Streamer> Streamer = FindStreamer(StreamerId))
			{
				Func(Streamer);
			}
		}
	}

	/**
	 * End IPixelStreaming2Module implementation
	 */

	TSharedPtr<FEpicRtcAudioMixingCapturer> FPixelStreaming2Module::GetAudioCapturer()
	{
		if (!AudioMixingCapturer)
		{
			AudioMixingCapturer = FEpicRtcAudioMixingCapturer::Create();
		}

		return AudioMixingCapturer;
	}

	void FPixelStreaming2Module::InitDefaultStreamer()
	{
		UE_LOG(LogPixelStreaming2, Log, TEXT("PixelStreaming2 streamer ID: %s"), *GetDefaultStreamerID());

		DefaultStreamer = CreateStreamer(GetDefaultStreamerID());
		// The PixelStreaming2EditorModule handles setting video input in the editor
		if (!GIsEditor)
		{
			// default to the scene viewport if we have a game engine
			if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
			{
				TSharedPtr<SWindow>						 TargetWindow = GameEngine->GameViewport->GetWindow();
				TSharedPtr<IPixelStreaming2InputHandler> InputHandler = DefaultStreamer->GetInputHandler().Pin();
				if (TargetWindow.IsValid() && InputHandler.IsValid())
				{
					InputHandler->SetTargetWindow(TargetWindow);
				}
				else
				{
					UE_LOG(LogPixelStreaming2, Error, TEXT("Cannot set target window - target window is not valid."));
				}
			}
		}

		if (!DefaultStreamer->GetSignallingServerURL().IsEmpty())
		{
			// The user has specified a URL on the command line meaning their intention is to start streaming immediately
			// in that case, set up the video input for them (as long as we're not in editor)
			if (UPixelStreaming2PluginSettings::CVarUseMediaCapture.GetValueOnAnyThread())
			{
				DefaultStreamer->SetVideoProducer(FVideoProducerMediaCapture::CreateActiveViewportCapture());
			}
			else
			{
				DefaultStreamer->SetVideoProducer(FVideoProducerBackBuffer::Create());
			}
		}
	}

	bool FPixelStreaming2Module::IsPlatformCompatible() const
	{
		bool bCompatible = true;

#if PLATFORM_WINDOWS
		bool bWin8OrHigher = IsWindows8OrGreater();
		if (!bWin8OrHigher)
		{
			FString ErrorString(TEXT("Failed to initialize Pixel Streaming plugin because minimum requirement is Windows 8"));
			FText	ErrorText = FText::FromString(ErrorString);
			FText	TitleText = FText::FromString(TEXT("Pixel Streaming Plugin"));
			FMessageDialog::Open(EAppMsgType::Ok, ErrorText, TitleText);
			UE_LOG(LogPixelStreaming2, Error, TEXT("%s"), *ErrorString);
			bCompatible = false;
		}
#endif

		const EVideoCodec SelectedCodec = GetEnumFromCVar<EVideoCodec>(UPixelStreaming2PluginSettings::CVarEncoderCodec);
		if ((SelectedCodec == EVideoCodec::H264 && !IsHardwareEncoderSupported<FVideoEncoderConfigH264>())
			|| (SelectedCodec == EVideoCodec::AV1 && !IsHardwareEncoderSupported<FVideoEncoderConfigAV1>()))
		{
			UE_LOG(LogPixelStreaming2, Warning, TEXT("Could not setup hardware encoder. This is usually a driver issue or hardware limitation, try reinstalling your drivers."));
			UE_LOG(LogPixelStreaming2, Warning, TEXT("Falling back to VP8 software video encoding."));
			UPixelStreaming2PluginSettings::CVarEncoderCodec.AsVariable()->Set(*GetCVarStringFromEnum(EVideoCodec::VP8), ECVF_SetByCommandline);
			if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
			{
				Delegates->OnFallbackToSoftwareEncoding.Broadcast();
				Delegates->OnFallbackToSoftwareEncodingNative.Broadcast();
			}
		}

		return bCompatible;
	}

	void FPixelStreaming2Module::RegisterCustomHandlers(TSharedPtr<IPixelStreaming2Streamer> Streamer)
	{
		if (TSharedPtr<IPixelStreaming2InputHandler> InputHandler = Streamer->GetInputHandler().Pin())
		{
			// Set Encoder.MinQP Legacy CVar
			InputHandler->SetCommandHandler(TEXT("Encoder.MinQP"), [](FString PlayerId, FString Descriptor, FString MinQPString) {
				int MinQP = FCString::Atoi(*MinQPString);
				UPixelStreaming2PluginSettings::CVarEncoderMinQuality->Set(100.0f * (1.0f - (FMath::Clamp<int32>(MinQP, 0, 51) / 51.0f)), ECVF_SetByCommandline);
			});

			// Set Encoder.MaxQP Legacy CVar
			InputHandler->SetCommandHandler(TEXT("Encoder.MaxQP"), [](FString PlayerId, FString Descriptor, FString MaxQPString) {
				int MaxQP = FCString::Atoi(*MaxQPString);
				UPixelStreaming2PluginSettings::CVarEncoderMaxQuality->Set(100.0f * (1.0f - (FMath::Clamp<int32>(MaxQP, 0, 51) / 51.0f)), ECVF_SetByCommandline);
			});

			// Set Encoder.MinQuality CVar
			InputHandler->SetCommandHandler(TEXT("Encoder.MinQuality"), [](FString PlayerId, FString Descriptor, FString MinQualityString) {
				int MinQuality = FCString::Atoi(*MinQualityString);
				UPixelStreaming2PluginSettings::CVarEncoderMinQuality->Set(FMath::Clamp<int32>(MinQuality, 0, 100), ECVF_SetByCommandline);
			});

			// Set Encoder.MaxQuality CVar
			InputHandler->SetCommandHandler(TEXT("Encoder.MaxQuality"), [](FString PlayerId, FString Descriptor, FString MaxQualityString) {
				int MaxQuality = FCString::Atoi(*MaxQualityString);
				UPixelStreaming2PluginSettings::CVarEncoderMaxQuality->Set(FMath::Clamp<int32>(MaxQuality, 0, 100), ECVF_SetByCommandline);
			});

			// Set WebRTC max FPS
			InputHandler->SetCommandHandler(TEXT("WebRTC.Fps"), [](FString PlayerId, FString Descriptor, FString FPSString) {
				int FPS = FCString::Atoi(*FPSString);
				UPixelStreaming2PluginSettings::CVarWebRTCFps->Set(FPS, ECVF_SetByCommandline);
			});

			// Set MinBitrate
			InputHandler->SetCommandHandler(TEXT("WebRTC.MinBitrate"), [InputHandler](FString PlayerId, FString Descriptor, FString MinBitrateString) {
				if (InputHandler->IsElevated(PlayerId))
				{
					int MinBitrate = FCString::Atoi(*MinBitrateString);
					UPixelStreaming2PluginSettings::CVarWebRTCMinBitrate->Set(MinBitrate, ECVF_SetByCommandline);
				}
			});

			// Set MaxBitrate
			InputHandler->SetCommandHandler(TEXT("WebRTC.MaxBitrate"), [InputHandler](FString PlayerId, FString Descriptor, FString MaxBitrateString) {
				if (InputHandler->IsElevated(PlayerId))
				{
					int MaxBitrate = FCString::Atoi(*MaxBitrateString);
					UPixelStreaming2PluginSettings::CVarWebRTCMaxBitrate->Set(MaxBitrate, ECVF_SetByCommandline);
				}
			});

			InputHandler->RegisterMessageHandler(EPixelStreaming2ToStreamerMessage::UIInteraction, [this](FString PlayerId, FMemoryReader Ar) { HandleUIInteraction(Ar); });

			// Handle special cases when the InputHandler itself wants to send a message out to all the peers.
			// Some special cases include when virtual gamepads are connected and a controller id needs to be transmitted.
			TWeakPtr<IPixelStreaming2Streamer> WeakStreamer = Streamer;
			InputHandler->OnSendMessage.AddLambda([WeakStreamer](FString MessageName, FMemoryReader Ar) {
				TSharedPtr<IPixelStreaming2Streamer> Streamer = WeakStreamer.Pin();
				if (!Streamer)
				{
					return;
				}
				FString Descriptor;
				Ar << Descriptor;
				Streamer->SendAllPlayersMessage(MessageName, Descriptor);
			});
		}
	}

	void FPixelStreaming2Module::HandleUIInteraction(FMemoryReader Ar)
	{
		FString Res;
		Res.GetCharArray().SetNumUninitialized(Ar.TotalSize() / 2 + 1);
		Ar.Serialize(Res.GetCharArray().GetData(), Ar.TotalSize());

		FString Descriptor = Res.Mid(1);

		UE_LOG(LogPixelStreaming2, Verbose, TEXT("UIInteraction: %s"), *Descriptor);
		for (UPixelStreaming2Input* InputComponent : InputComponents)
		{
			InputComponent->OnInputEvent.Broadcast(Descriptor);
		}
	}

	FString FPixelStreaming2Module::GetFieldTrials()
	{
		FString FieldTrials = UPixelStreaming2PluginSettings::CVarWebRTCFieldTrials.GetValueOnAnyThread();

		// Set the WebRTC-FrameDropper/Disabled/ if the CVar is set
		if (UPixelStreaming2PluginSettings::CVarWebRTCDisableFrameDropper.GetValueOnAnyThread())
		{
			FieldTrials += TEXT("WebRTC-FrameDropper/Disabled/");
		}

		if (UPixelStreaming2PluginSettings::CVarWebRTCEnableFlexFec.GetValueOnAnyThread())
		{
			FieldTrials += TEXT("WebRTC-FlexFEC-03-Advertised/Enabled/WebRTC-FlexFEC-03/Enabled/");
		}

		// Parse "WebRTC-Video-Pacing/" field trial
		{
			float PacingFactor = UPixelStreaming2PluginSettings::CVarWebRTCVideoPacingFactor.GetValueOnAnyThread();
			float PacingMaxDelayMs = UPixelStreaming2PluginSettings::CVarWebRTCVideoPacingMaxDelay.GetValueOnAnyThread();
			;
			if (PacingFactor >= 0.0f || PacingMaxDelayMs >= 0.0f)
			{
				FString VideoPacingFieldTrialStr = TEXT("WebRTC-Video-Pacing/");
				bool	bHasPacingFactor = PacingFactor >= 0.0f;
				if (bHasPacingFactor)
				{
					VideoPacingFieldTrialStr += FString::Printf(TEXT("factor:%.1f"), PacingFactor);
				}
				bool bHasMaxDelay = PacingMaxDelayMs >= 0.0f;
				if (bHasMaxDelay)
				{
					VideoPacingFieldTrialStr += bHasPacingFactor ? TEXT(",") : TEXT("");
					VideoPacingFieldTrialStr += FString::Printf(TEXT("max_delay:%.0f"), PacingMaxDelayMs);
				}
				VideoPacingFieldTrialStr += TEXT("/");
				FieldTrials += VideoPacingFieldTrialStr;
			}
		}

		return FieldTrials;
	}

	bool FPixelStreaming2Module::InitializeEpicRtc()
	{
		// Initialize the audio capturer (if not already called by some user code)
		AudioMixingCapturer = GetAudioCapturer();

		EpicRtcVideoEncoderInitializers = { new FEpicRtcVideoEncoderInitializer() };
		EpicRtcVideoDecoderInitializers = { new FEpicRtcVideoDecoderInitializer() };

		EpicRtcPlatformConfig PlatformConfig{
			._memory = new FEpicRtcAllocator()
		};

		EpicRtcErrorCode Result = GetOrCreatePlatform(PlatformConfig, EpicRtcPlatform.GetInitReference());
		if (Result != EpicRtcErrorCode::Ok && Result != EpicRtcErrorCode::FoundExistingPlatform)
		{
			UE_LOG(LogPixelStreaming2, Warning, TEXT("Unable to create EpicRtc Platform. GetOrCreatePlatform returned %s"), *ToString(Result));
			return false;
		}

		FUtf8String EpicRtcFieldTrials(GetFieldTrials());

		WebsocketFactory = MakeRefCount<FEpicRtcWebsocketFactory>();

		StatsCollector = MakeRefCount<FEpicRtcStatsCollector>();

		// clang-format off
		EpicRtcConfig ConferenceConfig = {
			._websocketFactory = WebsocketFactory.GetReference(),
			._signallingType = EpicRtcSignallingType::PixelStreaming,
			._signingPlugin = nullptr,
			._migrationPlugin = nullptr,
			._audioDevicePlugin = nullptr,
			._audioConfig = {
				._tickAdm = true,
				._audioEncoderInitializers = {}, // Not needed because we use the inbuilt audio codecs
				._audioDecoderInitializers = {}, // Not needed because we use the inbuilt audio codecs
				._enableBuiltInAudioCodecs = true,
			},
			._videoConfig = {
				._videoEncoderInitializers = {
					._ptr = const_cast<const EpicRtcVideoEncoderInitializerInterface**>(EpicRtcVideoEncoderInitializers.GetData()),
					._size = (uint64_t)EpicRtcVideoEncoderInitializers.Num()
				},
				._videoDecoderInitializers = {
					._ptr = const_cast<const EpicRtcVideoDecoderInitializerInterface**>(EpicRtcVideoDecoderInitializers.GetData()),
					._size = (uint64_t)EpicRtcVideoDecoderInitializers.Num()
				},
				._enableBuiltInVideoCodecs = false
			},
			._fieldTrials = {
				._fieldTrials = ToEpicRtcStringView(EpicRtcFieldTrials),
				._isGlobal = 0
			},
			._logging = {
				._logger = new FEpicRtcLogsRedirector(),
#if !NO_LOGGING // When building WITH_SHIPPING by default .GetVerbosity() does not exist
				._level = UnrealLogToEpicRtcCategoryMap[LogPixelStreaming2EpicRtc.GetVerbosity()],
				._levelWebRtc = UnrealLogToEpicRtcCategoryMap[LogPixelStreaming2EpicRtc.GetVerbosity()]
#endif
			},
			._stats = {
				._statsCollectorCallback = StatsCollector.GetReference(),
				._statsCollectorInterval = 1000,
				._jsonFormatOnly = false
			}
		};
		// clang-format on

		Result = EpicRtcPlatform->CreateConference(ToEpicRtcStringView(EpicRtcConferenceName), ConferenceConfig, EpicRtcConference.GetInitReference());
		if (Result != EpicRtcErrorCode::Ok)
		{
			UE_LOG(LogPixelStreaming2, Warning, TEXT("Unable to create EpicRtc Conference: CreateConference returned %s"), *ToString(Result));
			return false;
		}

		TickConferenceTask = FEpicRtcTickableTask::Create<FEpicRtcTickConferenceTask>(EpicRtcConference, TEXT("PixelStreaming2Module TickConferenceTask"));

		return true;
	}

	/**
	 * End own methods
	 */
} // namespace UE::PixelStreaming2

IMPLEMENT_MODULE(UE::PixelStreaming2::FPixelStreaming2Module, PixelStreaming2)
