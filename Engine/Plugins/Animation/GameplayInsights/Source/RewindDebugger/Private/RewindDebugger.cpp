// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebugger.h"

#include "DesktopPlatformModule.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimTrace.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformFileManager.h"
#include "IAnimationProvider.h"
#include "IDesktopPlatform.h"
#include "IGameplayProvider.h"
#include "IRewindDebuggerDoubleClickHandler.h"
#include "IRewindDebuggerExtension.h"
#include "Insights/IUnrealInsightsModule.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "ObjectTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "RewindDebuggerCommands.h"
#include "RewindDebuggerModule.h"
#include "RewindDebuggerObjectTrack.h"
#include "RewindDebuggerPlaceholderTrack.h"
#include "RewindDebuggerSettings.h"
#include "SLevelViewport.h"
#include "SModalSessionBrowser.h"
#include "ToolMenus.h"
#include "Trace/StoreClient.h"
#include "TraceServices/Model/Frames.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SSpacer.h"
#include "RewindDebuggerCommands.h"
#include "RewindDebuggerTrackCreators.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Kismet2/DebuggerCommands.h"
#include "RewindDebuggerRuntime/RewindDebuggerRuntime.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "RewindDebugger"

static void IterateExtensions(TFunction<void(IRewindDebuggerExtension* Extension)> IteratorFunction)
{
	// update extensions
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	const int32 NumExtensions = ModularFeatures.GetModularFeatureImplementationCount(IRewindDebuggerExtension::ModularFeatureName);
	for (int32 ExtensionIndex = 0; ExtensionIndex < NumExtensions; ++ExtensionIndex)
	{
		IRewindDebuggerExtension* Extension = static_cast<IRewindDebuggerExtension*>(ModularFeatures.GetModularFeatureImplementation(IRewindDebuggerExtension::ModularFeatureName, ExtensionIndex));
		IteratorFunction(Extension);
	}
}

static void TraceSubobjects(UObject* OuterObject)
{
	TArray<UObject*> Subobjects;
	GetObjectsWithOuter(OuterObject, Subobjects, true);
	for (UObject* Subobject : Subobjects)
	{
		TRACE_OBJECT_LIFETIME_BEGIN(Subobject);
	}
}

FRewindDebugger::FRewindDebugger()
{
	if (RewindDebugger::FRewindDebuggerRuntime::Instance() == nullptr)
	{
		RewindDebugger::FRewindDebuggerRuntime::Initialize();
	}

	if (RewindDebugger::FRewindDebuggerRuntime* Runtime = RewindDebugger::FRewindDebuggerRuntime::Instance())
	{
		Runtime->ClearRecording.AddRaw(this, &FRewindDebugger::OnClearRecording);
		Runtime->RecordingStarted.AddRaw(this, &FRewindDebugger::OnRecordingStarted);
		Runtime->RecordingStarted.AddRaw(this, &FRewindDebugger::OnRecordingStopped);
	}
	
	RewindDebugger::FRewindDebuggerTrackCreators::EnumerateCreators([this](const RewindDebugger::IRewindDebuggerTrackCreator* Creator)
    {
		Creator->GetTrackTypes(TrackTypes);
    });
	
	RecordingDuration.Set(0);
	
	UnrealInsightsModule = &FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");

	if (GEditor->bIsSimulatingInEditor || GEditor->PlayWorld)
	{
		OnPIEStarted(true);
	}

	FEditorDelegates::PreBeginPIE.AddRaw(this, &FRewindDebugger::OnPIEStarted);
	FEditorDelegates::PausePIE.AddRaw(this, &FRewindDebugger::OnPIEPaused);
	FEditorDelegates::ResumePIE.AddRaw(this, &FRewindDebugger::OnPIEResumed);
	FEditorDelegates::EndPIE.AddRaw(this, &FRewindDebugger::OnPIEStopped);
	FEditorDelegates::SingleStepPIE.AddRaw(this, &FRewindDebugger::OnPIESingleStepped);

	
	DebugTargetActor.OnPropertyChanged = DebugTargetActor.OnPropertyChanged.CreateLambda([this](FString Target)
		{
			URewindDebuggerSettings& Settings = URewindDebuggerSettings::Get();
			if (Settings.DebugTargetActor != Target)
			{
				Settings.DebugTargetActor = Target;
				Settings.Modify();
				Settings.SaveConfig();
			}
		
			TargetObjectIds.SetNum(0);
			GetTargetObjectIds(TargetObjectIds);
			// make sure all the SubObjects of the target actor have been traced
#if OBJECT_TRACE_ENABLED
			for (uint64 TargetObjectId : TargetObjectIds)
			{
				if (UObject* TargetObject = FObjectTrace::GetObjectFromId(TargetObjectId))
				{
					TraceSubobjects(TargetObject);
				}
			}
#endif

			RefreshDebugTracks();
		});

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("RewindDebugger"), 0.0f, [this](float DeltaTime)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FRewindDebuggerModule_Tick);

		Tick(DeltaTime);

		return true;
	});
}

FRewindDebugger::~FRewindDebugger() 
{
	FEditorDelegates::PostPIEStarted.RemoveAll(this);
	FEditorDelegates::PausePIE.RemoveAll(this);
	FEditorDelegates::ResumePIE.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
	FEditorDelegates::SingleStepPIE.RemoveAll(this);

	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	
	if (RewindDebugger::FRewindDebuggerRuntime* Runtime = RewindDebugger::FRewindDebuggerRuntime::Instance())
	{
		Runtime->RecordingStarted.RemoveAll(this);
	}
}

void FRewindDebugger::Initialize() 
{
	InternalInstance = new FRewindDebugger;
}

void FRewindDebugger::Shutdown() 
{
	delete InternalInstance;
}

void FRewindDebugger::OnComponentListChanged(const FOnComponentListChanged& InComponentListChangedDelegate)
{
	ComponentListChangedDelegate = InComponentListChangedDelegate;
}

void FRewindDebugger::OnTrackCursor(const FOnTrackCursor& InTrackCursorDelegate)
{
	TrackCursorDelegate = InTrackCursorDelegate;
}

void FRewindDebugger::OnPIEStarted(bool bSimulating)
{
	bPIEStarted = true;
	bPIESimulating = true;

	if (ShouldAutoRecordOnPIE())
	{
		bQueueStartRecording = true;
	}
}

void FRewindDebugger::OnPIEPaused(bool bSimulating)
{
	bPIESimulating = false;
	ControlState = EControlState::Pause;
	
	if (IsRecording())
	{
    #if OBJECT_TRACE_ENABLED
		UWorld* World = GetWorldToVisualize();
		SetCurrentScrubTime(FObjectTrace::GetWorldElapsedTime(World));
    #endif // OBJECT_TRACE_ENABLED
	}
	
	if (ShouldAutoEject() && FPlayWorldCommandCallbacks::IsInPIE())
	{
		bool CanEject= false;
		for (auto It = GUnrealEd->SlatePlayInEditorMap.CreateIterator(); It; ++It)
		{
			CanEject = CanEject || It.Value().DestinationSlateViewport.IsValid();
		}

		if (CanEject)
		{
			GEditor->RequestToggleBetweenPIEandSIE();
		}
	}
}

void FRewindDebugger::OnPIEResumed(bool bSimulating)
{
	bPIESimulating = true;

	if (ShouldAutoEject() && FPlayWorldCommandCallbacks::IsInSIE())
	{
		GEditor->RequestToggleBetweenPIEandSIE();
	}
}

void FRewindDebugger::OnPIESingleStepped(bool bSimulating)
{
	if (IsRecording())
	{
    #if OBJECT_TRACE_ENABLED
		UWorld* World = GetWorldToVisualize();
		SetCurrentScrubTime(FObjectTrace::GetWorldElapsedTime(World));
    #endif // OBJECT_TRACE_ENABLED
	}
}


void FRewindDebugger::OnPIEStopped(bool bSimulating)
{
	if (IsRecording() && bPIESimulating)
	{
#if OBJECT_TRACE_ENABLED
		UWorld* World = GetWorldToVisualize();
		SetCurrentScrubTime(FObjectTrace::GetWorldElapsedTime(World));
#endif // OBJECT_TRACE_ENABLED
	}
	
	bPIEStarted = false;
	bPIESimulating = false;

	StopRecording();
	
	bDisplayWorldIdValid = false;
}

bool FRewindDebugger::GetTargetActorPosition(FVector& OutPosition) const
{
	OutPosition = TargetActorPosition;
	return bTargetActorPositionValid;
}

uint64 FRewindDebugger::GetTargetActorId() const
{
	if (DebugTargetActor.Get() == "")
	{
		return 0;
	}

	uint64 TargetActorId = 0;

	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
		{
			GameplayProvider->EnumerateObjects(CurrentTraceRange.GetLowerBoundValue(), CurrentTraceRange.GetUpperBoundValue(), [this, &TargetActorId](const FObjectInfo& InObjectInfo)
			{
				if (DebugTargetActor.Get() == InObjectInfo.Name)
				{
					TargetActorId = InObjectInfo.Id;
				}
			});
		}
	}

	return TargetActorId;
}


void FRewindDebugger::GetTargetObjectIds(TArray<uint64>& OutTargetObjectIds) const
{
	OutTargetObjectIds.Empty(2);

	if (DebugTargetActor.Get() == "")
	{
		return;
	}

	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

		if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
		{
			GameplayProvider->EnumerateObjects(CurrentTraceRange.GetLowerBoundValue(), CurrentTraceRange.GetUpperBoundValue(), [this, &OutTargetObjectIds](const FObjectInfo& InObjectInfo)
				{
					if (DebugTargetActor.Get() == InObjectInfo.Name)
					{
						OutTargetObjectIds.Add(InObjectInfo.Id);
					}
				});
		}
	}

	// make sure all the SubObjects of the target actor have been traced
#if OBJECT_TRACE_ENABLED
	if (IsRecording())
	{
		for (uint64 OutTargetObjectId : TargetObjectIds)
		{
			if (UObject* TargetObject = FObjectTrace::GetObjectFromId(OutTargetObjectId))
			{
				TraceSubobjects(TargetObject);
			}
		}
	}
#endif
}


void FRewindDebugger::RefreshDebugTracks()
{
	static const FName DebugMessageTrackName = "DebugMessageDummyTrack";
	TRACE_CPUPROFILER_EVENT_SCOPE(FRewindDebugger::RefreshDebugTracks);

	if (TargetObjectIds.Num() == 0)
	{
		GetTargetObjectIds(TargetObjectIds);
	}
	
	FString DebugTargetActorName = DebugTargetActor.Get();
	
	if (TargetObjectIds.Num() == 0 && !DebugTargetActorName.IsEmpty())
	{
		// fallback codepath for when the target object is not found

		if (DebugTracks.Num() != 2)
		{
			// clear tracks so we don't show data from previous recordings
			DebugTracks.SetNum(0);
			DebugTracks.SetNum(2);
		}

		if (DebugTracks[1] == nullptr || DebugTracks[0] == nullptr || DebugTracks[0]->GetName().ToString() != DebugTargetActor.Get() )
		{
			DebugTracks[0] = MakeShared<FRewindDebuggerPlaceholderTrack>(FName(DebugTargetActorName), FText::FromString(DebugTargetActorName)); 
			DebugTracks[1] = MakeShared<FRewindDebuggerPlaceholderTrack>(DebugMessageTrackName, NSLOCTEXT("RewindDebugger", "No Debug Data", " - Start a recording to debug"));
			ComponentListChangedDelegate.ExecuteIfBound();
		}
	}
	else
	{
		if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
    	{
    		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
    
    		UWorld* World = GetWorldToVisualize();
    
    		if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
    		{
    
    			bool bChanged = false;
    
    			// remove any existing tracks that don't match the current list of object ids
    			for (int TrackIndex = DebugTracks.Num()-1; TrackIndex>=0; TrackIndex--)
    			{
    				uint64 TrackId = DebugTracks[TrackIndex]->GetObjectId();
    				if (!TargetObjectIds.Contains(TrackId))
    				{
    					DebugTracks.RemoveAt(TrackIndex);
    				}
    			}
    
    			// add new tracks for current list of object ids if they don't already exist
    			for (uint64 TargetObjectId : TargetObjectIds)
    			{
    				TSharedPtr<RewindDebugger::FRewindDebuggerTrack>* FoundTrack = DebugTracks.FindByPredicate([TargetObjectId](const TSharedPtr<RewindDebugger::FRewindDebuggerTrack>& Track) { return Track->GetObjectId() == TargetObjectId; });
    
    				if (!FoundTrack)
    				{
    					DebugTracks.Add(MakeShared<RewindDebugger::FRewindDebuggerObjectTrack>(TargetObjectId, DebugTargetActor.Get(), true));
    					bChanged = true;
    				}
    			}
    
    			// update all tracks
    			for (TSharedPtr<RewindDebugger::FRewindDebuggerTrack>& DebugTrack : DebugTracks )
    			{
    				if (DebugTrack->Update())
    				{
    					bChanged = true;
    				}
    			}
    
    			if (bChanged)
    			{
    				ComponentListChangedDelegate.ExecuteIfBound();
    			}
    		}
    	}	
	}
}

namespace
{
	static void DisableAllTraceChannels()
	{
		UE::Trace::EnumerateChannels([](const ANSICHAR* ChannelName, bool bEnabled, void*)
						{
							if (bEnabled)
							{
								FString ChannelNameFString(ChannelName);
								UE::Trace::ToggleChannel(ChannelNameFString.GetCharArray().GetData(), false);
							}
						}
						, nullptr);
	}
}

void FRewindDebugger::OnConnection()
{
	// queue up some operations to happen on the game thread next tick
	bTraceJustConnected = true;
	FTraceAuxiliary::OnConnection.RemoveAll(this);
}

void FRewindDebugger::StartRecording()
{
	if (!CanStartRecording())
	{
		return;
	}

	if (RewindDebugger::FRewindDebuggerRuntime* Runtime = RewindDebugger::FRewindDebuggerRuntime::Instance())
	{
		Runtime->StartRecording();
	}
}

void FRewindDebugger::OnClearRecording()
{
	ClearTrace();
	RecordingDuration.Set(0);
	TargetObjectIds.Empty(2);
	bTargetActorPositionValid = false;

	IterateExtensions([this](IRewindDebuggerExtension* Extension)
	{
		Extension->Clear(this);
	}
	);
}

void FRewindDebugger::OnRecordingStarted()
{
	IterateExtensions([this](IRewindDebuggerExtension* Extension)
	{
		Extension->RecordingStarted(this);
	}
	);
	
	UnrealInsightsModule->StartAnalysisForLastLiveSession(5.0);
}

void FRewindDebugger::OnRecordingStopped()
{
	IterateExtensions([this](IRewindDebuggerExtension* Extension)
	{
		Extension->RecordingStopped(this);
	}
	);
}

bool FRewindDebugger::CanOpenTrace() const
{
	return !bPIEStarted;
}


void FRewindDebugger::OpenTrace(const FString& FilePath)
{ 
	ClearTrace();

	bDisplayWorldIdValid = false;
	
	IUnrealInsightsModule& TraceInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	TraceInsightsModule.StartAnalysisForTraceFile(*FilePath);

	// todo: optionally open the map the trace file was recorded in
}

void FRewindDebugger::OpenTrace()
{
	FString FolderPath = "";
	
	TArray<FString> OutOpenFilenames;
	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
	{
		FString ExtensionStr;
		ExtensionStr += TEXT("Unreal Trace|*.utrace|");
	
		DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LOCTEXT("OpenDialogTitle", "Open Rewind Debugger Recording").ToString(),
			FolderPath,
			TEXT(""),
			*ExtensionStr,
			EFileDialogFlags::None,
			OutOpenFilenames
		);
	}

	if (OutOpenFilenames.Num() > 0)
	{
		if (OutOpenFilenames[0].EndsWith(TEXT("utrace")))
		{
			OpenTrace(OutOpenFilenames[0]);
		}
	}
}


void FRewindDebugger::AttachToSession()
{
	ClearTrace();
	const TSharedRef<SModalSessionBrowser> SessionBrowserModal = SNew(SModalSessionBrowser);

	if (SessionBrowserModal->ShowModal() != EAppReturnType::Cancel)
	{
		bool bSuccess = false;
		const SModalSessionBrowser::FTraceSessionInfo SessionInfo = SessionBrowserModal->GetSelectedTraceInfo();
		if (SessionInfo.bIsValid)
		{
			const FString SessionAddress = SessionBrowserModal->GetSelectedTraceStoreAddress();
			IUnrealInsightsModule& TraceInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
			TraceInsightsModule.StartAnalysisForTrace(SessionInfo.TraceID);
			bSuccess = TraceInsightsModule.GetAnalysisSession().IsValid();
		}

		if (!bSuccess)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("FailedToConnectToSessionMessage", "Failed to connect to session"));	
		}
	}
}

bool FRewindDebugger::CanClearTrace() const
{
	return GetAnalysisSession() != nullptr;
}

void FRewindDebugger::ClearTrace()
{
	StopRecording();
	RecordingDuration.Set(0);
	
	TargetObjectIds.Empty();
	CurrentTraceRange.SetLowerBoundValue(0);
	CurrentTraceRange.SetUpperBoundValue(0);
	RecordingDuration.Set(0.0);
	SetCurrentScrubTime(0.0);

	ComponentSelectionChanged(nullptr);
	
	// update extensions
	IterateExtensions([this](IRewindDebuggerExtension* Extension)
		{
			Extension->Clear(this);
		}
	);
	
	IUnrealInsightsModule& TraceInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	// only way I can find to clear the session is trying to load a name that doesn't exist.
	TraceInsightsModule.StartAnalysisForTraceFile(TEXT("0"));

	RefreshDebugTracks();
}

bool FRewindDebugger::CanSaveTrace() const
{
	const TraceServices::IAnalysisSession* Session = GetAnalysisSession();
	return Session != nullptr && Session->IsAnalysisComplete();
}

void FRewindDebugger::SaveTrace(FString FileName)
{

	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		if (Session->IsAnalysisComplete())
		{
			FString SourceFileName = Session->GetName();

			FPlatformFileManager& FileManager = FPlatformFileManager::Get();
			IPlatformFile& PlatformFile = FileManager.GetPlatformFile();

			PlatformFile.CopyFile(*FileName, *SourceFileName);
		}
		
	}
}

void FRewindDebugger::SaveTrace()
{
	FString FolderPath = "";
	
	TArray<FString> OutFilenames;
	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
	{
		FString ExtensionStr;
		ExtensionStr += TEXT("Rewind Debugger Recording |*.utrace|");
	
		DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LOCTEXT("SaveDialogTitle", "Save Rewind Debugger Recording").ToString(),
			FolderPath,
			TEXT(""),
			*ExtensionStr,
			EFileDialogFlags::None,
			OutFilenames
		);
	}

	if (OutFilenames.Num() > 0)
	{
		if (OutFilenames[0].EndsWith(TEXT(".utrace")))
		{
			SaveTrace(OutFilenames[0]);
		}
	}
}

bool FRewindDebugger::ShouldAutoRecordOnPIE() const
{
	return URewindDebuggerSettings::Get().bShouldAutoRecordOnPIE;
}

void FRewindDebugger::SetShouldAutoRecordOnPIE(bool value)
{
	URewindDebuggerSettings& RewindDebuggerSettings = URewindDebuggerSettings::Get();
	RewindDebuggerSettings.Modify();
	RewindDebuggerSettings.bShouldAutoRecordOnPIE = value;
	RewindDebuggerSettings.SaveConfig();
}

bool FRewindDebugger::ShouldAutoEject() const
{
	return URewindDebuggerSettings::Get().bShouldAutoEject;
}

void FRewindDebugger::SetShouldAutoEject(bool value)
{
	URewindDebuggerSettings& RewindDebuggerSettings = URewindDebuggerSettings::Get();
	RewindDebuggerSettings.Modify();
	RewindDebuggerSettings.bShouldAutoEject = value;
	RewindDebuggerSettings.SaveConfig();
}

void FRewindDebugger::StopRecording()
{
	if (RewindDebugger::FRewindDebuggerRuntime* Runtime = RewindDebugger::FRewindDebuggerRuntime::Instance())
	{
		Runtime->StopRecording();
	}
}

bool FRewindDebugger::CanPause() const
{
	return ControlState != EControlState::Pause;
}

void FRewindDebugger::Pause()
{
	if (CanPause())
	{
		if (bPIESimulating)
		{
			// pause PIE
		}

		ControlState = EControlState::Pause;
	}
}

bool FRewindDebugger::IsPlaying() const
{
	return ControlState==EControlState::Play && !bPIESimulating;
}

bool FRewindDebugger::CanPlay() const
{
	return ControlState!=EControlState::Play && !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::Play()
{
	if (CanPlay())
	{
		if (CurrentScrubTime >= RecordingDuration.Get())
		{
			SetCurrentScrubTime(0);
		}

		ControlState = EControlState::Play;
	}
}

bool FRewindDebugger::CanPlayReverse() const
{
	return ControlState!=EControlState::PlayReverse && !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::PlayReverse()
{
	if (CanPlayReverse())
	{
		if (CurrentScrubTime <= 0)
		{
			SetCurrentScrubTime(RecordingDuration.Get());
		}

		ControlState = EControlState::PlayReverse;
	}
}

bool FRewindDebugger::CanScrub() const
{
	return !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::ScrubToStart()
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(0);
		TrackCursorDelegate.ExecuteIfBound(false);
	}
}

void FRewindDebugger::ScrubToEnd()
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(RecordingDuration.Get());
		TrackCursorDelegate.ExecuteIfBound(false);
	}
}

void FRewindDebugger::Step(int frames)
{
	if (CanScrub())
	{
		Pause();

		if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
			UWorld* World = GetWorldToVisualize();

			if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
			{
				if (const IGameplayProvider::RecordingInfoTimeline* Recording = GameplayProvider->GetRecordingInfo(RecordingIndex))
				{
					uint64 EventCount = Recording->GetEventCount();

					if (EventCount > 0)
					{
						ScrubTimeInformation.FrameIndex = FMath::Clamp<int64>(ScrubTimeInformation.FrameIndex + frames, 0, (int64)EventCount - 1);
						const FRecordingInfoMessage& Event = Recording->GetEvent(ScrubTimeInformation.FrameIndex);

						SetCurrentScrubTime(Event.ElapsedTime);
						
						TrackCursorDelegate.ExecuteIfBound(false);
					}
				}
			}
		}
	}
}

void FRewindDebugger::StepForward()
{
	Step(1);
}

void FRewindDebugger::StepBackward()
{
	Step(-1);
}


void FRewindDebugger::ScrubToTime(double ScrubTime, bool bIsScrubbing)
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(ScrubTime);
	}
}

UWorld* FRewindDebugger::GetWorldToVisualize() const
{
	// we probably want to replace this with a world selector widget, if we are going to support tracing from anything other thn the PIE world

	UWorld* World = nullptr;

	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (GIsEditor && EditorEngine != nullptr && World == nullptr)
	{
		// lets use PlayWorld during PIE/Simulate and regular world from editor otherwise, to draw debug information
		World = EditorEngine->PlayWorld != nullptr ? ToRawPtr(EditorEngine->PlayWorld) : EditorEngine->GetEditorWorldContext().World();
	}

	return World;
}

bool FRewindDebugger::IsRecording() const
{
	if (RewindDebugger::FRewindDebuggerRuntime* Runtime = RewindDebugger::FRewindDebuggerRuntime::Instance())
	{
		return Runtime->IsRecording();
	}
	return false;
}

bool FRewindDebugger::IsTraceFileLoaded() const
{
	return GetAnalysisSession()!=nullptr && !bPIEStarted;
}

void FRewindDebugger::SetCurrentViewRange(const TRange<double>& Range)
{
	CurrentViewRange = Range;
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		GetScrubTimeInformation(CurrentViewRange.GetLowerBoundValue(), LowerBoundViewTimeInformation, RecordingIndex, Session);
		GetScrubTimeInformation(CurrentViewRange.GetUpperBoundValue(), UpperBoundViewTimeInformation, RecordingIndex, Session);
		
		CurrentTraceRange.SetLowerBoundValue(LowerBoundViewTimeInformation.ProfileTime);
		CurrentTraceRange.SetUpperBoundValue(UpperBoundViewTimeInformation.ProfileTime);
	}
}

void FRewindDebugger::SetCurrentScrubTime(double Time)
{
	CurrentScrubTime = Time;

	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		GetScrubTimeInformation(CurrentScrubTime, ScrubTimeInformation, RecordingIndex, Session);
		
		TraceTime.Set(ScrubTimeInformation.ProfileTime);
	}
}

void FRewindDebugger::GetScrubTimeInformation(double InDebugTime, FScrubTimeInformation & InOutTimeInformation, uint16 InRecordingIndex, const TraceServices::IAnalysisSession* AnalysisSession)
{
	const IGameplayProvider* GameplayProvider = AnalysisSession->ReadProvider<IGameplayProvider>("GameplayProvider");
	const IAnimationProvider* AnimationProvider = AnalysisSession->ReadProvider<IAnimationProvider>("AnimationProvider");
	
	if (GameplayProvider && AnimationProvider)
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);
		
		if (const IGameplayProvider::RecordingInfoTimeline* Recording = GameplayProvider->GetRecordingInfo(InRecordingIndex))
		{
			const uint64 EventCount = Recording->GetEventCount();

			if (EventCount > 0)
			{
				int ScrubFrameIndex = InOutTimeInformation.FrameIndex;
				const FRecordingInfoMessage& FirstEvent = Recording->GetEvent(0);
				const FRecordingInfoMessage& LastEvent = Recording->GetEvent(EventCount - 1);

				// Check if we are outside of the recorded range, and apply the first or last frame
				if (InDebugTime <= FirstEvent.ElapsedTime)
				{
					ScrubFrameIndex = FMath::Min<uint64>(1, EventCount - 1);
				}
				else if (InDebugTime >= LastEvent.ElapsedTime)
				{
					ScrubFrameIndex = EventCount - 1;
				}
				// Find the two keys surrounding the InDebugTime, and pick the nearest to update InOutTimeInformation
				else
				{
					const FRecordingInfoMessage& ScrubEvent = Recording->GetEvent(ScrubFrameIndex);
					constexpr float MaxTimeDifferenceInSeconds = 15.0f / 60.0f;
					
					// Use linear search on smaller time differences
					if (FMath::Abs(InDebugTime - ScrubEvent.ElapsedTime) <= MaxTimeDifferenceInSeconds)
					{
						if (Recording->GetEvent(ScrubFrameIndex).ElapsedTime > InDebugTime)
						{
							for (uint64 EventIndex = ScrubFrameIndex; EventIndex > 0; EventIndex--)
							{
								const FRecordingInfoMessage& Event = Recording->GetEvent(EventIndex);
								const FRecordingInfoMessage& NextEvent = Recording->GetEvent(EventIndex - 1);
								if (Event.ElapsedTime >= InDebugTime && NextEvent.ElapsedTime <= InDebugTime)
								{
									if (Event.ElapsedTime - InDebugTime < InDebugTime - NextEvent.ElapsedTime)
									{
										ScrubFrameIndex = EventIndex;
									}
									else
									{
										ScrubFrameIndex = EventIndex - 1;
									}
									break;
								}
							}
						}
						else
						{
							for (uint64 EventIndex = ScrubFrameIndex; EventIndex < EventCount - 1; EventIndex++)
							{
								const FRecordingInfoMessage& Event = Recording->GetEvent(EventIndex);
								const FRecordingInfoMessage& NextEvent = Recording->GetEvent(EventIndex + 1);
								if (Event.ElapsedTime <= InDebugTime && NextEvent.ElapsedTime >= InDebugTime)
								{
									if (InDebugTime - Event.ElapsedTime < NextEvent.ElapsedTime - InDebugTime)
									{
										ScrubFrameIndex = EventIndex;
									}
									else
									{
										ScrubFrameIndex = EventIndex + 1;
									}
									break;
								}
							}
						}
					}
					// Binary search for surrounding keys on big time differences
					else
					{
						uint64 StartEventIndex = 0;
						uint64 EndEventIndex = EventCount -1;
						
						while (EndEventIndex - StartEventIndex > 1)
						{
							const uint64 MiddleEventIndex = ((StartEventIndex + EndEventIndex) / 2);
							const FRecordingInfoMessage& MiddleEvent = Recording->GetEvent(MiddleEventIndex);
							if (InDebugTime < MiddleEvent.ElapsedTime)
							{
								EndEventIndex = MiddleEventIndex;
							}
							else
							{
								StartEventIndex = MiddleEventIndex;
							}
						}

						// Ensure there is not frames between start and end index
						check(EndEventIndex == StartEventIndex + 1)

						const FRecordingInfoMessage& Event = Recording->GetEvent(StartEventIndex);
						const FRecordingInfoMessage& NextEvent = Recording->GetEvent(EndEventIndex);

						// Ensure debug time is between both frames time range
						check (Event.ElapsedTime <= InDebugTime && NextEvent.ElapsedTime >= InDebugTime)

						// Choose frame that is nearest to the debug time
						if (InDebugTime - Event.ElapsedTime < NextEvent.ElapsedTime - InDebugTime)
						{
							ScrubFrameIndex = StartEventIndex;
						}
						else
						{
							ScrubFrameIndex = EndEventIndex;
						}
					}
				}
				
				const FRecordingInfoMessage& Event = Recording->GetEvent(ScrubFrameIndex);
				InOutTimeInformation.FrameIndex = ScrubFrameIndex;
				InOutTimeInformation.ProfileTime = Event.ProfileTime;
			}
		}
	}
}

const TraceServices::IAnalysisSession* FRewindDebugger::GetAnalysisSession() const
{
	if (UnrealInsightsModule == nullptr)
	{
		UnrealInsightsModule = &FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	}

	return UnrealInsightsModule ? UnrealInsightsModule->GetAnalysisSession().Get() : nullptr;
}

const FObjectInfo* FRewindDebugger::FindOwningActorInfo(const IGameplayProvider* GameplayProvider, uint64 ObjectId) const
{
	const FClassInfo* ActorClassInfo = GameplayProvider->FindClassInfo(*AActor::StaticClass()->GetPathName());
	
	while(true)
	{
		const FObjectInfo& ObjectInfo = GameplayProvider->GetObjectInfo(ObjectId);
		if (GameplayProvider->IsSubClassOf(ObjectInfo.ClassId, ActorClassInfo->Id))
		{
			return &ObjectInfo;
		}
		else
		{
			if (ObjectInfo.OuterId != 0)
			{
				ObjectId = ObjectInfo.OuterId;
			}
			else
			{
				return nullptr;
			}
		}
	}
}

void FRewindDebugger::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRewindDebugger::Tick);

	if (bQueueStartRecording)
	{
		StartRecording();
		bQueueStartRecording = false;
	}

	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		const IAnimationProvider* AnimationProvider = Session->ReadProvider<IAnimationProvider>("AnimationProvider");
		const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");
		
		if (AnimationProvider && GameplayProvider)
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

			// set a default display world when loading a trace (first client/standalone world)
			if (IsTraceFileLoaded() && !bDisplayWorldIdValid)
			{
				GameplayProvider->EnumerateWorlds([this](const FWorldInfo& WorldInfo)
           		{
					if (WorldInfo.Type == FWorldInfo::EType::PIE)
					{
						if(WorldInfo.NetMode == FWorldInfo::ENetMode::Client && WorldInfo.PIEInstanceId == 1)
						{
							DisplayWorldId = WorldInfo.Id;
							bDisplayWorldIdValid = true;
						}
						if(WorldInfo.NetMode == FWorldInfo::ENetMode::Standalone && WorldInfo.PIEInstanceId == 0)
						{
							DisplayWorldId = WorldInfo.Id;
							bDisplayWorldIdValid = true;
						}
					}
					else if (WorldInfo.Type == FWorldInfo::EType::Game)
					{
						DisplayWorldId = WorldInfo.Id;
						bDisplayWorldIdValid = true;
					}
           		});
			}

			double RecordingDurationValue = GameplayProvider->GetRecordingDuration();
			if (IsTraceFileLoaded() && RecordingDurationValue > RecordingDuration.Get())
			{
				// while trace file is loading up, force the trace range to update.
				SetCurrentViewRange(GetCurrentViewRange());
			}
			RecordingDuration.Set(RecordingDurationValue);
			
			RefreshDebugTracks();
			
			UWorld* World = GetWorldToVisualize();

			if (bPIESimulating)
			{
				if (IsRecording())
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(FRewindDebugger::Tick_UpdateSimulating);
					SetCurrentScrubTime(RecordingDurationValue);
					TrackCursorDelegate.ExecuteIfBound(false);
				}
				bTargetActorPositionValid = false;
			}
			else
			{
				if (RecordingDuration.Get() > 0 && CurrentScrubTime <= RecordingDuration.Get()) 
				{
					if (ControlState == EControlState::Play || ControlState == EControlState::PlayReverse)
					{
						float PlaybackRate = URewindDebuggerSettings::Get().PlaybackRate;
						TRACE_CPUPROFILER_EVENT_SCOPE(FRewindDebugger::Tick_UpdatePlayback);
						float Rate = PlaybackRate * (ControlState == EControlState::Play ? 1 : -1);
						SetCurrentScrubTime(FMath::Clamp(CurrentScrubTime + Rate * DeltaTime, 0.0f, RecordingDuration.Get()));
						TrackCursorDelegate.ExecuteIfBound(Rate<0);

						if (CurrentScrubTime == 0 || CurrentScrubTime == RecordingDuration.Get())
						{
							// pause at end.
							ControlState = EControlState::Pause;
						}
					}

					SetCurrentScrubTime(CurrentScrubTime);// update trace time
					
					const double CurrentTraceTime = TraceTime.Get();
					if (CurrentTraceTime != PreviousTraceTime)
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(FRewindDebugger::Tick_UpdateActorPosition);
						PreviousTraceTime = CurrentTraceTime;

						const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);
						TraceServices::FFrame Frame;
						if (FrameProvider.GetFrameFromTime(ETraceFrameType::TraceFrameType_Game, CurrentTraceTime, Frame))
						{
							bool bNewActor = false;
							if (!TargetObjectIds.Contains(TargetActorIdForMesh))
							{
								AnimationProvider->EnumerateSkeletalMeshPoseTimelines([this, &bNewActor, GameplayProvider](uint64 ObjectId, const IAnimationProvider::SkeletalMeshPoseTimeline& TimelineData)
								{
									// until we have actor transforms traced out, the first (from a non-server) skeletal mesh component transform on the target actor be used as as the actor position

									if (const FWorldInfo* WorldInfo = GameplayProvider->FindWorldInfoFromObject(ObjectId))
									{
										if (WorldInfo->NetMode != FWorldInfo::ENetMode::DedicatedServer)
										{
											if (const FObjectInfo* ActorInfo = FindOwningActorInfo(GameplayProvider, ObjectId))
											{
												if (TargetObjectIds.Contains(ActorInfo->Id))
												{
													bNewActor = true;
													TargetActorIdForMesh = ActorInfo->Id;
													TargetActorMeshId = ObjectId;
												}
											}
										}
									}
								});
							}
						
						
							AnimationProvider->ReadSkeletalMeshPoseTimeline(TargetActorMeshId, [this, &Frame, bNewActor](const IAnimationProvider::SkeletalMeshPoseTimeline& TimelineData, bool bHasCurves)
							{
								const FSkeletalMeshPoseMessage * PoseMessage = nullptr;

								// Get last pose in frame
								TimelineData.EnumerateEvents(Frame.StartTime, Frame.EndTime,
									[&PoseMessage](double InStartTime, double InEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& InPoseMessage)
									{
										PoseMessage = &InPoseMessage;
										return TraceServices::EEventEnumerate::Continue;
									});

								// Update position based on pose
								if (PoseMessage)
								{
									// mark the target position as invalid for a frame when the actor changes, so it will be treated as a teleport by the camera system
									bTargetActorPositionValid = !bNewActor;
									TargetActorPosition = PoseMessage->ComponentToWorld.GetTranslation();
								}
							});
						}
					}
				}
			}
		}

		// update extensions
		IterateExtensions([DeltaTime, this](IRewindDebuggerExtension* Extension)
			{
				Extension->Update(DeltaTime, this);
			}
		);
	}
}

void FRewindDebugger::OpenDetailsPanel()
{
	bIsDetailsPanelOpen = true;
	ComponentSelectionChanged(SelectedTrack);
}

void FRewindDebugger::ComponentSelectionChanged(TSharedPtr<RewindDebugger::FRewindDebuggerTrack> SelectedObject)
{
	SelectedTrack = SelectedObject;

	if (bIsDetailsPanelOpen)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		
		 // if we now have no selection, don't force the tab into focus - this happens when tracks disappear and can cause PIE to lose focus while playing
		bool bInvokeAsInactive = !SelectedTrack.IsValid();
		TSharedPtr<SDockTab> DetailsTab = LevelEditorTabManager->TryInvokeTab(FRewindDebuggerModule::DetailsTabName, bInvokeAsInactive);

		if (DetailsTab.IsValid())
		{
			UpdateDetailsPanel(DetailsTab.ToSharedRef());
		}
	}
}

void FRewindDebugger::UpdateDetailsPanel(TSharedRef<SDockTab> DetailsTab)
{
	if (bIsDetailsPanelOpen)
	{
		TSharedPtr<SWidget> DetailsView;

		if (SelectedTrack)
		{
			DetailsView = SelectedTrack->GetDetailsView();
		}

		if (DetailsView)
		{
			DetailsTab->SetContent(DetailsView.ToSharedRef());
		}
		else
		{
			static TSharedPtr<SWidget> EmptyDetails;
			if (EmptyDetails == nullptr)
			{
				EmptyDetails = SNew(SSpacer);
			}
			DetailsTab->SetContent(EmptyDetails.ToSharedRef());
		}
	}
}

void FRewindDebugger::RegisterComponentContextMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->FindMenu("RewindDebugger.ComponentContextMenu");
	
	FToolMenuSection& Section = Menu->FindOrAddSection("SelectedTrack");
	
	FToolMenuEntry& Entry = Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		const UComponentContextMenuContext* Context = InSection.FindContext<UComponentContextMenuContext>();

		if (Context && Context->SelectedTrack.IsValid())
		{
			Context->SelectedTrack->BuildContextMenu(InSection);
		}
	}));
}

void FRewindDebugger::MakeOtherWorldsMenu(UToolMenu* Menu)
{
	FRewindDebugger* RewindDebugger = FRewindDebugger::Instance();
	
	FToolMenuSection& Section = Menu->AddSection("Other Worlds", LOCTEXT("Other Worlds", "Other Worlds"));

	if (const TraceServices::IAnalysisSession* Session = RewindDebugger->GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
		const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");

		GameplayProvider->EnumerateWorlds([GameplayProvider, &Section](const FWorldInfo& WorldInfo)
		{
			const FObjectInfo* ObjectInfo = GameplayProvider->FindObjectInfo(WorldInfo.Id);
			FString Name = ObjectInfo->Name;

			if(WorldInfo.NetMode == FWorldInfo::ENetMode::DedicatedServer)
			{
				return;
			}
			else if (WorldInfo.Type == FWorldInfo::EType::Game || WorldInfo.Type == FWorldInfo::EType::PIE)
			{
				return;
			}
			else
			{
				if (WorldInfo.Type == FWorldInfo::EType::Editor)
				{
					Name = Name + " (Editor)";
				}
				else if (WorldInfo.Type == FWorldInfo::EType::Inactive)
				{
					Name = Name + " (Editor)";
				}
				else if (WorldInfo.Type == FWorldInfo::EType::EditorPreview)
				{
					Name = Name + " (Editor Preview)";
				}
				else if (WorldInfo.Type == FWorldInfo::EType::GamePreview)
				{
					Name = Name + " (Game Preview)";
				}
				else if (WorldInfo.Type == FWorldInfo::EType::GameRPC)
				{
					Name = Name + " (Game RPC)";
				}
			}
		
			Section.AddMenuEntry(FName(ObjectInfo->Name,WorldInfo.Id),
								FText::FromString(Name),
								FText(),
								FSlateIcon(),
								FUIAction( FExecuteAction::CreateLambda([World = WorldInfo.Id]()
								{
									FRewindDebugger::Instance()->SetDisplayWorld(World);
								}),
								FCanExecuteAction(),
								FIsActionChecked::CreateLambda([World = WorldInfo.Id]()
								{
									return FRewindDebugger::Instance()->DisplayWorldId == World;
								})),
								EUserInterfaceActionType::Check
							);
		
		});
	}
}

void FRewindDebugger::SetDisplayWorld(uint64 WorldId)
{
	DisplayWorldId = WorldId;
	
	IterateExtensions([this](IRewindDebuggerExtension* Extension)
	{
		Extension->Clear(this);
		Extension->Update(0.0,this);
	});
}
void FRewindDebugger::MakeWorldsMenu(UToolMenu* Menu)
{
	FRewindDebugger* RewindDebugger = FRewindDebugger::Instance();
	
	FToolMenuSection& ServerWorldsSection = Menu->AddSection("Server Worlds", LOCTEXT("Server", "Server"));
	FToolMenuSection& GameWorldsSection = Menu->AddSection("Game Worlds", LOCTEXT("Game Worlds", "Game Worlds"));
	FToolMenuSection& OtherWorldsSection = Menu->AddSection("Other Worlds", LOCTEXT("Other Worlds", "Other Worlds"));

	OtherWorldsSection.AddSubMenu("Other Worlds",
		LOCTEXT("Other Worlds", "Other Worlds"),
		LOCTEXT("Other Worlds Tooltip", "Additional worlds such as  Editor Preview worlds"),
		FNewToolMenuChoice(
			FNewToolMenuDelegate::CreateStatic(FRewindDebugger::MakeOtherWorldsMenu)
			));
	
	if (const TraceServices::IAnalysisSession* Session = RewindDebugger->GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
		const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");

		GameplayProvider->EnumerateWorlds([GameplayProvider,&GameWorldsSection, &OtherWorldsSection, &ServerWorldsSection](const FWorldInfo& WorldInfo)
		{
			const FObjectInfo* ObjectInfo = GameplayProvider->FindObjectInfo(WorldInfo.Id);
			FString Name = ObjectInfo->Name;

			FToolMenuSection* Section = &OtherWorldsSection;

			if(WorldInfo.NetMode == FWorldInfo::ENetMode::DedicatedServer)
			{
				Section = &ServerWorldsSection;
				Name = Name + " (Server)";
			}
			else if (WorldInfo.Type == FWorldInfo::EType::Game || WorldInfo.Type == FWorldInfo::EType::PIE)
			{
				Section = &GameWorldsSection;
				if(WorldInfo.NetMode == FWorldInfo::ENetMode::Client && WorldInfo.PIEInstanceId >= 0)
				{
					Name = Name + " (Client " + FString::FromInt(WorldInfo.PIEInstanceId) + ")";
				}
				if(WorldInfo.NetMode == FWorldInfo::ENetMode::Standalone && WorldInfo.PIEInstanceId >= 0)
				{
					Name = Name + " (Standalone " + FString::FromInt(WorldInfo.PIEInstanceId) + ")";
				}
			}
			else
			{
				return;
			}
			
			Section->AddMenuEntry(FName(ObjectInfo->Name,WorldInfo.Id),
								FText::FromString(Name),
								FText(),
								FSlateIcon(),
								FUIAction( FExecuteAction::CreateLambda([World = WorldInfo.Id]()
								{
									FRewindDebugger::Instance()->SetDisplayWorld(World);
								}),
								FCanExecuteAction(),
								FIsActionChecked::CreateLambda([World = WorldInfo.Id]()
								{
									return FRewindDebugger::Instance()->DisplayWorldId == World;
								})),
								EUserInterfaceActionType::Check
							);
		});
	}
}

void FRewindDebugger::RegisterToolBar()
{
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("RewindDebugger.ToolBar", NAME_None, EMultiBoxType::ToolBar);
	
	FToolMenuSection& Section = Menu->FindOrAddSection("VCRControls");
	const FRewindDebuggerCommands& Commands = FRewindDebuggerCommands::Get();
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			Commands.FirstFrame,
			FText(),
			TAttribute<FText>(),
			FSlateIcon("RewindDebuggerStyle", "RewindDebugger.FirstFrame.small")));
	
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			Commands.PreviousFrame,
			FText(),
			TAttribute<FText>(),
			FSlateIcon("RewindDebuggerStyle", "RewindDebugger.PreviousFrame.small")));
			
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.ReversePlay,
				FText(),
				TAttribute<FText>(),
				FSlateIcon("RewindDebuggerStyle", "RewindDebugger.ReversePlay.small")));
	
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.Pause,
				FText(),
				FText::Format(LOCTEXT("PauseButtonTooltip", "{0} ({1})"), Commands.Pause->GetDescription(), Commands.PauseOrPlay->GetInputText()),
				FSlateIcon("RewindDebuggerStyle", "RewindDebugger.Pause.small")));
	
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.Play,
				FText(),
				FText::Format(LOCTEXT("PlayButtonTooltip", "{0} ({1})"), Commands.Play->GetDescription(), Commands.PauseOrPlay->GetInputText()),
				FSlateIcon("RewindDebuggerStyle", "RewindDebugger.Play.small")));

	Section.AddEntry(
    		FToolMenuEntry::InitComboButton(
    			"PlaybackRate",
    			FToolUIActionChoice(),
    			FNewToolMenuChoice(
    				FNewToolMenuDelegate::CreateLambda([](UToolMenu* InNewToolMenu)
    				{
    					FToolMenuSection& Section = InNewToolMenu->AddSection("PlaybackSpeed", LOCTEXT("Playback Speed", "Playback Speed"));
    					
						Section.AddEntry(
							FToolMenuEntry::InitMenuEntry(
								"001",LOCTEXT("0.1","0.1"), LOCTEXT("Set playback speed to 0.1", "Set playback speed to 0.1"), FSlateIcon(),
								FUIAction(
								FExecuteAction::CreateLambda([]()
									{ 
										URewindDebuggerSettings::Get().PlaybackRate = 0.1;
									}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([]
									{
										return FMath::IsNearlyEqual(URewindDebuggerSettings::Get().PlaybackRate, 0.1);
									})
									)
									, EUserInterfaceActionType::RadioButton
								)
    					);
						Section.AddEntry(
							FToolMenuEntry::InitMenuEntry(
								"025",LOCTEXT("0.25","0.25"), LOCTEXT("Set playback speed to 0.25", "Set playback speed to 0.25"), FSlateIcon(),
								FUIAction(
								FExecuteAction::CreateLambda([]()
									{ 
										URewindDebuggerSettings::Get().PlaybackRate = 0.25;
									}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([]
									{
										return FMath::IsNearlyEqual(URewindDebuggerSettings::Get().PlaybackRate, 0.25);
									})
									)
									, EUserInterfaceActionType::RadioButton
							)
    					);
    					Section.AddEntry(
							FToolMenuEntry::InitMenuEntry(
								"05",LOCTEXT("0.5","0.5"), LOCTEXT("Set playback speed to 0.5", "Set playback speed to 0.5"), FSlateIcon(),
								FUIAction(
								FExecuteAction::CreateLambda([]()
									{ 
										URewindDebuggerSettings::Get().PlaybackRate = 0.5;
									}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([]
									{
										return FMath::IsNearlyEqual(URewindDebuggerSettings::Get().PlaybackRate, 0.5);
									})
									)
									, EUserInterfaceActionType::RadioButton
							)
						);

						Section.AddEntry(
							FToolMenuEntry::InitMenuEntry(
								"1",LOCTEXT("1","1"), LOCTEXT("Set playback speed to 1", "Set playback speed to 1"), FSlateIcon(),
								FUIAction(
								FExecuteAction::CreateLambda([]()
									{ 
										URewindDebuggerSettings::Get().PlaybackRate = 1;
									}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([]
									{
										return FMath::IsNearlyEqual(URewindDebuggerSettings::Get().PlaybackRate, 1);
									})
									)
									, EUserInterfaceActionType::RadioButton
							)
						);
    					
    					Section.AddEntry(
							FToolMenuEntry::InitMenuEntry(
								"2",LOCTEXT("2","2"), LOCTEXT("Set playback speed to 2", "Set playback speed to 2"), FSlateIcon(),
								FUIAction(
								FExecuteAction::CreateLambda([]()
									{ 
										URewindDebuggerSettings::Get().PlaybackRate = 2;
									}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([]
									{
										return FMath::IsNearlyEqual(URewindDebuggerSettings::Get().PlaybackRate, 2);
									})
									)
									, EUserInterfaceActionType::RadioButton
							)
						);

						Section.AddEntry(
							FToolMenuEntry::InitWidget(
								"EditInSequencerMenu", 
								SNew(SNumericEntryBox<float>)
									.Value_Lambda([]()
									{
										return URewindDebuggerSettings::Get().PlaybackRate;
									})
									.OnValueChanged_Lambda([](float Value)
									{
										URewindDebuggerSettings::Get().PlaybackRate = Value;
									}),
								FText::GetEmpty(),
								true, false, true
							)
						);
    				})
    			),
				FText(),
    			LOCTEXT("PlaybackRate_Tooltip", "Playback Options"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.PlaybackOptions")
    		)
    	);

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.NextFrame,
				FText(),
				TAttribute<FText>(),
				FSlateIcon("RewindDebuggerStyle", "RewindDebugger.NextFrame.small")));
	
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.LastFrame,
				FText(),
				TAttribute<FText>(),
				FSlateIcon("RewindDebuggerStyle", "RewindDebugger.LastFrame.small")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.StartRecording,
				FText(),
				TAttribute<FText>(),
				FSlateIcon("RewindDebuggerStyle", "RewindDebugger.StartRecording.small")));
				

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.StopRecording,
				FText(),
				TAttribute<FText>(),
				FSlateIcon("RewindDebuggerStyle", "RewindDebugger.StopRecording.small")));

	Section.AddSeparator(NAME_None);


	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.AttachToSession,
				FText(),
				TAttribute<FText>(),
				FSlateIcon("RewindDebuggerStyle", "RewindDebugger.ConnectToSession")));
				
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.OpenTrace,
				FText(),
				TAttribute<FText>(),
				 FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen")));
	
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
    			Commands.SaveTrace,
				FText(),
    			TAttribute<FText>(),
    			 FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save")));
				 
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.ClearTrace,
				FText(),
				TAttribute<FText>(),
				 FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete")));

	
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.AutoEject,
				FText(),
				TAttribute<FText>(),
				FSlateIcon("RewindDebuggerStyle", "RewindDebugger.AutoEject")));
					Section.AddSeparator(NAME_None);
                				
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.AutoRecord,
				FText(),
				TAttribute<FText>(),
				FSlateIcon("RewindDebuggerStyle", "RewindDebugger.AutoRecord")));
	
	Section.AddSeparator("NAME_None");

	Section.AddEntry(FToolMenuEntry::InitComboButton(
		"Display World",
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction::CreateLambda([](){ return FRewindDebugger::Instance()->IsTraceFileLoaded(); })
			),
		FNewToolMenuDelegate::CreateStatic(&FRewindDebugger::MakeWorldsMenu),
		LOCTEXT("Display World", "Display World"),
		LOCTEXT("Display World Tooltip", "When loading trace files, only the objects (Such as Skeletal Meshes) from the world selected here will be spawned for preview")
		));
	
	Menu->SetStyleSet(&FAppStyle::Get());
	Menu->StyleName = "PaletteToolBar";
}


void FRewindDebugger::ComponentDoubleClicked(TSharedPtr<RewindDebugger::FRewindDebuggerTrack> SelectedObject)
{
	if (!SelectedObject.IsValid())
	{
		return;
	}
	
	SelectedTrack = SelectedObject;
	SelectedTrack->HandleDoubleClick();
}

TSharedPtr<SWidget> FRewindDebugger::BuildComponentContextMenu() const
{
	UComponentContextMenuContext* MenuContext = NewObject<UComponentContextMenuContext>();
	MenuContext->SelectedObject = GetSelectedComponent();
	MenuContext->SelectedTrack = SelectedTrack;
	
	if (SelectedTrack.IsValid())
	{
		// build a list of class hierarchy names to make it easier for extensions to enable menu entries by type
		if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
	
			const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");
			const FObjectInfo& ObjectInfo = GameplayProvider->GetObjectInfo(SelectedTrack->GetObjectId());
			uint64 ClassId = ObjectInfo.ClassId;
			while (ClassId != 0)
			{
				const FClassInfo& ClassInfo = GameplayProvider->GetClassInfo(ClassId);
				MenuContext->TypeHierarchy.Add(ClassInfo.Name);
				ClassId = ClassInfo.SuperId;
			}
		}
	}

	return UToolMenus::Get()->GenerateWidget("RewindDebugger.ComponentContextMenu", FToolMenuContext(MenuContext));
 }


TSharedPtr<FDebugObjectInfo> FRewindDebugger::GetSelectedComponent() const
{
	if (!SelectedComponent.IsValid())
	{
		SelectedComponent = MakeShared<FDebugObjectInfo>(0, "");
	}
	
	if (SelectedTrack.IsValid())
	{
		SelectedComponent->ObjectId = SelectedTrack->GetObjectId();
		SelectedComponent->ObjectName = SelectedTrack->GetDisplayName().ToString();
		return SelectedComponent;
	}
	else
	{
		return TSharedPtr<FDebugObjectInfo>();
	}
}

TSharedPtr<RewindDebugger::FRewindDebuggerTrack> FRewindDebugger::GetSelectedTrack() const
{
	return SelectedTrack;
}

// build a component tree that's compatible with the public api from 5.0 for GetDebugComponents.
void FRewindDebugger::RefreshDebugComponents(TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>>& InTracks, TArray<TSharedPtr<FDebugObjectInfo>>& OutComponents)
{
	OutComponents.SetNum(0);
	for(TSharedPtr<RewindDebugger::FRewindDebuggerTrack>& Track : InTracks)
	{
		int Index = OutComponents.Num();
		OutComponents.Add(MakeShared<FDebugObjectInfo>(Track->GetObjectId(), Track->GetDisplayName().ToString()));
		TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>> TrackChildren;
		Track->IterateSubTracks([&TrackChildren](TSharedPtr<RewindDebugger::FRewindDebuggerTrack> Child) { TrackChildren.Add(Child); });
		RefreshDebugComponents(TrackChildren, OutComponents[Index]->Children);
	}
}

TArray<TSharedPtr<FDebugObjectInfo>>& FRewindDebugger::GetDebugComponents()
{
	RefreshDebugComponents(DebugTracks, DebugComponents);
	return DebugComponents;
}

#undef LOCTEXT_NAMESPACE
