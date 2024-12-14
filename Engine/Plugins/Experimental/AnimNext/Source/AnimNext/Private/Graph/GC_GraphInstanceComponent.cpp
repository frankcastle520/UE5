// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/GC_GraphInstanceComponent.h"

#include "TraitCore/ExecutionContext.h"
#include "TraitInterfaces/IGarbageCollection.h"

namespace UE::AnimNext
{
	FGCGraphInstanceComponent::FGCGraphInstanceComponent(FAnimNextGraphInstance& InOwnerInstance)
		: FGraphInstanceComponent(InOwnerInstance)
	{
	}

	void FGCGraphInstanceComponent::Register(const FWeakTraitPtr& InTraitPtr)
	{
		TraitsWithReferences.Add(InTraitPtr);
	}

	void FGCGraphInstanceComponent::Unregister(const FWeakTraitPtr& InTraitPtr)
	{
		const int32 EntryIndex = TraitsWithReferences.IndexOfByPredicate(
			[&InTraitPtr](const FWeakTraitPtr& TraitPtr)
			{
				return TraitPtr == InTraitPtr;
			});

		if (ensure(EntryIndex != INDEX_NONE))
		{
			TraitsWithReferences.RemoveAtSwap(EntryIndex);
		}
	}

	void FGCGraphInstanceComponent::AddReferencedObjects(FReferenceCollector& Collector) const
	{
		FExecutionContext Context;
		FTraitStackBinding TraitStack;
		TTraitBinding<IGarbageCollection> GCTrait;

		// TODO: If we kept the entries sorted by graph instance, we could re-use the execution context
		for (const FWeakTraitPtr& TraitPtr : TraitsWithReferences)
		{
			Context.BindTo(TraitPtr);

			if (Context.GetStack(TraitPtr, TraitStack))
			{
				ensure(TraitStack.GetInterface(GCTrait));

				GCTrait.AddReferencedObjects(Context, Collector);
			}
		}
	}
}
