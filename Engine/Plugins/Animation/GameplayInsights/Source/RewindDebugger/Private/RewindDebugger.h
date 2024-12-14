// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IRewindDebugger.h"
#include "RewindDebuggerTrack.h"
#include "BindableProperty.h"
#include "IRewindDebuggerTrackCreator.h"
#include "Containers/Ticker.h"
#include "UObject/WeakObjectPtr.h"

class FMenuBuilder;
class USkeletalMeshComponent;

namespace TraceServices
{
	class IAnalysisSession;
}

class SWidget;
class SDockTab;

struct FObjectInfo;
class IGameplayProvider;

// Singleton class that handles the logic for the Rewind Debugger
// handles:
//  Playback/Scrubbing state
//  Start/Stop recording
//  Keeping track of the current Debug Target actor, and outputing a list of it's Components for the UI

class FRewindDebugger : public IRewindDebugger
{
public:
	FRewindDebugger();
	virtual ~FRewindDebugger();

	// IRewindDebugger interface
	virtual double CurrentTraceTime() const override { return TraceTime.Get(); }
	virtual double GetScrubTime() const override { return CurrentScrubTime; }
	virtual const TRange<double>& GetCurrentTraceRange() const override { return CurrentTraceRange; }
	virtual const TRange<double>& GetCurrentViewRange() const override { return CurrentViewRange; }
	virtual const TraceServices::IAnalysisSession* GetAnalysisSession() const override;
	virtual uint64 GetTargetActorId() const override;
	virtual bool GetTargetActorPosition(FVector& OutPosition) const override;
	virtual UWorld* GetWorldToVisualize() const override;
	virtual bool IsRecording() const override;
	virtual bool IsPIESimulating() const override { return bPIESimulating; }
	virtual bool IsTraceFileLoaded() const override;
	virtual double GetRecordingDuration() const override { return RecordingDuration.Get(); }
	virtual TSharedPtr<FDebugObjectInfo> GetSelectedComponent() const override;
	virtual TSharedPtr<RewindDebugger::FRewindDebuggerTrack> GetSelectedTrack() const override;
	virtual TArray<TSharedPtr<FDebugObjectInfo>>& GetDebugComponents() override;
	virtual bool ShouldDisplayWorld(uint64 WorldId) override { return DisplayWorldId == WorldId; } 

	void OnConnection();

	void GetTargetObjectIds(TArray<uint64>& OutActorIds) const;

	TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>>& GetDebugTracks() { return DebugTracks; }

	// create singleton instance
	static void Initialize();

	// destroy singleton instance
	static void Shutdown();

	// get singleton instance
	static FRewindDebugger* Instance() { return static_cast<FRewindDebugger*>(InternalInstance); }

	// Start a new Recording:  Start tracing Object + Animation data, increment the current recording index, and reset the recording elapsed time to 0
	void StartRecording();
	void OnClearRecording();
	void OnRecordingStarted();
	void OnRecordingStopped();
	bool CanStartRecording() const { return !IsRecording() && bPIESimulating; }
	
	bool CanOpenTrace() const;
   	void OpenTrace(const FString& FilePath);
   	void OpenTrace();
	
   	void AttachToSession();

	bool CanClearTrace() const;
	void ClearTrace();
	
	bool CanSaveTrace() const;
	void SaveTrace(FString FileName);
	void SaveTrace();
        	
	bool ShouldAutoRecordOnPIE() const;
	void SetShouldAutoRecordOnPIE(bool value);
	
	bool ShouldAutoEject() const;
	void SetShouldAutoEject(bool value);

	// Stop recording: Stop tracing Object + Animation Data.
	void StopRecording();
	bool CanStopRecording() const { return IsRecording(); }

	// VCR controls

	bool CanPause() const;
	void Pause();

	bool CanPlay() const;
	void Play();
	bool IsPlaying() const;

	bool CanPlayReverse() const;
	void PlayReverse();

	void ScrubToStart();
	void ScrubToEnd();
	void Step(int32 frames);
	void StepForward();
	void StepBackward();

	bool CanScrub() const;
	void ScrubToTime(double ScrubTime, bool bIsScrubbing);

	// Tick function: While recording, update recording duration.  While paused, and we have recorded data, update skinned mesh poses for the current frame, and handle playback.
	void Tick(float DeltaTime);

	// update the list of tracks for the currently selected debug target
	void RefreshDebugTracks();
	
	void SetCurrentViewRange(const TRange<double>& Range);

	DECLARE_DELEGATE(FOnComponentListChanged)
	void OnComponentListChanged(const FOnComponentListChanged& ComponentListChangedCallback);
	
	void ComponentDoubleClicked(TSharedPtr<RewindDebugger::FRewindDebuggerTrack> SelectedObject);
	void ComponentSelectionChanged(TSharedPtr<RewindDebugger::FRewindDebuggerTrack> SelectedObject);
	TSharedPtr<SWidget> BuildComponentContextMenu() const;
	
	void UpdateDetailsPanel(TSharedRef<SDockTab> DetailsTab);
	static void RegisterComponentContextMenu();
	static void MakeOtherWorldsMenu(class UToolMenu* Menu);
	void SetDisplayWorld(uint64 WorldId);
	static void MakeWorldsMenu(class UToolMenu* Menu);
	static void RegisterToolBar();
	
	DECLARE_DELEGATE_OneParam( FOnTrackCursor, bool)
	void OnTrackCursor(const FOnTrackCursor& TrackCursorCallback);

	TBindableProperty<double>* GetTraceTimeProperty() { return &TraceTime; }
	TBindableProperty<double>* GetRecordingDurationProperty() { return &RecordingDuration; }
	TBindableProperty<FString, BindingType_Out>* GetDebugTargetActorProperty() { return &DebugTargetActor; }

	virtual void OpenDetailsPanel() override;
	void SetIsDetailsPanelOpen(bool bIsOpen) { bIsDetailsPanelOpen = bIsOpen; }
	bool IsDetailsPanelOpen(bool bIsOpen) { return bIsDetailsPanelOpen; }
	
	virtual const FObjectInfo* FindOwningActorInfo(const IGameplayProvider* GameplayProvider, uint64 ObjectId) const override;

	TArrayView<RewindDebugger::FRewindDebuggerTrackType> GetTrackTypes() { return TrackTypes; };

private:
	void RefreshDebugComponents(TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>>& InTracks, TArray<TSharedPtr<FDebugObjectInfo>>& OutComponents);
	
	void OnPIEStarted(bool bSimulating);
	void OnPIEPaused(bool bSimulating);
	void OnPIEResumed(bool bSimulating);
	void OnPIEStopped(bool bSimulating);
	void OnPIESingleStepped(bool bSimulating);

	void SetCurrentScrubTime(double Time);

	TBindableProperty<double> TraceTime;
	TBindableProperty<double> RecordingDuration;
	TBindableProperty<FString, BindingType_Out> DebugTargetActor;

	enum class EControlState : int8
	{
		Play,
		PlayReverse,
		Pause
	};

	EControlState ControlState = EControlState::Pause;

	FOnComponentListChanged ComponentListChangedDelegate;
	FOnTrackCursor TrackCursorDelegate;

	bool bQueueStartRecording = false;
	bool bTraceJustConnected = false;
	bool bPIEStarted = false;
	bool bPIESimulating = false;
	
	bool bRecording = false;

	double PreviousTraceTime = -1;
	double CurrentScrubTime = 0;
	TRange<double> CurrentViewRange {0, 10};
	TRange<double> CurrentTraceRange {0, 0};
	uint16 RecordingIndex = 0;

	struct FScrubTimeInformation
	{
		double ProfileTime = 0;  // Profiling/Tracing time
		int64 FrameIndex = 0;    // Scrub Frame Index
	};
	
	FScrubTimeInformation ScrubTimeInformation;
	FScrubTimeInformation LowerBoundViewTimeInformation;
	FScrubTimeInformation UpperBoundViewTimeInformation;

	static void GetScrubTimeInformation(double InDebugTime, FScrubTimeInformation & InOutTimeInformation, uint16 InRecordingIndex, const TraceServices::IAnalysisSession* AnalysisSession);
	
	TArray<TSharedPtr<FDebugObjectInfo>> DebugComponents;
	mutable TSharedPtr<FDebugObjectInfo> SelectedComponent;
	
	TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>> DebugTracks;
	TSharedPtr<RewindDebugger::FRewindDebuggerTrack> SelectedTrack;

	TArray<uint64> TargetObjectIds;

	mutable class IUnrealInsightsModule *UnrealInsightsModule = 0;
	FTSTicker::FDelegateHandle TickerHandle;

	bool bTargetActorPositionValid = false;
	FVector TargetActorPosition;
	uint64 TargetActorMeshId = 0;
	uint64 TargetActorIdForMesh = 0;

	TArray<RewindDebugger::FRewindDebuggerTrackType> TrackTypes;

	bool bIsDetailsPanelOpen = true;

	uint64 DisplayWorldId = 0;
	bool bDisplayWorldIdValid = false;
};
