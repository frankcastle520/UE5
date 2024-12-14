// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorPerformanceModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"
#include "SEditorPerformanceDialogs.h"
#include "SEditorPerformanceStatusBar.h"
#include "Styling/AppStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#include "KPIValue.h"
#include "Editor.h"
#include "ToolMenus.h"
#include "Editor/EditorPerformanceSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DerivedDataCacheUsageStats.h"
#include "Virtualization/VirtualizationSystem.h"
#include "Trace/Trace.h"
#include "StudioTelemetry.h"
#include "HAL/PlatformFileManager.h"
#include "ProfilingDebugging/StallDetector.h"

#define LOCTEXT_NAMESPACE "EditorPerformance"
 
IMPLEMENT_MODULE(FEditorPerformanceModule, EditorPerformance );

// Declare the KPI names and limits
const FName EditorCategoryName = TEXT("Editor");
const FName PIECategoryName = TEXT("PIE");
const FName CacheCategoryName = TEXT("Cache");
const FName HardwareCategoryName = TEXT("Hardware");

const FName EditorBootKPIName = TEXT("Boot");
const FName EditorInitializeKPIName = TEXT("Initialize");
const FName EditorLoadMapKPIName = TEXT("Load Map");
const FName EditorHitchRateKPIName = TEXT("Hitch Rate");
const FName EditorStallRateKPIName = TEXT("Stall Rate");
const FName EditorAssetRegistryScanKPIName = TEXT("Asset Registry Scan");
const FName EditorPluginCountKPIName = TEXT("Plugin Count");
const FName TotalTimeToEditorKPIName = TEXT("Total Time To Editor");
const FName TotalTimeToPIEKPIName = TEXT("Total Time To PIE");
const FName PIEFirstTransitionKPIName = TEXT("First Transition");
const FName PIETransitionKPIName = TEXT("Iterative Transition");
const FName PIEShutdownKPIName = TEXT("Shutdown");
const FName PIEHitchRateKPIName = TEXT("Hitch Rate");
const FName PIEStallRateKPIName = TEXT("Stall Rate");
const FName CloudDDCLatencyKPIName = TEXT("Unreal Cloud DDC Latency");
const FName CloudDDCReadSpeedKPIName = TEXT("Unreal Cloud DDC Speed");
const FName TotalDDCEfficiencyKPIName = TEXT("Effective Efficiency");
const FName LocalDDCEfficiencyKPIName = TEXT("Local Efficiency");
const FName VirtualAssetEfficiencyKPIName = TEXT("Virtual Asset Efficiency");
const FName CoreCountKPIName = TEXT("Core Count");
const FName TotalMemoryKPIName = TEXT("Total Memory");
const FName AvailableMemoryKPIName = TEXT("Available Memory");

float EditorBootKPILimit = 100;
float EditorInitializeKPILimit = 160;
float EditorLoadMapKPILimit = 120;
float EditorHitchRateKPILimit = 25;
float EditorStallRateKPILimit = 25;
float EditorAssetRegistryScanKPILimit = 140;
float EditorPluginCountKPILimit = 1500;
float TotalTimeToEditorKPILimit = 160;
float PIEFirstTransitionKPILimit = 220;
float PIETransitionKPILimit = 40;
float PIEShutdownKPILimit = 10;
float PIEHitchRateKPILimit = 25;
float PIEStallRateKPILimit = 25;
float TotalTimeToPIEKPILimit = 600;
float CloudDDCLatencyKPILimit = 100;
float CloudDDCReadSpeedKPILimit = 10;
float TotalDDCEffciencyKPILimit = 90;
float LocalDDCEffciencyKPILimit = 85;
float VirtualAssetEfficiencyKPILimit = 95;
float CoreCountKPILimit = 32;
float TotalMemoryKPILimit = 64;
float AvailableMemoryKPILimit = 16;

 
static const FName EditorPerformanceReportTabName = FName(TEXT("EditorPerformanceReportTab"));

void FEditorPerformanceModule::StartupModule()
{
	InitializeKPIs();
}

void FEditorPerformanceModule::ShutdownModule()
{
	TerminateEditor();
	TerminateKPIs();
}

void FEditorPerformanceModule::InitializeEditor()
{
	UEditorPerformanceSettings* EditorPerformanceSettings = GetMutableDefault<UEditorPerformanceSettings>();

	// Check if we want to have the tool enabled or not.
	if (EditorPerformanceSettings && EditorPerformanceSettings->bEnableEditorPeformanceTool==false )
	{
		return;
	}

	if (EditorPerformanceSettings)
	{
		// Enable any experimental features
		if (EditorPerformanceSettings->bEnableExperimentalFeatures)
		{
			CloudDDCLatencyKPI			= KPIRegistry.DeclareKPIValue(CacheCategoryName, CloudDDCLatencyKPIName, 0.0, CloudDDCLatencyKPILimit, FKPIValue::LessThan, FKPIValue::Milliseconds);
			CloudDDCReadSpeedKPI		= KPIRegistry.DeclareKPIValue(CacheCategoryName, CloudDDCReadSpeedKPIName, 100.0, CloudDDCReadSpeedKPILimit, FKPIValue::GreaterThan, FKPIValue::MegaBitsPerSecond);
			EditorStallRateKPI			= KPIRegistry.DeclareKPIValue(EditorCategoryName, EditorStallRateKPIName, 0.0, EditorStallRateKPILimit, FKPIValue::LessThan, FKPIValue::Percent);
			PIEStallRateKPI				= KPIRegistry.DeclareKPIValue(PIECategoryName, PIEStallRateKPIName, 0.0, PIEStallRateKPILimit, FKPIValue::LessThan, FKPIValue::Percent);
			VirtualAssetEfficiencyKPI	= KPIRegistry.DeclareKPIValue(CacheCategoryName, VirtualAssetEfficiencyKPIName, 100.0, VirtualAssetEfficiencyKPILimit, FKPIValue::GreaterThan, FKPIValue::Percent);
		}

		// Populate the notification list with all KPI values if it is empty. 
		if (EditorPerformanceSettings->NotificationList.IsEmpty())
		{
			for (FKPIValues::TConstIterator It(KPIRegistry.GetKPIValues()); It; ++It)
			{
				EditorPerformanceSettings->NotificationList.Emplace(It->Value.Path);
			}

			EditorPerformanceSettings->PostEditChange();
			EditorPerformanceSettings->SaveConfig();
		}
	}

	FTimerDelegate HeartBeatDelegate;
	HeartBeatDelegate.BindRaw(this, &FEditorPerformanceModule::HeartBeatCallback);
	GEditor->GetTimerManager()->SetTimer(HeartBeatTimerHandle, HeartBeatDelegate, HeartBeatIntervalSeconds, true);
	
	FTimerDelegate HitchSamplerDelegate;
	HitchSamplerDelegate.BindRaw(this, &FEditorPerformanceModule::HitchSamplerCallback);
	GEditor->GetTimerManager()->SetTimer(HitchSamplerTimerHandle, HitchSamplerDelegate, HitchSamplerIntervalSeconds, true);

	const FSlateIcon PerformanceReportIcon(FAppStyle::GetAppStyleSetName(), "EditorPerformance.Report.Panel");

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(EditorPerformanceReportTabName, FOnSpawnTab::CreateRaw(this, &FEditorPerformanceModule::CreatePerformanceReportTab))
		.SetDisplayName(LOCTEXT("EditorPerformanceReportTabTitle", "Editor Performance"))
		.SetTooltipText(LOCTEXT("EditorPerformanceReportTabToolTipText", "Opens the Editor Performance Report tab."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory())
		.SetIcon(PerformanceReportIcon);

#if WITH_RELOAD
	// This code attempts to relaunch the tabs when you reload this module
	if (IsReloadActive() && FSlateApplication::IsInitialized())
	{
		ShowPerformanceReportTab();
	}
#endif // WITH_RELOAD

	FEditorPerformanceStatusBarMenuCommands::Register();

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.StatusBar.ToolBar");

	FToolMenuSection& EditorPerfSection = Menu->AddSection("Perf", FText::GetEmpty(), FToolMenuInsert("DDC", EToolMenuInsertType::Before));

	EditorPerfSection.AddEntry(FToolMenuEntry::InitWidget("EditorPerformanceStatusBar", CreateStatusBarWidget(), FText::GetEmpty(), true, false));
}

void FEditorPerformanceModule::TerminateEditor()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(EditorPerformanceReportTabName);

		if (PerformanceReportTab.IsValid())
		{
			PerformanceReportTab.Pin()->RequestCloseTab();
		}
	}

	FEditorPerformanceStatusBarMenuCommands::Unregister();
}

TSharedRef<SWidget> FEditorPerformanceModule::CreateStatusBarWidget()
{
	return SNew(SEditorPerformanceStatusBarWidget);
}

TSharedPtr<SWidget> FEditorPerformanceModule::CreatePerformanceReportDialog()
{
	return SNew(SEditorPerformanceReportDialog);
}


TSharedRef<SDockTab> FEditorPerformanceModule::CreatePerformanceReportTab(const FSpawnTabArgs& Args)
{
	return SAssignNew(PerformanceReportTab, SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			CreatePerformanceReportDialog().ToSharedRef()
		];
}

void FEditorPerformanceModule::ShowPerformanceReportTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FTabId(EditorPerformanceReportTabName));
}

void FEditorPerformanceModule::InitializeKPIs()
{
	// Declare the KPIs 
	EditorBootKPI				= KPIRegistry.DeclareKPIValue(EditorCategoryName, EditorBootKPIName, 0.0, EditorBootKPILimit, FKPIValue::LessThan, FKPIValue::Minutes);
	EditorInitializeKPI			= //KPIRegistry.DeclareKPIValue(EditorCategoryName, EditorInitializeKPIName, 0.0, EditorInitializeKPILimit, FKPIValue::LessThan, FKPIValue::Minutes);
	EditorLoadMapKPI			= KPIRegistry.DeclareKPIValue(EditorCategoryName, EditorLoadMapKPIName, 0.0, EditorLoadMapKPILimit, FKPIValue::LessThan, FKPIValue::Minutes);
	EditorAssetRegistryScanKPI	= KPIRegistry.DeclareKPIValue(EditorCategoryName, EditorAssetRegistryScanKPIName, 0.0, EditorAssetRegistryScanKPILimit, FKPIValue::LessThan, FKPIValue::Minutes);
	EditorPluginCountKPI		= KPIRegistry.DeclareKPIValue(EditorCategoryName, EditorPluginCountKPIName, 0.0, EditorPluginCountKPILimit, FKPIValue::LessThan, FKPIValue::Decimal);
	EditorHitchRateKPI			= KPIRegistry.DeclareKPIValue(EditorCategoryName, EditorHitchRateKPIName, 0.0, EditorHitchRateKPILimit, FKPIValue::LessThan, FKPIValue::Percent);
	TotalTimeToEditorKPI		= KPIRegistry.DeclareKPIValue(EditorCategoryName, TotalTimeToEditorKPIName, 0.0, TotalTimeToEditorKPILimit, FKPIValue::LessThan, FKPIValue::Minutes);
	PIEFirstTransitionKPI		= KPIRegistry.DeclareKPIValue(PIECategoryName, PIEFirstTransitionKPIName, 0.0, PIEFirstTransitionKPILimit, FKPIValue::LessThan, FKPIValue::Minutes);
	PIETransitionKPI			= KPIRegistry.DeclareKPIValue(PIECategoryName, PIETransitionKPIName, 0.0, PIETransitionKPILimit, FKPIValue::LessThan, FKPIValue::Minutes);
	PIEShutdownKPI				= KPIRegistry.DeclareKPIValue(PIECategoryName, PIEShutdownKPIName, 0.0, PIEShutdownKPILimit, FKPIValue::LessThan, FKPIValue::Minutes);
	PIEHitchRateKPI				= KPIRegistry.DeclareKPIValue(PIECategoryName, PIEHitchRateKPIName, 0.0, PIEHitchRateKPILimit, FKPIValue::LessThan, FKPIValue::Percent);
	TotalTimeToPIEKPI			= KPIRegistry.DeclareKPIValue(PIECategoryName, TotalTimeToPIEKPIName, 0.0, TotalTimeToPIEKPILimit, FKPIValue::LessThan, FKPIValue::Minutes);
	//TotalDDCEfficiencyKPI		= KPIRegistry.DeclareKPIValue(CacheCategoryName, TotalDDCEfficiencyKPIName, 100.0, TotalDDCEffciencyKPILimit, FKPIValue::GreaterThan, FKPIValue::Percent);
	LocalDDCEfficiencyKPI		= KPIRegistry.DeclareKPIValue(CacheCategoryName, LocalDDCEfficiencyKPIName, 100.0, LocalDDCEffciencyKPILimit, FKPIValue::GreaterThan, FKPIValue::Percent);
	CoreCountKPI				= KPIRegistry.DeclareKPIValue(HardwareCategoryName, CoreCountKPIName, 128.0, CoreCountKPILimit, FKPIValue::GreaterThanOrEqual, FKPIValue::Decimal);
	TotalMemoryKPI				= KPIRegistry.DeclareKPIValue(HardwareCategoryName, TotalMemoryKPIName, 128.0, TotalMemoryKPILimit, FKPIValue::GreaterThanOrEqual, FKPIValue::GigaBytes);
	AvailableMemoryKPI			= KPIRegistry.DeclareKPIValue(HardwareCategoryName, AvailableMemoryKPIName, 128.0, AvailableMemoryKPILimit, FKPIValue::GreaterThanOrEqual, FKPIValue::GigaBytes);
	
	// Declare the KPI Hints
	KPIRegistry.DeclareKPIHint(EditorBootKPI, LOCTEXT("EditorBootHintMessage", "The Editor boot time is slow.\nCheck you have enbabled a Game Feature Plugin profile for your project and that the expected local cache efficiency is met.\nIf you are booting the Editor in the background then disable the Use Less CPU in Background option in the settings."), LOCTEXT("EditorBootHintURL","https://docs.unrealengine.com/5.0/en-US/"));
	KPIRegistry.DeclareKPIHint(TotalTimeToEditorKPI, LOCTEXT("EditorStartupHintMessage", "The Editor start-up time is slow.\nCheck you have enbabled a Game Feature Plugin profile for your project and that the expected local cache efficiency is met.\nIf you are booting the Editor in the background then disable the Use Less CPU in Background option in the settings."), LOCTEXT("EditorBootHintURL","https://docs.unrealengine.com/5.0/en-US/"));
	KPIRegistry.DeclareKPIHint(EditorPluginCountKPI, LOCTEXT("EditorPluginHintMessage", "The Editor is loading more plugins than expected and this will affect Editor start-up performance.\nCheck you have enbabled a Game Feature Plugin profile for your project."), LOCTEXT("EditorPluginHintURL", "https://docs.unrealengine.com/5.0/en-US/"));

	KPIRegistry.DeclareKPIHint(PIETransitionKPI, LOCTEXT("PIETransitionHintMessage", "The Editor transtion to PIE is slow.\nCheck that the expected local cache efficiency is met.\nIf you are transitioning to PIE with the Editor in the background then disable the Use Less CPU in Background option in the settings."), LOCTEXT("PIETransitionHintURL", "https://docs.unrealengine.com/5.0/en-US/"));
	
	KPIRegistry.DeclareKPIHint(LocalDDCEfficiencyKPI, LOCTEXT("LocalCacheEfficencyHintMessage", "The Editor will not perform well if the local cache efficiency has not yet met the expected value.\nIf this is the first time you have booted the Editor after a sync then this is to be expected."), LOCTEXT("EditorCacheHintURL","https://docs.unrealengine.com/5.3/en-US/derived-data-cache/"));
	
	KPIRegistry.DeclareKPIHint(CoreCountKPI, LOCTEXT("LowCoreCountHintMessage", "Your hardware has a low CPU core count.\nUsing a lower than recommended hardware specifcation for development is not recommended for good developer efficiency."), LOCTEXT("LowCoreCountHintURL", "https://docs.unrealengine.com/5.0/en-US/"));
	KPIRegistry.DeclareKPIHint(TotalMemoryKPI, LOCTEXT("LowTotalMemoryHintMessage", "Your hardware has a low Total Memory.\nUsing a lower than recommended hardware specifcation for development is not recommended for good developer efficiency."), LOCTEXT("LowTotalMemoryHintURL", "https://docs.unrealengine.com/5.0/en-US/"));
	KPIRegistry.DeclareKPIHint(AvailableMemoryKPI, LOCTEXT("LowAvaliableMemoryHintMessage", "You hardware is running low on memory.\nTry closing applciations that are no longer needed to recover available memory."), LOCTEXT("LowAvailableMemoryHintURL", "https://docs.unrealengine.com/5.0/en-US/"));

	// Load the KPI profiles
	KPIRegistry.LoadKPIProfiles(TEXT("EditorPerformance.Profile"), GEditorIni);

	// Apply any non map specific profiles
	for (FKPIProfiles::TConstIterator It(KPIRegistry.GetKPIProfiles()); It; ++It)
	{
		const FKPIProfile& Profile = It->Value;

		if (Profile.MapName.IsEmpty())
		{
			KPIProfileName = It->Key;
			KPIRegistry.ApplyKPIProfile(It->Value);
		}
	}

	// Gather hardware stats
	KPIRegistry.SetKPIValue(CoreCountKPI, float(FPlatformMisc::NumberOfCores()));
	KPIRegistry.SetKPIValue(TotalMemoryKPI, static_cast<float>(FPlatformMemory::GetStats().TotalPhysical) / (1024.0f * 1024.0f * 1024.0f));

	EditorState = EEditorState::Editor_Boot;

	// Register the delegates
	FEditorDelegates::OnEditorBoot.AddLambda([this](double TimeToBootEditor )
		{
			EditorBootTime = (float)TimeToBootEditor;
			KPIRegistry.SetKPIValue(EditorBootKPI, EditorBootTime);
			EditorState = EEditorState::Editor_Initialize;
		});

	FEditorDelegates::OnEditorInitialized.AddLambda([this](double TimeToInitializeEditor)
		{
			EditorStartUpTime = (float)TimeToInitializeEditor-EditorLoadMapTime;
			
			KPIRegistry.SetKPIValue(EditorInitializeKPI, EditorStartUpTime-EditorBootTime );
			KPIRegistry.SetKPIValue(TotalTimeToEditorKPI, EditorStartUpTime);

			InitializeEditor();

			EditorState = EEditorState::Editor_Interact;
		});

	FEditorDelegates::OnMapLoad.AddLambda([this](const FString& MapName, FCanLoadMap& OutCanLoadMap)
		{
			LoadMapStartTime = FDateTime::UtcNow();
			IsLoadingMap = true;
		});

	FEditorDelegates::OnMapOpened.AddLambda([this](const FString& MapName, bool Unused)
		{
			IsLoadingMap = false;

			if (MapName.Len() > 0)
			{
				EditorMapName = FPaths::GetBaseFilename(MapName);

				EditorLoadMapTime = float((FDateTime::UtcNow() - LoadMapStartTime).GetTotalSeconds());
				KPIRegistry.SetKPIValue(EditorLoadMapKPI, EditorLoadMapTime);

				// Apply any profile that matches the currently loaded map
				for (FKPIProfiles::TConstIterator It(KPIRegistry.GetKPIProfiles()); It; ++It)
				{
					const FKPIProfile& Profile = It->Value;

					if (Profile.MapName == EditorMapName)
					{
						KPIProfileName = It->Key;
						KPIRegistry.ApplyKPIProfile(It->Value);
					}
				}
			}
		});

	FEditorDelegates::StartPIE.AddLambda([this](bool)
		{
			PIEStartTime = FDateTime::UtcNow();
			EditorState = EEditorState::PIE_Startup;
		});

	FWorldDelegates::OnPIEReady.AddLambda([this](UGameInstance* GameInstance)
		{	
			const float PIETransitionTime = float((FDateTime::UtcNow() - PIEStartTime).GetTotalSeconds());

			if (IsFirstTimeToPIE)
			{
				BootToPIETime = EditorStartUpTime + EditorLoadMapTime + PIETransitionTime;
				KPIRegistry.SetKPIValue(TotalTimeToPIEKPI, BootToPIETime);

				KPIRegistry.SetKPIValue(PIEFirstTransitionKPI, PIETransitionTime);
			}
			else
			{
				KPIRegistry.SetKPIValue(PIETransitionKPI, PIETransitionTime);
			}

			EditorState = EEditorState::PIE_Interact;
			IsFirstTimeToPIE = false;
		});

	FEditorDelegates::EndPIE.AddLambda([this](bool)
		{
			PIEEndTime = FDateTime::UtcNow();
			EditorState = EEditorState::PIE_Shutdown;
		});

	FEditorDelegates::ShutdownPIE.AddLambda([this](bool)
		{
			const float PIEShutdownTime = float((FDateTime::UtcNow() - PIEEndTime).GetTotalSeconds());
			KPIRegistry.SetKPIValue(PIEShutdownKPI, PIEShutdownTime);
			EditorState = EEditorState::Editor_Interact;
		});

	FModuleManager::Get().OnModulesChanged().AddLambda([this](FName ModuleName, EModuleChangeReason ChangeReason)
		{
			switch ( ChangeReason )
			{ 
				default:
				{
					break;
				}
				
				case EModuleChangeReason::ModuleLoaded:
				{
					TotalPluginCount++;

					// Hook into Asset Registry Scan callbacks as as soon as it is loaded
					if (ModuleName == TEXT("AssetRegistry"))
					{
						FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

						AssetRegistryModule.Get().OnScanStarted().AddLambda([this]()
							{
								if (EditorAssetRegistryScanCount == 0)
								{
									AssetRegistryScanStartTime = FDateTime::UtcNow();
								}	

								FPlatformAtomics::InterlockedIncrement(&EditorAssetRegistryScanCount);
							});

						AssetRegistryModule.Get().OnScanEnded().AddLambda([this]()
							{
								FPlatformAtomics::InterlockedDecrement(&EditorAssetRegistryScanCount);

								if (EditorAssetRegistryScanCount == 0)
								{
									EditorAssetRegistryScanTime = float((FDateTime::UtcNow() - AssetRegistryScanStartTime).GetTotalSeconds());
								}	
							});
					}

					break;
				}
		
				case EModuleChangeReason::ModuleUnloaded:
				{
					TotalPluginCount--;
					break;
				}	
			}
		});

#if STALL_DETECTOR

	UE::FStallDetector::StallDetected.AddLambda([this](const UE::FStallDetectedParams& Params)
		{
			FPlatformAtomics::InterlockedIncrement(&StallDetectedCount);
		});

	UE::FStallDetector::StallCompleted.AddLambda([this](const UE::FStallCompletedParams& Params)
		{
			FPlatformAtomics::InterlockedDecrement(&StallDetectedCount);
		});

#endif //STALL_DETECTOR
}

extern ENGINE_API float GAverageFPS;

void FEditorPerformanceModule::HitchSamplerCallback()
{
	static uint32 SampleCount = 0;
	static uint32 HitchCount = 0;
	static uint32 StallCount = 0;
	
	// Only sample framerate and hitches and stalls when we have focus
	if (FApp::HasFocus())
	{
		if (GAverageFPS < MinFPSForHitching)
		{
			// This sample was hitching
			HitchCount++;
		}
	}

	if (StallDetectedCount > 0)
	{
		// This sample was stalling
		StallCount++;
	}

	SampleCount++;

	// Update HitchRate and StallRate after a set number of samples
	if (SampleCount > MinSamplesForHitching)
	{
		HitchRate = 100.0f * (float)HitchCount / (float)SampleCount;
		HitchCount = 0;

		StallRate = 100.0f * (float)StallCount / (float)SampleCount;
		StallCount = 0;
		
		SampleCount = 0;
	}
}

void FEditorPerformanceModule::HeartBeatCallback()
{
	// Update the KPIS
	UpdateKPIs(HeartBeatIntervalSeconds);
}

void FEditorPerformanceModule::UpdateKPIs(float InDeltaTime)
{
	// Gather live hardware stats
	KPIRegistry.SetKPIValue(AvailableMemoryKPI, static_cast<float>(FPlatformMemory::GetStats().AvailablePhysical) / (1024.0f * 1024.0f * 1024.0f));

	// Update stats that may have been captures before initialization
	KPIRegistry.SetKPIValue(EditorPluginCountKPI, (float)TotalPluginCount);

	if (EditorAssetRegistryScanCount > 0)
	{
		// Keep track of the first Asset Registry scan time
		EditorAssetRegistryScanTime = float((FDateTime::UtcNow() - AssetRegistryScanStartTime).GetTotalSeconds());
	}

	KPIRegistry.SetKPIValue(EditorAssetRegistryScanKPI, EditorAssetRegistryScanTime);

	// Gather the DDC summary stats
	FDerivedDataCacheSummaryStats SummaryStats;
	GatherDerivedDataCacheSummaryStats(SummaryStats);

	int64 TotalCloudGetHits = 0;
	int64 LocalGetHitsMisses = 0;
	float CloudLatency = 0.0;
	float CloudReadSpeed = 0.0;

	for (const FDerivedDataCacheSummaryStat& Stat : SummaryStats.Stats)
	{
		if (Stat.Key == TEXT("CloudGetHits"))
		{
			TotalCloudGetHits = FCString::Atoi(*Stat.Value);
		}
		else if (Stat.Key == TEXT("CloudLatency"))
		{
			CloudLatency = FCString::Atof(*Stat.Value);
		}
		else if (Stat.Key == TEXT("CloudReadSpeed"))
		{
			CloudReadSpeed = FCString::Atof(*Stat.Value) * 8.0f;
		}
		else if (Stat.Key == TEXT("TotalGetHitPct"))
		{
			const float Value = FCString::Atof(*Stat.Value) * 100.0f;

			if (Value > 0.0f)
			{
				KPIRegistry.SetKPIValue(TotalDDCEfficiencyKPI, Value);
			}
		}
		else if (Stat.Key == TEXT("LocalGetHitPct"))
		{
			const float Value = FCString::Atof(*Stat.Value) * 100.0f;

			if (Value > 0.0f)
			{
				KPIRegistry.SetKPIValue(LocalDDCEfficiencyKPI, Value);
			}
		}
		else if (Stat.Key == TEXT("LocalGetMisses"))
		{
			LocalGetHitsMisses = FCString::Atoi(*Stat.Value);
		}
	}

	// Gather the Virtual Assets stats
	if (UE::Virtualization::IVirtualizationSystem::Get().IsEnabled())
	{
		TArray<UE::Virtualization::FBackendStats> BackEndStatsList = UE::Virtualization::IVirtualizationSystem::Get().GetBackendStatistics();

		int64 CacheBackEndPullCount = 0;
		int64 PersistentBackEndPullCount = 0;

		for (const UE::Virtualization::FBackendStats& BackEndStats : BackEndStatsList)
		{
			switch (BackEndStats.Type)
			{
				case UE::Virtualization::EStorageType::Persistent:
				{
					PersistentBackEndPullCount += BackEndStats.PayloadActivity.Pull.PayloadCount;
					break;
				}

				default:
				case UE::Virtualization::EStorageType::Cache:
				{
					CacheBackEndPullCount += BackEndStats.PayloadActivity.Pull.PayloadCount;
					break;
				}
			}
		}

		const int64 TotalBackEndPullCount = CacheBackEndPullCount+PersistentBackEndPullCount;

		if (TotalBackEndPullCount > 0)
		{
			// Gather Virtualization analytics
			const float VirtualAssetsEfficiency = 100.0f * float(CacheBackEndPullCount) / float(TotalBackEndPullCount);
			KPIRegistry.SetKPIValue(VirtualAssetEfficiencyKPI, VirtualAssetsEfficiency);
		}
	}

	// Evaluate Cloud Cache performance
	const int64 MinimalCloudGetHits = 10;
	static int64 ElapsedCloudCacheHits = 0;
	static int64 PreviousTotalCloudGetHits = 0;
	static float AverageCloudLatency = 0;
	static float AverageCloudReadSpeed = 0;

	ElapsedCloudCacheHits = TotalCloudGetHits - PreviousTotalCloudGetHits;
	PreviousTotalCloudGetHits = TotalCloudGetHits;

	if (TotalCloudGetHits < MinimalCloudGetHits)
	{
		KPIRegistry.InvalidateKPIValue(CloudDDCLatencyKPI);
		KPIRegistry.InvalidateKPIValue(CloudDDCReadSpeedKPI);
	}
	else
	{
		KPIRegistry.SetKPIValue(CloudDDCLatencyKPI, CloudLatency);
		KPIRegistry.SetKPIValue(CloudDDCReadSpeedKPI, CloudReadSpeed);
	}

	// Record Hitch Rate
	if (EditorState == EEditorState::Editor_Interact)
	{
		KPIRegistry.SetKPIValue(EditorHitchRateKPI, HitchRate);
		KPIRegistry.SetKPIValue(EditorStallRateKPI, StallRate);
	}

	if (EditorState == EEditorState::PIE_Interact)
	{
		KPIRegistry.SetKPIValue(PIEHitchRateKPI, HitchRate);
		KPIRegistry.SetKPIValue(PIEStallRateKPI, StallRate);
	}

	static TArray<FGuid> RecordedKPIEvent;
	
	// Check for KPIs that have exceeded their value
	for (FKPIValues::TConstIterator It(GetKPIRegistry().GetKPIValues()); It; ++It)
	{
		const FKPIValue& KPIValue = It->Value;

		if (KPIValue.GetState() == FKPIValue::Bad)
		{
			if (RecordedKPIEvent.Find(KPIValue.Id) == INDEX_NONE)
			{
				// KPI has exceeding the threshold for the first time	
				const UEditorPerformanceSettings* EditorPerformanceSettings = GetDefault<UEditorPerformanceSettings>();

				if (EditorPerformanceSettings)
				{
					if (EditorPerformanceSettings->bEnableSnapshots)
					{
						// Create an Insights Snapshot
						RecordInsightsSnaphshot(KPIValue);
					}

					if (EditorPerformanceSettings->bEnableTelemetry)
					{
						// Create a new telemetry event
						RecordTelemetryEvent(KPIValue);
					}
				}

				// Add this KPI to the list so we don't send the event again
				RecordedKPIEvent.Emplace(KPIValue.Id);
			}
		}
		else
		{
			// No longer exceeding threshold, so next time this KPI is exceeded we will record the event
			RecordedKPIEvent.Remove(KPIValue.Id);
		}
	}
}

bool FEditorPerformanceModule::IsHotLocalCacheCase() const
{
	FKPIValue KPIValue;

	if (KPIRegistry.GetKPIValue(LocalDDCEfficiencyKPI, KPIValue))
	{
		return KPIValue.GetState()==FKPIValue::Good;
	}

	return false;
}

bool FEditorPerformanceModule::RecordInsightsSnaphshot(const FKPIValue& KPIValue)
{
	const uint32 MaxKPITraceCount = 10;

	// Create the snapshot file
	FString FileName = FString::Printf( TEXT("%s_%d.utrace"), *KPIValue.Path.ToString(), KPIValue.FailureCount % MaxKPITraceCount);
	FString FolderPath = FPaths::ProjectSavedDir() / TEXT("EditorPerformance");

	// Create the full output path
	FString FilePath = FolderPath / FileName;

	// Delete the existing trace file if it already exists
	if (FPaths::FileExists(FilePath))
	{
		if (!FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*FilePath))
		{
			// File existed but could not be overwritten
			return false;
		}
	}
		
	// Write the snapshot to a file
	return FTraceAuxiliary::WriteSnapshot(*FilePath);
}

bool FEditorPerformanceModule::RecordTelemetryEvent(const FKPIValue& KPIValue)
{
	// Record a new telemetry event for this KPI
	if (FStudioTelemetry::IsAvailable())
	{
		const int SchemaVersion = 2;
		TArray<FAnalyticsEventAttribute> Attributes;

		Attributes.Emplace(TEXT("SchemaVersion"), SchemaVersion);
		Attributes.Emplace(TEXT("MapName"), EditorMapName);
		Attributes.Emplace(TEXT("DDC_IsHotLocalCache"), IsHotLocalCacheCase());
		Attributes.Emplace(TEXT("KPI_Name"), *KPIValue.Name.ToString());
		Attributes.Emplace(TEXT("KPI_Category"), *KPIValue.Category.ToString());
		Attributes.Emplace(TEXT("KPI_CurrentValue"), KPIValue.CurrentValue);
		Attributes.Emplace(TEXT("KPI_ThresholdValue"), KPIValue.ThresholdValue);
		Attributes.Emplace(TEXT("KPI_DisplayType"), *FKPIValue::GetDisplayTypeAsString(KPIValue.DisplayType));
		Attributes.Emplace(TEXT("KPI_Profile"), KPIProfileName);
		
		FStudioTelemetry::Get().RecordEvent(TEXT("Editor.Performance.Warning"), Attributes);

		return true;
	}

	return false;
}

void FEditorPerformanceModule::TerminateKPIs()
{
}

const FKPIRegistry& FEditorPerformanceModule::GetKPIRegistry() const
{
	return KPIRegistry;
}

const FString& FEditorPerformanceModule::GetKPIProfileName() const
{
	return KPIProfileName;
}

FEditorPerformanceModule::EEditorState FEditorPerformanceModule::GetEditorState() const
{
	return EditorState;
}

#undef LOCTEXT_NAMESPACE
