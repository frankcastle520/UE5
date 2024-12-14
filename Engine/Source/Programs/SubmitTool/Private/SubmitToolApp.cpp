// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmitToolApp.h"
#include "CoreMinimal.h"
#include "SubmitToolUtils.h"
#include "Models/SubmitToolUserPrefs.h"

#include "RequiredProgramMainCPPInclude.h"
#include "StandaloneRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"

#include "CommandLine/CmdLineParameters.h"

#include "View/SubmitToolWindow.h"
#include "View/SubmitToolStyle.h"
#include "View/SubmitToolCommandHandler.h"
#include "View/SubmitToolMenu.h"

#include "Models/ModelInterface.h"

#include "Parameters/SubmitToolParameters.h"
#include "Parameters/SubmitToolParametersBuilder.h"

#include "Telemetry/TelemetryService.h"
#include "Version/AppVersion.h"

#include "Configuration/Configuration.h"

IMPLEMENT_APPLICATION(SubmitTool, "SubmitTool");

#define LOCTEXT_NAMESPACE "SubmitTool"


int RunSubmitTool(const TCHAR* CommandLine, const FGuid& InSessionID)
{
	FTaskTagScope TaskTagScope(ETaskTag::EGameThread);

	// need to make sure the cwd is correct before doing anything else
	FPlatformProcess::SetCurrentWorkingDirectoryToBaseDir();

	// start up the main loop
	GEngineLoop.PreInit(CommandLine);

	// ensure that the backlog is enabled
	if(GLog)
	{
		GLog->EnableBacklog(true);
	}

	UE_LOG(LogSubmitToolDebug, Log, TEXT("%s"), CommandLine);

	if(!FCmdLineParameters::Get().ValidateParameters())
	{
		UE_LOG(LogSubmitTool, Error, TEXT("Command line is not valid"));
		FCmdLineParameters::Get().LogParameters();
		FModelInterface::SetErrorState();
	}

	// Make sure all UObject classes are registered and default properties have been initialized
	ProcessNewlyLoadedUObjects();

	// Tell the module manager it may now process newly-loaded UObjects when new C++ modules are loaded
	FModuleManager::Get().StartProcessingNewlyLoadedObjects();

	// crank up a normal Slate application using the platform's standalone renderer
	FSlateApplication::InitializeAsStandaloneApplication(GetStandardStandaloneRenderer());
	FAppStyle::SetAppStyleSet(FSubmitToolStyle::Get());
	FSlateApplication::InitHighDPI(true);


	// App Scope
	{
		UE_LOG(LogSubmitTool, Log, TEXT("Version %s"), *FAppVersion::GetVersion());
		TUniquePtr<FSubmitToolUserPrefs> UserPrefs = FSubmitToolUserPrefs::Initialize(GetUserPrefsPath());

		// initialize the configuration system
		FConfiguration::Init();

		FString ParameterFile;
		FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::ParameterFile, ParameterFile);
		FSubmitToolParametersBuilder ParametersBuilder(ParameterFile);
		FSubmitToolParameters Parameters = ParametersBuilder.Build();

		FTelemetryService::Init(Parameters.Telemetry.Url, InSessionID);

		// Create a new instance of model Interface so that UI can communicate
		const TUniquePtr<FModelInterface> ModelInterface = MakeUnique<FModelInterface>(Parameters);

		// record that the application has started
		FTelemetryService::Get()->Start(ModelInterface->GetServiceProvider()->GetService<ISTSourceControlService>()->GetCurrentStreamName());

		// UI Scope
		{
			// Build the slate UI for the program window
			TSharedRef<SDockTab> MainDockTab = SNew(SDockTab);
			TSharedPtr<FTabManager> TabManager = FGlobalTabmanager::Get()->NewTabManager(MainDockTab);
			// set the application name
			FGlobalTabmanager::Get()->SetApplicationTitle(LOCTEXT("AppTitle", "SubmitTool"));
			TabManager->SetCanDoDragOperation(false);

			// set the main menu commands and interface
			TSharedRef<FUICommandList> CommandList = MakeShared<FUICommandList>();
			FSubmitToolCommandHandler CommandHandler;
			CommandHandler.AddToCommandList(ModelInterface.Get(), CommandList);

			FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(CommandList);
			MenuBarBuilder.AddPullDownMenu(LOCTEXT("MainMenu", "Main Menu"), LOCTEXT("OpensMainMenu", "Opens Main Menu"), FNewMenuDelegate::CreateStatic(&FSubmitToolMenu::FillMainMenuEntries));
#if !UE_BUILD_SHIPPING
			MenuBarBuilder.AddPullDownMenu(LOCTEXT("Debug Tools", "Debug"), LOCTEXT("OpensDebugMenu", "Opens Debug Menu"), FNewMenuDelegate::CreateStatic(&FSubmitToolMenu::FillDebugMenuEntries));
#endif

			const TSharedRef<SWidget> MenuWidget = MenuBarBuilder.MakeWidget();
			TabManager->SetAllowWindowMenuBar(true);
			TabManager->SetMenuMultiBox(MenuBarBuilder.GetMultiBox(), MenuWidget);

			SubmitToolWindow Window = SubmitToolWindow(ModelInterface.Get());
			FName TabName = FName("Submit Tool");

			TabManager->RegisterTabSpawner(TabName, FOnSpawnTab::CreateLambda([&Window](const FSpawnTabArgs& SpawnArgs) { return Window.BuildMainTab(SpawnArgs.GetOwnerWindow()); }));
			TabManager->RegisterDefaultTabWindowSize(TabName, FVector2D(1024, 600));

			TSharedPtr<SDockTab> Tab = TabManager->TryInvokeTab(TabName);
			FWindowSizeLimits WindowLimits;
			WindowLimits.SetMinWidth(600);
			WindowLimits.SetMinHeight(400);
			Tab->GetParentWindow()->SetSizeLimits(WindowLimits);

			if(!UserPrefs->WindowPosition.IsZero())
			{
				FDisplayMetrics DisplayMetrics;
				FSlateApplicationBase::Get().GetCachedDisplayMetrics(DisplayMetrics);
				const FPlatformRect& VirtualDisplayRect = DisplayMetrics.VirtualDisplayRect;

				if(UserPrefs->WindowPosition.X >= VirtualDisplayRect.Left &&
					UserPrefs->WindowPosition.X < VirtualDisplayRect.Right &&
					UserPrefs->WindowPosition.Y >= VirtualDisplayRect.Top &&
					UserPrefs->WindowPosition.Y < VirtualDisplayRect.Bottom)
				{
					Tab->GetParentWindow()->MoveWindowTo(UserPrefs->WindowPosition);
				}
			}

			if(!UserPrefs->WindowSize.IsZero())
			{
				Tab->GetParentWindow()->Resize(UserPrefs->WindowSize);
			}
			else
			{
				Tab->GetParentWindow()->Resize(FDeprecateSlateVector2D(1024, 768));
			}

			// Loop the engine
			while(!IsEngineExitRequested())
			{
				BeginExitIfRequested();

				FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
				FStats::AdvanceFrame(false);
				FTSTicker::GetCoreTicker().Tick(FSlateApplication::Get().GetDeltaTime());
				FSlateApplication::Get().PumpMessages();
				FSlateApplication::Get().Tick();

				if(IsEngineExitRequested())
				{
					// Dispose here so SCCProvider has time to clean up
					ModelInterface->Dispose();
				}

				FPlatformProcess::Sleep(0.01f);
				GFrameCounter++;

			}
		}
		// ensure all the telemetry events are flushed before unloading modules
		FTelemetryService::BlockFlush(5.f);
		FTelemetryService::Shutdown();
	}

	FSlateApplication::Shutdown();

	FEngineLoop::AppPreExit();
	FModuleManager::Get().UnloadModulesAtShutdown();
	FEngineLoop::AppExit();

	return 0;
}

FString GetUserPrefsPath()
{
	return FPaths::Combine(FSubmitToolUtils::GetLocalAppDataPath(), "SubmitTool", "SubmitToolPrefs.ini");
}
//Test comment for submit tool testing (remove if needed) x6
#undef LOCTEXT_NAMESPACE
