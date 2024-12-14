// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "KPIValue.h"

class FSpawnTabArgs;
class SDockTab;
class SWidget;
class SWindow;
struct FTimerHandle;

/**
 * The module holding all of the UI related pieces for EditorPerformance
 */
class EDITORPERFORMANCE_API FEditorPerformanceModule : public IModuleInterface
{
public:

	enum EEditorState : uint8
	{
		Editor_Boot,
		Editor_Initialize,
		Editor_Interact,
		PIE_Startup,
		PIE_Interact,
		PIE_Shutdown,
	};

	/**
	 * Called right after the module DLL has been loaded and the module object has been created
	 */
	virtual void StartupModule() override;

	/**
	 * Called before the module is unloaded, right before the module object is destroyed.
	 */
	virtual void ShutdownModule() override;

	TSharedRef<SWidget>	CreateStatusBarWidget();
	void ShowPerformanceReportTab();

	void UpdateKPIs(float DeltaTime);

	const FKPIRegistry& GetKPIRegistry() const;
	const FString& GetKPIProfileName() const;

	bool RecordInsightsSnaphshot(const FKPIValue& Value);
	bool RecordTelemetryEvent(const FKPIValue& Value);
	bool IsHotLocalCacheCase() const;
	FEditorPerformanceModule::EEditorState GetEditorState() const;

private:

	TSharedPtr<SWidget> CreatePerformanceReportDialog();
	TSharedRef<SDockTab> CreatePerformanceReportTab(const FSpawnTabArgs& Args);	
	TWeakPtr<SDockTab> PerformanceReportTab;

	void InitializeEditor();
	void TerminateEditor();

	void InitializeKPIs();
	void TerminateKPIs();

	void HeartBeatCallback();
	void HitchSamplerCallback();

	FKPIRegistry					KPIRegistry;
	TMap<FString,FKPIProfile>		KPIProfiles;
	FString							KPIProfileName=TEXT("Default");
	FDateTime						LoadMapStartTime;
	FDateTime						PIEStartTime;
	FDateTime						PIEEndTime;
	FDateTime						AssetRegistryScanStartTime; 
	bool							IsFirstTimeToPIE = true;
	bool							IsLoadingMap = false;
	EEditorState					EditorState = EEditorState::Editor_Boot;
	float							BootToPIETime=0;
	float							EditorBootTime = 0;
	float							EditorStartUpTime = 0;
	float							EditorLoadMapTime = 0;
	float							EditorAssetRegistryScanTime = 0;
	volatile int32					EditorAssetRegistryScanCount = 0;
	FString							EditorMapName=TEXT("Boot");
	FTimerHandle					HeartBeatTimerHandle;
	const float						HeartBeatIntervalSeconds = 1.0f;
	FTimerHandle					HitchSamplerTimerHandle;
	const float						HitchSamplerIntervalSeconds = 0.1f;
	const float						MinFPSForHitching = 15.0f;
	const uint32					MinSamplesForHitching = 10;
	volatile int32					StallDetectedCount = 0;
	float							HitchRate = 0;
	float							StallRate = 0;
	uint32							TotalPluginCount = 0;

	FGuid							EditorBootKPI;
	FGuid							EditorInitializeKPI;
	FGuid							EditorLoadMapKPI;
	FGuid							EditorHitchRateKPI;
	FGuid							EditorStallRateKPI;
	FGuid							EditorAssetRegistryScanKPI;
	FGuid							EditorPluginCountKPI;
	FGuid							TotalTimeToEditorKPI;
	FGuid							TotalTimeToPIEKPI;
	FGuid							PIEFirstTransitionKPI;
	FGuid							PIETransitionKPI;
	FGuid							PIEShutdownKPI;
	FGuid							PIEHitchRateKPI;
	FGuid							PIEStallRateKPI;
	FGuid							CloudDDCLatencyKPI;
	FGuid							CloudDDCReadSpeedKPI;
	FGuid							TotalDDCEfficiencyKPI;
	FGuid							LocalDDCEfficiencyKPI;
	FGuid							VirtualAssetEfficiencyKPI;
	FGuid							CoreCountKPI;
	FGuid							TotalMemoryKPI;
	FGuid							AvailableMemoryKPI;
};


