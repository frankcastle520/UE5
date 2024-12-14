// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2EditorModule.h"

#include "PixelStreaming2Style.h"
#include "PixelStreaming2Toolbar.h"
#include "AssetTypeActions_VideoProducer.h"
#include "Engine/GameViewportClient.h"
#include "CoreUtils.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "IPixelStreaming2Module.h"
#include "IPixelStreaming2Streamer.h"
#include "VideoProducerBackBufferComposited.h"
#include "VideoProducerViewport.h"
#include "PixelStreaming2PluginSettings.h"
#include "UnrealEngine.h"
#include "Interfaces/IMainFrameModule.h"
#include "PixelStreaming2Utils.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "LevelEditorViewport.h"
#include "Slate/SceneViewport.h"
#include "SLevelViewport.h"
#include "PixelStreaming2InputEnums.h"
#include "IPixelStreaming2InputModule.h"
#include "IPixelStreaming2InputHandler.h"
#include "IPixelStreaming2DataProtocol.h"
#include "PixelStreaming2Delegates.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Logging.h"

#define LOCTEXT_NAMESPACE "PixelStreaming2EditorModule"

#define IMAGE_BRUSH_SVG(Style, RelativePath, ...) FSlateVectorImageBrush(Style.RootToContentDir(RelativePath, TEXT(".svg")), __VA_ARGS__)

namespace UE::EditorPixelStreaming2
{
	/**
	 * IModuleInterface implementation
	 */
	void FPixelStreaming2EditorModule::StartupModule()
	{
		if (!UE::PixelStreaming2::IsStreamingSupported())
		{
			return;
		}

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_VideoProducer>());

		// Initialize the editor toolbar
		FPixelStreaming2Style::Initialize();
		FSlateStyleSet& StyleInstance = UE::EditorPixelStreaming2::FPixelStreaming2Style::Get();

		const FVector2D Icon20x20(20.0f, 20.0f);
		const FVector2D Icon64x64(64.0f, 64.0f);

		StyleInstance.Set("ClassThumbnail.PixelStreaming2VideoProducerBackBuffer", new IMAGE_BRUSH_SVG(StyleInstance, "PixelStreaming2_64", Icon64x64));
		StyleInstance.Set("ClassIcon.PixelStreaming2VideoProducerBackBuffer", new IMAGE_BRUSH_SVG(StyleInstance, "PixelStreaming2_20", Icon20x20));
		StyleInstance.Set("ClassThumbnail.PixelStreaming2VideoProducerRenderTarget", new IMAGE_BRUSH_SVG(StyleInstance, "PixelStreaming2_64", Icon64x64));
		StyleInstance.Set("ClassIcon.PixelStreaming2VideoProducerRenderTarget", new IMAGE_BRUSH_SVG(StyleInstance, "PixelStreaming2_20", Icon20x20));
		StyleInstance.Set("ClassThumbnail.PixelStreaming2VideoProducerMediaCapture", new IMAGE_BRUSH_SVG(StyleInstance, "PixelStreaming2_64", Icon64x64));
		StyleInstance.Set("ClassIcon.PixelStreaming2VideoProducerMediaCapture", new IMAGE_BRUSH_SVG(StyleInstance, "PixelStreaming2_20", Icon20x20));

		FPixelStreaming2Style::ReloadTextures();

		Toolbar = MakeShared<FPixelStreaming2Toolbar>();

		if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
		{
			Delegates->OnFallbackToSoftwareEncodingNative.AddLambda([]() {
				// Creates a new notification info, we pass in our text as the parameter.
				FNotificationInfo Info(LOCTEXT("PixelStreaming2EditorModule_Notification", "Pixel Streaming: Unable to create hardware encoder, falling back to VP8 software encoding."));
				// Set a default expire duration
				Info.ExpireDuration = 5.0f;
				// And call Add Notification
				FSlateNotificationManager::Get().AddNotification(Info);
			});
		}

		// We register console commands for "Stat xxx" here as the autocomplete logic doesn't execute in the editor
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Stat PixelStreaming2"),
			TEXT("Stats for the Pixel Streaming plugin and its peers."),
			FConsoleCommandDelegate::CreateLambda([]() {
				for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
				{
					UWorld*				 World = WorldContext.World();
					UGameViewportClient* ViewportClient = World->GetGameViewport();
					GEngine->SetEngineStat(World, ViewportClient, TEXT("PixelStreaming2"), true);
				}
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Stat PixelStreaming2Graphs"),
			TEXT("Draws stats graphs for the Pixel Streaming plugin."),
			FConsoleCommandDelegate::CreateLambda([]() {
				for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
				{
					UWorld*				 World = WorldContext.World();
					UGameViewportClient* ViewportClient = World->GetGameViewport();
					GEngine->SetEngineStat(World, ViewportClient, TEXT("PixelStreaming2"), true);
				}
			}),
			ECVF_Default);

		IPixelStreaming2Module& Module = IPixelStreaming2Module::Get();
		Module.OnReady().AddRaw(this, &FPixelStreaming2EditorModule::InitEditorStreaming);
	}

	void FPixelStreaming2EditorModule::ShutdownModule()
	{
		if (!UE::PixelStreaming2::IsStreamingSupported())
		{
			return;
		}

		StopStreaming();
	}

	void FPixelStreaming2EditorModule::InitEditorStreaming(IPixelStreaming2Module& Module)
	{
		FString EditorStreamerID;
		if (!FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingID="), EditorStreamerID))
		{
			EditorStreamerID = "Editor";
		}

		EditorStreamer = Module.CreateStreamer(EditorStreamerID);

		// Bind to start/stop streaming so we disable/restore relevant editor settings
		EditorStreamer->OnStreamingStarted().AddLambda([this](IPixelStreaming2Streamer* Streamer) {
			DisableCPUThrottlingSetting();
		});
		EditorStreamer->OnStreamingStopped().AddLambda([this](IPixelStreaming2Streamer* Streamer) {
			if (!IsEngineExitRequested())
			{
				RestoreCPUThrottlingSetting();
			}
		});

		/**
		 * Called before the engine exits. Separate from OnPreExit as OnEnginePreExit occurs before shutting down any core modules.
		 */
		FCoreDelegates::OnEnginePreExit.AddLambda([this]() {
			RestoreCPUThrottlingSetting();
		});

		// Give the editor streamer the default url if the user hasn't specified one when launching the editor
		if (EditorStreamer->GetSignallingServerURL().IsEmpty())
		{
			// No URL was passed on the command line, initialize defaults
			StreamerPort = 8888;
			SignallingDomain = TEXT("ws://127.0.0.1");

			EditorStreamer->SetSignallingServerURL(FString::Printf(TEXT("%s:%d"), *SignallingDomain, StreamerPort));
		}
		else
		{
			FString			  SpecifiedSignallingURL = EditorStreamer->GetSignallingServerURL();
			TOptional<uint16> ExtractedStreamerPort = FGenericPlatformHttp::GetUrlPort(SpecifiedSignallingURL);
			StreamerPort = (int32)ExtractedStreamerPort.Get(8888);

			FString ExtractedSignallingDomain = FGenericPlatformHttp::GetUrlDomain(SpecifiedSignallingURL);
			if (FGenericPlatformHttp::IsSecureProtocol(SpecifiedSignallingURL).Get(false))
			{
				SignallingDomain = FString::Printf(TEXT("wss://%s"), *ExtractedSignallingDomain);
			}
			else
			{
				SignallingDomain = FString::Printf(TEXT("ws://%s"), *ExtractedSignallingDomain);
			}
		}

		EditorStreamer->SetConfigOption(FName(*FString(TEXT("DefaultToHover"))), TEXT("true"));

		IMainFrameModule::Get().OnMainFrameCreationFinished().AddLambda([&](TSharedPtr<SWindow> RootWindow, bool bIsRunningStartupDialog) {
			MaybeResizeEditor(RootWindow);

			if (UPixelStreaming2PluginSettings::CVarEditorStartOnLaunch.GetValueOnAnyThread())
			{
				EPixelStreaming2EditorStreamTypes Source = UE::PixelStreaming2::GetEnumFromCVar<EPixelStreaming2EditorStreamTypes>(UPixelStreaming2PluginSettings::CVarEditorSource);
				StartStreaming(Source);
			}
		});
	}

	void FPixelStreaming2EditorModule::StartStreaming(EPixelStreaming2EditorStreamTypes InStreamType)
	{
		// Activate our editor streamer
		if (!EditorStreamer.IsValid())
		{
			return;
		}

		TSharedPtr<IPixelStreaming2InputHandler> InputHandler = EditorStreamer->GetInputHandler().Pin();
		if (!InputHandler.IsValid())
		{
			return;
		}

		// Add custom handle for { type: "Command", Resolution.Width: "1920", Resolution.Height: "1080" } when doing Editor streaming
		// because we cannot resize the game viewport, but instead want to resize the parent window.
		InputHandler->SetCommandHandler("Resolution.Width",
			[](FString, FString Descriptor, FString WidthString) {
				bool	bSuccess;
				FString HeightString;
				UE::PixelStreaming2::ExtractJsonFromDescriptor(Descriptor, TEXT("Resolution.Height"), HeightString, bSuccess);
				int Width = FCString::Atoi(*WidthString);
				int Height = FCString::Atoi(*HeightString);
				if (Width < 1 || Height < 1)
				{
					return;
				}

				TSharedPtr<SWindow> ParentWindow = IMainFrameModule::Get().GetParentWindow();
				ParentWindow->Resize(FVector2D(Width, Height));
				FSlateApplication::Get().OnSizeChanged(ParentWindow->GetNativeWindow().ToSharedRef(), Width, Height);
				// Triggers the NullApplication to rebuild its DisplayMetrics with the new resolution and inform slate
				// about the updated virtual desktop size
				FSystemResolution::RequestResolutionChange(Width, Height, GSystemResolution.WindowMode);
				IConsoleManager::Get().CallAllConsoleVariableSinks();
			});

		switch (InStreamType)
		{
			case EPixelStreaming2EditorStreamTypes::LevelEditorViewport:
			{
				FLevelEditorModule&		   LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
				TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
				if (!ActiveLevelViewport.IsValid())
				{
					return;
				}

				FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
				FSceneViewport*				SceneViewport = static_cast<FSceneViewport*>(LevelViewportClient.Viewport);
				InputHandler->SetTargetViewport(SceneViewport->GetViewportWidget());
				InputHandler->SetTargetWindow(SceneViewport->FindWindow());
				InputHandler->SetInputType(EPixelStreaming2InputType::RouteToWindow);
				EditorStreamer->SetVideoProducer(FVideoProducerViewport::Create(EditorStreamer));
			}
			break;
			case EPixelStreaming2EditorStreamTypes::Editor:
			{
				InputHandler->SetTargetViewport(nullptr);
				InputHandler->SetTargetWindow(nullptr);
				InputHandler->SetInputType(EPixelStreaming2InputType::RouteToWindow);

				TSharedPtr<FVideoProducerBackBufferComposited> VideoProducer = FVideoProducerBackBufferComposited::Create();
				VideoProducer->OnFrameSizeChanged.AddSP(InputHandler.ToSharedRef(), &IPixelStreaming2InputHandler::SetTargetScreenRect);
				EditorStreamer->SetVideoProducer(VideoProducer);
			}
			break;
			default:
			{
				UE_LOG(LogPixelStreaming2Editor, Warning, TEXT("Specified Stream Type doesn't have an associated FPixelStreaming2VideoProducer"));
			}
				// Return here as we don't want to start streaming if we didn't set a viewport
				return;
		}

		if (!UPixelStreaming2PluginSettings::CVarEditorUseRemoteSignallingServer.GetValueOnAnyThread())
		{
			EditorStreamer->SetSignallingServerURL(FString::Printf(TEXT("%s:%d"), *SignallingDomain, StreamerPort));
			StartSignalling();
		}

		EditorStreamer->StartStreaming();
	}

	void FPixelStreaming2EditorModule::StopStreaming()
	{
		if (!EditorStreamer.IsValid())
		{
			return;
		}

		if (!UPixelStreaming2PluginSettings::CVarEditorUseRemoteSignallingServer.GetValueOnAnyThread())
		{
			StopSignalling();
		}

		if (TSharedPtr<IPixelStreaming2InputHandler> InputHandler = EditorStreamer->GetInputHandler().Pin())
		{
			InputHandler->SetTargetViewport(nullptr);
			InputHandler->SetTargetWindow(nullptr);
		}

		EditorStreamer->StopStreaming();
	}

	void FPixelStreaming2EditorModule::StartSignalling()
	{
		bool bAlreadyLaunched = SignallingServer.IsValid() && SignallingServer->HasLaunched();
		if (bAlreadyLaunched)
		{
			return;
		}

		// Download Pixel Streaming servers/frontend if we want to use a browser to view Pixel Streaming output
		// but only attempt this is we haven't already started a download before.
		if (!DownloadProcess.IsValid())
		{
			// We set bSkipIfPresent to false, which means the get_ps_servers script will always be run, that script will choose whether to download or not
			DownloadProcess = UE::PixelStreaming2Servers::DownloadPixelStreaming2Servers(false /* bSkipIfPresent */);
			if (DownloadProcess.IsValid())
			{
				DownloadProcess->OnCompleted().BindLambda([this](int ExitCode) {
					StopSignalling();
					StartSignalling();
				});

				return;
			}
		}

		// Launch signalling server
		SignallingServer = UE::PixelStreaming2Servers::MakeSignallingServer();

		UE::PixelStreaming2Servers::FLaunchArgs LaunchArgs;
		LaunchArgs.bPollUntilReady = false;
		LaunchArgs.ReconnectionTimeoutSeconds = 30.0f;
		LaunchArgs.ReconnectionIntervalSeconds = 2.0f;
		LaunchArgs.ProcessArgs = FString::Printf(TEXT("--HttpPort=%d --StreamerPort=%d"), ViewerPort, StreamerPort);

		SignallingServer->Launch(LaunchArgs);
	}

	void FPixelStreaming2EditorModule::StopSignalling()
	{
		if (SignallingServer.IsValid())
		{
			SignallingServer->Stop();
			SignallingServer.Reset();
		}
	}

	TSharedPtr<UE::PixelStreaming2Servers::IServer> FPixelStreaming2EditorModule::GetSignallingServer()
	{
		if (SignallingServer.IsValid())
		{
			return SignallingServer;
		}
		return nullptr;
	}

	void FPixelStreaming2EditorModule::SetSignallingDomain(const FString& InSignallingDomain)
	{
		SignallingDomain = InSignallingDomain;
	}

	void FPixelStreaming2EditorModule::SetStreamerPort(int32 InStreamerPort)
	{
		StreamerPort = InStreamerPort;
	}

	void FPixelStreaming2EditorModule::SetViewerPort(int32 InViewerPort)
	{
		ViewerPort = InViewerPort;
	}

	bool FPixelStreaming2EditorModule::ParseResolution(const TCHAR* InResolution, uint32& OutX, uint32& OutY)
	{
		if (*InResolution)
		{
			FString CmdString(InResolution);
			CmdString = CmdString.TrimStartAndEnd().ToLower();

			// Retrieve the X dimensional value
			const uint32 X = FMath::Max(FCString::Atof(*CmdString), 0.0f);

			// Determine whether the user has entered a resolution and extract the Y dimension.
			FString YString;

			// Find separator between values (Example of expected format: 1280x768)
			const TCHAR* YValue = nullptr;
			if (FCString::Strchr(*CmdString, 'x'))
			{
				YValue = FCString::Strchr(*CmdString, 'x') + 1;
				YString = YValue;
				// Remove any whitespace from the end of the string
				YString = YString.TrimStartAndEnd();
			}

			// If the Y dimensional value exists then setup to use the specified resolution.
			uint32 Y = 0;
			if (YValue && YString.Len() > 0)
			{
				if (YString.IsNumeric())
				{
					Y = FMath::Max(FCString::Atof(YValue), 0.0f);
					OutX = X;
					OutY = Y;
					return true;
				}
			}
		}
		return false;
	}

	void FPixelStreaming2EditorModule::MaybeResizeEditor(TSharedPtr<SWindow> RootWindow)
	{
		uint32	ResolutionX, ResolutionY = 0;
		FString ResolutionStr;
		bool	bSuccess = FParse::Value(FCommandLine::Get(), TEXT("EditorPixelStreamingRes="), ResolutionStr);
		if (bSuccess)
		{
			bSuccess = ParseResolution(*ResolutionStr, ResolutionX, ResolutionY);
		}
		else
		{
			bool UserSpecifiedWidth = FParse::Value(FCommandLine::Get(), TEXT("EditorPixelStreamingResX="), ResolutionX);
			bool UserSpecifiedHeight = FParse::Value(FCommandLine::Get(), TEXT("EditorPixelStreamingResY="), ResolutionY);
			bSuccess = UserSpecifiedWidth | UserSpecifiedHeight;

			const float AspectRatio = 16.0 / 9.0;
			if (UserSpecifiedWidth && !UserSpecifiedHeight)
			{
				ResolutionY = int32(ResolutionX / AspectRatio);
			}
			else if (UserSpecifiedHeight && !UserSpecifiedWidth)
			{
				ResolutionX = int32(ResolutionY * AspectRatio);
			}
		}

		if (bSuccess)
		{
			// Update editor window size
			RootWindow->Resize(FVector2D(ResolutionX, ResolutionY));
			FSlateApplication::Get().OnSizeChanged(RootWindow->GetNativeWindow().ToSharedRef(), ResolutionX, ResolutionY);
			// Triggers the NullApplication to rebuild its DisplayMetrics with the new resolution and inform slate
			// about the updated virtual desktop size
			FSystemResolution::RequestResolutionChange(ResolutionX, ResolutionY, GSystemResolution.WindowMode);
			IConsoleManager::Get().CallAllConsoleVariableSinks();
		}
	}

	void FPixelStreaming2EditorModule::RestoreCPUThrottlingSetting()
	{
		UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
		Settings->bThrottleCPUWhenNotForeground = bOldCPUThrottlingSetting;
		Settings->PostEditChange();
	}

	void FPixelStreaming2EditorModule::DisableCPUThrottlingSetting()
	{
		// Update editor settings so that editor won't slow down if not in focus
		UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();

		// Store whatever value the user had in here so we can restore it when we are done streaming.
		bOldCPUThrottlingSetting = Settings->bThrottleCPUWhenNotForeground;

		if (Settings->bThrottleCPUWhenNotForeground)
		{
			Settings->bThrottleCPUWhenNotForeground = false;
			Settings->PostEditChange();

			// Let the user know we are forcing this editor setting (so they know why their setting is not working potentially)
			// We can also use the new keyword here because the notification manager will call delete once the notification has been displayed
			FNotificationInfo* Info = new FNotificationInfo(LOCTEXT("PixelStreaming2EditorModule_CPUThrottlingNotification", "Pixel Streaming: Disabling setting \"Use less CPU in background\" for streaming performance."));
			Info->ExpireDuration = 5.0f;
			FSlateNotificationManager::Get().QueueNotification(Info);
		}
	}
} // namespace UE::EditorPixelStreaming2

#undef IMAGE_BRUSH_SVG

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(UE::EditorPixelStreaming2::FPixelStreaming2EditorModule, PixelStreaming2Editor)
