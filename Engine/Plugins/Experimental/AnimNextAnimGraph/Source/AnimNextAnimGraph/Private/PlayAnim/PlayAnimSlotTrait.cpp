// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayAnim/PlayAnimSlotTrait.h"

#include "Animation/AnimSequence.h"
#include "DataInterface/AnimNextDataInterfaceHost.h"
#include "DataInterface/DataInterfaceStructAdapter.h"
#include "TraitCore/ExecutionContext.h"
#include "TraitCore/NodeInstance.h"
#include "TraitInterfaces/ITimeline.h"
#include "Graph/AnimNextGraphInstance.h"
#include "Logging/StructuredLog.h"
#include "Module/ModuleEvents.h"

namespace UE::AnimNext
{
	AUTO_REGISTER_ANIM_TRAIT(FPlayAnimSlotTrait)

	// Trait implementation boilerplate
	#define TRAIT_INTERFACE_ENUMERATOR(GeneratorMacro) \
		GeneratorMacro(IDiscreteBlend) \
		GeneratorMacro(IGarbageCollection) \
		GeneratorMacro(IHierarchy) \
		GeneratorMacro(ISmoothBlend) \
		GeneratorMacro(IInertializerBlend) \
		GeneratorMacro(IUpdate) \
		GeneratorMacro(IUpdateTraversal) \

	#define TRAIT_EVENT_ENUMERATOR(GeneratorMacro) \
		GeneratorMacro(FPlayAnimSlotTrait::OnPlayEvent) \
		GeneratorMacro(FPlayAnimSlotTrait::OnStopEvent) \

	GENERATE_ANIM_TRAIT_IMPLEMENTATION(FPlayAnimSlotTrait, TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_INTERFACE_ENUMERATOR, TRAIT_EVENT_ENUMERATOR)
	#undef TRAIT_INTERFACE_ENUMERATOR
	#undef TRAIT_EVENT_ENUMERATOR

	void FPlayAnimSlotTrait::FPlayAnimSlotRequest::Initialize(FPlayAnimRequestPtr InRequest, const FPlayAnimBlendSettings& InBlendSettings, const UAnimNextAnimationGraph* InAnimationGraph)
	{
		Request = InRequest;
		BlendSettings = InBlendSettings;
		AnimationGraph = InAnimationGraph;

		// If no input is provided, we'll use the source
		State = InAnimationGraph != nullptr ? EPlayAnimRequestState::Active : EPlayAnimRequestState::ActiveSource;
		bWasRelevant = false;
	}

	void FPlayAnimSlotTrait::FInstanceData::Construct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Construct(Context, Binding);

		IGarbageCollection::RegisterWithGC(Context, Binding);
	}

	void FPlayAnimSlotTrait::FInstanceData::Destruct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Destruct(Context, Binding);

		IGarbageCollection::UnregisterWithGC(Context, Binding);
	}

	int32 FPlayAnimSlotTrait::FindFreeRequestIndexOrAdd(FInstanceData& InstanceData)
	{
		// Find an empty request we can use
		const int32 NumRequests = InstanceData.SlotRequests.Num();
		for (int32 RequestIndex = 0; RequestIndex < NumRequests; ++RequestIndex)
		{
			if (InstanceData.SlotRequests[RequestIndex].State == EPlayAnimRequestState::Inactive)
			{
				// This request is inactive, we can re-use it
				return RequestIndex;
			}
		}

		// All requests are in use, add a new one
		return InstanceData.SlotRequests.AddDefaulted();
	}

	ETraitStackPropagation FPlayAnimSlotTrait::OnPlayEvent(const FExecutionContext& Context, FTraitBinding& Binding, FPlayAnim_PlayEvent& Event) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		const FName SlotName = SharedData->GetSlotName(Binding);

		const FAnimNextPlayAnimRequestArgs& RequestArgs = Event.Request->GetArgs();
		if (SlotName == RequestArgs.SlotName)
		{
			FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

			ensureMsgf(!InstanceData->PendingRequest.IsValid(), TEXT("PlayAnim slot %s already contained a pending request, it will be overwritten"), *SlotName.ToString());

			// Overwrite any request we might have, we'll pick it up on the next update
			InstanceData->PendingRequest.Reset();
			InstanceData->PendingRequest.Request = Event.Request;

			Event.MarkConsumed();
		}

		return ETraitStackPropagation::Continue;
	}

	ETraitStackPropagation FPlayAnimSlotTrait::OnStopEvent(const FExecutionContext& Context, FTraitBinding& Binding, FPlayAnim_StopEvent& Event) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		const FName SlotName = SharedData->GetSlotName(Binding);

		const FAnimNextPlayAnimRequestArgs& RequestArgs = Event.Request->GetArgs();
		if (SlotName == RequestArgs.SlotName)
		{
			FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

			// Reset any pending request we might have, and cancel it
			InstanceData->PendingRequest.Reset();
			InstanceData->PendingRequest.bStop = true;

			Event.MarkConsumed();
		}

		return ETraitStackPropagation::Continue;
	}

	uint32 FPlayAnimSlotTrait::GetNumChildren(const FExecutionContext& Context, const TTraitBinding<IHierarchy>& Binding) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		return InstanceData->SlotRequests.Num();
	}

	void FPlayAnimSlotTrait::GetChildren(const FExecutionContext& Context, const TTraitBinding<IHierarchy>& Binding, FChildrenArray& Children) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		for (const FPlayAnimSlotRequest& SlotRequest : InstanceData->SlotRequests)
		{
			// Even if the request is inactive, we queue an empty handle
			Children.Add(SlotRequest.ChildPtr);
		}
	}

	void FPlayAnimSlotTrait::PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		TTraitBinding<IDiscreteBlend> DiscreteBlendTrait;
		Binding.GetStackInterface(DiscreteBlendTrait);

		const bool bHasActiveSubGraph = InstanceData->CurrentlyActiveRequestIndex != INDEX_NONE;
		if (bHasActiveSubGraph)
		{
			InstanceData->SlotRequests[InstanceData->CurrentlyActiveRequestIndex].bWasRelevant = true;
		}

		bool bJustTransitioned = false;
		if (InstanceData->PendingRequest.IsValid() || !bHasActiveSubGraph)
		{
			const FPlayAnimRequestPtr Request = InstanceData->PendingRequest.Request;

			// Clear it out now in case we early out below
			InstanceData->PendingRequest.Reset();

			FPlayAnimBlendSettings BlendSettings;
			const UAnimNextAnimationGraph* AnimationGraph = nullptr;
			if (Request)
			{
				// This is a new pending request, lookup the sub-graph to use with our chooser and the desired animation object

				// TODO: Rather than this built-in choosing logic, this could be implemented by a new optional trait:
				// - Implement some new trait that converts an event payload's object into a graph + payload struct for variable binding
				// - Could possibly remove Object from FAnimNextPlayAnimRequestArgs, or AnimationObject from FAnimNextPlayAnimPayload, as they are duplicated
				// - Call this trait's interface here and use the resulting animation graph (binding variables in OnBlendInitiated)
				// - Reentrancy needs to handle 'same graph, different parameters'
				const FAnimNextPlayAnimRequestArgs& RequestArgs = Request->GetArgs();
				if (RequestArgs.Object)
				{
					if (const UChooserTable* Chooser = SharedData->GetSubGraphChooser(Binding))
					{
						FAnimNextPlayAnimChooserParameters ChooserParameters;
						ChooserParameters.AnimationObjectType = RequestArgs.Object->GetClass();

						FChooserEvaluationContext ChooserContext;
						ChooserContext.AddStructParam(ChooserParameters);

						UChooserTable::EvaluateChooser(ChooserContext, Chooser, FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&AnimationGraph](UObject* InResult)
							{
								AnimationGraph = Cast<UAnimNextAnimationGraph>(InResult);
								return FObjectChooserBase::EIteratorStatus::Stop;
							}));
					}

					if (AnimationGraph != nullptr)
					{
						// Check for re-entrancy and early-out if we are linking back to the current instance or one of its parents
						const FName EntryPoint = AnimationGraph->DefaultEntryPoint;
						const FAnimNextGraphInstance* OwnerGraphInstance = &Binding.GetTraitPtr().GetNodeInstance()->GetOwner();
						while (OwnerGraphInstance != nullptr)
						{
							if (OwnerGraphInstance->UsesAnimationGraph(AnimationGraph) && OwnerGraphInstance->UsesEntryPoint(EntryPoint))
							{
								return;
							}

							OwnerGraphInstance = OwnerGraphInstance->GetParentGraphInstance();
						}
					}

					BlendSettings = RequestArgs.BlendInSettings;
				}
			}

			if (bHasActiveSubGraph)
			{
				// Queue our status update
				const FPlayAnimSlotRequest& OldSlotRequest = InstanceData->SlotRequests[InstanceData->CurrentlyActiveRequestIndex];
				if (OldSlotRequest.State == EPlayAnimRequestState::Active)
				{
					auto StatusUpdateEvent = MakeTraitEvent<FPlayAnim_StatusUpdateEvent>();
					StatusUpdateEvent->Request = OldSlotRequest.Request;
					StatusUpdateEvent->Status = EPlayAnimStatus::Playing | EPlayAnimStatus::Interrupted;

					Context.RaiseOutputTraitEvent(StatusUpdateEvent);
				}
			}

			// Find an empty request we can use
			const int32 FreeRequestIndex = FindFreeRequestIndexOrAdd(*InstanceData);

			FPlayAnimSlotRequest& SlotRequest = InstanceData->SlotRequests[FreeRequestIndex];
			SlotRequest.Initialize(Request, BlendSettings, AnimationGraph);

			const int32 OldChildIndex = InstanceData->CurrentlyActiveRequestIndex;
			const int32 NewChildIndex = FreeRequestIndex;

			InstanceData->CurrentlyActiveRequestIndex = FreeRequestIndex;

			DiscreteBlendTrait.OnBlendTransition(Context, OldChildIndex, NewChildIndex);

			bJustTransitioned = true;
		}

		float CurrentRequestTimeLeft = 0.0f;

		// Broadcast our timeline progress
		const int32 NumSlotRequests = InstanceData->SlotRequests.Num();
		for (int32 RequestIndex = 0; RequestIndex < NumSlotRequests; ++RequestIndex)
		{
			const FPlayAnimSlotRequest& SlotRequest = InstanceData->SlotRequests[RequestIndex];
			if (SlotRequest.State != EPlayAnimRequestState::Active)
			{
				continue;	// We don't care about this slot request
			}

			FTraitStackBinding ChildStack;
			ensure(Context.GetStack(SlotRequest.ChildPtr, ChildStack));

			TTraitBinding<ITimeline> ChildTimelineTrait;
			ensure(ChildStack.GetInterface(ChildTimelineTrait));

			const FTimelineProgress ChildProgress = ChildTimelineTrait.SimulateAdvanceBy(Context, TraitState.GetDeltaTime());

			if (InstanceData->CurrentlyActiveRequestIndex == RequestIndex)
			{
				CurrentRequestTimeLeft = ChildProgress.GetTimeLeft();
			}

			{
				auto TimelineUpdateEvent = MakeTraitEvent<FPlayAnim_TimelineUpdateEvent>();
				TimelineUpdateEvent->Request = SlotRequest.Request;
				TimelineUpdateEvent->TimelineProgress = ChildProgress;

				Context.RaiseOutputTraitEvent(TimelineUpdateEvent);
			}
		}

		// Check if we are blending out
		if (!bJustTransitioned && InstanceData->CurrentlyActiveRequestIndex != INDEX_NONE)
		{
			const FPlayAnimSlotRequest& ActiveSlotRequest = InstanceData->SlotRequests[InstanceData->CurrentlyActiveRequestIndex];

			if (ActiveSlotRequest.State == EPlayAnimRequestState::Active)
			{
				const FAnimNextPlayAnimRequestArgs& RequestArgs = ActiveSlotRequest.Request->GetArgs();

				const float BlendOutTime = RequestArgs.BlendOutSettings.Blend.BlendTime;
				if (CurrentRequestTimeLeft <= BlendOutTime)
				{
					// We are ready to start blending out
					{
						auto StatusUpdateEvent = MakeTraitEvent<FPlayAnim_StatusUpdateEvent>();
						StatusUpdateEvent->Request = ActiveSlotRequest.Request;
						StatusUpdateEvent->Status = EPlayAnimStatus::BlendingOut;

						Context.RaiseOutputTraitEvent(StatusUpdateEvent);
					}

					// Find an empty request we can use
					const int32 FreeRequestIndex = FindFreeRequestIndexOrAdd(*InstanceData);

					FPlayAnimSlotRequest& FreeSlotRequest = InstanceData->SlotRequests[FreeRequestIndex];
					FreeSlotRequest.Initialize(FPlayAnimRequestPtr(), RequestArgs.BlendOutSettings, nullptr);

					const int32 OldChildIndex = InstanceData->CurrentlyActiveRequestIndex;
					const int32 NewChildIndex = FreeRequestIndex;

					InstanceData->CurrentlyActiveRequestIndex = FreeRequestIndex;

					DiscreteBlendTrait.OnBlendTransition(Context, OldChildIndex, NewChildIndex);
				}
			}
		}
	}

	void FPlayAnimSlotTrait::QueueChildrenForTraversal(FUpdateTraversalContext& Context, const TTraitBinding<IUpdateTraversal>& Binding, const FTraitUpdateState& TraitState, FUpdateTraversalQueue& TraversalQueue) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		const int32 NumRequests = InstanceData->SlotRequests.Num();
		check(NumRequests != 0);	// Should never happen since the source is always present

		TTraitBinding<IDiscreteBlend> DiscreteBlendTrait;
		Binding.GetStackInterface(DiscreteBlendTrait);

		for (int32 RequestIndex = 0; RequestIndex < NumRequests; ++RequestIndex)
		{
			const FPlayAnimSlotRequest& SlotRequest = InstanceData->SlotRequests[RequestIndex];
			const float BlendWeight = DiscreteBlendTrait.GetBlendWeight(Context, RequestIndex);

			FTraitUpdateState RequestSlotTraitState = TraitState
				.WithWeight(BlendWeight)
				.AsBlendingOut(RequestIndex != InstanceData->CurrentlyActiveRequestIndex)
				.AsNewlyRelevant(!SlotRequest.bWasRelevant);

			TraversalQueue.Push(InstanceData->SlotRequests[RequestIndex].ChildPtr, RequestSlotTraitState);
		}
	}

	float FPlayAnimSlotTrait::GetBlendWeight(FExecutionContext& Context, const TTraitBinding<IDiscreteBlend>& Binding, int32 ChildIndex) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		if (ChildIndex == InstanceData->CurrentlyActiveRequestIndex)
		{
			return 1.0f;	// Active child has full weight
		}
		else if (InstanceData->SlotRequests.IsValidIndex(ChildIndex))
		{
			return 0.0f;	// Other children have no weight
		}
		else
		{
			// Invalid child index
			return -1.0f;
		}
	}

	int32 FPlayAnimSlotTrait::GetBlendDestinationChildIndex(FExecutionContext& Context, const TTraitBinding<IDiscreteBlend>& Binding) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		return InstanceData->CurrentlyActiveRequestIndex;
	}

	void FPlayAnimSlotTrait::OnBlendTransition(FExecutionContext& Context, const TTraitBinding<IDiscreteBlend>& Binding, int32 OldChildIndex, int32 NewChildIndex) const
	{
		TTraitBinding<IDiscreteBlend> DiscreteBlendTrait;
		Binding.GetStackInterface(DiscreteBlendTrait);

		// We initiate immediately when we transition
		DiscreteBlendTrait.OnBlendInitiated(Context, NewChildIndex);

		// We terminate immediately when we transition
		DiscreteBlendTrait.OnBlendTerminated(Context, OldChildIndex);
	}

	void FPlayAnimSlotTrait::OnBlendInitiated(FExecutionContext& Context, const TTraitBinding<IDiscreteBlend>& Binding, int32 ChildIndex) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		if (InstanceData->SlotRequests.IsValidIndex(ChildIndex))
		{
			// Allocate our new request instance
			FPlayAnimSlotRequest& SlotRequest = InstanceData->SlotRequests[ChildIndex];

			if (SlotRequest.State == EPlayAnimRequestState::Active)
			{
				const FName EntryPoint = SlotRequest.AnimationGraph->DefaultEntryPoint;
				SlotRequest.AnimationGraph->AllocateInstance(Binding.GetTraitPtr().GetNodeInstance()->GetOwner(), SlotRequest.GraphInstance, EntryPoint);
				SlotRequest.ChildPtr = SlotRequest.GraphInstance.GetGraphRootPtr();

				// Note: args are mutable here as bindings allow writes!
				FDataInterfaceStructAdapter VariableBinding(SlotRequest.AnimationGraph, SlotRequest.Request->GetMutableArgs().Payload);
				SlotRequest.GraphInstance.BindPublicVariables({ &VariableBinding });

				// TODO: Validate that our child implements the ITimeline interface

				{
					// Queue our status update
					auto StatusUpdateEvent = MakeTraitEvent<FPlayAnim_StatusUpdateEvent>();
					StatusUpdateEvent->Request = SlotRequest.Request;
					StatusUpdateEvent->Status = EPlayAnimStatus::Playing;

					Context.RaiseOutputTraitEvent(StatusUpdateEvent);
				}
			}
			else if (SlotRequest.State == EPlayAnimRequestState::ActiveSource)
			{
				const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();

				SlotRequest.ChildPtr = Context.AllocateNodeInstance(Binding, SharedData->Source);
			}
		}
	}

	void FPlayAnimSlotTrait::OnBlendTerminated(FExecutionContext& Context, const TTraitBinding<IDiscreteBlend>& Binding, int32 ChildIndex) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		if (InstanceData->SlotRequests.IsValidIndex(ChildIndex))
		{
			// Deallocate our request instance
			FPlayAnimSlotRequest& SlotRequest = InstanceData->SlotRequests[ChildIndex];

			if (SlotRequest.State == EPlayAnimRequestState::Active)
			{
				SlotRequest.GraphInstance.Release();

				{
					// Queue our status update
					auto StatusUpdateEvent = MakeTraitEvent<FPlayAnim_StatusUpdateEvent>();
					StatusUpdateEvent->Request = SlotRequest.Request;
					StatusUpdateEvent->Status = EPlayAnimStatus::Completed;

					Context.RaiseOutputTraitEvent(StatusUpdateEvent);
				}
			}

			SlotRequest.Request = nullptr;
			SlotRequest.ChildPtr.Reset();
			SlotRequest.State = EPlayAnimRequestState::Inactive;
			SlotRequest.bWasRelevant = false;
		}
	}

	float FPlayAnimSlotTrait::GetBlendTime(FExecutionContext& Context, const TTraitBinding<ISmoothBlend>& Binding, int32 ChildIndex) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->SlotRequests.IsValidIndex(ChildIndex))
		{
			const FPlayAnimSlotRequest& SlotRequest = InstanceData->SlotRequests[ChildIndex];
			return SlotRequest.BlendSettings.Blend.BlendTime;
		}
		else
		{
			// Unknown child
			return 0.0f;
		}
	}

	EAlphaBlendOption FPlayAnimSlotTrait::GetBlendType(FExecutionContext& Context, const TTraitBinding<ISmoothBlend>& Binding, int32 ChildIndex) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->SlotRequests.IsValidIndex(ChildIndex))
		{
			const FPlayAnimSlotRequest& SlotRequest = InstanceData->SlotRequests[ChildIndex];
			return SlotRequest.BlendSettings.Blend.BlendOption;
		}
		else
		{
			// Unknown child
			return EAlphaBlendOption::Linear;
		}
	}

	UCurveFloat* FPlayAnimSlotTrait::GetCustomBlendCurve(FExecutionContext& Context, const TTraitBinding<ISmoothBlend>& Binding, int32 ChildIndex) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->SlotRequests.IsValidIndex(ChildIndex))
		{
			const FPlayAnimSlotRequest& SlotRequest = InstanceData->SlotRequests[ChildIndex];
			return SlotRequest.BlendSettings.Blend.CustomCurve;
		}
		else
		{
			// Unknown child
			return nullptr;
		}
	}

	float FPlayAnimSlotTrait::GetBlendTime(FExecutionContext& Context, const TTraitBinding<IInertializerBlend>& Binding, int32 ChildIndex) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->SlotRequests.IsValidIndex(ChildIndex))
		{
			const FPlayAnimSlotRequest& SlotRequest = InstanceData->SlotRequests[ChildIndex];
			if (SlotRequest.BlendSettings.BlendMode == EAnimNextPlayAnimBlendMode::Inertialization)
			{
				return SlotRequest.BlendSettings.Blend.BlendTime;
			}
			else
			{
				// Not an inertializing blend
				return 0.0f;
			}
		}
		else
		{
			// Unknown child
			return 0.0f;
		}
	}

	void FPlayAnimSlotTrait::AddReferencedObjects(const FExecutionContext& Context, const TTraitBinding<IGarbageCollection>& Binding, FReferenceCollector& Collector) const
	{
		IGarbageCollection::AddReferencedObjects(Context, Binding, Collector);

		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		if (InstanceData->PendingRequest.Request)
		{
			InstanceData->PendingRequest.Request->AddReferencedObjects(Collector);
		}

		for (FPlayAnimSlotRequest& SlotRequest : InstanceData->SlotRequests)
		{
			if (SlotRequest.Request)
			{
				SlotRequest.Request->AddReferencedObjects(Collector);
			}

			Collector.AddReferencedObject(SlotRequest.AnimationGraph);
		}
	}
}
