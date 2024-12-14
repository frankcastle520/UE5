// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/Ticker.h"
#include "Framework/Commands/UICommandList.h"
#include "Input/DragAndDrop.h"

// TraceAnalysis
#include "Trace/StoreConnection.h"

// TraceInsightsCore
#include "InsightsCore/Common/AvailabilityCheck.h"
#include "InsightsCore/Common/Stopwatch.h"

// TraceInsights
#include "Insights/IInsightsManager.h"
#include "Insights/InsightsCommands.h"
#include "Insights/InsightsSettings.h"
#include "Insights/IUnrealInsightsModule.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::Trace
{
	class FStoreClient;
}

namespace TraceServices
{
	class IAnalysisService;
	class IModuleService;
}

class FInsightsTestRunner;

namespace UE::Insights
{

class SSessionInfoWindow;
class FInsightsMenuBuilder;

/**
 * Struct that holds data about in progress async operations
 */
struct FAsyncTaskData
{
	FString Name;
	FGraphEventRef GraphEvent;

	FAsyncTaskData(FGraphEventRef InGraphEvent, const FString InName)
	{
		Name = InName;
		GraphEvent = InGraphEvent;
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * This class manages following areas:
 *     Connecting/disconnecting to source trace
 *     Global Unreal Insights application state and settings
 */
class FInsightsManager : public TSharedFromThis<FInsightsManager>, public ::Insights::IInsightsManager
{
	friend class FInsightsActionManager;

public:
	/** Creates the main manager, only one instance can exist. */
	FInsightsManager(TSharedRef<TraceServices::IAnalysisService> TraceAnalysisService,
					 TSharedRef<TraceServices::IModuleService> TraceModuleService);

	/** Virtual destructor. */
	virtual ~FInsightsManager();

	/**
	 * Creates an instance of the main manager and initializes global instance with the previously created instance of the manager.
	 * @param TraceAnalysisService The trace analysis service
	 * @param TraceModuleService   The trace module service
	 */
	static TSharedPtr<FInsightsManager> CreateInstance(TSharedRef<TraceServices::IAnalysisService> TraceAnalysisService,
													   TSharedRef<TraceServices::IModuleService> TraceModuleService);

	/** @return the global instance of the main manager (FInsightsManager). */
	static TSharedPtr<FInsightsManager> Get();

	//////////////////////////////////////////////////
	// IInsightsComponent

	virtual void Initialize(IUnrealInsightsModule& InsightsModule) override;
	virtual void Shutdown() override;
	virtual void RegisterMajorTabs(IUnrealInsightsModule& InsightsModule) override;
	virtual void UnregisterMajorTabs() override;
	virtual bool Exec(const TCHAR* Cmd, FOutputDevice& Ar) override;

	//////////////////////////////////////////////////

	TSharedRef<TraceServices::IAnalysisService> GetAnalysisService() const { return AnalysisService; }
	TSharedRef<TraceServices::IModuleService> GetModuleService() const { return ModuleService; }

	//////////////////////////////////////////////////
	// Trace Store Connection wrapper

	bool ConnectToStore(const TCHAR* Host, uint32 Port = 0) { return TraceStoreConnection.ConnectToStore(Host, Port); }
	bool ReconnectToStore() { return TraceStoreConnection.ReconnectToStore(); }

	UE::Trace::FStoreClient* GetStoreClient() const { return TraceStoreConnection.GetStoreClient(); }
	FCriticalSection& GetStoreClientCriticalSection() const { return TraceStoreConnection.GetStoreClientCriticalSection(); }

	bool GetStoreAddressAndPort(uint32& OutStoreAddress, uint32& OutStorePort) const { return TraceStoreConnection.GetStoreAddressAndPort(OutStoreAddress, OutStorePort); }
	FString GetStoreDir() const { return TraceStoreConnection.GetStoreDir(); }

	const FString& GetLastStoreHost() const { return TraceStoreConnection.GetLastStoreHost(); }
	uint32 GetLastStorePort() const { return TraceStoreConnection.GetLastStorePort(); }

	const bool IsLocalHost() const { return TraceStoreConnection.IsLocalHost(); }
	const bool CanChangeStoreSettings() const { return TraceStoreConnection.CanChangeStoreSettings(); }

	//////////////////////////////////////////////////
	// Trace Session

	/** @return an instance of the trace analysis session. */
	TSharedPtr<const TraceServices::IAnalysisSession> GetSession() const;

	/** @return the id of the trace being analyzed. */
	uint32 GetTraceId() const { return CurrentTraceId; }

	/** @return the filename of the trace being analyzed. */
	const FString& GetTraceFilename() const { return CurrentTraceFilename; }

	//////////////////////////////////////////////////

	/** @return UI command list for the main manager. */
	const TSharedRef<FUICommandList> GetCommandList() const;

	/** @return an instance of the main commands. */
	static const FInsightsCommands& GetCommands();

	/** @return an instance of the main action manager. */
	static FInsightsActionManager& GetActionManager();

	/** @return an instance of the main settings. */
	static FInsightsSettings& GetSettings();

	//////////////////////////////////////////////////
	// Session Info

	void AssignSessionInfoWindow(const TSharedRef<SSessionInfoWindow>& InSessionInfoWindow)
	{
		SessionInfoWindow = InSessionInfoWindow;
	}

	void RemoveSessionInfoWindow()
	{
		SessionInfoWindow.Reset();
	}

	/**
	 * Converts profiler window weak pointer to a shared pointer and returns it.
	 * Make sure the returned pointer is valid before trying to dereference it.
	 */
	TSharedPtr<class SSessionInfoWindow> GetSessionInfoWindow() const
	{
		return SessionInfoWindow.Pin();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Getters and setters used by Toggle Commands.

	/** @return true, if UI is allowed to display debug info. */
	const bool IsDebugInfoEnabled() const { return bIsDebugInfoEnabled; }
	void SetDebugInfo(const bool bDebugInfoEnabledState) { bIsDebugInfoEnabled = bDebugInfoEnabledState; }

	////////////////////////////////////////////////////////////////////////////////////////////////////

	/**
	 * Shows the open file dialog for choosing a trace file.
	 * @param OutTraceFile - The chosen trace file, if successful
	 * @return True, if successful.
	 */
	bool ShowOpenTraceFileDialog(FString& OutTraceFile) const;

	/**
	 * Shows the open file dialog and starts analysis session for the chosen trace file, in a new Unreal Insights instance.
	 */
	void OpenTraceFile() const;

	/**
	 * Starts analysis session for the specified trace file, in a new Unreal Insights instance.
	 * @param TraceFilename - The trace file to analyze
	 */
	void OpenTraceFile(const FString& TraceFilename) const;

	void ToggleAutoLoadLiveSession() { bIsAutoLoadLiveSessionEnabled = !bIsAutoLoadLiveSessionEnabled; }
	bool IsAutoLoadLiveSessionEnabled() const { return bIsAutoLoadLiveSessionEnabled; }
	void AutoLoadLiveSession();

	/**
	 * Creates a new analysis session instance and loads the latest available trace that is live.
	 * Replaces the current analysis session.  On failure, if InRetryTime is > 0, will retry connecting every frame for RetryTime seconds 
	 * @param InRetryTime - On failure, how many seconds to retry connecting during FInsightsManager::Tick
	 */
	void LoadLastLiveSession(float InRetryTime = 1.0f);

	/**
	 * Creates a new analysis session instance using the specified trace id.
	 * Replaces the current analysis session.
	 * @param TraceId - The id of the trace to analyze
	 * @param InAutoQuit - The Application will close when session analysis is complete or fails to start
	 */
	void LoadTrace(uint32 TraceId, bool InAutoQuit = false);

	/**
	 * Shows the open file dialog and creates a new analysis session instance for the chosen trace file.
	 * Replaces the current analysis session.
	 */
	void LoadTraceFile();

	/**
	 * Creates a new analysis session instance and loads a trace file from the specified location.
	 * Replaces the current analysis session.
	 * @param TraceFilename - The trace file to analyze
	 * @param InAutoQuit - The Application will close when session analysis is complete or fails to start
	 */
	void LoadTraceFile(const FString& TraceFilename, bool InAutoQuit = false);

	bool OnDragOver(const FDragDropEvent& DragDropEvent);
	bool OnDrop(const FDragDropEvent& DragDropEvent);

	void UpdateAppTitle();

	/** Opens the Settings dialog. */
	void OpenSettings();

	void UpdateSessionDuration();
	void CheckMemoryUsage();

	bool IsAnalysisComplete() const { return bIsAnalysisComplete; }
	double GetSessionDuration() const { return SessionDuration; }
	double GetAnalysisDuration() const { return AnalysisDuration; }
	double GetAnalysisSpeedFactor() const { return AnalysisSpeedFactor; }

	TSharedPtr<FInsightsMenuBuilder> GetInsightsMenuBuilder() { return InsightsMenuBuilder; }

	const FName& GetLogListingName() const { return LogListingName; }

	void ScheduleCommand(const FString& InCmd);

	/** Resets (closes) current session instance. */
	void ResetSession(bool bNotify = true);

	void OpenTraceControlWindow();

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// SessionChangedEvent

public:
	/** The event to execute when the session has changed. */
	virtual ::Insights::IInsightsManager::FSessionChangedEvent& GetSessionChangedEvent() override { return SessionChangedEvent; }
private:
	/** The event to execute when the session has changed. */
	::Insights::IInsightsManager::FSessionChangedEvent SessionChangedEvent;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// SessionAnalysisCompletedEvent

public:
	/** The event to execute when session analysis is complete. */
	virtual ::Insights::IInsightsManager::FSessionAnalysisCompletedEvent& GetSessionAnalysisCompletedEvent() override { return SessionAnalysisCompletedEvent; }
private:
	/** The event to execute when session analysis is completed. */
	::Insights::IInsightsManager::FSessionAnalysisCompletedEvent SessionAnalysisCompletedEvent;

	////////////////////////////////////////////////////////////////////////////////////////////////////

private:
	/** Binds our UI commands to delegates. */
	void BindCommands();

	/** Called to spawn the Session Info major tab. */
	TSharedRef<SDockTab> SpawnSessionInfoTab(const FSpawnTabArgs& Args);

	/** Callback called when the Session Info major tab is closed. */
	void OnSessionInfoTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Updates this manager, done through FCoreTicker. */
	bool Tick(float DeltaTime);

	/** Extract messages from the session */
	void PollAnalysisInfo();

	void OnSessionChanged();
	void OnSessionAnalysisCompleted();

	void SpawnAndActivateTabs();

	void ActivateTimingInsightsTab();

	bool HandleResponseFileCmd(const TCHAR* ResponseFile, FOutputDevice& Ar);

	void RegisterTraceControlTab();

	TSharedRef<SDockTab> SpawnTraceControlTab(const FSpawnTabArgs& Args);

	void OnTraceControlTabClosed(TSharedRef<SDockTab> TabBeingClosed);

private:
	bool bIsInitialized = false;

	/** If true, the "high system memory usage warning" will be disabled until the system memory usage first drops below a certain threshold. */
	bool bMemUsageLimitHysteresis = false;

	/** The timestamp when has occurred the last check for system memory usage. */
	uint64 MemUsageLimitLastTimestamp = 0;

	/** The name of the Unreal Insights log listing. */
	FName LogListingName;

	/** Name used for Analysis log in Message Log. */
	FName AnalysisLogListingName;

	/** The delegate to be invoked when this manager ticks. */
	FTickerDelegate OnTick;

	/** Handle to the registered OnTick. */
	FTSTicker::FDelegateHandle OnTickHandle;

	TSharedRef<TraceServices::IAnalysisService> AnalysisService;
	TSharedRef<TraceServices::IModuleService> ModuleService;

	/** The trace analysis session. */
	TSharedPtr<const TraceServices::IAnalysisSession> Session;

	/** The id of the trace being analyzed. */
	uint32 CurrentTraceId = 0;

	/** The filename of the trace being analyzed. */
	FString CurrentTraceFilename;

	/** List of UI commands for this manager. This will be filled by this and corresponding classes. */
	TSharedRef<FUICommandList> CommandList;

	/** An instance of the main action manager. */
	FInsightsActionManager ActionManager;

	/** An instance of the main settings. */
	FInsightsSettings Settings;

	/** A weak pointer to the Session Info window. */
	TWeakPtr<class SSessionInfoWindow> SessionInfoWindow;

	/** If enabled, UI can display additional info for debugging purposes. */
	bool bIsDebugInfoEnabled = false;

	bool bIsMainTabSet = false;
	bool bIsSessionInfoSet = false;

	bool bIsAnalysisComplete = false;
	bool bSessionAnalysisCompletedAutoQuit = false;

	float RetryLoadLastLiveSessionTimer = 0.0f;
	bool bIsAutoLoadLiveSessionEnabled = false;
	TSet<uint32> AutoLoadedTraceIds; // list of trace ids for the auto loaded live sessions

	UE::Insights::FStopwatch AnalysisStopwatch;
	double SessionDuration = 0.0;
	double AnalysisDuration = 0.0;
	double AnalysisSpeedFactor = 0.0;

	TSharedPtr<FInsightsMenuBuilder> InsightsMenuBuilder;
	TSharedPtr<FInsightsTestRunner> TestRunner;

	FString SessionAnalysisCompletedCmd;

	static const TCHAR* AutoQuitMsg;
	static const TCHAR* AutoQuitMsgOnFail;

	TDoubleLinkedList<FAsyncTaskData> InProgressAsyncTasks;

	/** A shared pointer to the global instance of the main manager. */
	static TSharedPtr<FInsightsManager> Instance;

	/** The Trace Store connection */
	UE::Trace::FStoreConnection TraceStoreConnection;

	FGuid InstanceId;
	TWeakPtr<SWidget> TraceControl;
};

} // namespace UE::Insights
