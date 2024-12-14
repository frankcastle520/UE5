// Copyright Epic Games, Inc. All Rights Reserved

#pragma once

#include "CoreTypes.h"

#include "Insights/IUnrealInsightsModule.h"
#include "Framework/Docking/TabManager.h"

// TraceAnalysis
#include "Trace/StoreService.h"

namespace Insights
{
	class IInsightsManager;
}

namespace TraceServices
{
	class IAnalysisService;
	class IModuleService;
}

class SDockTab;
class FSpawnTabArgs;
class SWindow;

namespace UE::Insights
{

/**
 * Implements the Trace Insights module.
 */
class FTraceInsightsModule : public IUnrealInsightsModule
{
public:
	virtual ~FTraceInsightsModule()
	{
	}

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	virtual void CreateDefaultStore() override;
	FString GetDefaultStoreDir();

	virtual UE::Trace::FStoreClient* GetStoreClient() override;
	virtual bool ConnectToStore(const TCHAR* InStoreHost, uint32 InStorePort=0) override;

	virtual void CreateSessionViewer(bool bAllowDebugTools = false) override;

	virtual TSharedPtr<const TraceServices::IAnalysisSession> GetAnalysisSession() const override;
	virtual void StartAnalysisForTrace(uint32 InTraceId, bool InAutoQuit = false) override;
	virtual void StartAnalysisForLastLiveSession(float InRetryTime = 1.0f) override;
	virtual void StartAnalysisForTraceFile(const TCHAR* InTraceFile, bool InAutoQuit = false) override;

	virtual void ShutdownUserInterface() override;

	virtual void RegisterComponent(TSharedPtr<IInsightsComponent> Component) override;
	virtual void UnregisterComponent(TSharedPtr<IInsightsComponent> Component) override;

	virtual TSharedPtr<::Insights::IInsightsManager> GetInsightsManager() override;

	virtual void RegisterMajorTabConfig(const FName& InMajorTabId, const FInsightsMajorTabConfig& InConfig) override;
	virtual void UnregisterMajorTabConfig(const FName& InMajorTabId) override;
	virtual FOnInsightsMajorTabCreated& OnMajorTabCreated() override { return OnInsightsMajorTabCreatedDelegate; }
	virtual FOnRegisterMajorTabExtensions& OnRegisterMajorTabExtension(const FName& InMajorTabId) override;

	virtual const FInsightsMajorTabConfig& FindMajorTabConfig(const FName& InMajorTabId) const override;

	FOnRegisterMajorTabExtensions* FindMajorTabLayoutExtension(const FName& InMajorTabId);

	/** Retrieve ini path for saving persistent layout data. */
	static const FString& GetUnrealInsightsLayoutIni();

	/** Set the ini path for saving persistent layout data. */
	virtual void SetUnrealInsightsLayoutIni(const FString& InIniPath) override;

	virtual void InitializeTesting(bool InInitAutomationModules, bool InAutoQuit) override;
	virtual void ScheduleCommand(const FString& InCmd) override;
	virtual bool Exec(const TCHAR* Cmd, FOutputDevice& Ar) override;

private:
	void RegisterTabSpawners();
	void UnregisterTabSpawners();

	void AddAreaForSessionViewer(TSharedRef<FTabManager::FLayout> Layout);
	void AddAreaForWidgetReflector(TSharedRef<FTabManager::FLayout> Layout, bool bAllowDebugTools);

	/** Session Info */
	TSharedRef<SDockTab> SpawnSessionInfoTab(const FSpawnTabArgs& Args);

	void OnWindowClosedEvent(const TSharedRef<SWindow>&);

	void UpdateAppTitle();

	void HandleCodeAccessorOpenFileFailed(const FString& Filename);

private:
	TSharedPtr<TraceServices::IAnalysisService> TraceAnalysisService;
	TSharedPtr<TraceServices::IModuleService> TraceModuleService;

	TMap<FName, FInsightsMajorTabConfig> TabConfigs;
	TMap<FName, FOnRegisterMajorTabExtensions> MajorTabExtensionDelegates;

	FOnInsightsMajorTabCreated OnInsightsMajorTabCreatedDelegate;

	TSharedPtr<FTabManager::FLayout> PersistentLayout;
	static FString UnrealInsightsLayoutIni;

	TArray<TSharedRef<IInsightsComponent>> Components;
};

} // namespace UE::Insights
