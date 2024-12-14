// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTree.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeNodeBase.h"
#include "Experimental/ConcurrentLinearAllocator.h"
#include "Templates/IsInvocable.h"

struct FGameplayTag;
struct FInstancedPropertyBag;
struct FStateTreeExecutionContext;
struct FStateTreeReferenceOverrides;
struct FStateTreeEvaluatorBase;
struct FStateTreeTaskBase;
struct FStateTreeConditionBase;
struct FStateTreeEvent;
struct FStateTreeTransitionRequest;
struct FStateTreeInstanceDebugId;
struct FStateTreeReference;

/**
 * Delegate used by the execution context to collect external data views for a given StateTree asset.
 * The caller is expected to iterate over the ExternalDataDescs array, find the matching external data,
 * and store it in the OutDataViews at the same index:
 *
 *	for (int32 Index = 0; Index < ExternalDataDescs.Num(); Index++)
 *	{
 *		const FStateTreeExternalDataDesc& Desc = ExternalDataDescs[Index];
 *		// Find data requested by Desc
 *		OutDataViews[Index] = ...;
 *	}
 */
DECLARE_DELEGATE_RetVal_FourParams(bool, FOnCollectStateTreeExternalData, const FStateTreeExecutionContext& /*Context*/, const UStateTree* /*StateTree*/, TArrayView<const FStateTreeExternalDataDesc> /*ExternalDataDescs*/, TArrayView<FStateTreeDataView> /*OutDataViews*/);

/**
 * StateTree Execution Context is a helper that is used to update StateTree instance data.
 *
 * The context is meant to be temporary, you should not store a context across multiple frames.
 *
 * The owner is used as the owner of the instantiated UObjects in the instance data and logging,
 * it should have same or greater lifetime as the InstanceData. 
 *
 * In common case you can use the constructor to initialize the context, and us a helper struct
 * to set up the context data and external data getter:
 *
 *		FStateTreeExecutionContext Context(*GetOwner(), *StateTreeRef.GetStateTree(), InstanceData);
 *		if (SetContextRequirements(Context))
 *		{
 *			Context.Tick(DeltaTime);
 * 		}
 *
 * 
 *		bool UMyComponent::SetContextRequirements(FStateTreeExecutionContext& Context)
 *		{
 *			if (!Context.IsValid())
 *			{
 *				return false;
 *			}
 *			// Setup context data
 *			Context.SetContextDataByName(...);
 *			...
 *
 *			Context.SetCollectExternalDataCallback(FOnCollectStateTreeExternalData::CreateUObject(this, &UMyComponent::CollectExternalData);
 *
 *			return Context.AreContextDataViewsValid();
 *		}
 *
 *		bool UMyComponent::CollectExternalData(const FStateTreeExecutionContext& Context, const UStateTree* StateTree, TArrayView<const FStateTreeExternalDataDesc> ExternalDataDescs, TArrayView<FStateTreeDataView> OutDataViews)
 *		{
 *			...
 *			for (int32 Index = 0; Index < ExternalDataDescs.Num(); Index++)
 *			{
 *				const FStateTreeExternalDataDesc& Desc = ExternalDataDescs[Index];
 *				if (Desc.Struct->IsChildOf(UWorldSubsystem::StaticClass()))
 *				{
 *					UWorldSubsystem* Subsystem = World->GetSubsystemBase(Cast<UClass>(const_cast<UStruct*>(Desc.Struct.Get())));
 *					OutDataViews[Index] = FStateTreeDataView(Subsystem);
 *				}
 *				...
 *			}
 *			return true;
 *		}
 *
 * In this example the SetContextRequirements() method is used to set the context defined in the schema,
 * and the delegate FOnCollectStateTreeExternalData is used to query the external data required by the tasks and conditions.
 *
 * In case the State Tree links to other state tree assets, the collect external data might get called
 * multiple times, once for each asset.
 */
struct STATETREEMODULE_API FStateTreeExecutionContext
{
public:
	FStateTreeExecutionContext(UObject& InOwner, const UStateTree& InStateTree, FStateTreeInstanceData& InInstanceData, const FOnCollectStateTreeExternalData& CollectExternalDataCallback = {}, const EStateTreeRecordTransitions RecordTransitions = EStateTreeRecordTransitions::No);
	/** Construct an execution context from a parent context and another tree. Useful to run a subtree from the parent context with the same schema. */
	FStateTreeExecutionContext(const FStateTreeExecutionContext& InContextToCopy, const UStateTree& InStateTree, FStateTreeInstanceData& InInstanceData);
	virtual ~FStateTreeExecutionContext();

	/** Updates data view of the parameters by using the default values defined in the StateTree asset. */
	UE_DEPRECATED(5.4, "Not providing parameters to Start() leads to setting up default values now.")
	void SetDefaultParameters();

	/**
	 * Updates data view of the parameters by replacing the default values defined in the StateTree asset by the provided values.
	 * Note: caller is responsible to make sure external parameters lifetime matches the context.
	 */
	UE_DEPRECATED(5.4, "Provide parameters through Start() instead.")
	void SetParameters(const FInstancedPropertyBag& Parameters);

	/** Sets callback used to collect external data views during State Tree execution. */
	void SetCollectExternalDataCallback(const FOnCollectStateTreeExternalData& Callback);

	/** */
	void SetLinkedStateTreeOverrides(const FStateTreeReferenceOverrides* InLinkedStateTreeOverrides);

	const FStateTreeReference* GetLinkedStateTreeOverrideForTag(const FGameplayTag StateTag) const;
	
	/** @return the StateTree asset in use. */
	const UStateTree* GetStateTree() const { return &RootStateTree; }

	/** @return const references to the instance data in use, or nullptr if the context is not valid. */
	const FStateTreeInstanceData* GetInstanceData() const { return &InstanceData; }

	/** @return mutable references to the instance data in use, or nullptr if the context is not valid. */
	FStateTreeInstanceData* GetMutableInstanceData() const { return &InstanceData; }

	/** @return mutable references to the instance data in use. */
	const FStateTreeEventQueue& GetEventQueue() const { return InstanceData.GetEventQueue(); }

	/** @return mutable references to the instance data in use. */
	FStateTreeEventQueue& GetMutableEventQueue() const { return InstanceData.GetMutableEventQueue(); }

	/** @return The owner of the context */
	UObject* GetOwner() const { return &Owner; }
	/** @return The world of the owner or nullptr if the owner is not set. */ 
	UWorld* GetWorld() const { return Owner.GetWorld(); };

	/** @return True of the the execution context is valid and initialized. */ 
	bool IsValid() const { return RootStateTree.IsReadyToRun(); }
	
	/**
	 * Start executing.
	 * @param InitialParameters Optional override of parameters initial values
	 * @param RandomSeed Optional override of initial seed for RandomStream. By default FPlatformTime::Cycles() will be used.
	 * @return Tree execution status after the start.
	 */
	EStateTreeRunStatus Start(const FInstancedPropertyBag* InitialParameters = nullptr, int32 RandomSeed = -1);
	
	/**
	 * Stop executing if the tree is running.
	 * @param CompletionStatus Status (and terminal state) reported in the transition when the tree is stopped.
	 * @return Tree execution status at stop, can be CompletionStatus, or earlier status if the tree is not running. 
	 */
	EStateTreeRunStatus Stop(const EStateTreeRunStatus CompletionStatus = EStateTreeRunStatus::Stopped);

	/**
	 * Tick the state tree logic, updates the tasks and triggers transitions.
	 * @param DeltaTime time to advance the logic.
	 * @returns tree run status after the tick.
	 */
	EStateTreeRunStatus Tick(const float DeltaTime);

	/**
	 * Tick the state tree logic partially, updates the tasks.
	 * For full update TickTriggerTransitions() should be called after.
	 * @param DeltaTime time to advance the logic.
	 * @returns tree run status after the partial tick.
	 */
	EStateTreeRunStatus TickUpdateTasks(const float DeltaTime);
	
	/**
	 * Tick the state tree logic partially, triggers the transitions.
	 * For full update TickUpdateTasks() should be called before.
	 * @returns tree run status after the partial tick.
	 */
	EStateTreeRunStatus TickTriggerTransitions();
	
	/** @return the tree run status. */
	EStateTreeRunStatus GetStateTreeRunStatus() const;

	/** @return the status of the last tick function */
	EStateTreeRunStatus GetLastTickStatus() const;

	/** @return reference to the list of currently active states. */
	UE_DEPRECATED(5.4, "Use GetActiveFrames() instead.")
	const FStateTreeActiveStates& GetActiveStates() const
	{
		static FStateTreeActiveStates Dummy;
		return Dummy;
	}

	/** @return reference to the list of currently active frames and states. */
	TConstArrayView<FStateTreeExecutionFrame> GetActiveFrames() const;

#if WITH_GAMEPLAY_DEBUGGER
	/** @return Debug string describing the current state of the execution */
	FString GetDebugInfoString() const;
#endif // WITH_GAMEPLAY_DEBUGGER

#if WITH_STATETREE_DEBUG
	int32 GetStateChangeCount() const;

	void DebugPrintInternalLayout();
#endif

	/** @return the name of the active state. */
	FString GetActiveStateName() const;
	
	/** @return the names of all the active state. */
	TArray<FName> GetActiveStateNames() const;

	/** Sends event for the StateTree. */
	void SendEvent(const FGameplayTag Tag, const FConstStructView Payload = FConstStructView(), const FName Origin = FName()) const;

	/**
	 * Iterates over all events.
	 * @param Function a lambda which takes const FStateTreeSharedEvent& Event, and returns EStateTreeLoopEvents.
	 */
	template<typename TFunc>
	typename TEnableIf<TIsInvocable<TFunc, FStateTreeSharedEvent>::Value, void>::Type ForEachEvent(TFunc&& Function) const
	{
		if (!EventQueue)
		{
			return;
		}
		EventQueue->template ForEachEvent(Function);
	}

	/**
	 * Iterates over all events.
	 * @param Function a lambda which takes const FStateTreeSharedEvent& Event, and returns EStateTreeLoopEvents.
	 * Less preferable than FStateTreeSharedEvent version.
	 */
	template<typename TFunc>
	typename TEnableIf<TIsInvocable<TFunc, FStateTreeEvent>::Value, void>::Type ForEachEvent(TFunc&& Function) const
	{
		if (!EventQueue)
		{
			return;
		}
		EventQueue->template ForEachEvent([Function](const FStateTreeSharedEvent& Event)
		{
			return Function(*Event);
		});
	}

	/** @return events to process this tick. */
	TArrayView<FStateTreeSharedEvent> GetMutableEventsToProcessView()
	{
		return EventQueue ? EventQueue->GetMutableEventsView() : TArrayView<FStateTreeSharedEvent>();
	}

	/** @return events to process this tick. */
	TConstArrayView<FStateTreeSharedEvent> GetEventsToProcessView() const
	{
		return EventQueue ? EventQueue->GetMutableEventsView() : TArrayView<FStateTreeSharedEvent>();
	}

	/** Consumes and removes the specified event from the event queue. */
	void ConsumeEvent(const FStateTreeSharedEvent& Event)
	{
		if (EventQueue)
		{
			EventQueue->ConsumeEvent(Event);
		}
	}

	UE_DEPRECATED(5.5, "Use GetEventsToProcessView() instead.")
	TConstArrayView<FStateTreeEvent> GetEventsToProcess() const { return {}; }

	/** @return true if there is a pending event with specified tag. */
	bool HasEventToProcess(const FGameplayTag Tag) const
	{
		return GetEventsToProcessView().ContainsByPredicate([Tag](const FStateTreeSharedEvent& Event)
		{
			check(Event.IsValid());
			return Event->Tag.MatchesTag(Tag);
		});
	}

	/** @return the currently processed state if applicable. */
	FStateTreeStateHandle GetCurrentlyProcessedState() const { return CurrentlyProcessedState; }

	/** @return the currently processed execution frame if applicable. */
	const FStateTreeExecutionFrame* GetCurrentlyProcessedFrame() const { return CurrentlyProcessedFrame; }

	/** @return the currently processed execution parent frame if applicable. */
	const FStateTreeExecutionFrame* GetCurrentlyProcessedParentFrame() const { return CurrentlyProcessedParentFrame; }
	
	/** @return Pointer to a State or null if state not found */ 
	const FCompactStateTreeState* GetStateFromHandle(const FStateTreeStateHandle StateHandle) const
	{
		return RootStateTree.GetStateFromHandle(StateHandle);
	}

	/** @return Array view to external data descriptors associated with this context. Note: Init() must be called before calling this method. */
	UE_DEPRECATED(5.4, "Use CollectStateTreeExternalData delegate instead.")
	TConstArrayView<FStateTreeExternalDataDesc> GetExternalDataDescs() const
	{
		return RootStateTree.ExternalDataDescs;
	}

	/** @return Array view to named external data descriptors associated with this context. Note: Init() must be called before calling this method. */
	TConstArrayView<FStateTreeExternalDataDesc> GetContextDataDescs() const
	{
		return RootStateTree.GetContextDataDescs();
	}

	/** @return Handle to external data of type InStruct, or invalid handle if struct not found. */
	UE_DEPRECATED(5.4, "Not supported anymore.")
	FStateTreeExternalDataHandle GetExternalDataHandleByStruct(const UStruct* InStruct) const
	{
		const FStateTreeExternalDataDesc* DataDesc = RootStateTree.ExternalDataDescs.FindByPredicate([InStruct](const FStateTreeExternalDataDesc& Item) { return Item.Struct == InStruct; });
		return DataDesc != nullptr ? DataDesc->Handle : FStateTreeExternalDataHandle::Invalid;
	}

	/** Sets context data view value for specific item. */
	void SetContextData(const FStateTreeExternalDataHandle Handle, FStateTreeDataView DataView)
	{
		check(Handle.IsValid());
		check(Handle.DataHandle.GetSource() == EStateTreeDataSourceType::ContextData);
		ContextAndExternalDataViews[Handle.DataHandle.GetIndex()] = DataView;
	}

	/** Sets the context data based on name (name is defined in the schema), returns true if data was found */
	bool SetContextDataByName(const FName Name, FStateTreeDataView DataView);

	/** @return True if all context data pointers are set. */ 
	bool AreContextDataViewsValid() const;

	/** @return True if all required external data pointers are set. */ 
	UE_DEPRECATED(5.4, "Please AreContextDataViewsValid().")
	bool AreExternalDataViewsValid() const
	{
		return AreContextDataViewsValid();
	}

	/** Sets external data view value for specific item. */
	UE_DEPRECATED(5.4, "Use SetContextData() for context data, or set SetExternalDataDelegate() to provide external data.")
	void SetExternalData(const FStateTreeExternalDataHandle Handle, FStateTreeDataView DataView)
	{
		SetContextData(Handle, DataView);
	}

	/**
	 * Returns reference to external data based on provided handle. The return type is deduced from the handle's template type.
     * @param Handle Valid TStateTreeExternalDataHandle<> handle. 
	 * @return reference to external data based on handle or null if data is not set.
	 */ 
	template <typename T>
	typename T::DataType& GetExternalData(const T Handle) const
	{
		check(Handle.IsValid());
		check(Handle.DataHandle.GetSource() == EStateTreeDataSourceType::ExternalData);
		check(CurrentlyProcessedFrame);
		check(CurrentlyProcessedFrame->StateTree->ExternalDataDescs[Handle.DataHandle.GetIndex()].Requirement != EStateTreeExternalDataRequirement::Optional); // Optionals should query pointer instead.
		return ContextAndExternalDataViews[CurrentlyProcessedFrame->ExternalDataBaseIndex.Get() + Handle.DataHandle.GetIndex()].template GetMutable<typename T::DataType>();
	}

	/**
	 * Returns pointer to external data based on provided item handle. The return type is deduced from the handle's template type.
     * @param Handle Valid TStateTreeExternalDataHandle<> handle.
	 * @return pointer to external data based on handle or null if item is not set or handle is invalid.
	 */ 
	template <typename T>
	typename T::DataType* GetExternalDataPtr(const T Handle) const
	{
		if (Handle.IsValid())
		{
			check(CurrentlyProcessedFrame);
			check(Handle.DataHandle.GetSource() == EStateTreeDataSourceType::ExternalData);
			return ContextAndExternalDataViews[CurrentlyProcessedFrame->ExternalDataBaseIndex.Get() + Handle.DataHandle.GetIndex()].template GetMutablePtr<typename T::DataType>();
		}
		return nullptr;
	}

	FStateTreeDataView GetExternalDataView(const FStateTreeExternalDataHandle Handle)
	{
		if (Handle.IsValid())
		{
			check(CurrentlyProcessedFrame);
			check(Handle.DataHandle.GetSource() == EStateTreeDataSourceType::ExternalData);
			return ContextAndExternalDataViews[CurrentlyProcessedFrame->ExternalDataBaseIndex.Get() + Handle.DataHandle.GetIndex()];
		}
		return FStateTreeDataView();
	}

	/** @returns pointer to the instance data of specified node. */
	template <typename T>
	T* GetInstanceDataPtr(const FStateTreeNodeBase& Node) const
	{
		check(CurrentNodeDataHandle == Node.InstanceDataHandle);
		return CurrentNodeInstanceData.template GetMutablePtr<T>();
	}

	/** @returns reference to the instance data of specified node. */
	template <typename T>
	T& GetInstanceData(const FStateTreeNodeBase& Node) const
	{
		check(CurrentNodeDataHandle == Node.InstanceDataHandle);
		return CurrentNodeInstanceData.template GetMutable<T>();
	}

	/** @returns reference to the instance data of specified node. Infers the instance data type from the node's FInstanceDataType. */
	template <typename T>
	typename T::FInstanceDataType& GetInstanceData(const T& Node) const
	{
		static_assert(TIsDerivedFrom<T, FStateTreeNodeBase>::IsDerived, "Expecting Node to derive from FStateTreeNodeBase.");
		check(CurrentNodeDataHandle == Node.InstanceDataHandle);
		return CurrentNodeInstanceData.template GetMutable<typename T::FInstanceDataType>();
	}

	/** @returns reference to instance data struct that can be passed to lambdas. See TStateTreeInstanceDataStructRef for usage. */
	template <typename T>
	TStateTreeInstanceDataStructRef<typename T::FInstanceDataType> GetInstanceDataStructRef(const T& Node) const
	{
		static_assert(TIsDerivedFrom<T, FStateTreeNodeBase>::IsDerived, "Expecting Node to derive from FStateTreeNodeBase.");
		check(CurrentlyProcessedFrame);
		return TStateTreeInstanceDataStructRef<typename T::FInstanceDataType>(InstanceData, *CurrentlyProcessedFrame, Node.InstanceDataHandle);
	}

	/**
	 * Requests transition to a state.
	 * If called during during transition processing (e.g. from FStateTreeTaskBase::TriggerTransitions()) the transition
	 * is attempted to be activate immediately (it can fail e.g. because of preconditions on a target state).
	 * If called outside the transition handling, the request is buffered and handled at the beginning of next transition processing.
	 * @param Request The state to transition to.
	 */
	void RequestTransition(const FStateTreeTransitionRequest& Request);

	/** @return data view of the specified handle relative to given frame. */
	static FStateTreeDataView GetDataViewFromInstanceStorage(FStateTreeInstanceStorage& InstanceDataStorage, FStateTreeInstanceStorage* CurrentlyProcessedSharedInstanceStorage, const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataHandle Handle);

	/** Looks for a frame in provided list of frames. */
	static const FStateTreeExecutionFrame* FindFrame(const UStateTree* StateTree, FStateTreeStateHandle RootState, TConstArrayView<FStateTreeExecutionFrame> Frames, const FStateTreeExecutionFrame*& OutParentFrame);

	UE_DEPRECATED(5.5, "Use GetDataViewFromInstanceStorage instead.")
	static FStateTreeDataView GetDataView(FStateTreeInstanceStorage& InstanceDataStorage, FStateTreeInstanceStorage* CurrentlyProcessedSharedInstanceStorage, const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, TConstArrayView<FStateTreeDataView> ContextAndExternalDataViews, const FStateTreeDataHandle Handle)
	{
		return GetDataViewFromInstanceStorage(InstanceDataStorage, CurrentlyProcessedSharedInstanceStorage, ParentFrame, CurrentFrame, Handle);
	}

	/**
	* Forces transition to a state from a previously recorded state tree transition result.
	* Primarily used for replication purposes so that a client state tree stay in sync with its server counterpart.
	* @param Recorded state transition to run on the state tree.
	* @return The new run status for the state tree.
	*/
	EStateTreeRunStatus ForceTransition(const FRecordedStateTreeTransitionResult& Transition);

	/** Returns the recorded transitions for this context. */
	TConstArrayView<FRecordedStateTreeTransitionResult> GetRecordedTransitions() const { return RecordedTransitions; }

protected:
	/**
	 * Describes a result of States Selection.
	 */
	struct FStateSelectionResult
	{
		/** Max number of execution frames handled during state selection. */
		static constexpr int32 MaxExecutionFrames = 8;

		FStateSelectionResult() = default;
		explicit FStateSelectionResult(TConstArrayView<FStateTreeExecutionFrame> InFrames)
		{
			SelectedFrames = InFrames;
			FramesStateSelectionEvents.SetNum(SelectedFrames.Num());
		}

		bool IsFull() const
		{
			return SelectedFrames.Num() == MaxExecutionFrames;
		}

		void PushFrame(FStateTreeExecutionFrame Frame)
		{
			SelectedFrames.Add(Frame);
			FramesStateSelectionEvents.AddDefaulted();
		}

		void PopFrame()
		{
			SelectedFrames.Pop();
			FramesStateSelectionEvents.Pop();
		}

		bool ContainsFrames() const
		{
			return !SelectedFrames.IsEmpty();
		}

		int32 FramesNum() const
		{
			return SelectedFrames.Num();
		}

		TArrayView<FStateTreeExecutionFrame> GetSelectedFrames()
		{
			return SelectedFrames;
		}

		TArrayView<FStateTreeFrameStateSelectionEvents> GetFramesStateSelectionEvents()
		{
			return FramesStateSelectionEvents;
		}

	protected:
		TArray<FStateTreeExecutionFrame, TFixedAllocator<MaxExecutionFrames>> SelectedFrames;
		TArray<FStateTreeFrameStateSelectionEvents, TFixedAllocator<MaxExecutionFrames>> FramesStateSelectionEvents;
	};
	
#if WITH_STATETREE_TRACE
	FStateTreeInstanceDebugId GetInstanceDebugId() const;
#endif // WITH_STATETREE_TRACE

	/** @return Prefix that will be used by STATETREE_LOG and STATETREE_CLOG, Owner name by default. */
	virtual FString GetInstanceDescription() const;

	/** Callback when delayed transition is triggered. Contexts that are event based can use this to trigger a future event. */
	virtual void BeginDelayedTransition(const FStateTreeTransitionDelayedState& DelayedState) {};

	void UpdateInstanceData(TConstArrayView<FStateTreeExecutionFrame> CurrentActiveFrames, TArrayView<FStateTreeExecutionFrame> NextActiveFrames);

	/**
	 * Handles logic for entering State. EnterState is called on new active Evaluators and Tasks that are part of the re-planned tree.
	 * Re-planned tree is from the transition target up to the leaf state. States that are parent to the transition target state
	 * and still active after the transition will remain intact.
	 * @return Run status returned by the tasks.
	 */
	EStateTreeRunStatus EnterState(FStateTreeTransitionResult& Transition);

	/**
	 * Handles logic for exiting State. ExitState is called on current active Evaluators and Tasks that are part of the re-planned tree.
	 * Re-planned tree is from the transition target up to the leaf state. States that are parent to the transition target state
	 * and still active after the transition will remain intact.
	 */
	void ExitState(const FStateTreeTransitionResult& Transition);

	/**
	 * Handles logic for signalling State completed. StateCompleted is called on current active Evaluators and Tasks in reverse order (from leaf to root).
	 */
	void StateCompleted();

	/**
	 * Tick evaluators and global tasks by delta time.
	 */
	EStateTreeRunStatus TickEvaluatorsAndGlobalTasks(const float DeltaTime, bool bTickGlobalTasks = true);

	/**
	 * Starts evaluators and global tasks.
	 * @return run status returned by the global tasks.
	 */
	EStateTreeRunStatus StartEvaluatorsAndGlobalTasks(FStateTreeIndex16& OutLastInitializedTaskIndex);

	/**
	 * Stops evaluators and global tasks.
	 */
	void StopEvaluatorsAndGlobalTasks(const EStateTreeRunStatus CompletionStatus, const FStateTreeIndex16 LastInitializedTaskIndex = FStateTreeIndex16());

	/**
	 * Stops evaluators and global tasks of the given frame. Expects nodes data to be already bound.
	 */
	void CallStopOnEvaluatorsAndGlobalTasks(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& Frame, const FStateTreeTransitionResult& Transition, const FStateTreeIndex16 LastInitializedTaskIndex = FStateTreeIndex16());

	/** Starts temporary instances of global evaluators and tasks for a given frame. */
	EStateTreeRunStatus StartTemporaryEvaluatorsAndGlobalTasks(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame);

	/** Stops temporary global evaluators and tasks for the provided frame. */
	void StopTemporaryEvaluatorsAndGlobalTasks(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame);

	/**
	 * Ticks tasks of all active states starting from current state by delta time.
	 * @return Run status returned by the tasks.
	 */
	EStateTreeRunStatus TickTasks(const float DeltaTime);

	/** Common functionality shared by the tick methods. */
	EStateTreeRunStatus TickPrelude();
	EStateTreeRunStatus TickPostlude();

	/** Handles task ticking part of the tick. */
	void TickUpdateTasksInternal(const float DeltaTime);
	
	/** Handles transition triggering part of the tick. */
	void TickTriggerTransitionsInternal();

	/**
	 * Checks all conditions at given range
	 * @return True if all conditions pass.
	 */
	bool TestAllConditions(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const int32 ConditionsOffset, const int32 ConditionsNum);

	/**
	 * Calculate the final score of all considerations at given range
	 * @return the final score
	 */
	float EvaluateUtility(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const int32 ConsiderationsOffset, const int32 ConsiderationsNum, const float StateWeight);

	/* Evaluate all function at given range. Should be used only on active instances, assumes valid handles and does not consider temporary instances. */
	void EvaluatePropertyFunctionsOnActiveInstances(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame, FStateTreeIndex16 FuncsBegin, uint16 FuncsNum);


	/* Evaluate all function at given range. This version validates the data handles and looks up temporary instances. */
	void EvaluatePropertyFunctionsWithValidation(const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame, FStateTreeIndex16 FuncsBegin, uint16 FuncsNum);

	/**
	 * Requests transition to a specified state with specified priority.
	 */
	bool RequestTransition(
		const FStateTreeExecutionFrame& CurrentFrame,
		const FStateTreeStateHandle NextState,
		const EStateTreeTransitionPriority Priority,
		const FStateTreeSharedEvent* TransitionEvent = nullptr,
		const EStateTreeSelectionFallback Fallback = EStateTreeSelectionFallback::None);

	/**
	 * Sets up NextTransition based on the provided parameters and the current execution status. 
	 */
	void SetupNextTransition(const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeStateHandle NextState, const EStateTreeTransitionPriority Priority);

	/**
	 * Triggers transitions based on current run status. CurrentStatus is used to select which transitions events are triggered.
	 * If CurrentStatus is "Running", "Conditional" transitions pass, "Completed/Failed" will trigger "OnCompleted/OnSucceeded/OnFailed" transitions.
	 * Transition target state can point to a selector state. For that reason the result contains both the target state, as well ass
	 * the actual next state returned by the selector.
	 * @return Transition result describing the source state, state transitioned to, and next selected state.
	 */
	bool TriggerTransitions();

	/**
	 * Runs state selection logic starting at the specified state, walking towards the leaf states.
	 * If a state cannot be selected, false is returned. 
	 * If NextState is a selector state, SelectStateInternal is called recursively (depth-first) to all child states (where NextState will be one of child states).
	 * If NextState is a leaf state, the active states leading from root to the leaf are returned.
	 * @param CurrentFrame The frame where the NextState is valid. 
	 * @param NextState The state which we try to select next.
	 * @param OutSelectionResult The result of selection.
	 * @param TransitionEvent Event used by transition to trigger the state selection.
	 * @param Fallback selection behavior to execute if it fails to select the desired state
	 * @return True if succeeded to select new active states.
	 */
	bool SelectState(
		const FStateTreeExecutionFrame& CurrentFrame,
		const FStateTreeStateHandle NextState,
		FStateSelectionResult& OutSelectionResult,
		const FStateTreeSharedEvent* TransitionEvent = nullptr,
		const EStateTreeSelectionFallback Fallback = EStateTreeSelectionFallback::None);

	/**
	 * Used internally to do the recursive part of the SelectState().
	 */
	bool SelectStateInternal(
		const FStateTreeExecutionFrame* CurrentParentFrame,
		FStateTreeExecutionFrame& CurrentFrame,
		const FStateTreeExecutionFrame* CurrentFrameInActiveFrames,
		TConstArrayView<FStateTreeStateHandle> PathToNextState,
		FStateSelectionResult& OutSelectionResult,
		const FStateTreeSharedEvent* TransitionEvent = nullptr);

	/** @return StateTree execution state from the instance storage. */
	FStateTreeExecutionState& GetExecState()
	{
		check(InstanceDataStorage);
		return InstanceDataStorage->GetMutableExecutionState();
	}

	/** @return const StateTree execution state from the instance storage. */
	const FStateTreeExecutionState& GetExecState() const
	{
		check(InstanceDataStorage);
		return InstanceDataStorage->GetExecutionState();
	}

	/** @return String describing state status for logging and debug. */
	FString GetStateStatusString(const FStateTreeExecutionState& ExecState) const;

	/** @return String describing state name for logging and debug. */
	FString GetSafeStateName(const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeStateHandle State) const;

	/** @return String describing full path of an activate state for logging and debug. */
	FString DebugGetStatePath(TConstArrayView<FStateTreeExecutionFrame> ActiveFrames, const FStateTreeExecutionFrame* CurrentFrame = nullptr, const int32 ActiveStateIndex = INDEX_NONE) const;

	/** @return String describing all events that are currently being processed  for logging and debug. */
	FString DebugGetEventsAsString() const;

	/** @return data view of the specified handle relative to given frame. */
	FStateTreeDataView GetDataView(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataHandle Handle);

	/** @return true if handle source is valid cified handle relative to given frame. */
	bool IsHandleSourceValid(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataHandle Handle) const;

	/** @return data view of the specified handle relative to the given frame, or tries to find a matching temporary instance. */
	FStateTreeDataView GetDataViewOrTemporary(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataHandle Handle);

	/**
	 * Adds a temporary instance that can be located using frame and data handle later.
	 * @returns view to the newly added instance. If NewInstanceData is Object wrapper, the new object is returned.
	 */
	FStateTreeDataView AddTemporaryInstance(const FStateTreeExecutionFrame& Frame, const FStateTreeIndex16 OwnerNodeIndex, const FStateTreeDataHandle DataHandle, FConstStructView NewInstanceData);

	/** Copies a batch of properties to the data in TargetView. Should be used only on active instances, assumes valid handles and does not consider temporary instances. */
	bool CopyBatchOnActiveInstances(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataView TargetView, const FStateTreeIndex16 BindingsBatch);

	/** Copies a batch of properties to the data in TargetView. This version validates the data handles and looks up temporary instances. */
	bool CopyBatchWithValidation(const FStateTreeExecutionFrame* ParentFrame, const FStateTreeExecutionFrame& CurrentFrame, const FStateTreeDataView TargetView, const FStateTreeIndex16 BindingsBatch);

	/**
	 * Collects external data for all StateTrees in active frames.
	 * @returns true if all external data are set successfully. */
	bool CollectActiveExternalData();

	/**
	 * Collects external data for specific State Tree asset. If the data is already collected, cached index is returned.
	 * @returns index in ContextAndExternalDataViews for the first external data.
	 */
	FStateTreeIndex16 CollectExternalData(const UStateTree* StateTree);

	/**
	 * Stores copy of provided parameters as State Tree global parameters.
	 * @param Parameters parameters to copy
	 * @returns true if successfully set the parameters
	 */
	bool SetGlobalParameters(const FInstancedPropertyBag& Parameters);

	/**
	 * Captures StateTree events used during the state selection.
	 */
	void CaptureNewStateEvents(TConstArrayView<FStateTreeExecutionFrame> PrevFrames, TConstArrayView<FStateTreeExecutionFrame> NewFrames, TArrayView<FStateTreeFrameStateSelectionEvents> FramesStateSelectionEvents);
	
	/** Owner of the instance data. */
	UObject& Owner;

	/** The StateTree asset the context is initialized for */
	const UStateTree& RootStateTree;

	/** Instance data used during current tick. */
	FStateTreeInstanceData& InstanceData;

	/** Data storage of the instance data, cached for less indirections. */
	FStateTreeInstanceStorage* InstanceDataStorage = nullptr;

	/** Events queue to use, cached for less indirections. */
	TSharedPtr<FStateTreeEventQueue> EventQueue;

	/** Pointer to linked state tree overrides. */
	const FStateTreeReferenceOverrides* LinkedStateTreeOverrides = nullptr;
	
	/** Data view of the context data. */
	TArray<FStateTreeDataView, TConcurrentLinearArrayAllocator<FDefaultBlockAllocationTag>> ContextAndExternalDataViews;

	FOnCollectStateTreeExternalData CollectExternalDataDelegate;

	struct FCollectedExternalDataCache
	{
		const UStateTree* StateTree = nullptr;
		FStateTreeIndex16 BaseIndex;
	};
	TArray<FCollectedExternalDataCache, TConcurrentLinearArrayAllocator<FDefaultBlockAllocationTag>> CollectedExternalCache;

	bool bActiveExternalDataCollected = false;
	
	/** Next transition, used by RequestTransition(). */
	FStateTreeTransitionResult NextTransition;

	/** Structure describing the origin of the state transition that caused the state change. */
	FStateTreeTransitionSource NextTransitionSource;

	/** Current frame we're processing. */
	const FStateTreeExecutionFrame* CurrentlyProcessedParentFrame = nullptr; 
	const FStateTreeExecutionFrame* CurrentlyProcessedFrame = nullptr; 

	/** Pointer to the shared instance data of the current frame we're processing.  */
	FStateTreeInstanceStorage* CurrentlyProcessedSharedInstanceStorage = nullptr;

	/** Helper struct to track currently processed frame. */
	struct FCurrentlyProcessedFrameScope
	{
		FCurrentlyProcessedFrameScope(FStateTreeExecutionContext& InContext, const FStateTreeExecutionFrame* CurrentParentFrame, const FStateTreeExecutionFrame& CurrentFrame);

		~FCurrentlyProcessedFrameScope();

	private:
		FStateTreeExecutionContext& Context;
		int32 SavedFrameIndex = 0;
		FStateTreeInstanceStorage* SavedSharedInstanceDataStorage = nullptr;
		const FStateTreeExecutionFrame* SavedFrame = nullptr;
		const FStateTreeExecutionFrame* SavedParentFrame = nullptr;
	};
	
	/** Current state we're processing, or invalid if not applicable. */
	FStateTreeStateHandle CurrentlyProcessedState;
	
	/** Helper struct to track currently processed state. */
	struct FCurrentlyProcessedStateScope
	{
		FCurrentlyProcessedStateScope(FStateTreeExecutionContext& InContext, const FStateTreeStateHandle State)
			: Context(InContext)
		{
			SavedState = Context.CurrentlyProcessedState;
			Context.CurrentlyProcessedState = State;
		}

		~FCurrentlyProcessedStateScope()
		{
			Context.CurrentlyProcessedState = SavedState;
		}

	private:
		FStateTreeExecutionContext& Context;
		FStateTreeStateHandle SavedState = FStateTreeStateHandle::Invalid; 
	};

	/** Current event we're processing in transition, or invalid if not applicable. */
	const FStateTreeEvent* CurrentlyProcessedTransitionEvent = nullptr;

	/** Helper struct to track currently processed transition event. */
	struct FCurrentlyProcessedTransitionEventScope
	{
		FCurrentlyProcessedTransitionEventScope(FStateTreeExecutionContext& InContext, const FStateTreeEvent* Event)
			: Context(InContext)
		{
			check(Context.CurrentlyProcessedTransitionEvent == nullptr);
			Context.CurrentlyProcessedTransitionEvent = Event;
		}

		~FCurrentlyProcessedTransitionEventScope()
		{
			Context.CurrentlyProcessedTransitionEvent = nullptr;
		}

	private:
		FStateTreeExecutionContext& Context;
	};

	/** Current frames events we're processing during the state selection, or invalid if not applicable. */
	FStateTreeFrameStateSelectionEvents* CurrentlyProcessedStateSelectionEvents = nullptr;

	/** Helper struct to track currently processed state selection events. */
	struct FCurrentFrameStateSelectionEventsScope
	{
		FCurrentFrameStateSelectionEventsScope(FStateTreeExecutionContext& InContext, FStateTreeFrameStateSelectionEvents& InCurrentlyProcessedStateSelectionEvents)
			: Context(InContext)
		{
			SavedStateSelectionEvents = Context.CurrentlyProcessedStateSelectionEvents; 
			Context.CurrentlyProcessedStateSelectionEvents = &InCurrentlyProcessedStateSelectionEvents;
		}

		~FCurrentFrameStateSelectionEventsScope()
		{
			Context.CurrentlyProcessedStateSelectionEvents = SavedStateSelectionEvents;
		}

	private:
		FStateTreeExecutionContext& Context;
		FStateTreeFrameStateSelectionEvents* SavedStateSelectionEvents = nullptr;
	};

	/** True if transitions are allowed to be requested directly instead of buffering. */
	bool bAllowDirectTransitions = false;

	/** Helper struct to track when it is allowed to request transitions. */
	struct FAllowDirectTransitionsScope
	{
		FAllowDirectTransitionsScope(FStateTreeExecutionContext& InContext)
			: Context(InContext)
		{
			bSavedAllowDirectTransitions = Context.bAllowDirectTransitions; 
			Context.bAllowDirectTransitions = true;
		}

		~FAllowDirectTransitionsScope()
		{
			Context.bAllowDirectTransitions = bSavedAllowDirectTransitions;
		}

	private:
		FStateTreeExecutionContext& Context;
		bool bSavedAllowDirectTransitions = false;
	};

	/** Currently processed nodes instance data. Ideally we would pass these to the nodes directly, but do not want to change the API currently. */
	FStateTreeDataHandle CurrentNodeDataHandle;
	FStateTreeDataView CurrentNodeInstanceData;

	/** Helper struct to set current node data. */
	struct FNodeInstanceDataScope
	{
		FNodeInstanceDataScope(FStateTreeExecutionContext& InContext, const FStateTreeDataHandle InNodeDataHandle, const FStateTreeDataView InNodeInstanceData)
			: Context(InContext)
		{
			SavedNodeDataHandle = Context.CurrentNodeDataHandle;
			SavedNodeInstanceData = Context.CurrentNodeInstanceData;
			Context.CurrentNodeDataHandle = InNodeDataHandle;
			Context.CurrentNodeInstanceData = InNodeInstanceData;
		}

		~FNodeInstanceDataScope()
		{
			Context.CurrentNodeDataHandle = SavedNodeDataHandle;
			Context.CurrentNodeInstanceData = SavedNodeInstanceData;
		}

	private:
		FStateTreeExecutionContext& Context;
		FStateTreeDataHandle SavedNodeDataHandle;
		FStateTreeDataView SavedNodeInstanceData;
	};

	/** If true, the state tree context will create snapshots of transition events and capture them within RecordedTransitions for later use. */
	bool bRecordTransitions = false;

	/** Captured snapshots for transition results that can be used to recreate transitions. This array is only populated if bRecordTransitions is true. */
	TArray<FRecordedStateTreeTransitionResult> RecordedTransitions;
};

/**
 * The const version of a StateTree Execution Context that prevents using the FStateTreeInstanceData with non-const member function.
 */
struct FConstStateTreeExecutionContextView
{
public:
	FConstStateTreeExecutionContextView(UObject& InOwner, const UStateTree& InStateTree, const FStateTreeInstanceData& InInstanceData)
		: ExecutionContext(InOwner, InStateTree, const_cast<FStateTreeInstanceData&>(InInstanceData))
	{}

	operator const FStateTreeExecutionContext& ()
	{
		return ExecutionContext;
	}

	const FStateTreeExecutionContext& Get() const
	{
		return ExecutionContext;
	}

private:
	FConstStateTreeExecutionContextView(const FConstStateTreeExecutionContextView&) = delete;
	FConstStateTreeExecutionContextView& operator=(const FConstStateTreeExecutionContextView&) = delete;

private:
	FStateTreeExecutionContext ExecutionContext;
};
