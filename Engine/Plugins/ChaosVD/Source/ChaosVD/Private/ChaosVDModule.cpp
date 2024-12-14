// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDModule.h"

#include "ChaosVDCommands.h"
#include "ChaosVDEngine.h"
#include "ChaosVDParticleActorCustomization.h"
#include "ChaosVDSettingsManager.h"
#include "ChaosVDStyle.h"
#include "ChaosVDTabsIDs.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Trace/ChaosVDTraceManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SChaosVDMainTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "DetailsCustomizations/ChaosVDShapeDataCustomization.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

DEFINE_LOG_CATEGORY(LogChaosVDEditor);

FAutoConsoleCommand ChaosVDSpawnNewCVDInstance(
	TEXT("p.Chaos.VD.SpawnNewCVDInstance"),
	TEXT("Opens a new CVD windows wothout closing an existing one"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FChaosVDModule::Get().SpawnCVDTab();
	})
);

FString FChaosVDModule::ChaosVisualDebuggerProgramName = TEXT("ChaosVisualDebugger");

FChaosVDModule& FChaosVDModule::Get()
{
	return FModuleManager::Get().LoadModuleChecked<FChaosVDModule>(TEXT("ChaosVD"));
}

void FChaosVDModule::StartupModule()
{	
	FChaosVDStyle::Initialize();
	
	FChaosVDCommands::Register();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FChaosVDTabID::ChaosVisualDebuggerTab, FOnSpawnTab::CreateRaw(this, &FChaosVDModule::SpawnMainTab))
								.SetDisplayName(LOCTEXT("VisualDebuggerTabTitle", "Chaos Visual Debugger"))
								.SetTooltipText(LOCTEXT("VisualDebuggerTabDesc", "Opens the Chaos Visual Debugger window"))
								.SetIcon(FSlateIcon(FChaosVDStyle::GetStyleSetName(), "ChaosVisualDebugger"))
								.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory());

	ChaosVDTraceManager = MakeShared<FChaosVDTraceManager>();

	if (IsStandaloneChaosVisualDebugger())
	{
		// In the standalone app, once the engine is initialized we need to spawn the main tab otherwise there will be no UI
		// because we intentionally don't load the mainframe / rest of the editor UI
		FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FChaosVDModule::SpawnCVDTab);
	}

	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FChaosVDModule::CloseActiveInstances);
}

void FChaosVDModule::ShutdownModule()
{
	FChaosVDStyle::Shutdown();
	
	FChaosVDCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FChaosVDTabID::ChaosVisualDebuggerTab);

	for (const FName TabID : CreatedExtraTabSpawnersIDs)
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabID);
	}

	if (IsStandaloneChaosVisualDebugger())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);
	}

	FCoreDelegates::OnEnginePreExit.RemoveAll(this);

	CloseActiveInstances();
	
	FChaosVDSettingsManager::TearDown();
}

void FChaosVDModule::SpawnCVDTab()
{
	if (IsStandaloneChaosVisualDebugger())
	{
		// In the standalone app, we need to load the status bar module so the status bar subsystem is initialized
		FModuleManager::Get().LoadModule("StatusBar");
	}

	// Registering new tab spawners with random names is not the best idea to spawn new tabs, but it is good enough to test we can run multiple instances of CVD withing the editor.
	// This is also why spawning new tabs it is only exposed via console commands for now.
	// When this feature is deemed stable and exposed in the UI I will investigate a more correct way of implementing this
	const FName NewTabID = FName(FChaosVDTabID::ChaosVisualDebuggerTab.ToString() + FGuid::NewGuid().ToString());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(NewTabID, FOnSpawnTab::CreateRaw(this, &FChaosVDModule::SpawnMainTab))
						.SetDisplayName(LOCTEXT("VisualDebuggerTabTitle", "Chaos Visual Debugger"))
						.SetTooltipText(LOCTEXT("VisualDebuggerTabDesc", "Opens the Chaos Visual Debugger window"));

	FGlobalTabmanager::Get()->TryInvokeTab(NewTabID);

	CreatedExtraTabSpawnersIDs.Add(NewTabID);
}

TSharedRef<SDockTab> FChaosVDModule::SpawnMainTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> MainTabInstance =
		SNew(SDockTab)
		.TabRole(ETabRole::MajorTab)
		.Label(LOCTEXT("MainTabLabel", "Chaos Visual Debugger"))
		.ToolTipText(LOCTEXT("MainTabToolTip", "Chaos Visual Debugger is an experimental tool and it can be unstable"));

	// Initialize the Chaos VD Engine instance this tab will represent
	// For now its lifetime will be controlled by this tab
	const TSharedPtr<FChaosVDEngine> ChaosVDEngineInstance = MakeShared<FChaosVDEngine>();
	ChaosVDEngineInstance->Initialize();

	MainTabInstance->SetContent
	(
		SNew(SChaosVDMainTab, ChaosVDEngineInstance)
			.OwnerTab(MainTabInstance.ToSharedPtr())
	);

	const FGuid InstanceGuid = ChaosVDEngineInstance->GetInstanceGuid();
	RegisterChaosVDEngineInstance(InstanceGuid, ChaosVDEngineInstance);

	const SDockTab::FOnTabClosedCallback ClosedCallback = SDockTab::FOnTabClosedCallback::CreateRaw(this, &FChaosVDModule::HandleTabClosed, InstanceGuid);
	MainTabInstance->SetOnTabClosed(ClosedCallback);

	RegisterChaosVDTabInstance(InstanceGuid, MainTabInstance.ToSharedPtr());

	return MainTabInstance;
}

void FChaosVDModule::HandleTabClosed(TSharedRef<SDockTab> ClosedTab, FGuid InstanceGUID)
{
	if (IsStandaloneChaosVisualDebugger())
	{
		// If this is the standalone CVD app, we can assume that tab closed indicates an exit request
		RequestEngineExit(TEXT("MainCVDTabClosed"));
	}

	// Workaround. Currently the ChaosVD Engine instance determines the lifetime of the Editor world and other objects
	// Some widgets, like UE Level viewport tries to iterate on these objects on destruction
	// For now we can avoid any crashes by just de-initializing ChaosVD Engine on the next frame but that is not the real fix.
	// Unless we are shutting down the engine
	
	//TODO: Ensure that systems that uses the Editor World we create know beforehand when it is about to be Destroyed and GC'd
	// Related Jira Task UE-191876
	if (bIsShuttingDown)
	{
		DeregisterChaosVDTabInstance(InstanceGUID);
		DeregisterChaosVDEngineInstance(InstanceGUID);
	}
	else
	{
		DeregisterChaosVDTabInstance(InstanceGUID);

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, InstanceGUID](float DeltaTime)->bool
		{
			DeregisterChaosVDEngineInstance(InstanceGUID);
			return false;
		}));
	}
}

void FChaosVDModule::RegisterChaosVDEngineInstance(const FGuid& InstanceGuid, TSharedPtr<FChaosVDEngine> Instance)
{
	ActiveChaosVDInstances.Add(InstanceGuid, Instance);
}

void FChaosVDModule::DeregisterChaosVDEngineInstance(const FGuid& InstanceGuid)
{
	if (TSharedPtr<FChaosVDEngine>* InstancePtrPtr = ActiveChaosVDInstances.Find(InstanceGuid))
	{
		if (TSharedPtr<FChaosVDEngine> InstancePtr = *InstancePtrPtr)
		{
			InstancePtr->DeInitialize();
		}
	
		ActiveChaosVDInstances.Remove(InstanceGuid);
	}	
}

void FChaosVDModule::RegisterChaosVDTabInstance(const FGuid& InstanceGuid, TSharedPtr<SDockTab> Instance)
{
	ActiveCVDTabs.Add(InstanceGuid, Instance);
}

void FChaosVDModule::DeregisterChaosVDTabInstance(const FGuid& InstanceGuid)
{
	ActiveCVDTabs.Remove(InstanceGuid);
}

void FChaosVDModule::CloseActiveInstances()
{
	bIsShuttingDown = true;
	for (const TPair<FGuid, TWeakPtr<SDockTab>>& CVDTabWithID : ActiveCVDTabs)
	{
		if (TSharedPtr<SDockTab> CVDTab = CVDTabWithID.Value.Pin())
		{
			CVDTab->RequestCloseTab();
		}
		else
		{
			// if the tab Instance no longer exist, make sure the CVD engine instance is shutdown
			DeregisterChaosVDEngineInstance(CVDTabWithID.Key);
		}
	}

	ActiveChaosVDInstances.Reset();
	ActiveCVDTabs.Reset();
}

bool FChaosVDModule::IsStandaloneChaosVisualDebugger()
{
	return FPlatformProperties::IsProgram() && FApp::GetProjectName() == ChaosVisualDebuggerProgramName;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FChaosVDModule, ChaosVD)
