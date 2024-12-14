// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traits/BlendStackTrait.h"
#include "TraitCore/NodeInstance.h"
#include "TraitCore/ExecutionContext.h"

namespace UE::AnimNext
{
	AUTO_REGISTER_ANIM_TRAIT(FBlendStackCoreTrait)
	AUTO_REGISTER_ANIM_TRAIT(FBlendStackTrait)
	AUTO_REGISTER_ANIM_TRAIT(FBlendStackRequesterTrait)

	// Trait required interfaces implementation boilerplate
	#define TRAIT_INTERFACE_ENUMERATOR(GeneratorMacro) \
		GeneratorMacro(IDiscreteBlend) \
		GeneratorMacro(IGarbageCollection) \
		GeneratorMacro(IHierarchy) \
		GeneratorMacro(ISmoothBlend) \
		GeneratorMacro(IUpdateTraversal) \
		GeneratorMacro(IBlendStack) \

	GENERATE_ANIM_TRAIT_IMPLEMENTATION(FBlendStackCoreTrait, TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_EVENT_ENUMERATOR)
	#undef TRAIT_INTERFACE_ENUMERATOR

	// Trait required interfaces implementation boilerplate
	#define TRAIT_INTERFACE_ENUMERATOR(GeneratorMacro) \
		GeneratorMacro(IUpdate) \

	GENERATE_ANIM_TRAIT_IMPLEMENTATION(FBlendStackTrait, TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_EVENT_ENUMERATOR)

		// Trait required interfaces implementation boilerplate
#define TRAIT_REQUIRED_INTERFACE_ENUMERATOR(GeneratorMacroRequired) \
		GeneratorMacroRequired(IBlendStack) \

		GENERATE_ANIM_TRAIT_IMPLEMENTATION(FBlendStackRequesterTrait, TRAIT_INTERFACE_ENUMERATOR, TRAIT_REQUIRED_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_EVENT_ENUMERATOR)
#undef TRAIT_INTERFACE_ENUMERATOR
#undef TRAIT_REQUIRED_INTERFACE_ENUMERATOR



	void FBlendStackCoreTrait::FInstanceData::Construct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Construct(Context, Binding);
		IGarbageCollection::RegisterWithGC(Context, Binding);
	}

	void FBlendStackCoreTrait::FInstanceData::Destruct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Destruct(Context, Binding);
		IGarbageCollection::UnregisterWithGC(Context, Binding);
	}

	void FBlendStackCoreTrait::FGraphState::Initialize(const IBlendStack::FGraphRequest& GraphRequest)
	{
		Request = GraphRequest;
		State = FBlendStackCoreTrait::EGraphState::Active;
		bNewlyCreated = true;
	}

	void FBlendStackCoreTrait::FGraphState::Terminate()
	{
		Instance.Release();
		ChildPtr.Reset();
		bNewlyCreated = false;
		State = FBlendStackCoreTrait::EGraphState::Inactive;
	}

	int32 FBlendStackCoreTrait::FindFreeGraphIndexOrAdd(FInstanceData& InstanceData)
	{
		// Find an empty graph we can use
		const int32 NumGraphs = InstanceData.ChildGraphs.Num();
		for (int32 ChildIndex = 0; ChildIndex < NumGraphs; ++ChildIndex)
		{
			if (InstanceData.ChildGraphs[ChildIndex].State == FBlendStackCoreTrait::EGraphState::Inactive)
			{
				// This graph is inactive, we can re-use it
				return ChildIndex;
			}
		}

		// All graphs are in use, add a new one
		return InstanceData.ChildGraphs.AddDefaulted();
	}

	uint32 FBlendStackCoreTrait::GetNumChildren(const FExecutionContext& Context, const TTraitBinding<IHierarchy>& Binding) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		return InstanceData->ChildGraphs.Num();
	}

	void FBlendStackCoreTrait::GetChildren(const FExecutionContext& Context, const TTraitBinding<IHierarchy>& Binding, FChildrenArray& Children) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		for (const FBlendStackTrait::FGraphState& ChildGraph : InstanceData->ChildGraphs)
		{
			// Even if the request is inactive, we queue an empty handle
			Children.Add(ChildGraph.ChildPtr);
		}
	}

	void FBlendStackCoreTrait::QueueChildrenForTraversal(FUpdateTraversalContext& Context, const TTraitBinding<IUpdateTraversal>& Binding, const FTraitUpdateState& TraitState, FUpdateTraversalQueue& TraversalQueue) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		const int32 NumGraphs = InstanceData->ChildGraphs.Num();

		TTraitBinding<IDiscreteBlend> DiscreteBlendTrait;
		Binding.GetStackInterface(DiscreteBlendTrait);

		for (int32 ChildIndex = 0; ChildIndex < NumGraphs; ++ChildIndex)
		{
			FGraphState& Graph = InstanceData->ChildGraphs[ChildIndex];
			const float BlendWeight = DiscreteBlendTrait.GetBlendWeight(Context, ChildIndex);

			FTraitUpdateState ChildGraphTraitState = TraitState
				.WithWeight(BlendWeight)
				.AsBlendingOut(ChildIndex != InstanceData->CurrentlyActiveGraphIndex)
				.AsNewlyRelevant(Graph.bNewlyCreated);
			Graph.bNewlyCreated = false;

			TraversalQueue.Push(InstanceData->ChildGraphs[ChildIndex].ChildPtr, ChildGraphTraitState);
		}
	}

	int32 FBlendStackCoreTrait::GetBlendDestinationChildIndex(FExecutionContext& Context, const TTraitBinding<IDiscreteBlend>& Binding) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		return InstanceData->CurrentlyActiveGraphIndex;
	}

	void FBlendStackCoreTrait::OnBlendTransition(FExecutionContext& Context, const TTraitBinding<IDiscreteBlend>& Binding, int32 OldChildIndex, int32 NewChildIndex) const
	{
		TTraitBinding<IDiscreteBlend> DiscreteBlendTrait;
		Binding.GetStackInterface(DiscreteBlendTrait);

		// We initiate immediately when we transition
		DiscreteBlendTrait.OnBlendInitiated(Context, NewChildIndex);

		// We terminate immediately when we transition
		DiscreteBlendTrait.OnBlendTerminated(Context, OldChildIndex);
	}

	void FBlendStackCoreTrait::OnBlendInitiated(FExecutionContext& Context, const TTraitBinding<IDiscreteBlend>& Binding, int32 ChildIndex) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->ChildGraphs.IsValidIndex(ChildIndex))
		{
			FGraphState& Graph = InstanceData->ChildGraphs[ChildIndex];

			//@TODO: Remove or implement entry points once we decide if we still need them.
			static const FName EntryPoint = NAME_None;
			Graph.Request.AnimationGraph->AllocateInstance(Binding.GetTraitPtr().GetNodeInstance()->GetOwner(), Graph.Instance, EntryPoint);
			Graph.ChildPtr = Graph.Instance.GetGraphRootPtr();
		}
	}

	void FBlendStackCoreTrait::OnBlendTerminated(FExecutionContext& Context, const TTraitBinding<IDiscreteBlend>& Binding, int32 ChildIndex) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->ChildGraphs.IsValidIndex(ChildIndex))
		{
			FGraphState& Graph = InstanceData->ChildGraphs[ChildIndex];

			// Deallocate our graph
			Graph.Terminate();
		}
	}

	float FBlendStackCoreTrait::GetBlendTime(FExecutionContext& Context, const TTraitBinding<ISmoothBlend>& Binding, int32 ChildIndex) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->ChildGraphs.IsValidIndex(ChildIndex))
		{
			const FGraphState& Graph = InstanceData->ChildGraphs[ChildIndex];
			return Graph.Request.BlendArgs.BlendTime;
		}
		else
		{
			// Unknown child
			return 0.0f;
		}
	}

	EAlphaBlendOption FBlendStackCoreTrait::GetBlendType(FExecutionContext& Context, const TTraitBinding<ISmoothBlend>& Binding, int32 ChildIndex) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->ChildGraphs.IsValidIndex(ChildIndex))
		{
			const FGraphState& Graph = InstanceData->ChildGraphs[ChildIndex];
			return Graph.Request.BlendArgs.BlendOption;
		}
		else
		{
			// Unknown child
			return EAlphaBlendOption::Linear;
		}
	}

	UCurveFloat* FBlendStackCoreTrait::GetCustomBlendCurve(FExecutionContext& Context, const TTraitBinding<ISmoothBlend>& Binding, int32 ChildIndex) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->ChildGraphs.IsValidIndex(ChildIndex))
		{
			const FGraphState& Graph = InstanceData->ChildGraphs[ChildIndex];
			return Graph.Request.BlendArgs.CustomCurve;
		}
		else
		{
			// Unknown child
			return nullptr;
		}
	}

	void FBlendStackCoreTrait::AddReferencedObjects(const FExecutionContext& Context, const TTraitBinding<IGarbageCollection>& Binding, FReferenceCollector& Collector) const
	{
		IGarbageCollection::AddReferencedObjects(Context, Binding, Collector);

		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		for (FBlendStackCoreTrait::FGraphState& Graph : InstanceData->ChildGraphs)
		{
			Collector.AddReferencedObject(Graph.Request.AnimationGraph);
		}
	}

	void FBlendStackCoreTrait::PushGraph(FExecutionContext& Context, const TTraitBinding<IBlendStack>& Binding, const IBlendStack::FGraphRequest& GraphRequest, FAnimNextGraphInstancePtr& OutGraphInstance) const
	{
		if (GraphRequest.AnimationGraph == nullptr)
		{
			OutGraphInstance.Release();
			return;
		}

		//@TODO: Add depth limit and saturation policies
		
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		const int32 OldChildIndex = InstanceData->CurrentlyActiveGraphIndex;
		const int32 NewChildIndex = FBlendStackCoreTrait::FindFreeGraphIndexOrAdd(*InstanceData);
		FBlendStackCoreTrait::FGraphState& Graph = InstanceData->ChildGraphs[NewChildIndex];
		Graph.Initialize(GraphRequest);

		TTraitBinding<IDiscreteBlend> DiscreteBlendTrait;
		Binding.GetStackInterface(DiscreteBlendTrait);
		DiscreteBlendTrait.OnBlendTransition(Context, OldChildIndex, NewChildIndex);
		
		InstanceData->CurrentlyActiveGraphIndex = NewChildIndex;
		OutGraphInstance = Graph.Instance;
	}

	void FBlendStackCoreTrait::GetActiveGraphRequest(FExecutionContext& Context, const TTraitBinding<IBlendStack>& Binding, IBlendStack::FGraphRequest& OutRequest) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		const int32 CurrentlyActiveGraphIndex = InstanceData->CurrentlyActiveGraphIndex;
		if (CurrentlyActiveGraphIndex != INDEX_NONE)
		{
			OutRequest = InstanceData->ChildGraphs[CurrentlyActiveGraphIndex].Request;
			return;
		}

		return IBlendStack::GetActiveGraphRequest(Context, Binding, OutRequest);
	}

	void FBlendStackTrait::PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
 
		const UAnimNextAnimationGraph* DesiredAnimationGraph = SharedData->GetAnimationGraph(Binding);
		const int32 CurrentlyActiveGraphIndex = InstanceData->CurrentlyActiveGraphIndex;

		const bool bForceBlend = SharedData->GetbForceBlend(Binding);
		const bool bIsEmpty = InstanceData->CurrentlyActiveGraphIndex == INDEX_NONE;
		if (DesiredAnimationGraph && (bForceBlend || bIsEmpty || (DesiredAnimationGraph != InstanceData->ChildGraphs[CurrentlyActiveGraphIndex].Request.AnimationGraph)))
		{
			TTraitBinding<IBlendStack> BlendStackTrait;
			Binding.GetStackInterface(BlendStackTrait);

			FAnimNextGraphInstancePtr NewGraphInstance;
			IBlendStack::FGraphRequest GraphRequest;
			GraphRequest.AnimationGraph = DesiredAnimationGraph;
			GraphRequest.BlendArgs.BlendTime = SharedData->GetBlendTime(Binding);

			BlendStackTrait.PushGraph(Context, GraphRequest, NewGraphInstance);
		}
	}

	void FBlendStackRequesterTrait::PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();

		const UAnimNextAnimationGraph* DesiredAnimationGraph = SharedData->GetAnimationGraph(Binding);
		if (DesiredAnimationGraph != nullptr)
		{
			TTraitBinding<IBlendStack> BlendStackTrait;
			Binding.GetStackInterface(BlendStackTrait);

			IBlendStack::FGraphRequest ActiveGraphRequest;
			BlendStackTrait.GetActiveGraphRequest(Context, ActiveGraphRequest);
			const bool bForceBlend = SharedData->GetbForceBlend(Binding);
			if (bForceBlend || (DesiredAnimationGraph != ActiveGraphRequest.AnimationGraph))
			{
				FAnimNextGraphInstancePtr NewGraphInstance;
				IBlendStack::FGraphRequest GraphRequest;
				GraphRequest.AnimationGraph = DesiredAnimationGraph;
				GraphRequest.BlendArgs.BlendTime = SharedData->GetBlendTime(Binding);

				BlendStackTrait.PushGraph(Context, GraphRequest, NewGraphInstance);
			}
		}
	}

	void FBlendStackRequesterTrait::OnBecomeRelevant(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();

		TTraitBinding<IBlendStack> BlendStackTrait;
		Binding.GetStackInterface(BlendStackTrait);

		const UAnimNextAnimationGraph* DesiredAnimationGraph = SharedData->GetAnimationGraph(Binding);
		if (DesiredAnimationGraph != nullptr)
		{
			FAnimNextGraphInstancePtr NewGraphInstance;
			IBlendStack::FGraphRequest GraphRequest;
			GraphRequest.AnimationGraph = DesiredAnimationGraph;
			GraphRequest.BlendArgs.BlendTime = SharedData->GetBlendTime(Binding);

			BlendStackTrait.PushGraph(Context, GraphRequest, NewGraphInstance);
		}
	}
}
