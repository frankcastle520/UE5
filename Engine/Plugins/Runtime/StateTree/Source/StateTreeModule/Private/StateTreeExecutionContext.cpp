// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeExecutionContext.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreePropertyFunctionBase.h"
#include "StateTreeReference.h"
#include "Containers/StaticArray.h"
#include "Debugger/StateTreeTrace.h"
#include "Debugger/StateTreeTraceTypes.h"
#include "Misc/ScopeExit.h"
#include "VisualLogger/VisualLogger.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Logging/LogScopedVerbosityOverride.h"

#include "Engine/World.h"

#define STATETREE_LOG(Verbosity, Format, ...) UE_VLOG_ALWAYS_UELOG(GetOwner(), LogStateTree, Verbosity, TEXT("%s: ") Format, *GetInstanceDescription(), ##__VA_ARGS__)
#define STATETREE_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG_ALWAYS_UELOG((Condition), GetOwner(), LogStateTree, Verbosity, TEXT("%s: ") Format, *GetInstanceDescription(), ##__VA_ARGS__)

#define STATETREE_LOG_AND_TRACE(LogVerbosity, TraceVerbosity, Format, ...) \
	UE_VLOG_ALWAYS_UELOG(GetOwner(), LogStateTree, LogVerbosity, TEXT("%s: ") Format, *GetInstanceDescription(), ##__VA_ARGS__); \
	STATETREE_TRACE_LOG_EVENT(TraceVerbosity, Format, ##__VA_ARGS__)

#if WITH_STATETREE_TRACE
	#define ID_NAME PREPROCESSOR_JOIN(InstanceId,__LINE__) \

	#define STATETREE_TRACE_SCOPED_PHASE(Phase) \
		FStateTreeInstanceDebugId ID_NAME = GetInstanceDebugId(); \
		TRACE_STATETREE_PHASE_EVENT(ID_NAME, Phase, EStateTreeTraceEventType::Push, FStateTreeStateHandle::Invalid) \
		ON_SCOPE_EXIT { TRACE_STATETREE_PHASE_EVENT(ID_NAME, Phase, EStateTreeTraceEventType::Pop, FStateTreeStateHandle::Invalid) }

	#define STATETREE_TRACE_SCOPED_STATE(StateHandle) \
		FStateTreeInstanceDebugId ID_NAME = GetInstanceDebugId(); \
		TRACE_STATETREE_PHASE_EVENT(ID_NAME, EStateTreeUpdatePhase::Unset, EStateTreeTraceEventType::Push, StateHandle) \
		ON_SCOPE_EXIT { TRACE_STATETREE_PHASE_EVENT(ID_NAME, EStateTreeUpdatePhase::Unset, EStateTreeTraceEventType::Pop, StateHandle) }

	#define STATETREE_TRACE_SCOPED_STATE_PHASE(StateHandle, Phase) \
		FStateTreeInstanceDebugId ID_NAME = GetInstanceDebugId(); \
		TRACE_STATETREE_PHASE_EVENT(ID_NAME, Phase, EStateTreeTraceEventType::Push, StateHandle) \
		ON_SCOPE_EXIT { TRACE_STATETREE_PHASE_EVENT(ID_NAME, Phase, EStateTreeTraceEventType::Pop, StateHandle) }

	#define STATETREE_TRACE_INSTANCE_EVENT(EventType)						TRACE_STATETREE_INSTANCE_EVENT(GetInstanceDebugId(), GetStateTree(), *GetInstanceDescription(), EventType);
	#define STATETREE_TRACE_INSTANCE_FRAME_EVENT(InstanceDebugId, Frame)	TRACE_STATETREE_INSTANCE_FRAME_EVENT(InstanceDebugId, Frame);
	#define STATETREE_TRACE_PHASE_BEGIN(Phase)								TRACE_STATETREE_PHASE_EVENT(GetInstanceDebugId(), Phase, EStateTreeTraceEventType::Push, FStateTreeStateHandle::Invalid)
	#define STATETREE_TRACE_PHASE_END(Phase)								TRACE_STATETREE_PHASE_EVENT(GetInstanceDebugId(), Phase, EStateTreeTraceEventType::Pop, FStateTreeStateHandle::Invalid)
	#define STATETREE_TRACE_ACTIVE_STATES_EVENT(ActiveFrames)				TRACE_STATETREE_ACTIVE_STATES_EVENT(GetInstanceDebugId(), ActiveFrames);
	#define STATETREE_TRACE_LOG_EVENT(TraceVerbosity, Format, ...)			TRACE_STATETREE_LOG_EVENT(GetInstanceDebugId(), TraceVerbosity, Format, ##__VA_ARGS__)
	#define STATETREE_TRACE_STATE_EVENT(StateHandle, EventType)				TRACE_STATETREE_STATE_EVENT(GetInstanceDebugId(), StateHandle, EventType);
	#define STATETREE_TRACE_TASK_EVENT(Index, DataView, EventType, Status)	TRACE_STATETREE_TASK_EVENT(GetInstanceDebugId(), FStateTreeIndex16(Index), DataView, EventType, Status);
	#define STATETREE_TRACE_EVALUATOR_EVENT(Index, DataView, EventType)		TRACE_STATETREE_EVALUATOR_EVENT(GetInstanceDebugId(), FStateTreeIndex16(Index), DataView, EventType);
	#define STATETREE_TRACE_CONDITION_EVENT(Index, DataView, EventType)		TRACE_STATETREE_CONDITION_EVENT(GetInstanceDebugId(), FStateTreeIndex16(Index), DataView, EventType);	
	#define STATETREE_TRACE_TRANSITION_EVENT(Source, EventType)				TRACE_STATETREE_TRANSITION_EVENT(GetInstanceDebugId(), Source, EventType);
#else
	#define STATETREE_TRACE_SCOPED_PHASE(Phase)
	#define STATETREE_TRACE_SCOPED_STATE(StateHandle)
	#define STATETREE_TRACE_SCOPED_STATE_PHASE(StateHandle, Phase)
	#define STATETREE_TRACE_INSTANCE_EVENT(EventType)
	#define STATETREE_TRACE_INSTANCE_FRAME_EVENT(InstanceDebugId, Frame)
	#define STATETREE_TRACE_PHASE_BEGIN(Phase)
	#define STATETREE_TRACE_PHASE_END(Phase)
	#define STATETREE_TRACE_ACTIVE_STATES_EVENT(ActiveFrames)
	#define STATETREE_TRACE_LOG_EVENT(TraceVerbosity, Format, ...)
	#define STATETREE_TRACE_STATE_EVENT(StateHandle, EventType)
	#define STATETREE_TRACE_TASK_EVENT(Index, DataView, EventType, Status)
	#define STATETREE_TRACE_EVALUATOR_EVENT(Index, DataView, EventType)
	#define STATETREE_TRACE_CONDITION_EVENT(Index, DataView, EventType)
	#define STATETREE_TRACE_TRANSITION_EVENT(Source, EventType)
#endif // WITH_STATETREE_TRACE

namespace UE::StateTree
{
	constexpr int32 DebugIndentSize = 2;	// Debug printing indent for hierarchical data.
}; // UE::StateTree


FStateTreeExecutionContext::FCurrentlyProcessedFrameScope::FCurrentlyProcessedFrameScope(FStateTreeExecutionContext& InContext, const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame): Context(InContext)
{
	check(CurrentFrame.StateTree);
	FStateTreeInstanceStorage* SharedInstanceDataStorage = &CurrentFrame.StateTree->GetSharedInstanceData()->GetMutableStorage();

	SavedFrame = Context.CurrentlyProcessedFrame;
	SavedParentFrame = Context.CurrentlyProcessedParentFrame;
	SavedSharedInstanceDataStorage = Context.CurrentlyProcessedSharedInstanceStorage;
	Context.CurrentlyProcessedFrame = &CurrentFrame;
	Context.CurrentlyProcessedParentFrame = CurrentParentFrame;
	Context.CurrentlyProcessedSharedInstanceStorage = SharedInstanceDataStorage;
	
	STATETREE_TRACE_INSTANCE_FRAME_EVENT(Context.GetInstanceDebugId(), Context.CurrentlyProcessedFrame);
}

FStateTreeExecutionContext::FCurrentlyProcessedFrameScope::~FCurrentlyProcessedFrameScope()
{
	Context.CurrentlyProcessedFrame = SavedFrame;
	Context.CurrentlyProcessedParentFrame = SavedParentFrame;
	Context.CurrentlyProcessedSharedInstanceStorage = SavedSharedInstanceDataStorage;

	if (Context.CurrentlyProcessedFrame)
	{
		STATETREE_TRACE_INSTANCE_FRAME_EVENT(Context.GetInstanceDebugId(), Context.CurrentlyProcessedFrame);
	}
}


FStateTreeExecutionContext::FStateTreeExecutionContext(UObject& InOwner, const UStateTree& InStateTree, FStateTreeInstanceData& InInstanceData, const FOnCollectStateTreeExternalData& InCollectExternalDataDelegate, const EStateTreeRecordTransitions RecordTransitions)
	: Owner(InOwner)
	, RootStateTree(InStateTree)
	, InstanceData(InInstanceData)
	, CollectExternalDataDelegate(InCollectExternalDataDelegate)
{
	if (InStateTree.IsReadyToRun())
	{
		// Initialize data views for all possible items.
		ContextAndExternalDataViews.SetNum(RootStateTree.GetNumContextDataViews());

		InstanceDataStorage = &InstanceData.GetMutableStorage();
		check(InstanceDataStorage);
		
		EventQueue = InstanceData.GetSharedMutableEventQueue();

		bRecordTransitions = RecordTransitions == EStateTreeRecordTransitions::Yes;
	}
	else
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree asset is not valid ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
	}
}

FStateTreeExecutionContext::FStateTreeExecutionContext(const FStateTreeExecutionContext& InContextToCopy, const UStateTree& InStateTree, FStateTreeInstanceData& InInstanceData)
	: FStateTreeExecutionContext(InContextToCopy.Owner, InStateTree, InInstanceData, InContextToCopy.CollectExternalDataDelegate)
{
	LinkedStateTreeOverrides = InContextToCopy.LinkedStateTreeOverrides;
	const bool bIsSameSchema = RootStateTree.GetSchema()->GetClass() == InContextToCopy.GetStateTree()->GetSchema()->GetClass();
	if (bIsSameSchema)
	{
		for (const FStateTreeExternalDataDesc& TargetDataDesc : GetContextDataDescs())
		{
			const int32 TargetIndex = TargetDataDesc.Handle.DataHandle.GetIndex();
			ContextAndExternalDataViews[TargetIndex] = InContextToCopy.ContextAndExternalDataViews[TargetIndex];
		}
		
		EventQueue = InstanceData.GetSharedMutableEventQueue();
	}
	else
	{
		STATETREE_LOG(Error, TEXT("%hs: '%s' using StateTree '%s' trying to run subtree '%s' but their schemas don't match"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(InContextToCopy.GetStateTree()), *GetFullNameSafe(&RootStateTree));
	}
}

FStateTreeExecutionContext::~FStateTreeExecutionContext()
{
	// Mark external data indices as invalid
	FStateTreeExecutionState& Exec = InstanceData.GetMutableStorage().GetMutableExecutionState();
	for (FStateTreeExecutionFrame& Frame : Exec.ActiveFrames)
	{
		Frame.ExternalDataBaseIndex = {};
	}
}

void FStateTreeExecutionContext::SetCollectExternalDataCallback(const FOnCollectStateTreeExternalData& Callback)
{
	CollectExternalDataDelegate = Callback;
}

void FStateTreeExecutionContext::SetLinkedStateTreeOverrides(const FStateTreeReferenceOverrides* InLinkedStateTreeOverrides)
{
	LinkedStateTreeOverrides = InLinkedStateTreeOverrides;
}

const FStateTreeReference* FStateTreeExecutionContext::GetLinkedStateTreeOverrideForTag(const FGameplayTag StateTag) const
{
	if (!LinkedStateTreeOverrides)
	{
		return nullptr;
	}
	
	for (const FStateTreeReferenceOverrideItem& Item : LinkedStateTreeOverrides->GetOverrideItems())
	{
		if (Item.GetStateTag() == StateTag)
		{
			return &Item.GetStateTreeReference();
		}
	}

	return nullptr;
}


bool FStateTreeExecutionContext::AreContextDataViewsValid() const
{
	if (!IsValid())
	{
		return false;
	}
	
	bool bResult = true;
	
	for (const FStateTreeExternalDataDesc& DataDesc : RootStateTree.GetContextDataDescs())
	{
		const FStateTreeDataView& DataView = ContextAndExternalDataViews[DataDesc.Handle.DataHandle.GetIndex()];

		// Required items must have valid pointer of the expected type.  
		if (DataDesc.Requirement == EStateTreeExternalDataRequirement::Required)
		{
			if (!DataView.IsValid() || !DataDesc.IsCompatibleWith(DataView))
			{
				bResult = false;
				break;
			}
		}
		else // Optional items must have the expected type if they are set.
		{
			if (DataView.IsValid() && !DataDesc.IsCompatibleWith(DataView))
			{
				bResult = false;
				break;
			}
		}
	}
	return bResult;
}

bool FStateTreeExecutionContext::SetContextDataByName(const FName Name, FStateTreeDataView DataView)
{
	const FStateTreeExternalDataDesc* Desc = RootStateTree.GetContextDataDescs().FindByPredicate([&Name](const FStateTreeExternalDataDesc& Desc)
	{
		return Desc.Name == Name;
	});
	if (Desc)
	{
		ContextAndExternalDataViews[Desc->Handle.DataHandle.GetIndex()] = DataView;
		return true;
	}
	return false;
}

EStateTreeRunStatus FStateTreeExecutionContext::Start(const FInstancedPropertyBag* InitialParameters, int32 RandomSeed)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Start);

	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return EStateTreeRunStatus::Failed;
	}

	FStateTreeExecutionState& Exec = GetExecState();
	if (!ensureMsgf(Exec.CurrentPhase == EStateTreeUpdatePhase::Unset, TEXT("%hs can't be called while already in %s ('%s' using StateTree '%s')."),
			__FUNCTION__, *UEnum::GetDisplayValueAsText(Exec.CurrentPhase).ToString(), *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree)))
	{
		return EStateTreeRunStatus::Failed;
	}

	// Stop if still running previous state.
	if (Exec.TreeRunStatus == EStateTreeRunStatus::Running)
	{
		Stop();
	}

	// Initialize instance data. No active states yet, so we'll initialize the evals and global tasks.
	InstanceData.Reset();

	if (!InitialParameters || !SetGlobalParameters(*InitialParameters))
	{
		SetGlobalParameters(RootStateTree.GetDefaultParameters());
	}

	// Initialize for the init frame.
	FStateTreeExecutionFrame& InitFrame = Exec.ActiveFrames.AddDefaulted_GetRef();
	InitFrame.StateTree = &RootStateTree;
	InitFrame.RootState = FStateTreeStateHandle::Root;
	InitFrame.ActiveStates = {};
	InitFrame.bIsGlobalFrame = true;
	
	UpdateInstanceData({}, Exec.ActiveFrames);
	Exec.RandomStream.Initialize(RandomSeed == -1 ? FPlatformTime::Cycles() : RandomSeed);

	if (!CollectActiveExternalData())
	{
		STATETREE_LOG(Warning, TEXT("%hs: Failed to collect external data ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return EStateTreeRunStatus::Failed;
	}

	// Must sent instance creation event first 
	STATETREE_TRACE_INSTANCE_EVENT(EStateTreeTraceEventType::Push);
	
	// Set scoped phase only for properly initialized context with valid Instance data
	// since we need it to output the InstanceId
	STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::StartTree);

	STATETREE_LOG(VeryVerbose, TEXT("%hs: Starting State Tree %s on owner '%s'."),
		__FUNCTION__, *GetFullNameSafe(&RootStateTree), *GetNameSafe(&Owner));

	// From this point any calls to Stop should be deferred.
	Exec.CurrentPhase = EStateTreeUpdatePhase::StartTree;

	// Start evaluators and global tasks. Fail the execution if any global task fails.
	FStateTreeIndex16 LastInitializedTaskIndex;
	const EStateTreeRunStatus GlobalTasksRunStatus = StartEvaluatorsAndGlobalTasks(LastInitializedTaskIndex);
	if (GlobalTasksRunStatus == EStateTreeRunStatus::Running)
	{
		// First tick.
		// Tasks are not ticked here, since their behavior is that EnterState() (called above) is treated as a tick.  
		TickEvaluatorsAndGlobalTasks(0.0f, /*bTickGlobalTasks*/false);

		// Initialize to unset running state.
		Exec.TreeRunStatus = EStateTreeRunStatus::Running;
		Exec.LastTickStatus = EStateTreeRunStatus::Unset;

		static const FStateTreeStateHandle RootState = FStateTreeStateHandle(0);

		FStateSelectionResult StateSelectionResult;
		if (SelectState(InitFrame, RootState, StateSelectionResult))
		{
			check(StateSelectionResult.ContainsFrames());
			if (StateSelectionResult.GetSelectedFrames().Last().ActiveStates.Last().IsCompletionState())
			{
				// Transition to a terminal state (succeeded/failed).
				STATETREE_LOG(Warning, TEXT("%hs: Tree %s at StateTree start on '%s' using StateTree '%s'."),
					__FUNCTION__, StateSelectionResult.GetSelectedFrames().Last().ActiveStates.Last() == FStateTreeStateHandle::Succeeded ? TEXT("succeeded") : TEXT("failed"), *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
				Exec.TreeRunStatus = StateSelectionResult.GetSelectedFrames().Last().ActiveStates.Last().ToCompletionStatus();
			}
			else
			{
				// Enter state tasks can fail/succeed, treat it same as tick.
				FStateTreeTransitionResult Transition;
				Transition.TargetState = RootState;
				Transition.CurrentRunStatus = Exec.LastTickStatus;
				Transition.NextActiveFrames = StateSelectionResult.GetSelectedFrames(); // Enter state will update Exec.ActiveFrames.
				Transition.NextActiveFrameEvents = StateSelectionResult.GetFramesStateSelectionEvents();
				const EStateTreeRunStatus LastTickStatus = EnterState(Transition);
			
				Exec.LastTickStatus = LastTickStatus;

				// Report state completed immediately.
				if (Exec.LastTickStatus != EStateTreeRunStatus::Running)
				{
					StateCompleted();
				}
			}
		}

		if (Exec.LastTickStatus == EStateTreeRunStatus::Unset)
		{
			// Should not happen. This may happen if initial state could not be selected.
			STATETREE_LOG(Error, TEXT("%hs: Failed to select initial state on '%s' using StateTree '%s'. This should not happen, check that the StateTree logic can always select a state at start."),
				__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
			Exec.TreeRunStatus = EStateTreeRunStatus::Failed;
		}
	}
	else
	{
		StopEvaluatorsAndGlobalTasks(GlobalTasksRunStatus, LastInitializedTaskIndex);

		STATETREE_LOG(VeryVerbose, TEXT("%hs: Global tasks completed the StateTree %s on start in status '%s'."),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree), *UEnum::GetDisplayValueAsText(GlobalTasksRunStatus).ToString());

		// No active states or global tasks anymore, reset frames.
		Exec.ActiveFrames.Reset();
		
		// We are not considered as running yet so we only set the status without requiring a stop.
		Exec.TreeRunStatus = GlobalTasksRunStatus;
	}

	// Reset phase since we are now safe to stop.
	Exec.CurrentPhase = EStateTreeUpdatePhase::Unset;

	// Use local for resulting run state since Stop will reset the instance data.
	EStateTreeRunStatus Result = Exec.TreeRunStatus;
	
	if (Exec.RequestedStop != EStateTreeRunStatus::Unset)
	{
		STATETREE_LOG_AND_TRACE(VeryVerbose, Log, TEXT("Processing Deferred Stop"));
		Result = Stop(Exec.RequestedStop);
	}
	
	return Result;
}

EStateTreeRunStatus FStateTreeExecutionContext::Stop(EStateTreeRunStatus CompletionStatus)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Stop);

	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return EStateTreeRunStatus::Failed;
	}

	if (!CollectActiveExternalData())
	{
		STATETREE_LOG(Warning, TEXT("%hs: Failed to collect external data ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return EStateTreeRunStatus::Failed;
	}

	// Set scoped phase only for properly initialized context with valid Instance data
	// since we need it to output the InstanceId 
	STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::StopTree);

	// Make sure that we return a valid completion status (i.e. Succeeded, Failed or Stopped)
	if (CompletionStatus == EStateTreeRunStatus::Unset
		|| CompletionStatus == EStateTreeRunStatus::Running)
	{
		CompletionStatus = EStateTreeRunStatus::Stopped;
	}

	FStateTreeExecutionState& Exec = GetExecState();

	// A reentrant call to Stop or a call from Start or Tick must be deferred.
	if (Exec.CurrentPhase != EStateTreeUpdatePhase::Unset)
	{
		STATETREE_LOG_AND_TRACE(VeryVerbose, Log, TEXT("Deferring Stop at end of %s"), *UEnum::GetDisplayValueAsText(Exec.CurrentPhase).ToString());

		Exec.RequestedStop = CompletionStatus;
		return EStateTreeRunStatus::Running;
	}

	// No need to clear on exit since we reset all the instance data before leaving the function.
	Exec.CurrentPhase = EStateTreeUpdatePhase::StopTree;

	EStateTreeRunStatus Result = Exec.TreeRunStatus;
	
	// Exit states if still in some valid state.
	if (Exec.TreeRunStatus == EStateTreeRunStatus::Running)
	{
		// Transition to Succeeded state.
		FStateTreeTransitionResult Transition;
		Transition.TargetState = FStateTreeStateHandle::FromCompletionStatus(CompletionStatus);
		Transition.CurrentRunStatus = CompletionStatus;		
		ExitState(Transition);

		// No active states or global tasks anymore, reset frames.
		Exec.ActiveFrames.Reset();

		Result = CompletionStatus;
	}

	// Trace before resetting the instance data since it is required to provide all the event information
	STATETREE_TRACE_ACTIVE_STATES_EVENT({});
	STATETREE_TRACE_INSTANCE_EVENT(EStateTreeTraceEventType::Pop);

	// Destruct all allocated instance data (does not shrink the buffer). This will invalidate Exec too.
	InstanceData.Reset();

	// External data needs to be recollected if this exec context is reused.
	bActiveExternalDataCollected = false;

	return Result;
}

EStateTreeRunStatus FStateTreeExecutionContext::TickPrelude()
{
	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return EStateTreeRunStatus::Failed;
	}

	if (!CollectActiveExternalData())
	{
		STATETREE_LOG(Warning, TEXT("%hs: Failed to collect external data ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return EStateTreeRunStatus::Failed;
	}

	FStateTreeExecutionState& Exec = GetExecState();

	// No ticking if the tree is done or stopped.
	if (Exec.TreeRunStatus != EStateTreeRunStatus::Running)
	{
		return Exec.TreeRunStatus;
	}

	if (!ensureMsgf(Exec.CurrentPhase == EStateTreeUpdatePhase::Unset, TEXT("%hs can't be called while already in %s ('%s' using StateTree '%s')."),
			__FUNCTION__, *UEnum::GetDisplayValueAsText(Exec.CurrentPhase).ToString(), *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree)))
	{
		return EStateTreeRunStatus::Failed;
	}

	// From this point any calls to Stop should be deferred.
	Exec.CurrentPhase = EStateTreeUpdatePhase::TickStateTree;

	return EStateTreeRunStatus::Running;
}


EStateTreeRunStatus FStateTreeExecutionContext::TickPostlude()
{
	FStateTreeExecutionState& Exec = GetExecState();

	// Reset phase since we are now safe to stop.
	Exec.CurrentPhase = EStateTreeUpdatePhase::Unset;

	// Use local for resulting run state since Stop will reset the instance data.
	EStateTreeRunStatus Result = Exec.TreeRunStatus;
	
	if (Exec.RequestedStop != EStateTreeRunStatus::Unset)
	{
		STATETREE_LOG_AND_TRACE(VeryVerbose, Log, TEXT("Processing Deferred Stop"));
		Result = Stop(Exec.RequestedStop);
	}

	return Result;
}

EStateTreeRunStatus FStateTreeExecutionContext::Tick(const float DeltaTime)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Tick);

	const EStateTreeRunStatus PreludeResult = TickPrelude();
	if (PreludeResult != EStateTreeRunStatus::Running)
	{
		return PreludeResult;
	}

	TickUpdateTasksInternal(DeltaTime);
	TickTriggerTransitionsInternal();

	return TickPostlude();
}

EStateTreeRunStatus FStateTreeExecutionContext::TickUpdateTasks(const float DeltaTime)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Tick);

	const EStateTreeRunStatus PreludeResult = TickPrelude();
	if (PreludeResult != EStateTreeRunStatus::Running)
	{
		return PreludeResult;
	}

	TickUpdateTasksInternal(DeltaTime);
	
	return TickPostlude();
}
	
EStateTreeRunStatus FStateTreeExecutionContext::TickTriggerTransitions()
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Tick);

	const EStateTreeRunStatus PreludeResult = TickPrelude();
	if (PreludeResult != EStateTreeRunStatus::Running)
	{
		return PreludeResult;
	}

	TickTriggerTransitionsInternal();

	return TickPostlude();
}

void FStateTreeExecutionContext::TickUpdateTasksInternal(const float DeltaTime)
{
	FStateTreeExecutionState& Exec = GetExecState();
	
	// Update the delayed transitions.
	for (FStateTreeTransitionDelayedState& DelayedState : Exec.DelayedTransitions)
	{
		DelayedState.TimeLeft -= DeltaTime;
	}

	// Tick global evaluators and tasks.
	const EStateTreeRunStatus EvalAndGlobalTaskStatus = TickEvaluatorsAndGlobalTasks(DeltaTime);
	if (EvalAndGlobalTaskStatus == EStateTreeRunStatus::Running)
	{
		if (Exec.LastTickStatus == EStateTreeRunStatus::Running)
		{
			// Tick tasks on active states.
			Exec.LastTickStatus = TickTasks(DeltaTime);

			// Report state completed immediately.
			if (Exec.LastTickStatus != EStateTreeRunStatus::Running)
			{
				StateCompleted();
			}
		}
	}
	else
	{
		STATETREE_TRACE_LOG_EVENT(Log, TEXT("Global tasks completed (%s), stopping the tree"), *UEnum::GetDisplayValueAsText(EvalAndGlobalTaskStatus).ToString());
		Exec.RequestedStop = EvalAndGlobalTaskStatus;
	}
}

void FStateTreeExecutionContext::TickTriggerTransitionsInternal()
{
	FStateTreeExecutionState& Exec = GetExecState();

	// If stop is requested, do not try to trigger transitions.
	if (Exec.RequestedStop != EStateTreeRunStatus::Unset)
	{
		return;
	}

	// The state selection is repeated up to MaxIteration time. This allows failed EnterState() to potentially find a new state immediately.
	// This helps event driven StateTrees to not require another event/tick to find a suitable state.
	static constexpr int32 MaxIterations = 5;
	for (int32 Iter = 0; Iter < MaxIterations; Iter++)
	{
		// Trigger conditional transitions or state succeed/failed transitions. First tick transition is handled here too.
		if (TriggerTransitions())
		{
			STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::ApplyTransitions);
			STATETREE_TRACE_TRANSITION_EVENT(NextTransitionSource, EStateTreeTraceEventType::OnTransition);
			NextTransitionSource.Reset();

			ExitState(NextTransition);

			// Tree succeeded or failed.
			if (NextTransition.TargetState.IsCompletionState())
			{
				// Transition to a terminal state (succeeded/failed), or default transition failed.
				Exec.TreeRunStatus = NextTransition.TargetState.ToCompletionStatus();

				// Stop evaluators and global tasks.
				StopEvaluatorsAndGlobalTasks(Exec.TreeRunStatus);

				// No active states or global tasks anymore, reset frames.
				Exec.ActiveFrames.Reset();

				break;
			}

			// Enter state tasks can fail/succeed, treat it same as tick.
			const EStateTreeRunStatus LastTickStatus = EnterState(NextTransition);

			NextTransition.Reset();

			Exec.LastTickStatus = LastTickStatus;

			// Report state completed immediately.
			if (Exec.LastTickStatus != EStateTreeRunStatus::Running)
			{
				StateCompleted();
			}
		}

		// Stop as soon as have found a running state.
		if (Exec.LastTickStatus == EStateTreeRunStatus::Running)
		{
			break;
		}
	}
}

EStateTreeRunStatus FStateTreeExecutionContext::GetStateTreeRunStatus() const
{
	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return EStateTreeRunStatus::Failed;
	}

	if (const FStateTreeExecutionState* Exec = InstanceData.GetExecutionState())
	{
		return Exec->TreeRunStatus;
	}
	
	return EStateTreeRunStatus::Failed;
}

void FStateTreeExecutionContext::SendEvent(const FGameplayTag Tag, const FConstStructView Payload, const FName Origin) const
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_SendEvent);

	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return;
	}

	STATETREE_LOG_AND_TRACE(Verbose, Log, TEXT("Send Event '%s'"), *Tag.ToString());

	FStateTreeEventQueue& LocalEventQueue = InstanceData.GetMutableEventQueue();
	LocalEventQueue.SendEvent(&Owner, Tag, Payload, Origin);
}

void FStateTreeExecutionContext::RequestTransition(const FStateTreeTransitionRequest& Request)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_RequestTransition);

	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return;
	}

	FStateTreeExecutionState& Exec = GetExecState();

	if (bAllowDirectTransitions)
	{
		checkf(CurrentlyProcessedFrame, TEXT("Expecting CurrentlyProcessedFrame to be valid when called during TriggerTransitions()."));
		
		STATETREE_LOG(Verbose, TEXT("Request transition to '%s' at priority %s"), *GetSafeStateName(*CurrentlyProcessedFrame, Request.TargetState), *UEnum::GetDisplayValueAsText(Request.Priority).ToString());

		if (RequestTransition(*CurrentlyProcessedFrame, Request.TargetState, Request.Priority))
		{
			NextTransitionSource = FStateTreeTransitionSource(EStateTreeTransitionSourceType::ExternalRequest, Request.TargetState, Request.Priority);
		}
	}
	else
	{
		const FStateTreeExecutionFrame* RootFrame = &Exec.ActiveFrames[0];
		if (CurrentlyProcessedFrame)
		{
			RootFrame = CurrentlyProcessedFrame;
		}

		if (!RootFrame)
		{
			STATETREE_LOG(Warning, TEXT("%hs: RequestTransition called on %s using StateTree %s without active state. Start() must be called before requesting transition."),
				__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
			return;
		}
		
		STATETREE_LOG(Verbose, TEXT("Request transition to '%s' at priority %s"), *GetSafeStateName(*RootFrame, Request.TargetState), *UEnum::GetDisplayValueAsText(Request.Priority).ToString());

		FStateTreeTransitionRequest RequestWithSource = Request;
		RequestWithSource.SourceStateTree = RootFrame->StateTree;
		RequestWithSource.SourceRootState = RootFrame->ActiveStates[0];
		RequestWithSource.SourceState = CurrentlyProcessedState;
		
		InstanceData.AddTransitionRequest(&Owner, RequestWithSource);
	}
}

#if WITH_STATETREE_TRACE
FStateTreeInstanceDebugId FStateTreeExecutionContext::GetInstanceDebugId() const
{
	FStateTreeInstanceDebugId& InstanceDebugId = GetExecState().InstanceDebugId; 
	if (!InstanceDebugId.IsValid())
	{
		static std::atomic<uint32> SerialNumber = 0;
		InstanceDebugId = FStateTreeInstanceDebugId(GetTypeHash(GetInstanceDescription()), ++SerialNumber); 
	}
	return InstanceDebugId;
}
#endif // WITH_STATETREE_TRACE

void FStateTreeExecutionContext::UpdateInstanceData(TConstArrayView<FStateTreeExecutionFrame> CurrentActiveFrames, TArrayView<FStateTreeExecutionFrame> NextActiveFrames)
{
	// Estimate how many new instance data items we might have.
	int32 EstimatedNumStructs = 0;
	for (int32 FrameIndex = 0; FrameIndex < NextActiveFrames.Num(); FrameIndex++)
	{
		const FStateTreeExecutionFrame& NextFrame = NextActiveFrames[FrameIndex];
		if (NextFrame.bIsGlobalFrame)
		{
			EstimatedNumStructs += NextFrame.StateTree->NumGlobalInstanceData;
		}
		// States
		for (int32 StateIndex = 0; StateIndex < NextFrame.ActiveStates.Num(); StateIndex++)
		{
			const FStateTreeStateHandle StateHandle = NextFrame.ActiveStates[StateIndex];
			const FCompactStateTreeState& State = NextFrame.StateTree->States[StateHandle.Index];
			EstimatedNumStructs += State.InstanceDataNum;
		}
	}
	
	TArray<FConstStructView, TConcurrentLinearArrayAllocator<FDefaultBlockAllocationTag>> InstanceStructs;
	InstanceStructs.Reserve(EstimatedNumStructs);

	TArray<FInstancedStruct*, TConcurrentLinearArrayAllocator<FDefaultBlockAllocationTag>> TempInstanceStructs;
	TempInstanceStructs.Reserve(EstimatedNumStructs);

	TArray<FCompactStateTreeParameters, TFixedAllocator<FStateSelectionResult::MaxExecutionFrames>> TempParams;

	TArrayView<FStateTreeTemporaryInstanceData> TempInstances = InstanceDataStorage->GetMutableTemporaryInstances();
	auto FindInstanceTempData = [&TempInstances](const FStateTreeExecutionFrame& Frame, FStateTreeDataHandle DataHandle)
	{
		FStateTreeTemporaryInstanceData* TempData = TempInstances.FindByPredicate([&Frame, &DataHandle](const FStateTreeTemporaryInstanceData& Data)
		{
			return Data.StateTree == Frame.StateTree && Data.RootState == Frame.RootState && Data.DataHandle == DataHandle; 
		});
		return TempData ? &TempData->Instance : nullptr;
	};
	
	// Find next instance data sources and find common/existing section of instance data at start.
	int32 CurrentGlobalInstanceIndexBase = 0;
	int32 NumCommonInstanceData = 0;

	const UStruct* NextStateParameterDataStruct = nullptr;
	FStateTreeDataHandle NextStateParameterDataHandle = FStateTreeDataHandle::Invalid;
	
	FStateTreeDataHandle CurrentGlobalParameterDataHandle = FStateTreeDataHandle(EStateTreeDataSourceType::GlobalParameterData);

	bool bAreCommon = true;
	for (int32 FrameIndex = 0; FrameIndex < NextActiveFrames.Num(); FrameIndex++)
	{
		const bool bIsCurrentFrameValid = CurrentActiveFrames.IsValidIndex(FrameIndex)
						&& CurrentActiveFrames[FrameIndex].IsSameFrame(NextActiveFrames[FrameIndex]);

		bAreCommon &= bIsCurrentFrameValid;

		const FStateTreeExecutionFrame* CurrentFrame = bIsCurrentFrameValid ? &CurrentActiveFrames[FrameIndex] : nullptr;
		FStateTreeExecutionFrame& NextFrame = NextActiveFrames[FrameIndex];

		check(NextFrame.StateTree);

		if (NextFrame.bIsGlobalFrame)
		{
			// Handle global tree parameters
			if (NextStateParameterDataHandle.IsValid())
			{
				// Point to the parameter block set by linked state.
				check(NextStateParameterDataStruct == NextFrame.StateTree->GetDefaultParameters().GetPropertyBagStruct());
				CurrentGlobalParameterDataHandle = NextStateParameterDataHandle;
				NextStateParameterDataHandle = FStateTreeDataHandle::Invalid; // Mark as used.
			}
			
			// Global Evals
			const int32 BaseIndex = InstanceStructs.Num();
			CurrentGlobalInstanceIndexBase = BaseIndex;
			
			InstanceStructs.AddDefaulted(NextFrame.StateTree->NumGlobalInstanceData);
			TempInstanceStructs.AddZeroed(NextFrame.StateTree->NumGlobalInstanceData);
			
			for (int32 EvalIndex = NextFrame.StateTree->EvaluatorsBegin; EvalIndex < (NextFrame.StateTree->EvaluatorsBegin + NextFrame.StateTree->EvaluatorsNum); EvalIndex++)
			{
				const FStateTreeEvaluatorBase& Eval =  NextFrame.StateTree->Nodes[EvalIndex].Get<const FStateTreeEvaluatorBase>();
				const FConstStructView EvalInstanceData = NextFrame.StateTree->DefaultInstanceData.GetStruct(Eval.InstanceTemplateIndex.Get());
				InstanceStructs[BaseIndex + Eval.InstanceDataHandle.GetIndex()] = EvalInstanceData;
				if (!bAreCommon)
				{
					TempInstanceStructs[BaseIndex + Eval.InstanceDataHandle.GetIndex()] = FindInstanceTempData(NextFrame, Eval.InstanceDataHandle);
				}
			}

			// Global tasks
			for (int32 TaskIndex = NextFrame.StateTree->GlobalTasksBegin; TaskIndex < (NextFrame.StateTree->GlobalTasksBegin + NextFrame.StateTree->GlobalTasksNum); TaskIndex++)
			{
				const FStateTreeTaskBase& Task =  NextFrame.StateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
				const FConstStructView TaskInstanceData = NextFrame.StateTree->DefaultInstanceData.GetStruct(Task.InstanceTemplateIndex.Get());
				InstanceStructs[BaseIndex + Task.InstanceDataHandle.GetIndex()] = TaskInstanceData;
				if (!bAreCommon)
				{
					TempInstanceStructs[BaseIndex + Task.InstanceDataHandle.GetIndex()] = FindInstanceTempData(NextFrame, Task.InstanceDataHandle);
				}
			}

			if (bAreCommon)
			{
				NumCommonInstanceData = InstanceStructs.Num();
			}
		}

		// States
		const int32 BaseIndex = InstanceStructs.Num();

		NextFrame.GlobalParameterDataHandle = CurrentGlobalParameterDataHandle;
		NextFrame.GlobalInstanceIndexBase = FStateTreeIndex16(CurrentGlobalInstanceIndexBase);
		NextFrame.ActiveInstanceIndexBase = FStateTreeIndex16(BaseIndex);

		for (int32 StateIndex = 0; StateIndex < NextFrame.ActiveStates.Num(); StateIndex++)
		{
			// Check if the next state is still same as current state, GetStateSafe() will return invalid state if passed out of bounds index.
			bAreCommon = bAreCommon && (CurrentFrame && CurrentFrame->ActiveStates.GetStateSafe(StateIndex) == NextFrame.ActiveStates[StateIndex]);

			const FStateTreeStateHandle StateHandle = NextFrame.ActiveStates[StateIndex];
			const FCompactStateTreeState& State = NextFrame.StateTree->States[StateHandle.Index];

			InstanceStructs.AddDefaulted(State.InstanceDataNum);
			TempInstanceStructs.AddZeroed(State.InstanceDataNum);

			bool bCanHaveTempData = false;
			
			if (State.Type == EStateTreeStateType::Subtree)
			{
				check(State.ParameterDataHandle.IsValid());
				check(State.ParameterTemplateIndex.IsValid());
				const FConstStructView ParamsInstanceData = NextFrame.StateTree->DefaultInstanceData.GetStruct(State.ParameterTemplateIndex.Get());
				if (!NextStateParameterDataHandle.IsValid())
				{
					// Parameters are not set by a linked state, create instance data.
					InstanceStructs[BaseIndex + State.ParameterDataHandle.GetIndex()] = ParamsInstanceData;
					NextFrame.StateParameterDataHandle = State.ParameterDataHandle;
					bCanHaveTempData = true;
				}
				else
				{
					// Point to the parameter block set by linked state.
					const FCompactStateTreeParameters* Params = ParamsInstanceData.GetPtr<const FCompactStateTreeParameters>();
					const UStruct* StateParameterDataStruct = Params ? Params->Parameters.GetPropertyBagStruct() : nullptr;
					check(NextStateParameterDataStruct == StateParameterDataStruct);
					
					NextFrame.StateParameterDataHandle = NextStateParameterDataHandle;
					NextStateParameterDataHandle = FStateTreeDataHandle::Invalid; // Mark as used.

					// This state will not instantiate parameter data, so we don't care about the temp data either.
					bCanHaveTempData = false;
				}
			}
			else
			{
				if (State.ParameterTemplateIndex.IsValid())
				{
					// Linked state's instance data is the parameters.
					check(State.ParameterDataHandle.IsValid());

					const FCompactStateTreeParameters* Params = nullptr;
					if (FInstancedStruct* TempParamsInstanceData = FindInstanceTempData(NextFrame, State.ParameterDataHandle))
					{
						// If we have temp data for the parameters, then setup the instance data with just a type, so that we can steal the temp data below (TempInstanceStructs).
						// We expect overridden linked assets to hit this code path. 
						InstanceStructs[BaseIndex + State.ParameterDataHandle.GetIndex()] = FConstStructView(TempParamsInstanceData->GetScriptStruct());
						Params = TempParamsInstanceData->GetPtr<const FCompactStateTreeParameters>();
						bCanHaveTempData = true;
					}
					else
					{
						// If not temp data, use the states or linked assets default values.
						FConstStructView ParamsInstanceData;
						if (State.Type == EStateTreeStateType::LinkedAsset)
						{
							if (const FStateTreeReference* Override = GetLinkedStateTreeOverrideForTag(State.Tag))
							{
								ParamsInstanceData = FConstStructView::Make(TempParams.Emplace_GetRef(Override->GetParameters()));
							}
						}
						if (!ParamsInstanceData.IsValid())
						{
							ParamsInstanceData = NextFrame.StateTree->DefaultInstanceData.GetStruct(State.ParameterTemplateIndex.Get());
						}
						InstanceStructs[BaseIndex + State.ParameterDataHandle.GetIndex()] = ParamsInstanceData;
						Params = ParamsInstanceData.GetPtr<const FCompactStateTreeParameters>();
						bCanHaveTempData = true;
					}

					if (State.Type == EStateTreeStateType::Linked
						|| State.Type == EStateTreeStateType::LinkedAsset)
					{
						// Store the index of the parameter data, so that we can point the linked state to it.
						check(State.ParameterDataHandle.GetSource() == EStateTreeDataSourceType::StateParameterData);
						checkf(!NextStateParameterDataHandle.IsValid(), TEXT("NextStateParameterDataIndex not should be set yet when we encounter a linked state."));
						NextStateParameterDataHandle = State.ParameterDataHandle;
						NextStateParameterDataStruct = Params ? Params->Parameters.GetPropertyBagStruct() : nullptr;
					}
				}
			}
			
			if (!bAreCommon && bCanHaveTempData)
			{
				TempInstanceStructs[BaseIndex + State.ParameterDataHandle.GetIndex()] = FindInstanceTempData(NextFrame, State.ParameterDataHandle);
			}

			if (State.EventDataIndex.IsValid())
			{
				InstanceStructs[BaseIndex + State.EventDataIndex.Get()] = FConstStructView(FStateTreeSharedEvent::StaticStruct());
			}

			for (int32 TaskIndex = State.TasksBegin; TaskIndex < (State.TasksBegin + State.TasksNum); TaskIndex++)
			{
				const FStateTreeTaskBase& Task = NextFrame.StateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
				const FConstStructView TaskInstanceData = NextFrame.StateTree->DefaultInstanceData.GetStruct(Task.InstanceTemplateIndex.Get());
				InstanceStructs[BaseIndex + Task.InstanceDataHandle.GetIndex()] = TaskInstanceData;
				if (!bAreCommon)
				{
					TempInstanceStructs[BaseIndex + Task.InstanceDataHandle.GetIndex()] = FindInstanceTempData(NextFrame, Task.InstanceDataHandle);
				}
			}

			if (bAreCommon)
			{
				NumCommonInstanceData = InstanceStructs.Num();
			}
		}
	}
	
	// Common section should match.
	// @todo: put this behind a define when enough testing has been done.
	for (int32 Index = 0; Index < NumCommonInstanceData; Index++)
	{
		check(Index < InstanceData.Num());

		FConstStructView ExistingInstanceDataView = InstanceData.GetStruct(Index);
		FConstStructView NewInstanceDataView = InstanceStructs[Index]; 

		check(NewInstanceDataView.GetScriptStruct() == ExistingInstanceDataView.GetScriptStruct());

		const FStateTreeInstanceObjectWrapper* ExistingWrapper = ExistingInstanceDataView.GetPtr<const FStateTreeInstanceObjectWrapper>();
		const FStateTreeInstanceObjectWrapper* NewWrapper = ExistingInstanceDataView.GetPtr<const FStateTreeInstanceObjectWrapper>();
		if (ExistingWrapper && NewWrapper)
		{
			check(ExistingWrapper->InstanceObject && NewWrapper->InstanceObject);
			check(ExistingWrapper->InstanceObject->GetClass() == NewWrapper->InstanceObject->GetClass());
		}
	}

	// Remove instance data that was not common.
	InstanceData.ShrinkTo(NumCommonInstanceData);

	// Add new instance data.
	InstanceData.Append(Owner,
		MakeArrayView(InstanceStructs.GetData() + NumCommonInstanceData, InstanceStructs.Num() - NumCommonInstanceData),
		MakeArrayView(TempInstanceStructs.GetData() + NumCommonInstanceData, TempInstanceStructs.Num() - NumCommonInstanceData));

	InstanceData.ResetTemporaryInstances();
}

FStateTreeDataView FStateTreeExecutionContext::GetDataView(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataHandle Handle)
{
	switch (Handle.GetSource())
	{
	case EStateTreeDataSourceType::ContextData:
		check(!ContextAndExternalDataViews.IsEmpty())
		return ContextAndExternalDataViews[Handle.GetIndex()];

	case EStateTreeDataSourceType::ExternalData:
		check(!ContextAndExternalDataViews.IsEmpty())
		return ContextAndExternalDataViews[CurrentFrame.ExternalDataBaseIndex.Get() + Handle.GetIndex()];

	case EStateTreeDataSourceType::TransitionEvent:
		{
			if (CurrentlyProcessedTransitionEvent)
			{
				// const_cast because events are read only, but we cannot express that in FStateTreeDataView.
				return FStateTreeDataView(FStructView::Make(*const_cast<FStateTreeEvent*>(CurrentlyProcessedTransitionEvent)));
			}

			return nullptr;
		}

	case EStateTreeDataSourceType::StateEvent:
		{
			// If state selection is going, return FStateTreeEvent of the event currently captured by the state selection.
			if (CurrentlyProcessedStateSelectionEvents)
			{
				if (const FCompactStateTreeState* State = CurrentFrame.StateTree->GetStateFromHandle(Handle.GetState()))
				{
					// Events are read only, but we cannot express that in FStateTreeDataView.
					if (FStateTreeEvent* Event = CurrentlyProcessedStateSelectionEvents->Events[State->Depth].GetMutable())
					{
						return FStateTreeDataView(FStructView::Make(*Event));
					}
				}

				return {};
			}

			return GetDataViewFromInstanceStorage(*InstanceDataStorage, CurrentlyProcessedSharedInstanceStorage, ParentFrame, CurrentFrame, Handle);
		}

	default:
		return GetDataViewFromInstanceStorage(*InstanceDataStorage, CurrentlyProcessedSharedInstanceStorage, ParentFrame, CurrentFrame, Handle);
	}
}

EStateTreeRunStatus FStateTreeExecutionContext::ForceTransition(const FRecordedStateTreeTransitionResult& Transition)
{
	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return EStateTreeRunStatus::Failed;
	}

	FStateTreeTransitionResult TransitionResult = FStateTreeTransitionResult(Transition);

	ExitState(TransitionResult);

	return EnterState(TransitionResult);
}

FStateTreeDataView FStateTreeExecutionContext::GetDataViewFromInstanceStorage(FStateTreeInstanceStorage& InstanceDataStorage, FStateTreeInstanceStorage* CurrentlyProcessedSharedInstanceStorage, const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataHandle Handle)
{
	switch (Handle.GetSource())
	{
	case EStateTreeDataSourceType::None:
		return {};

	case EStateTreeDataSourceType::GlobalInstanceData:
		return InstanceDataStorage.GetMutableStruct(CurrentFrame.GlobalInstanceIndexBase.Get() + Handle.GetIndex());
	case EStateTreeDataSourceType::GlobalInstanceDataObject:
		return InstanceDataStorage.GetMutableObject(CurrentFrame.GlobalInstanceIndexBase.Get() + Handle.GetIndex());
		
	case EStateTreeDataSourceType::ActiveInstanceData:
		return InstanceDataStorage.GetMutableStruct(CurrentFrame.ActiveInstanceIndexBase.Get() + Handle.GetIndex());
	case EStateTreeDataSourceType::ActiveInstanceDataObject:
		return InstanceDataStorage.GetMutableObject(CurrentFrame.ActiveInstanceIndexBase.Get() + Handle.GetIndex());

	case EStateTreeDataSourceType::SharedInstanceData:
		check(CurrentlyProcessedSharedInstanceStorage);
		return CurrentlyProcessedSharedInstanceStorage->GetMutableStruct(Handle.GetIndex());
	case EStateTreeDataSourceType::SharedInstanceDataObject:
		check(CurrentlyProcessedSharedInstanceStorage);
		return CurrentlyProcessedSharedInstanceStorage->GetMutableObject(Handle.GetIndex());

	case EStateTreeDataSourceType::GlobalParameterData:
		// Defined in parent frame or is root state tree parameters
		if (ParentFrame)
		{
			return GetDataViewFromInstanceStorage(InstanceDataStorage, CurrentlyProcessedSharedInstanceStorage, nullptr, *ParentFrame, CurrentFrame.GlobalParameterDataHandle);
		}
		return InstanceDataStorage.GetMutableGlobalParameters();

	case EStateTreeDataSourceType::SubtreeParameterData:
		{
			// Defined in parent frame.
			if (ParentFrame)
			{
				// Linked subtree, params defined in parent scope.
				return GetDataViewFromInstanceStorage(InstanceDataStorage, CurrentlyProcessedSharedInstanceStorage, nullptr, *ParentFrame, CurrentFrame.StateParameterDataHandle);
			}
			// Standalone subtree, params define as state params.
			FCompactStateTreeParameters& SubtreeParams = InstanceDataStorage.GetMutableStruct(CurrentFrame.ActiveInstanceIndexBase.Get() + Handle.GetIndex()).Get<FCompactStateTreeParameters>();
			return SubtreeParams.Parameters.GetMutableValue();
		}

	case EStateTreeDataSourceType::StateParameterData:
		{
			FCompactStateTreeParameters& StateParams = InstanceDataStorage.GetMutableStruct(CurrentFrame.ActiveInstanceIndexBase.Get() + Handle.GetIndex()).Get<FCompactStateTreeParameters>();
			return StateParams.Parameters.GetMutableValue();
		}

	case EStateTreeDataSourceType::StateEvent:
		{
			// Return FStateTreeEvent from shared event.
			FStateTreeSharedEvent& SharedEvent = InstanceDataStorage.GetMutableStruct(CurrentFrame.ActiveInstanceIndexBase.Get() + Handle.GetIndex()).Get<FStateTreeSharedEvent>();
			check(SharedEvent.IsValid());
			// Events are read only, but we cannot express that in FStateTreeDataView.
			return FStateTreeDataView(FStructView::Make(*SharedEvent.GetMutable()));
		}

	case EStateTreeDataSourceType::ContextData:
	case EStateTreeDataSourceType::ExternalData:
	case EStateTreeDataSourceType::TransitionEvent:
		return {};
	
	default:
		checkf(false, TEXT("Unhandle case %s"), *UEnum::GetValueAsString(Handle.GetSource()));
	}

	return {};
}

const FStateTreeExecutionFrame* FStateTreeExecutionContext::FindFrame(const UStateTree* StateTree, FStateTreeStateHandle RootState, TConstArrayView<FStateTreeExecutionFrame> Frames, const FStateTreeExecutionFrame*& OutParentFrame)
{
	const int32 FrameIndex = Frames.IndexOfByPredicate([&StateTree, RootState](const FStateTreeExecutionFrame& Frame)
	{
		return Frame.StateTree == StateTree && Frame.RootState == RootState;
	});

	if (FrameIndex == INDEX_NONE)
	{
		OutParentFrame = nullptr;
		return nullptr;
	}

	if (FrameIndex > 0)
	{
		OutParentFrame = &Frames[FrameIndex - 1];
	}

	return &Frames[FrameIndex];
}

bool FStateTreeExecutionContext::IsHandleSourceValid(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataHandle Handle) const
{
	// Checks that the instance data is valid for specific handle types.
	// 
	// The CurrentFrame may not be yet properly initialized, for that reason we need to check
	// that the path to the handle makes sense (it's part of the active states) as well as that
	// we actually have instance data for the handle (index is valid).
	// 
	// The (base) indices can be invalid if the frame/state is not entered yet.
	// For active instance data we need to check that the frame is initialized for a specific state,
	// as well as that the instance data is initialized.

	switch (Handle.GetSource())
	{
	case EStateTreeDataSourceType::None:
		return true;

	case EStateTreeDataSourceType::GlobalInstanceData:
	case EStateTreeDataSourceType::GlobalInstanceDataObject:
		return CurrentFrame.GlobalInstanceIndexBase.IsValid()
			&& InstanceDataStorage->IsValidIndex(CurrentFrame.GlobalInstanceIndexBase.Get() + Handle.GetIndex());

	case EStateTreeDataSourceType::ActiveInstanceData:
	case EStateTreeDataSourceType::ActiveInstanceDataObject:
		return CurrentFrame.ActiveInstanceIndexBase.IsValid()
			&& CurrentFrame.ActiveStates.Contains(Handle.GetState(), CurrentFrame.NumCurrentlyActiveStates)
			&& InstanceDataStorage->IsValidIndex(CurrentFrame.ActiveInstanceIndexBase.Get() + Handle.GetIndex());
		
	case EStateTreeDataSourceType::SharedInstanceData:
	case EStateTreeDataSourceType::SharedInstanceDataObject:
		return true;

	case EStateTreeDataSourceType::ContextData:
		return true;

	case EStateTreeDataSourceType::ExternalData:
		return CurrentFrame.ExternalDataBaseIndex.IsValid()
			&& ContextAndExternalDataViews.IsValidIndex(CurrentFrame.ExternalDataBaseIndex.Get() + Handle.GetIndex());

	case EStateTreeDataSourceType::GlobalParameterData:
		return ParentFrame
			? IsHandleSourceValid(nullptr, *ParentFrame, CurrentFrame.GlobalParameterDataHandle)
			: CurrentFrame.GlobalParameterDataHandle.IsValid();

	case EStateTreeDataSourceType::SubtreeParameterData:
		if (ParentFrame)
		{
			// If the current subtree state is not instantiated yet, we cannot assume that the parameter data is instantiated in the parent frame either. 
			if (!CurrentFrame.ActiveInstanceIndexBase.IsValid())
			{
				return false;
			}
			// Linked subtree, params defined in parent scope.
			return IsHandleSourceValid(nullptr, *ParentFrame, CurrentFrame.StateParameterDataHandle);
		}
		// Standalone subtree, params define as state params.
		return CurrentFrame.ActiveInstanceIndexBase.IsValid()
			&& CurrentFrame.ActiveStates.Contains(Handle.GetState(), CurrentFrame.NumCurrentlyActiveStates)
			&& InstanceDataStorage->IsValidIndex(CurrentFrame.ActiveInstanceIndexBase.Get() + Handle.GetIndex());

	case EStateTreeDataSourceType::StateParameterData:
		return CurrentFrame.ActiveInstanceIndexBase.IsValid()
			&& CurrentFrame.ActiveStates.Contains(Handle.GetState(), CurrentFrame.NumCurrentlyActiveStates)
			&& InstanceDataStorage->IsValidIndex(CurrentFrame.ActiveInstanceIndexBase.Get() + Handle.GetIndex());

	case EStateTreeDataSourceType::TransitionEvent:
		return CurrentlyProcessedTransitionEvent != nullptr;

	case EStateTreeDataSourceType::StateEvent:
		return CurrentlyProcessedStateSelectionEvents != nullptr
			|| (CurrentFrame.ActiveInstanceIndexBase.IsValid()
			&& CurrentFrame.ActiveStates.Contains(Handle.GetState())
			&& InstanceDataStorage->IsValidIndex(CurrentFrame.ActiveInstanceIndexBase.Get() + Handle.GetIndex()));

	default:
		checkf(false, TEXT("Unhandle case %s"), *UEnum::GetValueAsString(Handle.GetSource()));
	}

	return false;
}

FStateTreeDataView FStateTreeExecutionContext::GetDataViewOrTemporary(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataHandle Handle)
{
	if (IsHandleSourceValid(ParentFrame, CurrentFrame, Handle))
	{
		return GetDataView(ParentFrame, CurrentFrame, Handle);
	}

	check(InstanceDataStorage);
	
	switch (Handle.GetSource())
	{
	case EStateTreeDataSourceType::GlobalInstanceData:
	case EStateTreeDataSourceType::ActiveInstanceData:
		return InstanceDataStorage->GetMutableTemporaryStruct(CurrentFrame, Handle);
		
	case EStateTreeDataSourceType::GlobalInstanceDataObject:
	case EStateTreeDataSourceType::ActiveInstanceDataObject:
		return InstanceDataStorage->GetMutableTemporaryObject(CurrentFrame, Handle);

	case EStateTreeDataSourceType::GlobalParameterData:
		if (ParentFrame)
		{
			if (FCompactStateTreeParameters* Params = InstanceDataStorage->GetMutableTemporaryStruct(*ParentFrame, CurrentFrame.GlobalParameterDataHandle).GetPtr<FCompactStateTreeParameters>())
			{
				return Params->Parameters.GetMutableValue();
			}
		}
		break;
		
	case EStateTreeDataSourceType::SubtreeParameterData:
		if (ParentFrame)
		{
			// Linked subtree, params defined in parent scope.
			if (FCompactStateTreeParameters* Params = InstanceDataStorage->GetMutableTemporaryStruct(*ParentFrame, CurrentFrame.StateParameterDataHandle).GetPtr<FCompactStateTreeParameters>())
			{
				return Params->Parameters.GetMutableValue();
			}
		}
		// Standalone subtree, params define as state params.
		if (FCompactStateTreeParameters* Params = InstanceDataStorage->GetMutableTemporaryStruct(CurrentFrame, Handle).GetPtr<FCompactStateTreeParameters>())
		{
			return Params->Parameters.GetMutableValue();
		}
		break;

	case EStateTreeDataSourceType::StateParameterData:
		if (FCompactStateTreeParameters* Params = InstanceDataStorage->GetMutableTemporaryStruct(CurrentFrame, Handle).GetPtr<FCompactStateTreeParameters>())
		{
			return Params->Parameters.GetMutableValue();
		}
		break;

	default:
		return {};
	}

	return {};
}

FStateTreeDataView FStateTreeExecutionContext::AddTemporaryInstance(const FStateTreeExecutionFrame& Frame, const FStateTreeIndex16 OwnerNodeIndex, const FStateTreeDataHandle DataHandle, FConstStructView NewInstanceData)
{
	check(InstanceDataStorage);
	const FStructView NewInstance = InstanceDataStorage->AddTemporaryInstance(Owner, Frame, OwnerNodeIndex, DataHandle, NewInstanceData);
	if (FStateTreeInstanceObjectWrapper* Wrapper = NewInstance.GetPtr<FStateTreeInstanceObjectWrapper>())
	{
		return FStateTreeDataView(Wrapper->InstanceObject);
	}
	return NewInstance;
}

bool FStateTreeExecutionContext::CopyBatchOnActiveInstances(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataView TargetView, const FStateTreeIndex16 BindingsBatch)
{
	const FStateTreePropertyCopyBatch& Batch = CurrentFrame.StateTree->PropertyBindings.GetBatch(BindingsBatch);
	check(TargetView.GetStruct() == Batch.TargetStruct.Struct);

	if (Batch.PropertyFunctionsBegin != Batch.PropertyFunctionsEnd)
	{
		check(Batch.PropertyFunctionsBegin.IsValid() && Batch.PropertyFunctionsEnd.IsValid());
		EvaluatePropertyFunctionsOnActiveInstances(ParentFrame, CurrentFrame, Batch.PropertyFunctionsBegin, Batch.PropertyFunctionsEnd.Get() - Batch.PropertyFunctionsBegin.Get());
	}

	bool bSucceed = true;
	for (const FStateTreePropertyCopy& Copy : CurrentFrame.StateTree->PropertyBindings.GetBatchCopies(Batch))
	{
		const FStateTreeDataView SourceView = GetDataView(ParentFrame, CurrentFrame, Copy.SourceDataHandle);
		bSucceed &= CurrentFrame.StateTree->PropertyBindings.CopyProperty(Copy, SourceView, TargetView);
	}
	return bSucceed;
}

bool FStateTreeExecutionContext::CopyBatchWithValidation(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataView TargetView, const FStateTreeIndex16 BindingsBatch)
{
	const FStateTreePropertyCopyBatch& Batch = CurrentFrame.StateTree->PropertyBindings.GetBatch(BindingsBatch);
	check(TargetView.GetStruct() == Batch.TargetStruct.Struct);

	if (Batch.PropertyFunctionsBegin != Batch.PropertyFunctionsEnd)
	{
		check(Batch.PropertyFunctionsBegin.IsValid() && Batch.PropertyFunctionsEnd.IsValid());
		EvaluatePropertyFunctionsWithValidation(ParentFrame, CurrentFrame, Batch.PropertyFunctionsBegin, Batch.PropertyFunctionsEnd.Get() - Batch.PropertyFunctionsBegin.Get());
	}

	bool bSucceed = true;
	for (const FStateTreePropertyCopy& Copy : CurrentFrame.StateTree->PropertyBindings.GetBatchCopies(Batch))
	{
		const FStateTreeDataView SourceView = GetDataViewOrTemporary(ParentFrame, CurrentFrame, Copy.SourceDataHandle);
		if (!SourceView.IsValid())
		{
			bSucceed = false;
			break;
		}
		
		bSucceed &= CurrentFrame.StateTree->PropertyBindings.CopyProperty(Copy, SourceView, TargetView);
	}
	return bSucceed;
}


bool FStateTreeExecutionContext::CollectActiveExternalData()
{
	if (bActiveExternalDataCollected)
	{
		return true;
	}

	bool bAllExternalDataValid = true;
	FStateTreeExecutionState& Exec = GetExecState();
	const FStateTreeExecutionFrame* PrevFrame = nullptr;
	
	for (FStateTreeExecutionFrame& Frame : Exec.ActiveFrames)
	{
		if (PrevFrame && PrevFrame->StateTree == Frame.StateTree)
		{
			Frame.ExternalDataBaseIndex = PrevFrame->ExternalDataBaseIndex;
		}
		else
		{
			Frame.ExternalDataBaseIndex = CollectExternalData(Frame.StateTree);
		}

		if (!Frame.ExternalDataBaseIndex.IsValid())
		{
			bAllExternalDataValid = false;
		}
		
		PrevFrame = &Frame;
	}

	if (bAllExternalDataValid)
	{
		bActiveExternalDataCollected = true;
	}
	
	return bAllExternalDataValid;
}

FStateTreeIndex16 FStateTreeExecutionContext::CollectExternalData(const UStateTree* StateTree)
{
	if (!StateTree)
	{
		return FStateTreeIndex16::Invalid;
	}

	// If one of the active states share the same state tree, get the external data from there.
	for (const FCollectedExternalDataCache& Cache : CollectedExternalCache)
	{
		if (Cache.StateTree == StateTree)
		{
			return Cache.BaseIndex;
		}
	}
	
	const TConstArrayView<FStateTreeExternalDataDesc> ExternalDataDescs = StateTree->GetExternalDataDescs();
	const int32 BaseIndex = ContextAndExternalDataViews.Num();
	const int32 NumDescs = ExternalDataDescs.Num();
	FStateTreeIndex16 Result(BaseIndex);

	if (NumDescs > 0)
	{
		ContextAndExternalDataViews.AddDefaulted(NumDescs);
		const TArrayView<FStateTreeDataView> DataViews = MakeArrayView(ContextAndExternalDataViews.GetData() + BaseIndex, NumDescs);  

		if (ensureMsgf(CollectExternalDataDelegate.IsBound(), TEXT("The StateTree asset has external data, expecting CollectExternalData delegate to be provided.")))
		{
			if (!CollectExternalDataDelegate.Execute(*this, StateTree, StateTree->GetExternalDataDescs(), DataViews))
			{
				// The caller is responsible for error reporting. 
				return FStateTreeIndex16::Invalid;
			}
		}

		// Check that the data is valid and present.
		for (int32 Index = 0; Index < NumDescs; Index++)
		{
			const FStateTreeExternalDataDesc& DataDesc = ExternalDataDescs[Index];
			const FStateTreeDataView& DataView = ContextAndExternalDataViews[BaseIndex + Index];

			if (DataDesc.Requirement == EStateTreeExternalDataRequirement::Required)
			{
				// Required items must have valid pointer of the expected type.  
				if (!DataView.IsValid() || !DataDesc.IsCompatibleWith(DataView))
				{
					Result = FStateTreeIndex16::Invalid;
					break;
				}
			}
			else
			{
				// Optional items must have same type if they are set.
				if (DataView.IsValid() && !DataDesc.IsCompatibleWith(DataView))
				{
					Result = FStateTreeIndex16::Invalid;
					break;
				}
			}
		}
	}

	if (!Result.IsValid())
	{
		// Rollback
		ContextAndExternalDataViews.SetNum(BaseIndex);
	}

	// Cached both succeeded and failed attempts.
	CollectedExternalCache.Add({ StateTree, Result });

	return FStateTreeIndex16(Result);
}

bool FStateTreeExecutionContext::SetGlobalParameters(const FInstancedPropertyBag& Parameters)
{
	if (ensureMsgf(RootStateTree.GetDefaultParameters().GetPropertyBagStruct() == Parameters.GetPropertyBagStruct(),
		TEXT("Parameters must be of the same struct type. Make sure to migrate the provided parameters to the same type as the StateTree default parameters.")))
	{
		InstanceDataStorage->SetGlobalParameters(Parameters);
		return true;
	}

	return false;
}

void FStateTreeExecutionContext::CaptureNewStateEvents(TConstArrayView<FStateTreeExecutionFrame> PrevFrames, TConstArrayView<FStateTreeExecutionFrame> NewFrames, TArrayView<FStateTreeFrameStateSelectionEvents> FramesStateSelectionEvents)
{
	// Mark the events from delayed transitions as in use, so that each State will receive unique copy of the event struct. 
	TArray<FStateTreeSharedEvent, TInlineAllocator<16>> EventsInUse;
	for (const FStateTreeTransitionDelayedState& DelayedTransition : GetExecState().DelayedTransitions)
	{
		if (DelayedTransition.CapturedEvent.IsValid())
		{
			EventsInUse.Add(DelayedTransition.CapturedEvent);
		}
	}

	for (int32 FrameIndex = 0; FrameIndex < NewFrames.Num(); ++FrameIndex)
	{
		const FStateTreeExecutionFrame& NewFrame = NewFrames[FrameIndex];

		// Find states that are unique to the new frame.
		TConstArrayView<FStateTreeStateHandle> UniqueStates = NewFrame.ActiveStates.States;
		if (PrevFrames.IsValidIndex(FrameIndex))
		{
			const FStateTreeExecutionFrame& PrevFrame = PrevFrames[FrameIndex];

			if (PrevFrame.RootState == NewFrame.RootState
				&& PrevFrame.StateTree == NewFrame.StateTree)
			{
				for (int32 StateIndex = 0; StateIndex < NewFrame.ActiveStates.Num(); ++StateIndex)
				{
					if (!PrevFrame.ActiveStates.IsValidIndex(StateIndex) || PrevFrame.ActiveStates[StateIndex] != NewFrame.ActiveStates[StateIndex])
					{
						UniqueStates = TConstArrayView<FStateTreeStateHandle>(&NewFrame.ActiveStates[StateIndex], NewFrame.ActiveStates.Num() - StateIndex);
						break;
					}
				}
			}
		}

		// Capture events for the new states.
		for (const FStateTreeStateHandle StateHandle : UniqueStates)
		{
			if (const FCompactStateTreeState* State = NewFrame.StateTree->GetStateFromHandle(StateHandle))
			{
				if (State->EventDataIndex.IsValid())
				{
					FStateTreeSharedEvent& StateTreeEvent = InstanceDataStorage->GetMutableStruct(NewFrame.ActiveInstanceIndexBase.Get() + State->EventDataIndex.Get()).Get<FStateTreeSharedEvent>();
					
					const FStateTreeSharedEvent& EventToCapture = FramesStateSelectionEvents[FrameIndex].Events[State->Depth];
					if (EventsInUse.Contains(EventToCapture))
					{
						// Event is already spoken for, make a copy.
						StateTreeEvent = FStateTreeSharedEvent(*EventToCapture);
					}
					else
					{
						// Event not in use, steal it.
						StateTreeEvent = FramesStateSelectionEvents[FrameIndex].Events[State->Depth];
						EventsInUse.Add(EventToCapture);
					}
				}
			}
		}
	}
}

EStateTreeRunStatus FStateTreeExecutionContext::EnterState(FStateTreeTransitionResult& Transition)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_EnterState);

	if (Transition.NextActiveFrames.IsEmpty())
	{
		return EStateTreeRunStatus::Failed;
	}

	FStateTreeExecutionState& Exec = GetExecState();

	if (bRecordTransitions)
	{
		RecordedTransitions.Add(FRecordedStateTreeTransitionResult(Transition));
	}

	// Allocate new tasks.
	UpdateInstanceData(Exec.ActiveFrames, Transition.NextActiveFrames);

	CaptureNewStateEvents(Exec.ActiveFrames, Transition.NextActiveFrames, Transition.NextActiveFrameEvents);

	Exec.StateChangeCount++;
	Exec.CompletedFrameIndex = FStateTreeIndex16::Invalid;
	Exec.CompletedStateHandle = FStateTreeStateHandle::Invalid;
	Exec.EnterStateFailedFrameIndex = FStateTreeIndex16::Invalid; // This will make all tasks to be accepted.
	Exec.EnterStateFailedTaskIndex = FStateTreeIndex16::Invalid; // This will make all tasks to be accepted.
	
	// On target branch means that the state is the target of current transition or child of it.
	// States which were active before and will remain active, but are not on target branch will not get
	// EnterState called. That is, a transition is handled as "replan from this state".
	bool bOnTargetBranch = false;
	FStateTreeTransitionResult CurrentTransition = Transition;
	EStateTreeRunStatus Result = EStateTreeRunStatus::Running;

	STATETREE_LOG(Log, TEXT("Enter state '%s' (%d)"), *DebugGetStatePath(Transition.NextActiveFrames), Exec.StateChangeCount);
	STATETREE_TRACE_PHASE_BEGIN(EStateTreeUpdatePhase::EnterStates);

	// The previous active frames are needed for state enter logic.
	TArray<FStateTreeExecutionFrame, TConcurrentLinearArrayAllocator<FDefaultBlockAllocationTag>> PreviousActiveFrames;
	PreviousActiveFrames = Exec.ActiveFrames;

	// Reset the current active frames, new ones are added one by one.
	Exec.ActiveFrames.Reset();

	for (int32 FrameIndex = 0; FrameIndex < Transition.NextActiveFrames.Num() && Result != EStateTreeRunStatus::Failed; FrameIndex++)
	{
		const FStateTreeExecutionFrame& NextFrame = Transition.NextActiveFrames[FrameIndex];
		
		FStateTreeExecutionFrame* CurrentParentFrame = !Exec.ActiveFrames.IsEmpty() ? &Exec.ActiveFrames.Last() : nullptr;
		FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames.Add_GetRef(NextFrame);
		
		// We'll add new states one by one, so that active states contain only the states which have EnterState called.
		CurrentFrame.ActiveStates.Reset();

		// Get previous active states, they are used to calculate transition type.
		FStateTreeActiveStates PreviousActiveStates;
		if (PreviousActiveFrames.IsValidIndex(FrameIndex)
			&& PreviousActiveFrames[FrameIndex].IsSameFrame(NextFrame))
		{
			PreviousActiveStates = PreviousActiveFrames[FrameIndex].ActiveStates;
		}

		FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);
		const UStateTree* CurrentStateTree = NextFrame.StateTree;

		for (int32 Index = 0; Index < NextFrame.ActiveStates.Num() && Result != EStateTreeRunStatus::Failed; Index++)
		{
			const FStateTreeStateHandle CurrentHandle = NextFrame.ActiveStates[Index];
			const FStateTreeStateHandle PreviousHandle = PreviousActiveStates.GetStateSafe(Index);
			const FCompactStateTreeState& State = CurrentStateTree->States[CurrentHandle.Index];

			FCurrentlyProcessedStateScope StateScope(*this, CurrentHandle);
			
			// Add only enabled States to the list of active States
			if (State.bEnabled && !CurrentFrame.ActiveStates.Push(CurrentHandle))
			{
				STATETREE_LOG(Error, TEXT("%hs: Reached max execution depth when trying to enter state '%s'.  '%s' using StateTree '%s'."),
					__FUNCTION__, *GetStateStatusString(Exec), *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
				break;
			}
			CurrentFrame.NumCurrentlyActiveStates = static_cast<uint8>(CurrentFrame.ActiveStates.Num());

			if (State.Type == EStateTreeStateType::Linked
				|| State.Type == EStateTreeStateType::LinkedAsset)
			{
				if (State.ParameterDataHandle.IsValid()
					&& State.ParameterBindingsBatch.IsValid())
				{
					const FStateTreeDataView StateParamsDataView = GetDataView(CurrentParentFrame, CurrentFrame, State.ParameterDataHandle);
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, StateParamsDataView, State.ParameterBindingsBatch);
				}
			}

			bOnTargetBranch |= CurrentFrame.StateTree == Transition.SourceStateTree && CurrentFrame.RootState == Transition.SourceRootState && CurrentHandle == Transition.TargetState;
			const bool bWasActive = PreviousHandle == CurrentHandle;

			// Do not enter a disabled State tasks but maintain property bindings
			const bool bIsEnteringState = (!bWasActive || bOnTargetBranch) && State.bEnabled;

			CurrentTransition.CurrentState = CurrentHandle;
			CurrentTransition.ChangeType = bWasActive ? EStateTreeStateChangeType::Sustained : EStateTreeStateChangeType::Changed;

			if (bIsEnteringState)
			{
				STATETREE_TRACE_STATE_EVENT(CurrentHandle, EStateTreeTraceEventType::OnEntering);
				STATETREE_LOG(Log, TEXT("%*sState '%s' %s"), Index*UE::StateTree::DebugIndentSize, TEXT(""),
					*DebugGetStatePath(Transition.NextActiveFrames, &NextFrame, Index),
					*UEnum::GetDisplayValueAsText(CurrentTransition.ChangeType).ToString());
			}

			// Call state change events on conditions if needed.
			if (bIsEnteringState && State.bHasStateChangeConditions)
			{
				for (int32 ConditionIndex = State.EnterConditionsBegin; ConditionIndex < (State.EnterConditionsBegin + State.EnterConditionsNum); ConditionIndex++)
				{
					const FStateTreeConditionBase& Cond = CurrentFrame.StateTree->Nodes[ConditionIndex].Get<const FStateTreeConditionBase>();
					if (Cond.bHasShouldCallStateChangeEvents)
					{
						const bool bShouldCallStateChange = CurrentTransition.ChangeType == EStateTreeStateChangeType::Changed
															|| (CurrentTransition.ChangeType == EStateTreeStateChangeType::Sustained && Cond.bShouldStateChangeOnReselect);

						if (bShouldCallStateChange)
						{
							const FStateTreeDataView ConditionInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Cond.InstanceDataHandle);
							FNodeInstanceDataScope DataScope(*this, Cond.InstanceDataHandle, ConditionInstanceView);

							if (Cond.BindingsBatch.IsValid())
							{
								// Use validated copy, since we test in situations where the sources are not always valid (e.g. enter conditions may try to access inactive parent state).
								CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, ConditionInstanceView, Cond.BindingsBatch);
							}
								
							Cond.EnterState(*this, Transition);

							// Reset copied properties that might contain object references.
							if (Cond.BindingsBatch.IsValid())
							{
								CurrentFrame.StateTree->PropertyBindings.ResetObjects(Cond.BindingsBatch, ConditionInstanceView);
							}
						}
					}
				}
			}

			// Activate tasks on current state.
			for (int32 TaskIndex = State.TasksBegin; TaskIndex < (State.TasksBegin + State.TasksNum); TaskIndex++)
			{
				const FStateTreeTaskBase& Task = NextFrame.StateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
				const FStateTreeDataView TaskInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Task.InstanceDataHandle);

				FNodeInstanceDataScope DataScope(*this, Task.InstanceDataHandle, TaskInstanceView);

				// Copy bound properties.
				if (Task.BindingsBatch.IsValid())
				{
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, TaskInstanceView, Task.BindingsBatch);
				}

				// Ignore disabled task
				if (Task.bTaskEnabled == false)
				{
					STATETREE_LOG(VeryVerbose, TEXT("%*sSkipped 'EnterState' for disabled Task: '%s'"), UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
					continue;
				}

				const bool bShouldCallStateChange = CurrentTransition.ChangeType == EStateTreeStateChangeType::Changed
													|| (CurrentTransition.ChangeType == EStateTreeStateChangeType::Sustained && Task.bShouldStateChangeOnReselect);

				if (bIsEnteringState && bShouldCallStateChange)
				{
					STATETREE_LOG(Verbose, TEXT("%*s  Task '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());

					EStateTreeRunStatus Status = EStateTreeRunStatus::Unset;
					{
						QUICK_SCOPE_CYCLE_COUNTER(StateTree_Task_EnterState);
						CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Task_EnterState);
					
						Status = Task.EnterState(*this, CurrentTransition);
					}

					STATETREE_TRACE_TASK_EVENT(TaskIndex, TaskInstanceView, EStateTreeTraceEventType::OnEntered, Status);

					if (Status != EStateTreeRunStatus::Running)
					{
						// Store the first state that completed, will be used to decide where to trigger transitions.
						if (!Exec.CompletedStateHandle.IsValid())
						{
							Exec.CompletedFrameIndex = FStateTreeIndex16(FrameIndex);
							Exec.CompletedStateHandle = CurrentHandle;
						}
						Result = Status;
					}
					
					if (Status == EStateTreeRunStatus::Failed)
					{
						// Store how far in the enter state we got. This will be used to match the StateCompleted() and ExitState() calls.
						Exec.EnterStateFailedFrameIndex = FStateTreeIndex16(FrameIndex); 
						Exec.EnterStateFailedTaskIndex = FStateTreeIndex16(TaskIndex);
						break;
					}
				}
			}

			if (bIsEnteringState)
			{
				STATETREE_TRACE_STATE_EVENT(CurrentHandle, EStateTreeTraceEventType::OnEntered);
			}
		}
	}

	STATETREE_TRACE_PHASE_END(EStateTreeUpdatePhase::EnterStates);

	STATETREE_TRACE_ACTIVE_STATES_EVENT(Exec.ActiveFrames);

	return Result;
}

void FStateTreeExecutionContext::ExitState(const FStateTreeTransitionResult& Transition)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_ExitState);

	FStateTreeExecutionState& Exec = GetExecState();

	if (Exec.ActiveFrames.IsEmpty())
	{
		return;
	}

	// On target branch means that the state is the target of current transition or child of it.
	// States which were active before and will remain active, but are not on target branch will not get
	// EnterState called. That is, a transition is handled as "replan from this state".
	bool bOnTargetBranch = false;

	struct FExitStateCall
	{
		FExitStateCall() = default;
		FExitStateCall(const EStateTreeStateChangeType InChangeType, const bool bInShouldCall)
			: ChangeType(InChangeType)
			, bShouldCall(bInShouldCall)
		{
		}

		EStateTreeStateChangeType ChangeType = EStateTreeStateChangeType::None;
		bool bShouldCall = false; 
	};

	TArray<FExitStateCall, TConcurrentLinearArrayAllocator<FDefaultBlockAllocationTag>> ExitStateCalls;
	
	for (int32 FrameIndex = 0; FrameIndex < Exec.ActiveFrames.Num(); FrameIndex++)
	{
		FStateTreeExecutionFrame* CurrentParentFrame = FrameIndex > 0 ? &Exec.ActiveFrames[FrameIndex - 1] : nullptr;
		FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
		const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

		FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);

		const FStateTreeExecutionFrame* NextFrame = nullptr;
		if (Transition.NextActiveFrames.IsValidIndex(FrameIndex)
			&& Transition.NextActiveFrames[FrameIndex].IsSameFrame(CurrentFrame))
		{
			NextFrame = &Transition.NextActiveFrames[FrameIndex];
		}

		const bool bShouldCallOnEvaluatorsAndGlobalTasks = NextFrame == nullptr && CurrentFrame.bIsGlobalFrame;
		ExitStateCalls.Emplace(EStateTreeStateChangeType::Changed, bShouldCallOnEvaluatorsAndGlobalTasks);
		
		if (bShouldCallOnEvaluatorsAndGlobalTasks)
		{
			for (int32 EvalIndex = CurrentStateTree->EvaluatorsBegin; EvalIndex < (CurrentStateTree->EvaluatorsBegin + CurrentStateTree->EvaluatorsNum); EvalIndex++)
			{
				const FStateTreeEvaluatorBase& Eval = CurrentStateTree->Nodes[EvalIndex].Get<const FStateTreeEvaluatorBase>();
				const FStateTreeDataView EvalInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Eval.InstanceDataHandle);
				FNodeInstanceDataScope DataScope(*this, Eval.InstanceDataHandle, EvalInstanceView);

				if (Eval.BindingsBatch.IsValid())
				{
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, EvalInstanceView, Eval.BindingsBatch);
				}
			}

			for (int32 TaskIndex = CurrentStateTree->GlobalTasksBegin; TaskIndex < (CurrentStateTree->GlobalTasksBegin + CurrentStateTree->GlobalTasksNum); TaskIndex++)
			{
				const FStateTreeTaskBase& Task = CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
				const FStateTreeDataView TaskInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Task.InstanceDataHandle);
				FNodeInstanceDataScope DataScope(*this, Task.InstanceDataHandle, TaskInstanceView);

				if (Task.BindingsBatch.IsValid() && Task.bShouldCopyBoundPropertiesOnExitState)
				{
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, TaskInstanceView, Task.BindingsBatch);
				}
			}
		}

		for (int32 Index = 0; Index < CurrentFrame.ActiveStates.Num(); Index++)
		{
			const FStateTreeStateHandle CurrentHandle = CurrentFrame.ActiveStates[Index];
			const FStateTreeStateHandle NextHandle = NextFrame ? NextFrame->ActiveStates.GetStateSafe(Index) : FStateTreeStateHandle::Invalid;
			const FCompactStateTreeState& State = CurrentStateTree->States[CurrentHandle.Index];

			FCurrentlyProcessedStateScope StateScope(*this, CurrentHandle);

			if (State.Type == EStateTreeStateType::Linked
				|| State.Type == EStateTreeStateType::LinkedAsset)
			{
				if (State.ParameterDataHandle.IsValid()
					&& State.ParameterBindingsBatch.IsValid())
				{
					const FStateTreeDataView StateParamsDataView = GetDataView(CurrentParentFrame, CurrentFrame, State.ParameterDataHandle);
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, StateParamsDataView, State.ParameterBindingsBatch);
				}
			}

			const bool bRemainsActive = NextHandle == CurrentHandle;
			const bool bCurrentIsTarget = CurrentFrame.StateTree == Transition.SourceStateTree && CurrentFrame.RootState == Transition.SourceRootState && CurrentHandle == Transition.TargetState;
			bOnTargetBranch = bOnTargetBranch || bCurrentIsTarget;

			const EStateTreeStateChangeType ChangeType = bRemainsActive ? EStateTreeStateChangeType::Sustained : EStateTreeStateChangeType::Changed;
			
			// Should call ExitState() on this state.
			const bool bShouldCall = !bRemainsActive || bOnTargetBranch; 
			ExitStateCalls.Emplace(ChangeType, bShouldCall);

			// Do property copies, ExitState() is called below.
			for (int32 TaskIndex = State.TasksBegin; TaskIndex < (State.TasksBegin + State.TasksNum); TaskIndex++)
			{
				const FStateTreeTaskBase& Task = CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
				const FStateTreeDataView TaskInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Task.InstanceDataHandle);

				// Copy bound properties.
				if (Task.BindingsBatch.IsValid() && Task.bShouldCopyBoundPropertiesOnExitState)
				{
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, TaskInstanceView, Task.BindingsBatch);
				}
			}
		}
	}

	// Call in reverse order.
	STATETREE_LOG(Log, TEXT("Exit state '%s' (%d)"), *DebugGetStatePath(Exec.ActiveFrames), Exec.StateChangeCount);
	STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::ExitStates);

	FStateTreeTransitionResult CurrentTransition = Transition;
	int32 CallIndex = ExitStateCalls.Num() - 1;

	for (int32 FrameIndex = Exec.ActiveFrames.Num() - 1; FrameIndex >= 0; FrameIndex--)
	{
		FStateTreeExecutionFrame* CurrentParentFrame = FrameIndex > 0 ? &Exec.ActiveFrames[FrameIndex - 1] : nullptr;
		FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
		const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

		FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);

		for (int32 Index = CurrentFrame.ActiveStates.Num() - 1; Index >= 0; Index--)
		{
			const FStateTreeStateHandle CurrentHandle = CurrentFrame.ActiveStates[Index];
			const FCompactStateTreeState& State = CurrentStateTree->States[CurrentHandle.Index];

			const FExitStateCall& ExitCall = ExitStateCalls[CallIndex--];
			CurrentTransition.ChangeType = ExitCall.ChangeType;

			STATETREE_LOG(Log, TEXT("%*sState '%s' %s"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(Exec.ActiveFrames, &CurrentFrame, CurrentHandle.Index), *UEnum::GetDisplayValueAsText(CurrentTransition.ChangeType).ToString());

			STATETREE_TRACE_STATE_EVENT(CurrentHandle, EStateTreeTraceEventType::OnExiting);
				
			if (ExitCall.bShouldCall)
			{
				FCurrentlyProcessedStateScope StateScope(*this, CurrentHandle);

				// Remove any delayed transitions that belong to this state.
				Exec.DelayedTransitions.RemoveAllSwap(
					[StateTree = CurrentFrame.StateTree, Begin = State.TransitionsBegin, End = State.TransitionsBegin + State.TransitionsNum](const FStateTreeTransitionDelayedState& DelayedState)
					{
						return  DelayedState.StateTree == StateTree && DelayedState.TransitionIndex.Get() >= Begin && DelayedState.TransitionIndex.Get() < End;
					});

				CurrentTransition.CurrentState = CurrentHandle;

				// Do property copies, ExitState() is called below.
				for (int32 TaskIndex = (State.TasksBegin + State.TasksNum) - 1; TaskIndex >= State.TasksBegin; TaskIndex--)
				{

					// Call task completed only if EnterState() was called.
					// The task order in the tree (BF) allows us to use the comparison.
					// Relying here that invalid value of Exec.EnterStateFailedTaskIndex == MAX_uint16.
					if (TaskIndex <= Exec.EnterStateFailedTaskIndex.Get())
					{
						const FStateTreeTaskBase& Task = CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
						const FStateTreeDataView TaskInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Task.InstanceDataHandle);

						FNodeInstanceDataScope DataScope(*this, Task.InstanceDataHandle, TaskInstanceView);

						// Ignore disabled task
						if (Task.bTaskEnabled == false)
						{
							STATETREE_LOG(VeryVerbose, TEXT("%*sSkipped 'ExitState' for disabled Task: '%s'"), UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
							continue;
						}

						const bool bShouldCallStateChange = CurrentTransition.ChangeType == EStateTreeStateChangeType::Changed
									|| (CurrentTransition.ChangeType == EStateTreeStateChangeType::Sustained && Task.bShouldStateChangeOnReselect);

						if (bShouldCallStateChange)
						{

							STATETREE_LOG(Verbose, TEXT("%*s  Task '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
							{
								QUICK_SCOPE_CYCLE_COUNTER(StateTree_Task_ExitState);
								CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Task_ExitState);
								Task.ExitState(*this, CurrentTransition);
							}
							STATETREE_TRACE_TASK_EVENT(TaskIndex, TaskInstanceView, EStateTreeTraceEventType::OnExited, Transition.CurrentRunStatus);
						}
					}
				}

				// Call state change events on conditions if needed.
				if (State.bHasStateChangeConditions)
				{
					for (int32 ConditionIndex = (State.EnterConditionsBegin + State.EnterConditionsNum) - 1; ConditionIndex >= State.EnterConditionsBegin; ConditionIndex--)
					{
						const FStateTreeConditionBase& Cond = CurrentFrame.StateTree->Nodes[ConditionIndex].Get<const FStateTreeConditionBase>();
						if (Cond.bHasShouldCallStateChangeEvents)
						{
							const bool bShouldCallStateChange = CurrentTransition.ChangeType == EStateTreeStateChangeType::Changed
										|| (CurrentTransition.ChangeType == EStateTreeStateChangeType::Sustained && Cond.bShouldStateChangeOnReselect);

							if (bShouldCallStateChange)
							{
								const FStateTreeDataView ConditionInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Cond.InstanceDataHandle);
								FNodeInstanceDataScope DataScope(*this, Cond.InstanceDataHandle, ConditionInstanceView);

								if (Cond.BindingsBatch.IsValid())
								{
									// Use validated copy, since we test in situations where the sources are not always valid (e.g. enter conditions may try to access inactive parent state).
									CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, ConditionInstanceView, Cond.BindingsBatch);
								}
								
								Cond.ExitState(*this, Transition);

								// Reset copied properties that might contain object references.
								if (Cond.BindingsBatch.IsValid())
								{
									CurrentFrame.StateTree->PropertyBindings.ResetObjects(Cond.BindingsBatch, ConditionInstanceView);
								}
							}
						}
					}
				}
			}

			STATETREE_TRACE_STATE_EVENT(CurrentHandle, EStateTreeTraceEventType::OnExited);
		}

		// Frame exit call
		{
			const FExitStateCall& ExitCall = ExitStateCalls[CallIndex--];
			if (ExitCall.bShouldCall)
			{
				CurrentTransition.ChangeType = ExitCall.ChangeType;
				CallStopOnEvaluatorsAndGlobalTasks(CurrentParentFrame, CurrentFrame, CurrentTransition);
			}
		}
	}

}

void FStateTreeExecutionContext::StateCompleted()
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_StateCompleted);

	const FStateTreeExecutionState& Exec = GetExecState();

	if (Exec.ActiveFrames.IsEmpty())
	{
		return;
	}

	STATETREE_LOG(Verbose, TEXT("State Completed %s (%d)"), *UEnum::GetDisplayValueAsText(Exec.LastTickStatus).ToString(), Exec.StateChangeCount);
	STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::StateCompleted);

	// Call from child towards root to allow to pass results back.
	// Note: Completed is assumed to be called immediately after tick or enter state, so there's no property copying.

	for (int32 FrameIndex = Exec.ActiveFrames.Num() - 1; FrameIndex >= 0; FrameIndex--)
	{
		const FStateTreeExecutionFrame* CurrentParentFrame = FrameIndex > 0 ? &Exec.ActiveFrames[FrameIndex - 1] : nullptr;
		const FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
		const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

		FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);

		if (FrameIndex <= Exec.EnterStateFailedFrameIndex.Get())
		{
			for (int32 Index = CurrentFrame.ActiveStates.Num() - 1; Index >= 0; Index--)
			{
				const FStateTreeStateHandle CurrentHandle = CurrentFrame.ActiveStates[Index];
				const FCompactStateTreeState& State = CurrentStateTree->States[CurrentHandle.Index];

				FCurrentlyProcessedStateScope StateScope(*this, CurrentHandle);
				
				STATETREE_LOG(Verbose, TEXT("%*sState '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(Exec.ActiveFrames, &CurrentFrame, Index));
				STATETREE_TRACE_STATE_EVENT(CurrentHandle, EStateTreeTraceEventType::OnStateCompleted);

				// Notify Tasks
				for (int32 TaskIndex = (State.TasksBegin + State.TasksNum) - 1; TaskIndex >= State.TasksBegin; TaskIndex--)
				{
					// Call task completed only if EnterState() was called.
					// The task order in the tree (BF) allows us to use the comparison.
					// Relying here that invalid value of Exec.EnterStateFailedTaskIndex == MAX_uint16.
					if (TaskIndex <= Exec.EnterStateFailedTaskIndex.Get())
					{
						const FStateTreeTaskBase& Task = CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
						const FStateTreeDataView TaskInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Task.InstanceDataHandle);
						FNodeInstanceDataScope DataScope(*this, Task.InstanceDataHandle, TaskInstanceView);

						// Ignore disabled task
						if (Task.bTaskEnabled == false)
						{
							STATETREE_LOG(VeryVerbose, TEXT("%*sSkipped 'StateCompleted' for disabled Task: '%s'"), UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
							continue;
						}

						STATETREE_LOG(Verbose, TEXT("%*s  Task '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
						Task.StateCompleted(*this, Exec.LastTickStatus, CurrentFrame.ActiveStates);
					}
				}
				
				// Call state change events on conditions if needed.
				if (State.bHasStateChangeConditions)
				{
					for (int32 ConditionIndex = (State.EnterConditionsBegin + State.EnterConditionsNum) - 1; ConditionIndex >= State.EnterConditionsBegin; ConditionIndex--)
					{
						const FStateTreeConditionBase& Cond = CurrentFrame.StateTree->Nodes[ConditionIndex].Get<const FStateTreeConditionBase>();
						if (Cond.bHasShouldCallStateChangeEvents)
						{
							const FStateTreeDataView ConditionInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Cond.InstanceDataHandle);
							FNodeInstanceDataScope DataScope(*this, Cond.InstanceDataHandle, ConditionInstanceView);

							if (Cond.BindingsBatch.IsValid())
							{
								// Use validated copy, since we test in situations where the sources are not always valid (e.g. enter conditions may try to access inactive parent state).
								CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, ConditionInstanceView, Cond.BindingsBatch);
							}
							
							Cond.StateCompleted(*this, Exec.LastTickStatus, CurrentFrame.ActiveStates);

							// Reset copied properties that might contain object references.
							if (Cond.BindingsBatch.IsValid())
							{
								CurrentFrame.StateTree->PropertyBindings.ResetObjects(Cond.BindingsBatch, ConditionInstanceView);
							}
						}
					}
				}
			}
		}
	}
}

EStateTreeRunStatus FStateTreeExecutionContext::TickEvaluatorsAndGlobalTasks(const float DeltaTime, const bool bTickGlobalTasks)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_TickEvaluators);
	STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::TickingGlobalTasks);

	STATETREE_LOG(VeryVerbose, TEXT("Ticking Evaluators & Global Tasks"));

	FStateTreeExecutionState& Exec = GetExecState();

	EStateTreeRunStatus Result = EStateTreeRunStatus::Running;

	for (int32 FrameIndex = 0; FrameIndex < Exec.ActiveFrames.Num(); FrameIndex++)
	{
		FStateTreeExecutionFrame* CurrentParentFrame = FrameIndex > 0 ? &Exec.ActiveFrames[FrameIndex - 1] : nullptr;
		FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
		if (CurrentFrame.bIsGlobalFrame)
		{
			FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);

			const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

			// Tick evaluators
			for (int32 EvalIndex = CurrentStateTree->EvaluatorsBegin; EvalIndex < (CurrentStateTree->EvaluatorsBegin + CurrentStateTree->EvaluatorsNum); EvalIndex++)
			{
				const FStateTreeEvaluatorBase& Eval = CurrentStateTree->Nodes[EvalIndex].Get<const FStateTreeEvaluatorBase>();
				const FStateTreeDataView EvalInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Eval.InstanceDataHandle);
				FNodeInstanceDataScope DataScope(*this, Eval.InstanceDataHandle, EvalInstanceView);

				// Copy bound properties.
				if (Eval.BindingsBatch.IsValid())
				{
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, EvalInstanceView, Eval.BindingsBatch);
				}
				STATETREE_LOG(VeryVerbose, TEXT("  Tick: '%s'"), *Eval.Name.ToString());
				{
					QUICK_SCOPE_CYCLE_COUNTER(StateTree_Eval_Tick);
					Eval.Tick(*this, DeltaTime);

					STATETREE_TRACE_EVALUATOR_EVENT(EvalIndex, EvalInstanceView, EStateTreeTraceEventType::OnTicked);
				}
			}

			if (bTickGlobalTasks)
			{
				// Used to stop ticking tasks after one fails, but we still want to keep updating the data views so that property binding works properly.
				bool bShouldTickTasks = true;
				const bool bHasEvents = EventQueue && EventQueue->HasEvents();

				for (int32 TaskIndex = CurrentStateTree->GlobalTasksBegin; TaskIndex < (CurrentStateTree->GlobalTasksBegin + CurrentStateTree->GlobalTasksNum); TaskIndex++)
				{
					const FStateTreeTaskBase& Task = CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
					const FStateTreeDataView TaskInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Task.InstanceDataHandle);
					FNodeInstanceDataScope DataScope(*this, Task.InstanceDataHandle, TaskInstanceView);

					// Ignore disabled task
					if (Task.bTaskEnabled == false)
					{
						STATETREE_LOG(VeryVerbose, TEXT("%*sSkipped 'Tick' for disabled Task: '%s'"), UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
						continue;
					}

					const bool bNeedsTick = bShouldTickTasks && (Task.bShouldCallTick || (bHasEvents && Task.bShouldCallTickOnlyOnEvents));
					STATETREE_LOG(VeryVerbose, TEXT("  Tick: '%s' %s"), *Task.Name.ToString(), !bNeedsTick ? TEXT("[not ticked]") : TEXT(""));
					if (!bNeedsTick)
					{
						continue;
					}

					// Copy bound properties.
					// Only copy properties when the task is actually ticked, and copy properties at tick is requested.
					if (Task.BindingsBatch.IsValid() && Task.bShouldCopyBoundPropertiesOnTick)
					{
						CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, TaskInstanceView, Task.BindingsBatch);
					}

					//STATETREE_TRACE_TASK_EVENT(TaskIndex, TaskDataView, EStateTreeTraceEventType::OnTickingTask, EStateTreeRunStatus::Running);
					EStateTreeRunStatus TaskResult = EStateTreeRunStatus::Unset;
					{
						QUICK_SCOPE_CYCLE_COUNTER(StateTree_Task_Tick);
						CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Task_Tick);

						TaskResult = Task.Tick(*this, DeltaTime);
					}

					STATETREE_TRACE_TASK_EVENT(TaskIndex, TaskInstanceView,
						TaskResult != EStateTreeRunStatus::Running ? EStateTreeTraceEventType::OnTaskCompleted : EStateTreeTraceEventType::OnTicked,
						TaskResult);

					// If a global task succeeds or fails, it will stop the whole tree.
					if (TaskResult != EStateTreeRunStatus::Running)
					{
						Result = TaskResult;
					}
						
					if (TaskResult == EStateTreeRunStatus::Failed)
					{
						bShouldTickTasks = false;
					}
				}
			}
		}
	}

	return Result;
}

EStateTreeRunStatus FStateTreeExecutionContext::StartEvaluatorsAndGlobalTasks(FStateTreeIndex16& OutLastInitializedTaskIndex)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_StartEvaluators);
	STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::StartGlobalTasks);

	STATETREE_LOG(Verbose, TEXT("Start Evaluators & Global tasks"));

	FStateTreeExecutionState& Exec = GetExecState();

	OutLastInitializedTaskIndex = FStateTreeIndex16();
	EStateTreeRunStatus Result = EStateTreeRunStatus::Running;

	for (int32 FrameIndex = 0; FrameIndex < Exec.ActiveFrames.Num(); FrameIndex++)
	{
		FStateTreeExecutionFrame* CurrentParentFrame = FrameIndex > 0 ? &Exec.ActiveFrames[FrameIndex - 1] : nullptr;
		FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
		if (CurrentFrame.bIsGlobalFrame)
		{
			FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);
			
			const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

			// Start evaluators
			for (int32 EvalIndex = CurrentStateTree->EvaluatorsBegin; EvalIndex < (CurrentStateTree->EvaluatorsBegin + CurrentStateTree->EvaluatorsNum); EvalIndex++)
			{
				const FStateTreeEvaluatorBase& Eval = CurrentStateTree->Nodes[EvalIndex].Get<const FStateTreeEvaluatorBase>();
				const FStateTreeDataView EvalInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Eval.InstanceDataHandle);
				FNodeInstanceDataScope DataScope(*this, Eval.InstanceDataHandle, EvalInstanceView);

				// Copy bound properties.
				if (Eval.BindingsBatch.IsValid())
				{
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, EvalInstanceView, Eval.BindingsBatch);
				}
				STATETREE_LOG(Verbose, TEXT("  Start: '%s'"), *Eval.Name.ToString());
				{
					QUICK_SCOPE_CYCLE_COUNTER(StateTree_Eval_TreeStart);
					Eval.TreeStart(*this);

					STATETREE_TRACE_EVALUATOR_EVENT(EvalIndex, EvalInstanceView, EStateTreeTraceEventType::OnTreeStarted);
				}
			}

			// Start Global tasks
			// Even if we call Enter/ExitState() on global tasks, they do not enter any specific state.
			const FStateTreeTransitionResult Transition = {}; // Empty transition
		
			for (int32 TaskIndex = CurrentStateTree->GlobalTasksBegin; TaskIndex < (CurrentStateTree->GlobalTasksBegin + CurrentStateTree->GlobalTasksNum); TaskIndex++)
			{
				const FStateTreeTaskBase& Task =  CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
				const FStateTreeDataView TaskInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Task.InstanceDataHandle);
				FNodeInstanceDataScope DataScope(*this, Task.InstanceDataHandle, TaskInstanceView);

				// Copy bound properties.
				if (Task.BindingsBatch.IsValid())
				{
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, TaskInstanceView, Task.BindingsBatch);
				}

				// Ignore disabled task
				if (Task.bTaskEnabled == false)
				{
					STATETREE_LOG(VeryVerbose, TEXT("%*sSkipped 'EnterState' for disabled Task: '%s'"), UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
					continue;
				}

				STATETREE_LOG(Verbose, TEXT("  Start: '%s'"), *Task.Name.ToString());
				{
					QUICK_SCOPE_CYCLE_COUNTER(StateTree_Task_TreeStart);
					const EStateTreeRunStatus TaskStatus = Task.EnterState(*this, Transition); 

					STATETREE_TRACE_TASK_EVENT(TaskIndex, TaskInstanceView, EStateTreeTraceEventType::OnEntered, TaskStatus);

					if (TaskStatus != EStateTreeRunStatus::Running)
					{
						OutLastInitializedTaskIndex = FStateTreeIndex16(TaskIndex);
						Result = TaskStatus;
						break;
					}
				}
			}
		}
	}

	return Result;
}

void FStateTreeExecutionContext::StopEvaluatorsAndGlobalTasks(const EStateTreeRunStatus CompletionStatus, const FStateTreeIndex16 LastInitializedTaskIndex)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_StopEvaluators);
	STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::StopGlobalTasks);

	STATETREE_LOG(Verbose, TEXT("Stop Evaluators & Global Tasks"));

	FStateTreeExecutionState& Exec = GetExecState();

	// Update bindings
	for (int32 FrameIndex = 0; FrameIndex < Exec.ActiveFrames.Num(); FrameIndex++)
	{
		FStateTreeExecutionFrame* CurrentParentFrame = FrameIndex > 0 ? &Exec.ActiveFrames[FrameIndex - 1] : nullptr;
		FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
		if (CurrentFrame.bIsGlobalFrame)
		{
			FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);
			
			const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

			for (int32 EvalIndex = CurrentStateTree->EvaluatorsBegin; EvalIndex < (CurrentStateTree->EvaluatorsBegin + CurrentStateTree->EvaluatorsNum); EvalIndex++)
			{
				const FStateTreeEvaluatorBase& Eval = CurrentStateTree->Nodes[EvalIndex].Get<const FStateTreeEvaluatorBase>();
				const FStateTreeDataView EvalInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Eval.InstanceDataHandle);
				FNodeInstanceDataScope DataScope(*this, Eval.InstanceDataHandle, EvalInstanceView);

				// Copy bound properties.
				if (Eval.BindingsBatch.IsValid())
				{
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, EvalInstanceView, Eval.BindingsBatch);
				}
			}

			for (int32 TaskIndex = CurrentStateTree->GlobalTasksBegin; TaskIndex < (CurrentStateTree->GlobalTasksBegin + CurrentStateTree->GlobalTasksNum); TaskIndex++)
			{
				const FStateTreeTaskBase& Task = CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
				const FStateTreeDataView TaskInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Task.InstanceDataHandle);
				FNodeInstanceDataScope DataScope(*this, Task.InstanceDataHandle, TaskInstanceView);

				// Copy bound properties.
				if (Task.BindingsBatch.IsValid() && Task.bShouldCopyBoundPropertiesOnExitState)
				{
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, TaskInstanceView, Task.BindingsBatch);
				}
			}
		}
	}

	// Call in reverse order.
	FStateTreeTransitionResult Transition;
	Transition.TargetState = FStateTreeStateHandle::FromCompletionStatus(CompletionStatus);
	Transition.CurrentRunStatus = CompletionStatus;

	bool bIsLastGlobalFrame = true;
	for (int32 FrameIndex = Exec.ActiveFrames.Num() - 1; FrameIndex >= 0; FrameIndex--)
	{
		const FStateTreeExecutionFrame* CurrentParentFrame = FrameIndex > 0 ? &Exec.ActiveFrames[FrameIndex - 1] : nullptr;
		const FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
		if (CurrentFrame.bIsGlobalFrame)
		{
			// LastInitializedTaskIndex belongs to the last frame.
			const FStateTreeIndex16 LastTaskToBeStopped = bIsLastGlobalFrame ? LastInitializedTaskIndex : FStateTreeIndex16::Invalid;
			CallStopOnEvaluatorsAndGlobalTasks(CurrentParentFrame, CurrentFrame, Transition, LastTaskToBeStopped);
			bIsLastGlobalFrame = false;
		}
	}
}

void FStateTreeExecutionContext::CallStopOnEvaluatorsAndGlobalTasks(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& Frame, const FStateTreeTransitionResult& Transition, const FStateTreeIndex16 LastInitializedTaskIndex /*= FStateTreeIndex16()*/)
{
	check(Frame.bIsGlobalFrame);

	FCurrentlyProcessedFrameScope FrameScope(*this, ParentFrame, Frame);
	const UStateTree* CurrentStateTree = Frame.StateTree;

	for (int32 TaskIndex = (CurrentStateTree->GlobalTasksBegin + CurrentStateTree->GlobalTasksNum) - 1;  TaskIndex >= CurrentStateTree->GlobalTasksBegin; TaskIndex--)
	{
		const FStateTreeTaskBase& Task =  CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
		const FStateTreeDataView TaskInstanceView = GetDataView(ParentFrame, Frame, Task.InstanceDataHandle);
		FNodeInstanceDataScope DataScope(*this, Task.InstanceDataHandle, TaskInstanceView);

		// Ignore disabled task
		if (Task.bTaskEnabled == false)
		{
			STATETREE_LOG(VeryVerbose, TEXT("%*sSkipped 'ExitState' for disabled Task: '%s'"), UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
			continue;
		}

		// Relying here that invalid value of LastInitializedTaskIndex == MAX_uint16.
		if (TaskIndex <= LastInitializedTaskIndex.Get())
		{
			STATETREE_LOG(Verbose, TEXT("  Stop: '%s'"), *Task.Name.ToString());
			{
				QUICK_SCOPE_CYCLE_COUNTER(StateTree_Task_TreeStop);
				Task.ExitState(*this, Transition);
			}
			STATETREE_TRACE_TASK_EVENT(TaskIndex, TaskInstanceView, EStateTreeTraceEventType::OnExited, Transition.CurrentRunStatus);
		}
	}

	for (int32 EvalIndex = (CurrentStateTree->EvaluatorsBegin + CurrentStateTree->EvaluatorsNum) - 1; EvalIndex >= CurrentStateTree->EvaluatorsBegin; EvalIndex--)
	{
		const FStateTreeEvaluatorBase& Eval = CurrentStateTree->Nodes[EvalIndex].Get<const FStateTreeEvaluatorBase>();
		const FStateTreeDataView EvalInstanceView = GetDataView(ParentFrame, Frame, Eval.InstanceDataHandle);
		FNodeInstanceDataScope DataScope(*this, Eval.InstanceDataHandle, EvalInstanceView);

		STATETREE_LOG(Verbose, TEXT("  Stop: '%s'"), *Eval.Name.ToString());
		{
			QUICK_SCOPE_CYCLE_COUNTER(StateTree_Eval_TreeStop);
			Eval.TreeStop(*this);

			STATETREE_TRACE_EVALUATOR_EVENT(EvalIndex, EvalInstanceView, EStateTreeTraceEventType::OnTreeStopped);
		}
	}
}

EStateTreeRunStatus FStateTreeExecutionContext::StartTemporaryEvaluatorsAndGlobalTasks(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame)
{
	if (!CurrentFrame.bIsGlobalFrame)
	{
		return EStateTreeRunStatus::Failed;
	}
	
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_StartEvaluators);
	// @todo: figure out debugger phase for temporary start.
	//	STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::StartGlobalTasks);

	STATETREE_LOG(Verbose, TEXT("Start Temporary Evaluators & Global tasks while trying to select linked asset: %s"), *GetNameSafe(CurrentFrame.StateTree));

	FStateTreeExecutionState& Exec = GetExecState();
	EStateTreeRunStatus Result = EStateTreeRunStatus::Running;
	FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);
	const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

	// Start evaluators
	for (int32 EvalIndex = CurrentStateTree->EvaluatorsBegin; EvalIndex < (CurrentStateTree->EvaluatorsBegin + CurrentStateTree->EvaluatorsNum); EvalIndex++)
	{
		const FStateTreeEvaluatorBase& Eval = CurrentStateTree->Nodes[EvalIndex].Get<const FStateTreeEvaluatorBase>();
		FStateTreeDataView EvalInstanceView = GetDataViewOrTemporary(CurrentParentFrame, CurrentFrame, Eval.InstanceDataHandle);
		bool bWasCreated = false;
		if (!EvalInstanceView.IsValid())
		{
			EvalInstanceView = AddTemporaryInstance(CurrentFrame, FStateTreeIndex16(EvalIndex), Eval.InstanceDataHandle, CurrentFrame.StateTree->DefaultInstanceData.GetStruct(Eval.InstanceTemplateIndex.Get()));
			check(EvalInstanceView.IsValid());
			bWasCreated = true;
		}
		
		FNodeInstanceDataScope DataScope(*this, Eval.InstanceDataHandle, EvalInstanceView);
		// Copy bound properties.
		if (Eval.BindingsBatch.IsValid())
		{
			CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, EvalInstanceView, Eval.BindingsBatch);
		}

		if (bWasCreated)
		{
			STATETREE_LOG(Verbose, TEXT("  Start: '%s'"), *Eval.Name.ToString());
			{
				QUICK_SCOPE_CYCLE_COUNTER(StateTree_Eval_TreeStart);
				Eval.TreeStart(*this);

				STATETREE_TRACE_EVALUATOR_EVENT(EvalIndex, EvalInstanceView, EStateTreeTraceEventType::OnTreeStarted);
			}
		}
	}

	// Start Global tasks
	// Even if we call Enter/ExitState() on global tasks, they do not enter any specific state.
	const FStateTreeTransitionResult Transition = {}; // Empty transition

	for (int32 TaskIndex = CurrentStateTree->GlobalTasksBegin; TaskIndex < (CurrentStateTree->GlobalTasksBegin + CurrentStateTree->GlobalTasksNum); TaskIndex++)
	{
		const FStateTreeTaskBase& Task =  CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
		// Ignore disabled task
		if (Task.bTaskEnabled == false)
		{
			STATETREE_LOG(VeryVerbose, TEXT("%*sSkipped 'EnterState' for disabled Task: '%s'"), UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
			continue;
		}

		FStateTreeDataView TaskDataView = GetDataViewOrTemporary(CurrentParentFrame, CurrentFrame, Task.InstanceDataHandle);
		bool bWasCreated = false;
		if (!TaskDataView.IsValid())
		{
			TaskDataView = AddTemporaryInstance(CurrentFrame, FStateTreeIndex16(TaskIndex), Task.InstanceDataHandle, CurrentFrame.StateTree->DefaultInstanceData.GetStruct(Task.InstanceTemplateIndex.Get()));
			check(TaskDataView.IsValid())
			bWasCreated = true;
		}

		FNodeInstanceDataScope DataScope(*this, Task.InstanceDataHandle, TaskDataView);

		// Copy bound properties.
		if (Task.BindingsBatch.IsValid())
		{
			CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, TaskDataView, Task.BindingsBatch);
		}

		STATETREE_LOG(Verbose, TEXT("  Start: '%s'"), *Task.Name.ToString());
		if (bWasCreated)
		{
			QUICK_SCOPE_CYCLE_COUNTER(StateTree_Task_TreeStart);
			const EStateTreeRunStatus TaskStatus = Task.EnterState(*this, Transition);

			STATETREE_TRACE_TASK_EVENT(TaskIndex, TaskDataView, EStateTreeTraceEventType::OnEntered, TaskStatus);

			if (TaskStatus != EStateTreeRunStatus::Running)
			{
				Result = TaskStatus;
				break;
			}
		}
	}

	return Result;
}

void FStateTreeExecutionContext::StopTemporaryEvaluatorsAndGlobalTasks(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame)
{
	// @todo: figure out debugger phase for temporary stop.
	STATETREE_LOG(Verbose, TEXT("Stop Temporary Evaluators & Global tasks"));

	// Create temporary transition to stop the unused global tasks and evaluators.
	constexpr  EStateTreeRunStatus CompletionStatus = EStateTreeRunStatus::Stopped; 
	FStateTreeTransitionResult Transition;
	Transition.TargetState = FStateTreeStateHandle::FromCompletionStatus(CompletionStatus);
	Transition.CurrentRunStatus = CompletionStatus;

	TArrayView<FStateTreeTemporaryInstanceData> TempInstances = InstanceDataStorage->GetMutableTemporaryInstances();
	for (int32 Index = TempInstances.Num() - 1; Index >= 0; Index--)
	{
		FStateTreeTemporaryInstanceData& TempInstance = TempInstances[Index];
		if (TempInstance.StateTree != CurrentFrame.StateTree || TempInstance.RootState != CurrentFrame.RootState)
		{
			continue;
		}

		if (TempInstance.OwnerNodeIndex.IsValid()
			&& TempInstance.Instance.IsValid())
		{
			FStateTreeDataView NodeInstanceView;
			if (FStateTreeInstanceObjectWrapper* Wrapper = TempInstance.Instance.GetMutablePtr<FStateTreeInstanceObjectWrapper>())
			{
				NodeInstanceView = FStateTreeDataView(Wrapper->InstanceObject);
			}
			else
			{
				NodeInstanceView = FStateTreeDataView(TempInstance.Instance);
			}
			
			FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);
			FNodeInstanceDataScope DataScope(*this, TempInstance.DataHandle, NodeInstanceView);

			FConstStructView NodeView = CurrentFrame.StateTree->Nodes[TempInstance.OwnerNodeIndex.Get()];
			if (const FStateTreeTaskBase* Task = NodeView.GetPtr<const FStateTreeTaskBase>())
			{
				STATETREE_LOG(Verbose, TEXT("  Stop: '%s'"), *Task->Name.ToString());
				{
					QUICK_SCOPE_CYCLE_COUNTER(StateTree_Task_TreeStop);
					Task->ExitState(*this, Transition);
				}
				STATETREE_TRACE_TASK_EVENT(TempInstance.OwnerNodeIndex.Get(), NodeInstanceView, EStateTreeTraceEventType::OnExited, Transition.CurrentRunStatus);
			}
			else if (const FStateTreeEvaluatorBase* Eval = NodeView.GetPtr<const FStateTreeEvaluatorBase>())
			{
				STATETREE_LOG(Verbose, TEXT("  Stop: '%s'"), *Eval->Name.ToString());
				{
					QUICK_SCOPE_CYCLE_COUNTER(StateTree_Eval_TreeStop);
					Eval->TreeStop(*this);

					STATETREE_TRACE_EVALUATOR_EVENT(TempInstance.OwnerNodeIndex.Get(), NodeInstanceView, EStateTreeTraceEventType::OnTreeStopped);
				}
			}
		}
	}
}

EStateTreeRunStatus FStateTreeExecutionContext::TickTasks(const float DeltaTime)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_TickTasks);
	STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::TickingTasks);

	FStateTreeExecutionState& Exec = GetExecState();

	if (Exec.ActiveFrames.IsEmpty())
	{
		return EStateTreeRunStatus::Failed;
	}

	EStateTreeRunStatus Result = EStateTreeRunStatus::Running;
	int32 NumTotalTasks = 0;

	const bool bHasEvents = EventQueue && EventQueue->HasEvents();

	Exec.CompletedFrameIndex = FStateTreeIndex16::Invalid;
	Exec.CompletedStateHandle = FStateTreeStateHandle::Invalid;
	
	// Used to stop ticking tasks after one fails, but we still want to keep updating the data views so that property binding works properly.
	bool bShouldTickTasks = true;

	STATETREE_CLOG(Exec.ActiveFrames.Num() > 0, VeryVerbose, TEXT("Ticking Tasks"));

	for (int32 FrameIndex = 0; FrameIndex < Exec.ActiveFrames.Num(); FrameIndex++)
	{
		const FStateTreeExecutionFrame* CurrentParentFrame = FrameIndex > 0 ? &Exec.ActiveFrames[FrameIndex - 1] : nullptr;
		const FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
		const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

		FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);

		for (int32 Index = 0; Index < CurrentFrame.ActiveStates.Num(); Index++)
		{
			const FStateTreeStateHandle CurrentHandle = CurrentFrame.ActiveStates[Index];
			const FCompactStateTreeState& State = CurrentStateTree->States[CurrentHandle.Index];

			FCurrentlyProcessedStateScope StateScope(*this, CurrentHandle);
			STATETREE_TRACE_SCOPED_STATE(CurrentHandle);

			STATETREE_CLOG(State.TasksNum > 0, VeryVerbose, TEXT("%*sState '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(Exec.ActiveFrames, &CurrentFrame, Index));

			if (State.Type == EStateTreeStateType::Linked
				|| State.Type == EStateTreeStateType::LinkedAsset)
			{
				if (State.ParameterDataHandle.IsValid()
					&& State.ParameterBindingsBatch.IsValid())
				{
					const FStateTreeDataView StateParamsDataView = GetDataView(CurrentParentFrame, CurrentFrame, State.ParameterDataHandle);
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, StateParamsDataView, State.ParameterBindingsBatch);
				}
			}

			// Update Tasks data and tick if possible (ie. if no task has yet failed and so bShouldTickTasks is true)
			for (int32 TaskIndex = State.TasksBegin; TaskIndex < (State.TasksBegin + State.TasksNum); TaskIndex++)
			{
				const FStateTreeTaskBase& Task = CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
				const FStateTreeDataView TaskInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Task.InstanceDataHandle);
				FNodeInstanceDataScope DataScope(*this, Task.InstanceDataHandle, TaskInstanceView);

				// Ignore disabled task
				if (Task.bTaskEnabled == false)
				{
					STATETREE_LOG(VeryVerbose, TEXT("%*sSkipped 'Tick' for disabled Task: '%s'"), UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
					continue;
				}

				const bool bNeedsTick = bShouldTickTasks && (Task.bShouldCallTick || (bHasEvents && Task.bShouldCallTickOnlyOnEvents));
				STATETREE_LOG(VeryVerbose, TEXT("%*s  Tick: '%s' %s"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString(), !bNeedsTick ? TEXT("[not ticked]") : TEXT(""));
				if (!bNeedsTick)
				{
					continue;
				}
				
				// Copy bound properties.
				// Only copy properties when the task is actually ticked, and copy properties at tick is requested.
				if (Task.BindingsBatch.IsValid() && Task.bShouldCopyBoundPropertiesOnTick)
				{
					CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, TaskInstanceView, Task.BindingsBatch);
				}

				//STATETREE_TRACE_TASK_EVENT(TaskIndex, TaskDataView, EStateTreeTraceEventType::OnTickingTask, EStateTreeRunStatus::Running);
				EStateTreeRunStatus TaskResult = EStateTreeRunStatus::Unset;
				{
					QUICK_SCOPE_CYCLE_COUNTER(StateTree_Task_Tick);
					CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Task_Tick);

					TaskResult = Task.Tick(*this, DeltaTime);
				}

				STATETREE_TRACE_TASK_EVENT(TaskIndex, TaskInstanceView,
					TaskResult != EStateTreeRunStatus::Running ? EStateTreeTraceEventType::OnTaskCompleted : EStateTreeTraceEventType::OnTicked,
					TaskResult);
				
				// TODO: Add more control over which states can control the failed/succeeded result.
				if (TaskResult != EStateTreeRunStatus::Running)
				{
					// Store the first state that completed, will be used to decide where to trigger transitions.
					if (!Exec.CompletedStateHandle.IsValid())
					{
						Exec.CompletedFrameIndex = FStateTreeIndex16(FrameIndex);
						Exec.CompletedStateHandle = CurrentHandle;
					}
					Result = TaskResult;
				}
				
				if (TaskResult == EStateTreeRunStatus::Failed)
				{
					bShouldTickTasks = false;
				}
			}
			NumTotalTasks += State.TasksNum;
		}
	}

	if (NumTotalTasks == 0)
	{
		// No tasks, done ticking.
		Result = EStateTreeRunStatus::Succeeded;
		Exec.CompletedFrameIndex = FStateTreeIndex16(0);
		Exec.CompletedStateHandle = Exec.ActiveFrames.Num() > 0 ? Exec.ActiveFrames[0].ActiveStates.Last() : FStateTreeStateHandle::Invalid;
	}

	return Result;
}

bool FStateTreeExecutionContext::TestAllConditions(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const int32 ConditionsOffset, const int32 ConditionsNum)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_TestConditions);

	if (ConditionsNum == 0)
	{
		return true;
	}

	TStaticArray<EStateTreeExpressionOperand, UE::StateTree::MaxExpressionIndent + 1> Operands(InPlace, EStateTreeExpressionOperand::Copy);
	TStaticArray<bool, UE::StateTree::MaxExpressionIndent + 1> Values(InPlace, false);

	int32 Level = 0;
	
	for (int32 Index = 0; Index < ConditionsNum; Index++)
	{
		const int32 ConditionIndex = ConditionsOffset + Index;
		const FStateTreeConditionBase& Cond = CurrentFrame.StateTree->Nodes[ConditionIndex].Get<const FStateTreeConditionBase>();
		const FStateTreeDataView ConditionInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Cond.InstanceDataHandle);
		FNodeInstanceDataScope DataScope(*this, Cond.InstanceDataHandle, ConditionInstanceView);

		bool bValue = false;
		if (Cond.EvaluationMode == EStateTreeConditionEvaluationMode::Evaluated)
		{
			// Copy bound properties.
			if (Cond.BindingsBatch.IsValid())
			{
				// Use validated copy, since we test in situations where the sources are not always valid (e.g. enter conditions may try to access inactive parent state). 
				if (!CopyBatchWithValidation(CurrentParentFrame, CurrentFrame, ConditionInstanceView, Cond.BindingsBatch))
				{
					// If the source data cannot be accessed, the whole expression evaluates to false.
					STATETREE_TRACE_CONDITION_EVENT(ConditionIndex, ConditionInstanceView, EStateTreeTraceEventType::InternalForcedFailure);
					STATETREE_TRACE_LOG_EVENT(Warning, TEXT("Evaluation forced to false: source data cannot be accessed (e.g. enter conditions trying to access inactive parent state)"));
					Values[0] = false;
					break;
				}
			}
			
			bValue = Cond.TestCondition(*this);
			STATETREE_TRACE_CONDITION_EVENT(ConditionIndex, ConditionInstanceView, bValue ? EStateTreeTraceEventType::Passed : EStateTreeTraceEventType::Failed);
			
			// Reset copied properties that might contain object references.
			if (Cond.BindingsBatch.IsValid())
			{
				CurrentFrame.StateTree->PropertyBindings.ResetObjects(Cond.BindingsBatch, ConditionInstanceView);
			}
		}
		else
		{
			bValue = Cond.EvaluationMode == EStateTreeConditionEvaluationMode::ForcedTrue;
			STATETREE_TRACE_CONDITION_EVENT(ConditionIndex, FStateTreeDataView{}, bValue ? EStateTreeTraceEventType::ForcedSuccess : EStateTreeTraceEventType::ForcedFailure);
		}

		const int32 DeltaIndent = Cond.DeltaIndent;
		const int32 OpenParens = FMath::Max(0, DeltaIndent) + 1;	// +1 for the current value that is stored at the empty slot at the top of the value stack.
		const int32 ClosedParens = FMath::Max(0, -DeltaIndent) + 1;

		// Store the operand to apply when merging higher level down when returning to this level.
		// @todo: remove this conditions in 5.1, needs resaving existing StateTrees.
		const EStateTreeExpressionOperand Operand = Index == 0 ? EStateTreeExpressionOperand::Copy : Cond.Operand;
		Operands[Level] = Operand;

		// Store current value at the top of the stack.
		Level += OpenParens;
		Values[Level] = bValue;

		// Evaluate and merge down values based on closed braces.
		// The current value is placed in parens (see +1 above), which makes merging down and applying the new value consistent.
		// The default operand is copy, so if the value is needed immediately, it is just copied down, or if we're on the same level,
		// the operand storing above gives handles with the right logic.
		for (int32 Paren = 0; Paren < ClosedParens; Paren++)
		{
			Level--;
			switch (Operands[Level])
			{
			case EStateTreeExpressionOperand::Copy:
				Values[Level] = Values[Level + 1];
				break;
			case EStateTreeExpressionOperand::And:
				Values[Level] &= Values[Level + 1];
				break;
			case EStateTreeExpressionOperand::Or:
				Values[Level] |= Values[Level + 1];
				break;
			}
			Operands[Level] = EStateTreeExpressionOperand::Copy;
		}
	}
	
	return Values[0];
}

float FStateTreeExecutionContext::EvaluateUtility(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const int32 ConsiderationsOffset, const int32 ConsiderationsNum, const float StateWeight)
{
	// @todo: Tracing support
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_EvaluateUtility);

	if (ConsiderationsNum == 0)
	{
		return .0f;
	}

	TStaticArray<EStateTreeExpressionOperand, UE::StateTree::MaxExpressionIndent + 1> Operands(InPlace, EStateTreeExpressionOperand::Copy);
	TStaticArray<float, UE::StateTree::MaxExpressionIndent + 1> Values(InPlace, false);

	int32 Level = 0;
	float Value = .0f;
	for (int32 Index = 0; Index < ConsiderationsNum; Index++)
	{
		const int32 ConsiderationIndex = ConsiderationsOffset + Index;
		const FStateTreeConsiderationBase& Consideration = CurrentFrame.StateTree->Nodes[ConsiderationIndex].Get<const FStateTreeConsiderationBase>();
		const FStateTreeDataView ConsiderationInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Consideration.InstanceDataHandle);
		FNodeInstanceDataScope DataScope(*this, Consideration.InstanceDataHandle, ConsiderationInstanceView);

		// Copy bound properties.
		if (Consideration.BindingsBatch.IsValid())
		{
			// Use validated copy, since we test in situations where the sources are not always valid (e.g. considerations may try to access inactive parent state). 
			if (!CopyBatchWithValidation(CurrentParentFrame, CurrentFrame, ConsiderationInstanceView, Consideration.BindingsBatch))
			{
				// If the source data cannot be accessed, the whole expression evaluates to zero.
				Values[0] = .0f;
				break;
			}
		}

		Value = Consideration.GetNormalizedScore(*this);

		// Reset copied properties that might contain object references.
		if (Consideration.BindingsBatch.IsValid())
		{
			CurrentFrame.StateTree->PropertyBindings.ResetObjects(Consideration.BindingsBatch, ConsiderationInstanceView);
		}

		const int32 DeltaIndent = Consideration.DeltaIndent;
		const int32 OpenParens = FMath::Max(0, DeltaIndent) + 1;	// +1 for the current value that is stored at the empty slot at the top of the value stack.
		const int32 ClosedParens = FMath::Max(0, -DeltaIndent) + 1;

		// Store the operand to apply when merging higher level down when returning to this level.
		const EStateTreeExpressionOperand Operand = Index == 0 ? EStateTreeExpressionOperand::Copy : Consideration.Operand;
		Operands[Level] = Operand;

		// Store current value at the top of the stack.
		Level += OpenParens;
		Values[Level] = Value;

		// Evaluate and merge down values based on closed braces.
		// The current value is placed in parens (see +1 above), which makes merging down and applying the new value consistent.
		// The default operand is copy, so if the value is needed immediately, it is just copied down, or if we're on the same level,
		// the operand storing above gives handles with the right logic.
		for (int32 Paren = 0; Paren < ClosedParens; Paren++)
		{
			Level--;
			switch (Operands[Level])
			{
			case EStateTreeExpressionOperand::Copy:
				Values[Level] = Values[Level + 1];
				break;
			case EStateTreeExpressionOperand::And:
				Values[Level] = FMath::Min(Values[Level], Values[Level + 1]);
				break;
			case EStateTreeExpressionOperand::Or:
				Values[Level] = FMath::Max(Values[Level], Values[Level + 1]);
				break;
			}
			Operands[Level] = EStateTreeExpressionOperand::Copy;
		}
	}

	return StateWeight * Values[0];
}

void FStateTreeExecutionContext::EvaluatePropertyFunctionsOnActiveInstances(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame, FStateTreeIndex16 FuncsBegin, uint16 FuncsNum)
{
	for (int32 FuncIndex = FuncsBegin.Get(); FuncIndex < FuncsBegin.Get() + FuncsNum; ++FuncIndex)
	{
		const FStateTreePropertyFunctionBase& Func = CurrentFrame.StateTree->Nodes[FuncIndex].Get<const FStateTreePropertyFunctionBase>();
		const FStateTreeDataView FuncInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Func.InstanceDataHandle);
		FNodeInstanceDataScope DataScope(*this, Func.InstanceDataHandle, FuncInstanceView);

		// Copy bound properties.
		if (Func.BindingsBatch.IsValid())
		{
			// Use validated copy, since we test in situations where the sources are not always valid (e.g. enter conditions may try to access inactive parent state). 
			CopyBatchOnActiveInstances(CurrentParentFrame, CurrentFrame, FuncInstanceView, Func.BindingsBatch);
		}
			
		Func.Execute(*this);
			
		// Reset copied properties that might contain object references.
		if (Func.BindingsBatch.IsValid())
		{
			CurrentFrame.StateTree->PropertyBindings.ResetObjects(Func.BindingsBatch, FuncInstanceView);
		}
	}
}

void FStateTreeExecutionContext::EvaluatePropertyFunctionsWithValidation(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame, FStateTreeIndex16 FuncsBegin, uint16 FuncsNum)
{
	for (int32 FuncIndex = FuncsBegin.Get(); FuncIndex < FuncsBegin.Get() + FuncsNum; ++FuncIndex)
	{
		const FStateTreePropertyFunctionBase& Func = CurrentFrame.StateTree->Nodes[FuncIndex].Get<const FStateTreePropertyFunctionBase>();
		const FStateTreeDataView FuncInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Func.InstanceDataHandle);
		FNodeInstanceDataScope DataScope(*this, Func.InstanceDataHandle, FuncInstanceView);

		// Copy bound properties.
		if (Func.BindingsBatch.IsValid())
		{
			// Use validated copy, since we test in situations where the sources are not always valid (e.g. enter conditions may try to access inactive parent state). 
			CopyBatchWithValidation(CurrentParentFrame, CurrentFrame, FuncInstanceView, Func.BindingsBatch);
		}
			
		Func.Execute(*this);
			
		// Reset copied properties that might contain object references.
		if (Func.BindingsBatch.IsValid())
		{
			CurrentFrame.StateTree->PropertyBindings.ResetObjects(Func.BindingsBatch, FuncInstanceView);
		}
	}
}

FString FStateTreeExecutionContext::DebugGetEventsAsString() const
{
	TStringBuilder<512> StrBuilder;

	if (EventQueue)
	{
		for (const FStateTreeSharedEvent& Event : EventQueue->GetEventsView())
		{
			if (Event.IsValid())
			{
				if (StrBuilder.Len() > 0)
				{
					StrBuilder << TEXT(", ");
				}

				const bool bHasTag = Event->Tag.IsValid();
				const bool bHasPayload = Event->Payload.GetScriptStruct() != nullptr;
			
				if (bHasTag || bHasPayload)
				{
					StrBuilder << (TEXT('('));
				
					if (bHasTag)
					{
						StrBuilder << TEXT("Tag: '");
						StrBuilder << Event->Tag.GetTagName();
						StrBuilder << TEXT('\'');
					}
					if (bHasTag && bHasPayload)
					{
						StrBuilder << TEXT(", ");
					}
					if (bHasPayload)
					{
						StrBuilder << TEXT(" Payload: '");
						StrBuilder << Event->Payload.GetScriptStruct()->GetFName();
						StrBuilder << TEXT('\'');
					}
					StrBuilder << TEXT(") ");
				}
			}
		}
	}

	return StrBuilder.ToString();
}

bool FStateTreeExecutionContext::RequestTransition(
	const FStateTreeExecutionFrame& CurrentFrame,
	const FStateTreeStateHandle NextState,
	const EStateTreeTransitionPriority Priority,
	const FStateTreeSharedEvent* TransitionEvent,
	const EStateTreeSelectionFallback Fallback)
{
	// Skip lower priority transitions.
	if (NextTransition.Priority >= Priority)
	{
		return false;
	}

	if (NextState.IsCompletionState())
	{
		SetupNextTransition(CurrentFrame, NextState, Priority);
		STATETREE_LOG(Verbose, TEXT("Transition on state '%s' -> state '%s'"),
			*GetSafeStateName(CurrentFrame, CurrentFrame.ActiveStates.Last()), *NextState.Describe());
		return true;
	}
	if (!NextState.IsValid())
	{
		// NotSet is no-operation, but can be used to mask a transition at parent state. Returning unset keeps updating current state.
		SetupNextTransition(CurrentFrame, FStateTreeStateHandle::Invalid, Priority);
		return true;
	}

	FStateSelectionResult StateSelectionResult;
	if (SelectState(CurrentFrame, NextState, StateSelectionResult, TransitionEvent, Fallback))
	{
		SetupNextTransition(CurrentFrame, NextState, Priority);
		NextTransition.NextActiveFrames = StateSelectionResult.GetSelectedFrames();
		NextTransition.NextActiveFrameEvents = StateSelectionResult.GetFramesStateSelectionEvents();

		// Consume events from states, if required. 
		for (int32 FrameIndex = 0; FrameIndex < NextTransition.NextActiveFrames.Num(); FrameIndex++)
		{
			const FStateTreeExecutionFrame& Frame = NextTransition.NextActiveFrames[FrameIndex]; 
			const FStateTreeFrameStateSelectionEvents& FrameEvents = NextTransition.NextActiveFrameEvents[FrameIndex];

			for (int32 StateIndex = 0; StateIndex < Frame.ActiveStates.Num(); StateIndex++)
			{
				if (FrameEvents.Events[StateIndex].IsValid())
				{
					const FCompactStateTreeState& State = Frame.StateTree->States[StateIndex];
					if (State.bConsumeEventOnSelect)
					{
						 ConsumeEvent(FrameEvents.Events[StateIndex]);
					}
				}
			}
		}
		
		STATETREE_LOG(Verbose, TEXT("Transition on state '%s' -[%s]-> state '%s'"),
			*GetSafeStateName(CurrentFrame, CurrentFrame.ActiveStates.Last()),
			*GetSafeStateName(CurrentFrame, NextState),
			*GetSafeStateName(NextTransition.NextActiveFrames.Last(), NextTransition.NextActiveFrames.Last().ActiveStates.Last()));
		
		return true;
	}
		
	return false;
}

void FStateTreeExecutionContext::SetupNextTransition(const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeStateHandle NextState, const EStateTreeTransitionPriority Priority)
{
	const FStateTreeExecutionState& Exec = GetExecState();

	NextTransition.CurrentRunStatus = Exec.LastTickStatus;
	NextTransition.SourceState = CurrentlyProcessedState;
	NextTransition.SourceStateTree = CurrentFrame.StateTree;
	NextTransition.SourceRootState = CurrentFrame.ActiveStates.GetStateSafe(0);
	NextTransition.TargetState = NextState;
	NextTransition.Priority = Priority;

	FStateTreeExecutionFrame& NewFrame = NextTransition.NextActiveFrames.AddDefaulted_GetRef();
	NewFrame.StateTree = CurrentFrame.StateTree;
	NewFrame.RootState = CurrentFrame.RootState;

	if (NextState == FStateTreeStateHandle::Invalid)
	{
		NewFrame.ActiveStates = {};
	}
	else
	{
		NewFrame.ActiveStates = FStateTreeActiveStates(NextState);
	}
}

bool FStateTreeExecutionContext::TriggerTransitions()
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_TriggerTransition);
	STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::TriggerTransitions);

	FAllowDirectTransitionsScope AllowDirectTransitionsScope(*this); // Set flag for the scope of this function to allow direct transitions without buffering.
	FStateTreeExecutionState& Exec = GetExecState();

	if (EventQueue && EventQueue->HasEvents())
	{
		STATETREE_LOG_AND_TRACE(Verbose, Log, TEXT("Trigger transitions with events: %s"), *DebugGetEventsAsString());
	}

	NextTransition.Reset();

	//
	// Process transition requests
	//
	for (const FStateTreeTransitionRequest& Request : InstanceData.GetTransitionRequests())
	{
		// Find frame associated with the request.
		const FStateTreeExecutionFrame* ParentFrame = nullptr;
		const FStateTreeExecutionFrame* CurrentFrame = FindFrame(Request.SourceStateTree, Request.SourceRootState, Exec.ActiveFrames, ParentFrame);

		if (CurrentFrame)
		{
			if (RequestTransition(*CurrentFrame, Request.TargetState, Request.Priority))
			{
				NextTransitionSource = FStateTreeTransitionSource(EStateTreeTransitionSourceType::ExternalRequest, Request.TargetState, Request.Priority);
			}
		}
	}
	InstanceData.ResetTransitionRequests();

	//
	// Collect tick, event, and task based transitions.
	//
	struct FTransitionHandler
	{
		FTransitionHandler() = default;
		
		FTransitionHandler(const uint8 InFrameIndex, const FStateTreeStateHandle InStateHandle, const EStateTreeTransitionPriority InPriority)
			: StateHandle(InStateHandle)
			, TaskIndex(FStateTreeIndex16::Invalid)
			, FrameIndex(InFrameIndex)
			, Priority(InPriority)
		{
		}

		FTransitionHandler(const uint8 InFrameIndex, const FStateTreeStateHandle InStateHandle, const FStateTreeIndex16 InTaskIndex, const EStateTreeTransitionPriority InPriority)
			: StateHandle(InStateHandle)
			, TaskIndex(InTaskIndex)
			, FrameIndex(InFrameIndex)
			, Priority(InPriority)
		{
		}

		FStateTreeStateHandle StateHandle;
		FStateTreeIndex16 TaskIndex = FStateTreeIndex16::Invalid;
		uint8 FrameIndex = 0;
		EStateTreeTransitionPriority Priority = EStateTreeTransitionPriority::Normal;

		bool operator<(const FTransitionHandler& Other) const
		{
			// Highest priority first.
			return Priority > Other.Priority;
		}
	};

	TArray<FTransitionHandler, TInlineAllocator<16>> TransitionHandlers;

	if (Exec.ActiveFrames.Num() > 0)
	{
		for (int32 FrameIndex = Exec.ActiveFrames.Num() - 1; FrameIndex >= 0; FrameIndex--)
		{
			FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
			const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

			for (int32 StateIndex = CurrentFrame.ActiveStates.Num() - 1; StateIndex >= 0; StateIndex--)
			{
				const FStateTreeStateHandle StateHandle = CurrentFrame.ActiveStates[StateIndex];
				const FCompactStateTreeState& State = CurrentStateTree->States[StateHandle.Index];

				// Do not process any transitions from a disabled state
				if (!State.bEnabled)
				{
					continue;
				}

				// Transition tasks.
				if (State.bHasTransitionTasks)
				{
					for (int32 TaskIndex = (State.TasksBegin + State.TasksNum) - 1; TaskIndex >= State.TasksBegin; TaskIndex--)
					{
						const FStateTreeTaskBase& Task = CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
						if (Task.bShouldAffectTransitions)
						{
							TransitionHandlers.Emplace((uint8)FrameIndex, StateHandle, FStateTreeIndex16(TaskIndex), Task.TransitionHandlingPriority);
						}
					}
				}
				
				// Regular transitions on state
				if (State.TransitionsNum > 0)
				{
					TransitionHandlers.Emplace((uint8)FrameIndex, StateHandle, EStateTreeTransitionPriority::Normal);
				}
			}

			if (CurrentFrame.bIsGlobalFrame)
			{
				// Global transition tasks.
				if (CurrentFrame.StateTree->bHasGlobalTransitionTasks)
				{
					for (int32 TaskIndex = (CurrentStateTree->GlobalTasksBegin + CurrentStateTree->GlobalTasksNum) - 1; TaskIndex >= CurrentFrame.StateTree->GlobalTasksBegin; TaskIndex--)
					{
						const FStateTreeTaskBase& Task = CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
						if (Task.bShouldAffectTransitions)
						{
							TransitionHandlers.Emplace((uint8)FrameIndex, FStateTreeStateHandle(), FStateTreeIndex16(TaskIndex), Task.TransitionHandlingPriority);
						}
					}
				}				
			}
		}

		// Sort by priority and adding order.
		TransitionHandlers.StableSort();
	}

	//
	// Process task and state transitions in priority order. 
	//
	for (const FTransitionHandler& Handler : TransitionHandlers)
	{
		const int32 FrameIndex = Handler.FrameIndex;
		FStateTreeExecutionFrame* CurrentParentFrame = FrameIndex > 0 ? &Exec.ActiveFrames[FrameIndex - 1] : nullptr;
		FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
		const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

		FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);
		FCurrentlyProcessedStateScope StateScope(*this, Handler.StateHandle);
		STATETREE_TRACE_SCOPED_STATE(Handler.StateHandle);

		if (Handler.TaskIndex.IsValid())
		{
			const FStateTreeTaskBase& Task = CurrentStateTree->Nodes[Handler.TaskIndex.Get()].Get<const FStateTreeTaskBase>();

			// Ignore disabled task
			if (Task.bTaskEnabled == false)
			{
				STATETREE_LOG(VeryVerbose, TEXT("%*sSkipped 'TriggerTransitions' for disabled Task: '%s'"), UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
				continue;
			}

			const FStateTreeDataView TaskInstanceView = GetDataView(CurrentParentFrame, CurrentFrame, Task.InstanceDataHandle);
			FNodeInstanceDataScope DataScope(*this, Task.InstanceDataHandle, TaskInstanceView);

			STATETREE_LOG(VeryVerbose, TEXT("%*sTriggerTransitions: '%s'"), UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
			STATETREE_TRACE_TASK_EVENT(Handler.TaskIndex.Get(), TaskInstanceView, EStateTreeTraceEventType::OnEvaluating, EStateTreeRunStatus::Running);
			check(TaskInstanceView.IsValid());
			Task.TriggerTransitions(*this);
		}
		else if (Handler.StateHandle.IsValid())
		{
			const FCompactStateTreeState& State = CurrentStateTree->States[Handler.StateHandle.Index];

			// Transitions
			for (uint8 i = 0; i < State.TransitionsNum; i++)
			{
				// All transition conditions must pass
				const int16 TransitionIndex = State.TransitionsBegin + i;
				const FCompactStateTransition& Transition = CurrentStateTree->Transitions[TransitionIndex];

				// Skip disabled transitions
				if (Transition.bTransitionEnabled == false)
				{
					continue;
				}
				
				// No need to test the transition if same or higher priority transition has already been processed.
				if (Transition.Priority <= NextTransition.Priority)
				{
					continue;
				}

				// Skip completion transitions
				if (EnumHasAnyFlags(Transition.Trigger, EStateTreeTransitionTrigger::OnStateCompleted))
				{
					continue;
				}

				// If a delayed transition has passed the delay, and remove it from the queue, and try trigger it.
				if (Transition.HasDelay())
				{
					bool bTriggeredDelayedTransition = false;
					const TArray<FStateTreeTransitionDelayedState, TInlineAllocator<8>> ExpiredDelayedStates = Exec.FindAndRemoveExpiredDelayedTransitions(CurrentFrame.StateTree, FStateTreeIndex16(TransitionIndex));
					for (const FStateTreeTransitionDelayedState& DelayedState : ExpiredDelayedStates)
					{
						STATETREE_LOG(Verbose, TEXT("Passed delayed transition from '%s' (%s) -> '%s'"),
							*GetSafeStateName(CurrentFrame, CurrentFrame.ActiveStates.Last()), *State.Name.ToString(), *GetSafeStateName(CurrentFrame, Transition.State));

						// Trigger Delayed Transition when the delay has passed.
						if (RequestTransition(CurrentFrame, Transition.State, Transition.Priority, &DelayedState.CapturedEvent, Transition.Fallback))
						{
							// If the transition was successfully requested with a specific event, consume and remove the event, it's been used.  
							if (DelayedState.CapturedEvent.IsValid() && Transition.bConsumeEventOnSelect)
							{
								ConsumeEvent(DelayedState.CapturedEvent);
							}

							NextTransitionSource = FStateTreeTransitionSource(FStateTreeIndex16(TransitionIndex), Transition.State, Transition.Priority);
							bTriggeredDelayedTransition = true;
							break;
						}
					}

					if (bTriggeredDelayedTransition)
					{
						continue;
					}
				}

				TArray<const FStateTreeSharedEvent*, TInlineAllocator<8>> TransitionEvents;

				if (Transition.Trigger == EStateTreeTransitionTrigger::OnEvent)
				{
					check(Transition.RequiredEvent.IsValid());

					TConstArrayView<FStateTreeSharedEvent> EventsQueue = GetEventsToProcessView();
					for (const FStateTreeSharedEvent& Event : EventsQueue)
					{
						check(Event.IsValid());
						if (Transition.RequiredEvent.DoesEventMatchDesc(*Event))
						{
							TransitionEvents.Emplace(&Event);
						}
					}
				}
				else if (Transition.Trigger == EStateTreeTransitionTrigger::OnTick)
				{
					// Dummy event to make sure we iterate to loop below once.
					TransitionEvents.Emplace(nullptr);
				}
				
				for (const FStateTreeSharedEvent* TransitionEvent : TransitionEvents)
				{
					bool bPassed = false; 
					{
						FCurrentlyProcessedTransitionEventScope TransitionEventScope(*this, TransitionEvent ? TransitionEvent->Get() : nullptr);
						STATETREE_TRACE_TRANSITION_EVENT(FStateTreeTransitionSource(FStateTreeIndex16(TransitionIndex), Transition.State, Transition.Priority), EStateTreeTraceEventType::OnEvaluating);
						STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::TransitionConditions);
						bPassed = TestAllConditions(CurrentParentFrame, CurrentFrame, Transition.ConditionsBegin, Transition.ConditionsNum);
					}

					if (bPassed)
					{
						// If the transitions is delayed, set up the delay. 
						if (Transition.HasDelay())
						{
							uint32 TransitionEventHash = 0u;
							if (TransitionEvent && TransitionEvent->IsValid())
							{		
								TransitionEventHash = GetTypeHash(*TransitionEvent->Get());
							}

							const bool bIsDelayedTransitionExisting = Exec.DelayedTransitions.ContainsByPredicate([StateTree = CurrentFrame.StateTree, TransitionIndex, TransitionEventHash](const FStateTreeTransitionDelayedState& DelayedState)
							{
								return DelayedState.StateTree == StateTree && DelayedState.TransitionIndex.Get() == TransitionIndex && DelayedState.CapturedEventHash == TransitionEventHash;
							});

							if (!bIsDelayedTransitionExisting)
							{
								// Initialize new delayed transition.
								const float DelayDuration = Transition.Delay.GetRandomDuration(Exec.RandomStream);
								if (DelayDuration > 0.0f)
								{
									FStateTreeTransitionDelayedState& DelayedState = Exec.DelayedTransitions.AddDefaulted_GetRef();
									DelayedState.StateTree = CurrentFrame.StateTree;
									DelayedState.TransitionIndex = FStateTreeIndex16(TransitionIndex);
									DelayedState.TimeLeft = DelayDuration;
									if (TransitionEvent && TransitionEvent->IsValid())
									{
										DelayedState.CapturedEvent = *TransitionEvent;
										DelayedState.CapturedEventHash = TransitionEventHash;
									}

									BeginDelayedTransition(DelayedState);
									STATETREE_LOG(Verbose, TEXT("Delayed transition triggered from '%s' (%s) -> '%s' %.1fs"),
										*GetSafeStateName(CurrentFrame, CurrentFrame.ActiveStates.Last()), *State.Name.ToString(), *GetSafeStateName(CurrentFrame, Transition.State), DelayedState.TimeLeft);
								
									// Delay state added, skip requesting the transition.
									continue;
								}
								// Fallthrough to request transition if duration was zero. 
							}
							else
							{
								// We get here if the transitions re-triggers during the delay, on which case we'll just ignore it.
								continue;
							}
						}

						if (RequestTransition(CurrentFrame, Transition.State, Transition.Priority, TransitionEvent, Transition.Fallback))
						{
							// If the transition was successfully requested with a specific event, consume and remove the event, it's been used.  
							if (TransitionEvent && Transition.bConsumeEventOnSelect)
							{
								ConsumeEvent(*TransitionEvent);
							}
							
							NextTransitionSource = FStateTreeTransitionSource(FStateTreeIndex16(TransitionIndex), Transition.State, Transition.Priority);
							break;
						}
					}
				}
			}
		}
	}

	// All events have had the change to be reacted to, clear the event queue (if this instance owns it).
	if (InstanceData.IsOwningEventQueue())
	{
		EventQueue->Reset();
	}

	//
	// Check state completion transitions.
	//
	bool bProcessSubTreeCompletion = true;

	if (NextTransition.Priority == EStateTreeTransitionPriority::None
		&& Exec.LastTickStatus != EStateTreeRunStatus::Running)
	{
		// Start from the last completed state if specified.
		const int32 FrameStartIndex = Exec.CompletedFrameIndex.IsValid() ? Exec.CompletedFrameIndex.AsInt32() : (Exec.ActiveFrames.Num() - 1);
		check(FrameStartIndex >= 0 && FrameStartIndex < Exec.ActiveFrames.Num());
		
		for (int32 FrameIndex = FrameStartIndex; FrameIndex >= 0; FrameIndex--)
		{
			FStateTreeExecutionFrame* CurrentParentFrame = FrameIndex > 0 ? &Exec.ActiveFrames[FrameIndex - 1] : nullptr;
			FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
			const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

			FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);

			int32 StateStartIndex = CurrentFrame.ActiveStates.Num() - 1; // This is ok, even if the ActiveStates is 0, -1 will skip the whole state loop below.
			if (FrameIndex == FrameStartIndex
				&& Exec.CompletedStateHandle.IsValid())
			{
				StateStartIndex = CurrentFrame.ActiveStates.IndexOfReverse(Exec.CompletedStateHandle);
				// INDEX_NONE (-1) will skip the whole state loop below. We still want to warn.
				ensureMsgf(StateStartIndex != INDEX_NONE, TEXT("If CompletedFrameIndex and CompletedStateHandle are specified, we expect that the state is found"));
			}
			
			const EStateTreeTransitionTrigger CompletionTrigger = Exec.LastTickStatus == EStateTreeRunStatus::Succeeded ? EStateTreeTransitionTrigger::OnStateSucceeded : EStateTreeTransitionTrigger::OnStateFailed;
		
			// Check completion transitions
			for (int32 StateIndex = StateStartIndex; StateIndex >= 0; StateIndex--)
			{
				const FStateTreeStateHandle StateHandle = CurrentFrame.ActiveStates[StateIndex];
				const FCompactStateTreeState& State = CurrentStateTree->States[StateHandle.Index];

				FCurrentlyProcessedStateScope StateScope(*this, StateHandle);
				STATETREE_TRACE_SCOPED_STATE_PHASE(StateHandle, EStateTreeUpdatePhase::TriggerTransitions);

				for (uint8 i = 0; i < State.TransitionsNum; i++)
				{
					// All transition conditions must pass
					const int16 TransitionIndex = State.TransitionsBegin + i;
					const FCompactStateTransition& Transition = CurrentStateTree->Transitions[TransitionIndex];

					// Skip disabled transitions
					if (Transition.bTransitionEnabled == false)
					{
						continue;
					}

					if (EnumHasAnyFlags(Transition.Trigger, CompletionTrigger))
					{
						bool bPassed = false;
						{
							STATETREE_TRACE_TRANSITION_EVENT(FStateTreeTransitionSource(FStateTreeIndex16(TransitionIndex), Transition.State, Transition.Priority), EStateTreeTraceEventType::OnEvaluating);
							STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::TransitionConditions);
							bPassed = TestAllConditions(CurrentParentFrame, CurrentFrame, Transition.ConditionsBegin, Transition.ConditionsNum);
						}

						if (bPassed)
						{
							// No delay allowed on completion conditions.
							// No priority on completion transitions, use the priority to signal that state is selected.
							if (RequestTransition(CurrentFrame, Transition.State, EStateTreeTransitionPriority::Normal, /*TransitionEvent*/nullptr, Transition.Fallback))
							{
								NextTransitionSource = FStateTreeTransitionSource(FStateTreeIndex16(TransitionIndex), Transition.State, Transition.Priority);
								break;
							}
						}
					}
				}

				if (NextTransition.Priority != EStateTreeTransitionPriority::None) //-V547
				{
					break;
				}
			}
		}

		// Handle the case where no transition was found.
		if (NextTransition.Priority == EStateTreeTransitionPriority::None) //-V547
		{
			STATETREE_LOG_AND_TRACE(Verbose, Warning, TEXT("Could not trigger completion transition, jump back to root state."));

			check(!Exec.ActiveFrames.IsEmpty());
			FStateTreeExecutionFrame& RootFrame = Exec.ActiveFrames[0];
			FCurrentlyProcessedFrameScope RootFrameScope(*this, nullptr, RootFrame);
			FCurrentlyProcessedStateScope RootStateScope(*this, FStateTreeStateHandle::Root);
				
			if (RequestTransition(RootFrame, FStateTreeStateHandle::Root, EStateTreeTransitionPriority::Normal))
			{
				NextTransitionSource = FStateTreeTransitionSource(EStateTreeTransitionSourceType::Internal, FStateTreeStateHandle::Root, EStateTreeTransitionPriority::Normal);
			}
			else
			{
				STATETREE_LOG_AND_TRACE(Warning, Error, TEXT("Failed to select root state. Stopping the tree with failure."));

				SetupNextTransition(RootFrame, FStateTreeStateHandle::Failed, EStateTreeTransitionPriority::Critical);

				// In this case we don't want to complete subtrees, we want to force the whole tree to stop.
				bProcessSubTreeCompletion = false;
			}
		}
	}

	// Check if the transition was succeed/failed, if we're on a sub-tree, complete the subtree instead of transition.
	if (NextTransition.TargetState.IsCompletionState() && bProcessSubTreeCompletion)
	{
		const int32 SourceFrameIndex = Exec.ActiveFrames.IndexOfByPredicate([&NextTransition = NextTransition](const FStateTreeExecutionFrame& Frame)
		{
			return Frame.StateTree == NextTransition.SourceStateTree && Frame.RootState == NextTransition.SourceRootState;
		});
		// Check that the transition source frame is a sub-tree, the first frame (0 index) is not a subtree. 
		if (SourceFrameIndex > 0)
		{
			const FStateTreeExecutionFrame& SourceFrame = Exec.ActiveFrames[SourceFrameIndex];
			const int32 ParentFrameIndex = SourceFrameIndex - 1;
			const FStateTreeExecutionFrame& ParentFrame = Exec.ActiveFrames[ParentFrameIndex];
			const FStateTreeStateHandle ParentLinkedState = ParentFrame.ActiveStates.Last();

			if (ParentLinkedState.IsValid())
			{
				const EStateTreeRunStatus RunStatus = NextTransition.TargetState.ToCompletionStatus(); 
				STATETREE_LOG(Verbose, TEXT("Completed subtree '%s' from state '%s': %s"),
					*GetSafeStateName(ParentFrame, ParentLinkedState), *GetSafeStateName(SourceFrame, NextTransition.SourceState), *UEnum::GetDisplayValueAsText(RunStatus).ToString());

				// Set the parent linked state as last completed state, and update tick status to the status from the transition.
				Exec.CompletedFrameIndex = FStateTreeIndex16(ParentFrameIndex);
				Exec.CompletedStateHandle = ParentLinkedState;
				Exec.LastTickStatus = RunStatus;

				// Clear the transition and return that no transition took place.
				// Since the LastTickStatus != running, the transition loop will try another transition
				// now starting from the linked parent state. If we run out of retires in the selection loop (e.g. very deep hierarchy)
				// we will continue on next tick.
				NextTransition.Reset();
				return false;
			}
		}
	}

	return NextTransition.TargetState.IsValid();
}

bool FStateTreeExecutionContext::SelectState(const FStateTreeExecutionFrame& CurrentFrame,
											const FStateTreeStateHandle NextState,
											FStateSelectionResult& OutSelectionResult,
											const FStateTreeSharedEvent* TransitionEvent,
											const EStateTreeSelectionFallback Fallback)
{
	const FStateTreeExecutionState& Exec = GetExecState();

	if (Exec.ActiveFrames.IsEmpty())
	{
		STATETREE_LOG(Error, TEXT("%hs: SelectState can only be called on initialized tree.  '%s' using StateTree '%s'."),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return false;
	}
	
	if (!NextState.IsValid())
	{
		return false;
	}

	// Walk towards the root from current state.
	TArray<FStateTreeStateHandle, TInlineAllocator<FStateTreeActiveStates::MaxStates>> PathToNextState;
	FStateTreeStateHandle CurrState = NextState;
	while (CurrState.IsValid())
	{
		if (PathToNextState.Num() == FStateTreeActiveStates::MaxStates)
		{
			STATETREE_LOG(Error, TEXT("%hs: Reached max execution depth when trying to select state %s from '%s'.  '%s' using StateTree '%s'."),
				__FUNCTION__, *GetSafeStateName(CurrentFrame, NextState), *GetStateStatusString(Exec), *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
			return false;
		}
		// Store the states that are in between the 'NextState' and common ancestor. 
		PathToNextState.Push(CurrState);
		CurrState = CurrentFrame.StateTree->States[CurrState.Index].Parent;
	}

	Algo::Reverse(PathToNextState);

	const UStateTree* NextStateTree = CurrentFrame.StateTree;
	const FStateTreeStateHandle NextRootState = PathToNextState[0]; 

	// Find the frame that the next state belongs to.
	int32 CurrentFrameIndex = INDEX_NONE;
	int32 CurrentStateTreeIndex = INDEX_NONE;

	for (int32 FrameIndex = Exec.ActiveFrames.Num() - 1; FrameIndex >= 0; FrameIndex--)
	{
		const FStateTreeExecutionFrame& Frame = Exec.ActiveFrames[FrameIndex]; 
		if (Frame.StateTree == NextStateTree)
		{
			CurrentStateTreeIndex = FrameIndex;
			if (Frame.RootState == NextRootState)
			{
				CurrentFrameIndex = FrameIndex;
				break;
			}
		}
	}

	// Copy common frames over.
	// ReferenceCurrentFrame is the original of the last copied frame. It will be used to keep track if we are following the current active frames and states.
	const FStateTreeExecutionFrame* CurrentFrameInActiveFrames  = nullptr;
	if (CurrentFrameIndex != INDEX_NONE)
	{
		const int32 NumCommonFrames = CurrentFrameIndex + 1;
		OutSelectionResult = FStateSelectionResult(MakeArrayView(Exec.ActiveFrames.GetData(), NumCommonFrames));
		CurrentFrameInActiveFrames  = &Exec.ActiveFrames[CurrentFrameIndex];
	}
	else if (CurrentStateTreeIndex != INDEX_NONE)
	{
		// If we could not find a common frame, we assume that we jumped to different subtree in same asset.
		const int32 NumCommonFrames = CurrentStateTreeIndex + 1;
		OutSelectionResult = FStateSelectionResult(MakeArrayView(Exec.ActiveFrames.GetData(), NumCommonFrames));
		CurrentFrameInActiveFrames  = &Exec.ActiveFrames[CurrentStateTreeIndex];
	}
	else
	{
		STATETREE_LOG(Error, TEXT("%hs: Encountered unrecognized state %s during state selection from '%s'.  '%s' using StateTree '%s'."),
			__FUNCTION__, *GetNameSafe(NextStateTree), *GetStateStatusString(Exec), *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return false;
	}
	
	// Append in between state in reverse order, they were collected from leaf towards the root.
	// Note: NextState will be added by SelectStateInternal() if conditions pass.
	const int32 LastFrameIndex = OutSelectionResult.FramesNum() - 1;
	FStateTreeExecutionFrame& LastFrame = OutSelectionResult.GetSelectedFrames()[LastFrameIndex];

	// Find index of the first state to be evaluated.
	int32 FirstNewStateIndex = 0;
	if (CurrentFrameIndex != INDEX_NONE)
	{
		// If LastFrame.ActiveStates is a subset of PathToNextState (e.g when someone use "TryEnter" selection behavior and then make a transition to it's child or if one is reentering the same state).
		// In such case loop below won't break on anything and FirstNewStateIndex will be incorrectly 0, thus we initialize it to be right after the shorter range.
		FirstNewStateIndex = FMath::Max(0, FMath::Min(PathToNextState.Num(), LastFrame.ActiveStates.Num()) - 1);
		for (int32 Index = 0; Index < FMath::Min(PathToNextState.Num(), LastFrame.ActiveStates.Num()); ++Index)
		{
			if (LastFrame.ActiveStates[Index] != PathToNextState[Index])
			{
				FirstNewStateIndex = Index;
				break;
			}
		}
	}

	LastFrame.ActiveStates.SetNum(FirstNewStateIndex);

	// Existing state's data is safe to access during select.
	LastFrame.NumCurrentlyActiveStates = static_cast<uint8>(LastFrame.ActiveStates.Num());

	FStateSelectionResult InitialSelection;

	if (Fallback == EStateTreeSelectionFallback::NextSelectableSibling)
	{
		InitialSelection = OutSelectionResult;
	}
	
	// We take copy of the last frame and assign it later, as SelectStateInternal() might change the array and invalidate the pointer.
	const FStateTreeExecutionFrame* CurrentParentFrame = LastFrameIndex > 0 ? &OutSelectionResult.GetSelectedFrames()[LastFrameIndex - 1] : nullptr;

	// Path from the first new state up to the NextState
	TConstArrayView<FStateTreeStateHandle> NewStatesPathToNextState(&PathToNextState[FirstNewStateIndex], PathToNextState.Num() - FirstNewStateIndex);

	if (SelectStateInternal(CurrentParentFrame, OutSelectionResult.GetSelectedFrames()[LastFrameIndex], CurrentFrameInActiveFrames, NewStatesPathToNextState, OutSelectionResult, TransitionEvent))
	{
		return true;
	}

	// Failed to Select Next State, handle fallback here
	// Return true on the first next sibling that gets selected successfully
	if (Fallback == EStateTreeSelectionFallback::NextSelectableSibling && PathToNextState.Num() >= 2)
	{
		const FStateTreeStateHandle Parent = PathToNextState.Last(1);
		if (Parent.IsValid())
		{
			const FCompactStateTreeState& ParentState = CurrentFrame.StateTree->States[Parent.Index];

			uint16 ChildState = CurrentFrame.StateTree->States[NextState.Index].GetNextSibling();
			for (; ChildState < ParentState.ChildrenEnd; ChildState = CurrentFrame.StateTree->States[ChildState].GetNextSibling())
			{
				FStateTreeStateHandle ChildStateHandle = FStateTreeStateHandle(ChildState);

				// Start selection from blank slate.
				OutSelectionResult = InitialSelection;
	
				// We take copy of the last frame and assign it later, as SelectStateInternal() might change the array and invalidate the pointer.
				CurrentParentFrame = LastFrameIndex > 0 ? &OutSelectionResult.GetSelectedFrames()[LastFrameIndex - 1] : nullptr; 
				if (SelectStateInternal(CurrentParentFrame, OutSelectionResult.GetSelectedFrames()[LastFrameIndex], CurrentFrameInActiveFrames, {ChildStateHandle}, OutSelectionResult))
				{
					return true;
				}
			}
		}
	}
	
	return false;
}

bool FStateTreeExecutionContext::SelectStateInternal(
	const FStateTreeExecutionFrame* CurrentParentFrame,
	FStateTreeExecutionFrame& CurrentFrame,
	const FStateTreeExecutionFrame* CurrentFrameInActiveFrames,
	TConstArrayView<FStateTreeStateHandle> PathToNextState,
	FStateSelectionResult& OutSelectionResult,
	const FStateTreeSharedEvent* TransitionEvent)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_SelectState);

	const FStateTreeExecutionState& Exec = GetExecState();

	check(!PathToNextState.IsEmpty());
	const FStateTreeStateHandle NextStateHandle = PathToNextState[0];
	if (!NextStateHandle.IsValid())
	{
		// Trying to select non-existing state.
		STATETREE_LOG(Error, TEXT("%hs: Trying to select invalid state from '%s'.  '%s' using StateTree '%s'."),
            __FUNCTION__, *GetStateStatusString(Exec), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
		return false;
	}

	FCurrentlyProcessedFrameScope FrameScope(*this, CurrentParentFrame, CurrentFrame);
	FCurrentlyProcessedStateScope NextStateScope(*this, NextStateHandle);
	FCurrentFrameStateSelectionEventsScope CapturedEventsScope(*this, OutSelectionResult.GetFramesStateSelectionEvents().Last());

	const UStateTree* CurrentStateTree = CurrentFrame.StateTree;
	const FCompactStateTreeState& NextState = CurrentStateTree->States[NextStateHandle.Index];

	if (NextState.bEnabled == false)
	{
		// Do not select disabled state
		STATETREE_LOG(VeryVerbose, TEXT("%hs: Ignoring disabled state '%s'.  '%s' using StateTree '%s'."),
			__FUNCTION__, *GetSafeStateName(CurrentFrame, NextStateHandle), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
		return false;
	}

	STATETREE_TRACE_SCOPED_STATE_PHASE(NextStateHandle, EStateTreeUpdatePhase::StateSelection);

	// The state cannot be directly selected.
	if (NextState.SelectionBehavior == EStateTreeStateSelectionBehavior::None)
	{
		return false;
	}

	const UStateTree* NextLinkedStateAsset = NextState.LinkedAsset;

	// Look up linked state overrides
	const FInstancedPropertyBag* NextLinkedStateParameterOverride = nullptr;
	if (NextState.Type == EStateTreeStateType::LinkedAsset)
	{
		if (const FStateTreeReference* Override = GetLinkedStateTreeOverrideForTag(NextState.Tag))
		{
			NextLinkedStateAsset = Override->GetStateTree();
			NextLinkedStateParameterOverride = &Override->GetParameters();

			STATETREE_LOG(VeryVerbose, TEXT("%hs: In state '%s', overriding linked asset '%s' with '%s'. '%s' using StateTree '%s'."),
					__FUNCTION__, *GetSafeStateName(CurrentFrame, NextStateHandle),
					*GetFullNameSafe(NextState.LinkedAsset), *GetFullNameSafe(NextLinkedStateAsset),
					*GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
		}
	}

	if (NextState.ParameterDataHandle.IsValid())
	{
		// Instantiate state parameters if not done yet.
		FStateTreeDataView NextStateParametersView = GetDataViewOrTemporary(CurrentParentFrame, CurrentFrame, NextState.ParameterDataHandle);
		if (!NextStateParametersView.IsValid())
		{
			// Allocate temporary instance for parameters if the state has params.
			// The subtree state selection below assumes that this creates always a valid temporary, we'll create the temp data even if parameters are empty.
			// @todo: Empty params is valid and common case, we should not require to create empty parameters data (this needs to be handle in compiler and UpdateInstanceData too).
			if (NextLinkedStateParameterOverride)
			{
				// Create from an override.
				FStateTreeDataView TempStateParametersView = AddTemporaryInstance(CurrentFrame, FStateTreeIndex16::Invalid, NextState.ParameterDataHandle, FConstStructView(TBaseStructure<FCompactStateTreeParameters>::Get()));
				check(TempStateParametersView.IsValid());
				FCompactStateTreeParameters& StateParams = TempStateParametersView.GetMutable<FCompactStateTreeParameters>();
				StateParams.Parameters = *NextLinkedStateParameterOverride;
				NextStateParametersView = FStateTreeDataView(StateParams.Parameters.GetMutableValue());
			}
			else
			{
				// Create from template in the asset.
				const FConstStructView DefaultStateParamsInstanceData = CurrentFrame.StateTree->DefaultInstanceData.GetStruct(NextState.ParameterTemplateIndex.Get());
				FStateTreeDataView TempStateParametersView = AddTemporaryInstance(CurrentFrame, FStateTreeIndex16::Invalid, NextState.ParameterDataHandle, DefaultStateParamsInstanceData);
				check(TempStateParametersView.IsValid());
				FCompactStateTreeParameters& StateParams = TempStateParametersView.GetMutable<FCompactStateTreeParameters>();
				NextStateParametersView = FStateTreeDataView(StateParams.Parameters.GetMutableValue());
			}
		}

		// Copy parameters if needed
		if (NextStateParametersView.IsValid()
			&& NextState.ParameterDataHandle.IsValid()
			&& NextState.ParameterBindingsBatch.IsValid())
		{
			// Note: the parameters are for the current (linked) state, stored in current frame.
			// The copy can fail, if the overridden parameters do not match, this is by design.
			CopyBatchWithValidation(CurrentParentFrame, CurrentFrame, NextStateParametersView, NextState.ParameterBindingsBatch);
		}
	}

	const bool bIsDestinationState = PathToNextState.Num() < 2;
	const bool bShouldPrerequisitesBeChecked = bIsDestinationState || NextState.bCheckPrerequisitesWhenActivatingChildDirectly;
	TArray<const FStateTreeSharedEvent*, TInlineAllocator<FStateTreeEventQueue::MaxActiveEvents>> StateSelectionEvents;
	if (NextState.EventDataIndex.IsValid())
	{
		check(NextState.RequiredEventToEnter.IsValid());

		// Use the same event as performed transition unless it didn't lead to this state as only state selected by the transition should get it's event.
		if (TransitionEvent && TransitionEvent->IsValid() && bIsDestinationState)
		{
			if (NextState.RequiredEventToEnter.DoesEventMatchDesc(*TransitionEvent->Get()))
			{
				StateSelectionEvents.Emplace(TransitionEvent);
			}
		}
		else
		{
			TArrayView<FStateTreeSharedEvent> EventsQueue = GetMutableEventsToProcessView();
			for (FStateTreeSharedEvent& Event : EventsQueue)
			{
				check(Event.IsValid());
				if (NextState.RequiredEventToEnter.DoesEventMatchDesc(*Event))
				{
					StateSelectionEvents.Emplace(&Event);
				}
			}

			// Couldn't find matching state's event, but it's marked as not required. Adding an empty event which allows us to continue the state selection.
			if (!bShouldPrerequisitesBeChecked && StateSelectionEvents.IsEmpty())
			{
				StateSelectionEvents.Emplace();
			}
		}

		if (StateSelectionEvents.IsEmpty())
		{
			return false;
		}
	}
	else
	{
		StateSelectionEvents.Emplace();
	}

	if (!CurrentFrame.ActiveStates.Push(NextStateHandle))
	{
		STATETREE_LOG(Error, TEXT("%hs: Reached max execution depth when trying to select state %s from '%s'.  '%s' using StateTree '%s'."),
			__FUNCTION__, *GetSafeStateName(CurrentFrame, NextStateHandle), *GetStateStatusString(Exec), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
		return false;
	}

	// Check if we're still tracking on the current active frame and state.
	// If we are, update the NumCurrentlyActiveStates to indicate that this state's instance data can be accessed. 
	const uint8 PrevNumCurrentlyActiveStates = CurrentFrame.NumCurrentlyActiveStates; 
	if (CurrentFrame.ActiveInstanceIndexBase.IsValid()
		&& CurrentFrameInActiveFrames)
	{
		const int32 CurrentStateIndex = CurrentFrame.ActiveStates.Num() - 1;
		const FStateTreeStateHandle MatchingActiveHandle = CurrentFrameInActiveFrames->ActiveStates.GetStateSafe(CurrentStateIndex);
		if (MatchingActiveHandle == NextStateHandle)
		{
			CurrentFrame.NumCurrentlyActiveStates = static_cast<uint8>(CurrentFrame.ActiveStates.Num());
		}
	}

	bool bSucceededToSelectState = false;

	for (const FStateTreeSharedEvent* StateSelectionEvent : StateSelectionEvents)
	{
		if (StateSelectionEvent)
		{
			CurrentlyProcessedStateSelectionEvents->Events[NextState.Depth] = *StateSelectionEvent;
		}
		
		if (bShouldPrerequisitesBeChecked)
		{
			// Check that the state can be entered
			STATETREE_TRACE_PHASE_BEGIN(EStateTreeUpdatePhase::EnterConditions);
			const bool bEnterConditionsPassed = TestAllConditions(CurrentParentFrame, CurrentFrame, NextState.EnterConditionsBegin, NextState.EnterConditionsNum);
			STATETREE_TRACE_PHASE_END(EStateTreeUpdatePhase::EnterConditions);

			if (!bEnterConditionsPassed)
			{
				continue;
			}
		}
		
		if (!bIsDestinationState)
		{
			// Next child state is already known. Passing TransitionEvent further so state selected directly by transition can use it.
			if (SelectStateInternal(CurrentParentFrame, CurrentFrame, CurrentFrameInActiveFrames, PathToNextState.Mid(1), OutSelectionResult, TransitionEvent))
			{
				bSucceededToSelectState = true;
				break;
			}
		}
		else if (NextState.Type == EStateTreeStateType::Linked)
		{
			if (NextState.LinkedState.IsValid())
			{
				if (OutSelectionResult.IsFull())
				{
					STATETREE_LOG(Error, TEXT("%hs: Reached max execution depth when trying to select state %s from '%s'.  '%s' using StateTree '%s'."),
						__FUNCTION__, *GetSafeStateName(CurrentFrame, NextStateHandle), *GetStateStatusString(Exec), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
					break;
				}

				FStateTreeExecutionFrame NewFrame;
				NewFrame.StateTree = CurrentFrame.StateTree;
				NewFrame.RootState = NextState.LinkedState;
				NewFrame.ExternalDataBaseIndex = CurrentFrame.ExternalDataBaseIndex;

				// Check and prevent recursion.
				const bool bNewFrameAlreadySelected = OutSelectionResult.GetSelectedFrames().ContainsByPredicate([&NewFrame](const FStateTreeExecutionFrame& Frame) {
					return Frame.IsSameFrame(NewFrame);
				});
				
				if (bNewFrameAlreadySelected)
				{
					STATETREE_LOG(Error, TEXT("%hs: Trying to recursively enter subtree '%s' from '%s'.  '%s' using StateTree '%s'."),
						__FUNCTION__, *GetSafeStateName(NewFrame, NewFrame.RootState), *GetStateStatusString(Exec), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
					break;
				}

				// If the Frame already exists, copy instance indices so that conditions that rely on active states work correctly.
				const FStateTreeExecutionFrame* ExistingFrame = Exec.ActiveFrames.FindByPredicate(
					[StateTree = NewFrame.StateTree, RootState = NewFrame.RootState](const FStateTreeExecutionFrame& Frame)
					{
						return Frame.StateTree == StateTree && Frame.RootState == RootState;
					});
				if (ExistingFrame)
				{
					NewFrame.ActiveInstanceIndexBase = ExistingFrame->ActiveInstanceIndexBase;
					NewFrame.GlobalInstanceIndexBase = ExistingFrame->GlobalInstanceIndexBase;
					NewFrame.StateParameterDataHandle = ExistingFrame->StateParameterDataHandle;
					NewFrame.GlobalParameterDataHandle = ExistingFrame->GlobalParameterDataHandle;
				}
				else
				{
					// Since the StateTree is the same, we can access the global tasks of CurrentFrame, if they are initialized.   
					NewFrame.GlobalParameterDataHandle = CurrentFrame.GlobalParameterDataHandle;
					NewFrame.GlobalInstanceIndexBase = CurrentFrame.GlobalInstanceIndexBase;
					NewFrame.StateParameterDataHandle = NextState.ParameterDataHandle; // Temporary allocated earlier if did not exists.
				}

				OutSelectionResult.PushFrame(NewFrame);

				// If State is linked, proceed to the linked state.
				if (SelectStateInternal(&CurrentFrame, OutSelectionResult.GetSelectedFrames().Last(), ExistingFrame, {NewFrame.RootState}, OutSelectionResult))
				{
					bSucceededToSelectState = true;
					break;
				}
				
				OutSelectionResult.PopFrame();
			}
			else
			{
				STATETREE_LOG(Warning, TEXT("%hs: Trying to enter invalid linked subtree from '%s'.  '%s' using StateTree '%s'."),
					__FUNCTION__, *GetStateStatusString(Exec), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
			}
		}
		else if (NextState.Type == EStateTreeStateType::LinkedAsset)
		{
			if (NextLinkedStateAsset == nullptr)
			{
				break;
			}

			if (OutSelectionResult.IsFull())
			{
				STATETREE_LOG(Error, TEXT("%hs: Reached max execution depth when trying to select state %s from '%s'.  '%s' using StateTree '%s'."),
					__FUNCTION__, *GetSafeStateName(CurrentFrame, NextStateHandle), *GetStateStatusString(Exec), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
				break;
			}

			// The linked state tree should have compatible context requirements.
			if (!NextLinkedStateAsset->HasCompatibleContextData(RootStateTree))
			{
				STATETREE_LOG(Error, TEXT("%hs: The linked State Tree '%s' does not have compatible schema, trying to select state %s from '%s'.  '%s' using StateTree '%s'."),
					__FUNCTION__, *GetFullNameSafe(NextLinkedStateAsset), *GetSafeStateName(CurrentFrame, NextStateHandle), *GetStateStatusString(Exec), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
				break;
			}
				
			FStateTreeExecutionFrame NewFrame;
			NewFrame.StateTree = NextLinkedStateAsset;
			NewFrame.RootState = FStateTreeStateHandle::Root;
			NewFrame.bIsGlobalFrame = true;

			// Check and prevent recursion.
			const bool bNewFrameAlreadySelected = OutSelectionResult.GetSelectedFrames().ContainsByPredicate([&NewFrame](const FStateTreeExecutionFrame& Frame) {
				return Frame.IsSameFrame(NewFrame);
			});
				
			if (bNewFrameAlreadySelected)
			{
				STATETREE_LOG(Error, TEXT("%hs: Trying to recursively enter subtree '%s' from '%s'.  '%s' using StateTree '%s'."),
					__FUNCTION__, *GetSafeStateName(NewFrame, NewFrame.RootState), *GetStateStatusString(Exec), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
				break;
			}

			// If the Frame already exists, copy instance indices so that conditions that rely on active states work correctly.
			const FStateTreeExecutionFrame* ExistingFrame = Exec.ActiveFrames.FindByPredicate(
				[StateTree = NewFrame.StateTree, RootState = NewFrame.RootState](const FStateTreeExecutionFrame& Frame)
				{
					return Frame.StateTree == StateTree && Frame.RootState == RootState;
				});

			bool bStartedTemporaryEvaluatorsAndGlobalTasks = false;
			if (ExistingFrame)
			{
				NewFrame.ActiveInstanceIndexBase = ExistingFrame->ActiveInstanceIndexBase;
				NewFrame.GlobalInstanceIndexBase = ExistingFrame->GlobalInstanceIndexBase;
				NewFrame.StateParameterDataHandle = ExistingFrame->StateParameterDataHandle;
				NewFrame.GlobalParameterDataHandle = ExistingFrame->GlobalParameterDataHandle;
				NewFrame.ExternalDataBaseIndex = ExistingFrame->ExternalDataBaseIndex;
			}
			else
			{
				// Pass the linked state's parameters as global parameters to the linked asset.
				NewFrame.GlobalParameterDataHandle = NextState.ParameterDataHandle;

				// Collect external data if needed
				NewFrame.ExternalDataBaseIndex = CollectExternalData(NewFrame.StateTree);
				if (!NewFrame.ExternalDataBaseIndex.IsValid())
				{
					STATETREE_LOG(VeryVerbose, TEXT("%hs: Cannot select state '%s' because failed to collect external data for nested tree '%s'.  '%s' using StateTree '%s'."),
						__FUNCTION__, *GetSafeStateName(CurrentFrame, NextStateHandle), *GetFullNameSafe(NewFrame.StateTree), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
					break;
				}
					
				// The state parameters will be from the root state.
				const FCompactStateTreeState& RootState = NewFrame.StateTree->States[NewFrame.RootState.Index];
				NewFrame.StateParameterDataHandle = RootState.ParameterDataHandle;

				// Start global tasks and evaluators temporarily, so that their data is available already during select.
				if (StartTemporaryEvaluatorsAndGlobalTasks(nullptr, NewFrame) != EStateTreeRunStatus::Running)
				{
					STATETREE_LOG(VeryVerbose, TEXT("%hs: Cannot select state '%s' because cannot start nested tree's '%s' global tasks and evaluators.  '%s' using StateTree '%s'."),
						__FUNCTION__, *GetSafeStateName(CurrentFrame, NextStateHandle), *GetFullNameSafe(NewFrame.StateTree), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
					
					StopTemporaryEvaluatorsAndGlobalTasks(nullptr, NewFrame);
					break;
				}

				bStartedTemporaryEvaluatorsAndGlobalTasks = true;
			}
				
			OutSelectionResult.PushFrame(NewFrame);

			// If State is linked, proceed to the linked state.
			if (SelectStateInternal(&CurrentFrame, OutSelectionResult.GetSelectedFrames().Last(), ExistingFrame, {NewFrame.RootState}, OutSelectionResult))
			{
				bSucceededToSelectState = true;
				break;
			}

			if (bStartedTemporaryEvaluatorsAndGlobalTasks)
			{
				StopTemporaryEvaluatorsAndGlobalTasks(nullptr, NewFrame);
			}
				
			OutSelectionResult.PopFrame();
		}
		else if (NextState.SelectionBehavior == EStateTreeStateSelectionBehavior::TryEnterState)
		{
			// Select this state.
			STATETREE_TRACE_STATE_EVENT(NextStateHandle, EStateTreeTraceEventType::OnStateSelected);
			bSucceededToSelectState = true;
			break;
		}
		else if (NextState.SelectionBehavior == EStateTreeStateSelectionBehavior::TryFollowTransitions)
		{
			STATETREE_TRACE_SCOPED_STATE_PHASE(NextStateHandle, EStateTreeUpdatePhase::TrySelectBehavior);

			EStateTreeTransitionPriority CurrentPriority = EStateTreeTransitionPriority::None;

			for (uint8 i = 0; i < NextState.TransitionsNum; i++)
			{
				const int16 TransitionIndex = NextState.TransitionsBegin + i;
				const FCompactStateTransition& Transition = RootStateTree.Transitions[TransitionIndex];

				// Skip disabled transitions
				if (Transition.bTransitionEnabled == false)
				{
					continue;
				}

				// No need to test the transition if same or higher priority transition has already been processed.
				if (Transition.Priority <= CurrentPriority)
				{
					continue;
				}

				// Skip completion transitions
				if (EnumHasAnyFlags(Transition.Trigger, EStateTreeTransitionTrigger::OnStateCompleted))
				{
					continue;
				}

				// Cannot follow transitions with delay.
				if (Transition.HasDelay())
				{
					continue;
				}

				// Try to prevent (infinite) loops in the selection.
				if (CurrentFrame.ActiveStates.Contains(Transition.State))
				{
					STATETREE_LOG(Error, TEXT("%hs: Loop detected when trying to select state %s from '%s'. Prior states: %s.  '%s' using StateTree '%s'."),
						__FUNCTION__, *GetSafeStateName(CurrentFrame, NextStateHandle), *GetStateStatusString(Exec), *DebugGetStatePath(OutSelectionResult.GetSelectedFrames(), &CurrentFrame), *GetNameSafe(&Owner), *GetFullNameSafe(CurrentFrame.StateTree));
					continue;
				}

				TArray<const FStateTreeSharedEvent*, TInlineAllocator<FStateTreeEventQueue::MaxActiveEvents>> SelectedStateTransitionEvents;
				if (Transition.Trigger == EStateTreeTransitionTrigger::OnEvent)
				{
					check(Transition.RequiredEvent.IsValid());

					if (StateSelectionEvent)
					{
						SelectedStateTransitionEvents.Emplace(StateSelectionEvent);
					}
					else
					{
						TArrayView<FStateTreeSharedEvent> EventsQueue = GetMutableEventsToProcessView();
						for (FStateTreeSharedEvent& Event : EventsQueue)
						{
							check(Event.IsValid());
							if (Transition.RequiredEvent.DoesEventMatchDesc(*Event))
							{
								SelectedStateTransitionEvents.Emplace(&Event);
							}
						}
					}
				}
				else if (Transition.Trigger == EStateTreeTransitionTrigger::OnTick)
				{
					SelectedStateTransitionEvents.Emplace();
				}

				for (const FStateTreeSharedEvent* SelectedStateTransitionEvent : SelectedStateTransitionEvents)
				{
					bool bTransitionConditionsPassed = false;
					{
						FCurrentlyProcessedTransitionEventScope TransitionEventScope(*this, SelectedStateTransitionEvent ? SelectedStateTransitionEvent->Get() : nullptr);

						STATETREE_TRACE_TRANSITION_EVENT(FStateTreeTransitionSource(FStateTreeIndex16(TransitionIndex), Transition.State, Transition.Priority), EStateTreeTraceEventType::OnEvaluating);
						STATETREE_TRACE_SCOPED_PHASE(EStateTreeUpdatePhase::TransitionConditions);

						bTransitionConditionsPassed = TestAllConditions(CurrentParentFrame, CurrentFrame, Transition.ConditionsBegin, Transition.ConditionsNum);
					}

					if (bTransitionConditionsPassed)
					{
						// Using SelectState() instead of SelectStateInternal to treat the transitions the same way as regular transitions,
						// e.g. it may jump to a completely different branch.
						FStateSelectionResult StateSelectionResult;
						if (SelectState(CurrentFrame, Transition.State, StateSelectionResult, SelectedStateTransitionEvent, Transition.Fallback))
						{
							// Selection succeeded.
							// Cannot break yet because higher priority transitions may override the selection. 
							OutSelectionResult = StateSelectionResult;
							CurrentPriority = Transition.Priority;
							break;
						}
					}
				}
			}

			if (CurrentPriority != EStateTreeTransitionPriority::None)
			{
				bSucceededToSelectState = true;
				break;
			}
		}
		else if (NextState.SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder)
		{
			if (NextState.HasChildren())
			{
				STATETREE_TRACE_SCOPED_STATE_PHASE(NextStateHandle, EStateTreeUpdatePhase::TrySelectBehavior);

				// If the state has children, proceed to select children.
				for (uint16 ChildState = NextState.ChildrenBegin; ChildState < NextState.ChildrenEnd; ChildState = CurrentStateTree->States[ChildState].GetNextSibling())
				{
					if (SelectStateInternal(CurrentParentFrame, CurrentFrame, CurrentFrameInActiveFrames, {FStateTreeStateHandle(ChildState)}, OutSelectionResult))
					{
						// Selection succeeded
						bSucceededToSelectState = true;
						break;
					}
				}

				if (bSucceededToSelectState)
				{
					break;
				}
			}
			else
			{
				// Select this state (For backwards compatibility)
				STATETREE_TRACE_STATE_EVENT(NextStateHandle, EStateTreeTraceEventType::OnStateSelected);
				bSucceededToSelectState = true;
				break;
			}
		}
		else if (NextState.SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom)
		{
			if (NextState.HasChildren())
			{
				STATETREE_TRACE_SCOPED_STATE_PHASE(NextStateHandle, EStateTreeUpdatePhase::TrySelectBehavior);
				
				TArray<uint16, TInlineAllocator<8>> NextLevelChildStates;
				for (uint16 ChildState = NextState.ChildrenBegin; ChildState < NextState.ChildrenEnd; ChildState = CurrentStateTree->States[ChildState].GetNextSibling())
				{
					NextLevelChildStates.Push(ChildState);
				}

				while (!NextLevelChildStates.IsEmpty())
				{
					const int32 ChildStateIndex = Exec.RandomStream.RandRange(0, NextLevelChildStates.Num() - 1);
					if (SelectStateInternal(CurrentParentFrame, CurrentFrame, CurrentFrameInActiveFrames, { FStateTreeStateHandle(ChildStateIndex) }, OutSelectionResult))
					{
						// Selection succeeded
						bSucceededToSelectState = true;
						break;
					}

					constexpr EAllowShrinking AllowShrinking = EAllowShrinking::No;
					NextLevelChildStates.RemoveAtSwap(ChildStateIndex, AllowShrinking);
				}

				if (bSucceededToSelectState)
				{
					break;
				}
			}
			else
			{
				// Select this state (For backwards compatibility)
				STATETREE_TRACE_STATE_EVENT(NextStateHandle, EStateTreeTraceEventType::OnStateSelected);
				bSucceededToSelectState = true;
				break;
			}
		}
		else if (NextState.SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility)
		{
			if (NextState.HasChildren())
			{
				STATETREE_TRACE_SCOPED_STATE_PHASE(NextStateHandle, EStateTreeUpdatePhase::TrySelectBehavior);

				TArray<uint16, TInlineAllocator<8>> NextLevelChildStates;
				for (uint16 ChildState = NextState.ChildrenBegin; ChildState < NextState.ChildrenEnd; ChildState = CurrentStateTree->States[ChildState].GetNextSibling())
				{
					NextLevelChildStates.Push(ChildState);
				}

				while (!NextLevelChildStates.IsEmpty())
				{
					//Find one with highest score in the remaining candidates
					float HighestScore = -std::numeric_limits<float>::infinity();;
					uint16 StateIndexWithHighestScore = FStateTreeStateHandle::InvalidIndex;
					int32 ArrayIndexWithHighestScore = INDEX_NONE;
					for (int32 Index = 0; Index < NextLevelChildStates.Num(); ++Index)
					{
						const uint16 CurrentStateIndex = NextLevelChildStates[Index];
						const FCompactStateTreeState& CurrentState = CurrentStateTree->States[CurrentStateIndex];
						const float Score = EvaluateUtility(CurrentParentFrame, CurrentFrame, CurrentState.UtilityConsiderationsBegin, CurrentState.UtilityConsiderationsNum, CurrentState.Weight);
						if (Score > HighestScore)
						{
							HighestScore = Score;
							StateIndexWithHighestScore = CurrentStateIndex;
							ArrayIndexWithHighestScore = Index;
						}
					}

					if (FStateTreeStateHandle::IsValidIndex(StateIndexWithHighestScore))
					{
						if (SelectStateInternal(CurrentParentFrame, CurrentFrame, CurrentFrameInActiveFrames, { FStateTreeStateHandle(StateIndexWithHighestScore) }, OutSelectionResult))
						{
							// Selection succeeded
							bSucceededToSelectState = true;
							break;
						}
						
						// Disqualify the state we failed to enter
						NextLevelChildStates.RemoveAtSwap(ArrayIndexWithHighestScore, EAllowShrinking::No);
					}
					else
					{
						// No states in array were valid
						break;
					}
				}

				if (bSucceededToSelectState)
				{
					break;
				}
			}
			else
			{
				// Select this state (For backwards compatibility)
				STATETREE_TRACE_STATE_EVENT(NextStateHandle, EStateTreeTraceEventType::OnStateSelected);
				bSucceededToSelectState = true;
				break;
			}
		}
		else if (NextState.SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility)
		{
			if (NextState.HasChildren())
			{
				TArray<TTuple<uint16, float>, TInlineAllocator<8>> NextLevelChildStates;
				float TotalScore = .0f;
				for (uint16 CurrentStateIndex = NextState.ChildrenBegin; CurrentStateIndex < NextState.ChildrenEnd; CurrentStateIndex = CurrentStateTree->States[CurrentStateIndex].GetNextSibling())
				{
					const FCompactStateTreeState& CurrentState = CurrentStateTree->States[CurrentStateIndex];
					const float CurrentStateScore = EvaluateUtility(CurrentParentFrame, CurrentFrame, CurrentState.UtilityConsiderationsBegin, CurrentState.UtilityConsiderationsNum, CurrentState.Weight);
					NextLevelChildStates.Emplace(CurrentStateIndex, CurrentStateScore);
					TotalScore += CurrentStateScore;
				}

				while (!NextLevelChildStates.IsEmpty())
				{
					const float RandomScore = Exec.RandomStream.FRand() * TotalScore;
					float AccumulatedScore = .0f;
					for (int32 Index = 0; Index < NextLevelChildStates.Num(); ++Index)
					{
						const TTuple<uint16, float>& StateScorePair = NextLevelChildStates[Index];
						const uint16 StateIndex = StateScorePair.Key;
						const float StateScore = StateScorePair.Value;
						AccumulatedScore += StateScore;

						if (RandomScore < AccumulatedScore || (Index == (NextLevelChildStates.Num() - 1)))
						{
							// States with zero possibility won't be selected
							if (StateScore != 0.f && SelectStateInternal(CurrentParentFrame, CurrentFrame, CurrentFrameInActiveFrames, { FStateTreeStateHandle(StateIndex) }, OutSelectionResult))
							{
								// Selection succeeded
								bSucceededToSelectState = true;
								break;
							}

							//Disqualify the state we failed to enter, and restart the loop
							TotalScore -= StateScore;
							constexpr EAllowShrinking AllowShrinking = EAllowShrinking::No;
							NextLevelChildStates.RemoveAtSwap(Index, AllowShrinking);

							break;
						}
					}

					if (bSucceededToSelectState)
					{
						break;
					}
				}

				if (bSucceededToSelectState)
				{
					break;
				}
			}
			else
			{
				// Select this state (For backwards compatibility)
				STATETREE_TRACE_STATE_EVENT(NextStateHandle, EStateTreeTraceEventType::OnStateSelected);
				bSucceededToSelectState = true;
				break;
			}
		}
	}

	if (!bSucceededToSelectState)
	{
		// State could not be selected, restore.
		CurrentFrame.NumCurrentlyActiveStates = PrevNumCurrentlyActiveStates;
		CurrentFrame.ActiveStates.Pop();
	}

	return bSucceededToSelectState;
}

FString FStateTreeExecutionContext::GetSafeStateName(const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeStateHandle State) const
{
	if (State == FStateTreeStateHandle::Invalid)
	{
		return TEXT("(State Invalid)");
	}
	else if (State == FStateTreeStateHandle::Succeeded)
	{
		return TEXT("(State Succeeded)");
	}
	else if (State == FStateTreeStateHandle::Failed)
	{
		return TEXT("(State Failed)");
	}
	else if (CurrentFrame.StateTree && CurrentFrame.StateTree->States.IsValidIndex(State.Index))
	{
		return *CurrentFrame.StateTree->States[State.Index].Name.ToString();
	}
	return TEXT("(Unknown)");
}

FString FStateTreeExecutionContext::DebugGetStatePath(TConstArrayView<FStateTreeExecutionFrame> ActiveFrames, const FStateTreeExecutionFrame* CurrentFrame, const int32 ActiveStateIndex) const
{
	FString StatePath;
	const UStateTree* LastStateTree = &RootStateTree;
		
	for (const FStateTreeExecutionFrame& Frame : ActiveFrames)
	{
		if (!ensure(Frame.StateTree))
		{
			return StatePath;
		}

		// If requested up the active state, clamp count.
		int32 Num = Frame.ActiveStates.Num();
		if (CurrentFrame == &Frame && Frame.ActiveStates.IsValidIndex(ActiveStateIndex))
		{
			Num = ActiveStateIndex + 1;
		}

		if (Frame.StateTree != LastStateTree)
		{
			StatePath.Appendf(TEXT("[%s]"), *GetNameSafe(Frame.StateTree));
			LastStateTree = Frame.StateTree;
		}
		
		for (int32 i = 0; i < Num; i++)
		{
			const FCompactStateTreeState& State = Frame.StateTree->States[Frame.ActiveStates[i].Index];
			StatePath.Appendf(TEXT("%s%s"), i == 0 ? TEXT("") : TEXT("."), *State.Name.ToString());
		}
	}
		
	return StatePath;
}

FString FStateTreeExecutionContext::GetStateStatusString(const FStateTreeExecutionState& ExecState) const
{
	if (ExecState.TreeRunStatus != EStateTreeRunStatus::Running)
	{
		return TEXT("--:") + UEnum::GetDisplayValueAsText(ExecState.LastTickStatus).ToString();
	}
	return GetSafeStateName(ExecState.ActiveFrames.Last(), ExecState.ActiveFrames.Last().ActiveStates.Last()) + TEXT(":") + UEnum::GetDisplayValueAsText(ExecState.LastTickStatus).ToString();
}

EStateTreeRunStatus FStateTreeExecutionContext::GetLastTickStatus() const
{
	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return EStateTreeRunStatus::Failed;
	}

	const FStateTreeExecutionState& Exec = GetExecState();
	return Exec.LastTickStatus;
}

void FStateTreeExecutionContext::SetDefaultParameters()
{
	SetGlobalParameters(RootStateTree.GetDefaultParameters());
}

void FStateTreeExecutionContext::SetParameters(const FInstancedPropertyBag& Parameters)
{
	SetGlobalParameters(Parameters);
}

FString FStateTreeExecutionContext::GetInstanceDescription() const
{
	return FString::Printf(TEXT("%s"), *GetNameSafe(&Owner));
}

TConstArrayView<FStateTreeExecutionFrame> FStateTreeExecutionContext::GetActiveFrames() const
{
	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return TConstArrayView<FStateTreeExecutionFrame>();
	}

	const FStateTreeExecutionState& Exec = GetExecState();
	return Exec.ActiveFrames;
}


#if WITH_GAMEPLAY_DEBUGGER

FString FStateTreeExecutionContext::GetDebugInfoString() const
{
	TStringBuilder<2048> DebugString;
	DebugString << TEXT("StateTree (asset: '");
	RootStateTree.GetFullName(DebugString);
	DebugString << TEXT("')");

	if (IsValid())
	{
		const FStateTreeExecutionState& Exec = GetExecState();

		DebugString << TEXT("Status: ");
		DebugString << UEnum::GetDisplayValueAsText(Exec.TreeRunStatus).ToString();
		DebugString << TEXT("\n");

		// Active States
		DebugString << TEXT("Current State:\n");
		for (const FStateTreeExecutionFrame& CurrentFrame : Exec.ActiveFrames)
		{
			const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

			if (CurrentFrame.bIsGlobalFrame)
			{
				DebugString.Appendf(TEXT("\nEvaluators\n  [ %-30s | %8s | %15s ]\n"),
					TEXT("Name"), TEXT("Bindings"), TEXT("Data Handle"));
				for (int32 EvalIndex = CurrentStateTree->EvaluatorsBegin; EvalIndex < (CurrentStateTree->EvaluatorsBegin + CurrentStateTree->EvaluatorsNum); EvalIndex++)
				{
					const FStateTreeEvaluatorBase& Eval = CurrentStateTree->Nodes[EvalIndex].Get<const FStateTreeEvaluatorBase>();
					DebugString.Appendf(TEXT("| %-30s | %8d | %15s |\n"),
						*Eval.Name.ToString(), Eval.BindingsBatch.Get(), *Eval.InstanceDataHandle.Describe());
				}

				DebugString << TEXT("\nGlobal Tasks\n");
				for (int32 TaskIndex = CurrentStateTree->GlobalTasksBegin; TaskIndex < (CurrentStateTree->GlobalTasksBegin + CurrentStateTree->GlobalTasksNum); TaskIndex++)
				{
					const FStateTreeTaskBase& Task = CurrentStateTree->Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
					if (Task.bTaskEnabled)
					{
						FString TempString;
						Task.AppendDebugInfoString(TempString, *this);
						DebugString << TempString;

					}
				}
			}

			for (int32 Index = 0; Index < CurrentFrame.ActiveStates.Num(); Index++)
			{
				FStateTreeStateHandle Handle = CurrentFrame.ActiveStates[Index];
				if (Handle.IsValid())
				{
					const FCompactStateTreeState& State = RootStateTree.States[Handle.Index];
					DebugString << TEXT('[');
					DebugString << State.Name;
					DebugString << TEXT("]\n");

					if (State.TasksNum > 0)
					{
						DebugString += TEXT("\nTasks:\n");
						for (int32 TaskIndex = State.TasksBegin; TaskIndex < (State.TasksBegin + State.TasksNum); TaskIndex++)
						{
							const FStateTreeTaskBase& Task = RootStateTree.Nodes[TaskIndex].Get<const FStateTreeTaskBase>();
							if (Task.bTaskEnabled)
							{
								FString TempString;
								Task.AppendDebugInfoString(TempString, *this);
								DebugString << TempString;
							}
						}
					}
				}
			}
		}
	}
	else
	{
		DebugString << TEXT("StateTree context is not initialized properly.");
	}

	return DebugString.ToString();
}
#endif // WITH_GAMEPLAY_DEBUGGER

#if WITH_STATETREE_DEBUG
void FStateTreeExecutionContext::DebugPrintInternalLayout()
{
	LOG_SCOPE_VERBOSITY_OVERRIDE(LogStateTree, ELogVerbosity::Log);
	UE_LOG(LogStateTree, Log, TEXT("%s"), *RootStateTree.DebugInternalLayoutAsString());
}

int32 FStateTreeExecutionContext::GetStateChangeCount() const
{
	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return 0;
	}

	const FStateTreeExecutionState& Exec = GetExecState();
	return Exec.StateChangeCount;
}

#endif // WITH_STATETREE_DEBUG

FString FStateTreeExecutionContext::GetActiveStateName() const
{
	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return FString();
	}

	const FStateTreeExecutionState& Exec = GetExecState();

	TStringBuilder<1024> FullStateName;
	
	const UStateTree* LastStateTree = &RootStateTree;
	int32 Indent = 0;

	for (int32 FrameIndex = 0; FrameIndex < Exec.ActiveFrames.Num(); FrameIndex++)
	{
		const FStateTreeExecutionFrame& CurrentFrame = Exec.ActiveFrames[FrameIndex];
		const UStateTree* CurrentStateTree = CurrentFrame.StateTree;

		// Append linked state marker at the end of the previous line.
		if (Indent > 0)
		{
			FullStateName << TEXT(" >");
		}
		// If tree has changed, append that too.
		if (CurrentFrame.StateTree != LastStateTree)
		{
			FullStateName << TEXT(" [");
			FullStateName << CurrentFrame.StateTree.GetFName();
			FullStateName << TEXT(']');

			LastStateTree = CurrentFrame.StateTree;
		}

		for (int32 Index = 0; Index < CurrentFrame.ActiveStates.Num(); Index++)
		{
			const FStateTreeStateHandle Handle = CurrentFrame.ActiveStates[Index];
			if (Handle.IsValid())
			{
				const FCompactStateTreeState& State = CurrentStateTree->States[Handle.Index];
				if (Indent > 0)
				{
					FullStateName += TEXT("\n");
				}
				FullStateName.Appendf(TEXT("%*s-"), Indent * 3, TEXT("")); // Indent
				FullStateName << State.Name;
				Indent++;
			}
		}
	}

	switch (Exec.TreeRunStatus)
	{
	case EStateTreeRunStatus::Failed:
		FullStateName << TEXT(" FAILED\n");
		break;
	case EStateTreeRunStatus::Succeeded:
		FullStateName << TEXT(" SUCCEEDED\n");
		break;
	case EStateTreeRunStatus::Running:
		// Empty
		break;
	default:
		FullStateName << TEXT("--\n");
	}

	return FullStateName.ToString();
}

TArray<FName> FStateTreeExecutionContext::GetActiveStateNames() const
{
	if (!IsValid())
	{
		STATETREE_LOG(Warning, TEXT("%hs: StateTree context is not initialized properly ('%s' using StateTree '%s')"),
			__FUNCTION__, *GetNameSafe(&Owner), *GetFullNameSafe(&RootStateTree));
		return TArray<FName>();
	}

	TArray<FName> Result;
	const FStateTreeExecutionState& Exec = GetExecState();

	// Active States
	for (const FStateTreeExecutionFrame& CurrentFrame : Exec.ActiveFrames)
	{
		const UStateTree* CurrentStateTree = CurrentFrame.StateTree;
		for (int32 Index = 0; Index < CurrentFrame.ActiveStates.Num(); Index++)
		{
			const FStateTreeStateHandle Handle = CurrentFrame.ActiveStates[Index];
			if (Handle.IsValid())
			{
				const FCompactStateTreeState& State = CurrentStateTree->States[Handle.Index];
				Result.Add(State.Name);
			}
		}
	}

	return Result;
}

#undef STATETREE_LOG
#undef STATETREE_CLOG
