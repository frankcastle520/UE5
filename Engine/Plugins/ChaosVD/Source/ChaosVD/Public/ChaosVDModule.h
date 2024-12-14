// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Modules/ModuleManager.h"
#include "Misc/Guid.h"

class SWidget;
class FChaosVDTraceManager;
class FChaosVDEngine;
class SChaosVDMainTab;
class SDockTab;
class FSpawnTabArgs;
struct FGuid;

/** Main module class for the Chaos Visual Debugger editor */
/** Main module class for the Chaos Visual Debugger editor */
DECLARE_LOG_CATEGORY_EXTERN(LogChaosVDEditor, Log, Log);
class FChaosVDModule : public IModuleInterface
{
public:

	using ToolkitHostType = SChaosVDMainTab;

	static FChaosVDModule& Get();

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Returns the Trace manager instance used by CVD */
	TSharedPtr<FChaosVDTraceManager>& GetTraceManager() { return ChaosVDTraceManager; };

	/** Spawns a new instance of CVD */
	void SpawnCVDTab();

	static bool IsStandaloneChaosVisualDebugger();

private:

	TSharedRef<SDockTab> SpawnMainTab(const FSpawnTabArgs& Args);

	void HandleTabClosed(TSharedRef<SDockTab> ClosedTab, FGuid InstanceGUID);

	void RegisterChaosVDEngineInstance(const FGuid& InstanceGuid, TSharedPtr<FChaosVDEngine> Instance);
	void DeregisterChaosVDEngineInstance(const FGuid& InstanceGuid);

	void RegisterChaosVDTabInstance(const FGuid& InstanceGuid, TSharedPtr<SDockTab> Instance);
	void DeregisterChaosVDTabInstance(const FGuid& InstanceGuid);
	
	void CloseActiveInstances();
	
	TMap<FGuid, TSharedPtr<FChaosVDEngine>> ActiveChaosVDInstances;

	TMap<FGuid, TWeakPtr<SDockTab>> ActiveCVDTabs;

	TArray<FName> CreatedExtraTabSpawnersIDs;

	TSharedPtr<FChaosVDTraceManager> ChaosVDTraceManager;

	static FString ChaosVisualDebuggerProgramName;

	bool bIsShuttingDown = false;
};
