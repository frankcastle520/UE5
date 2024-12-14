// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/TraitPtr.h"
#include "Graph/GraphInstanceComponent.h"

class FReferenceCollector;

namespace UE::AnimNext
{
	/**
	 * FGCGraphInstanceComponent
	 *
	 * This component maintains the necessary state to garbage collection.
	 */
	struct FGCGraphInstanceComponent : public FGraphInstanceComponent
	{
		DECLARE_ANIM_GRAPH_INSTANCE_COMPONENT(FGCGraphInstanceComponent)

		explicit FGCGraphInstanceComponent(FAnimNextGraphInstance& InOwnerInstance);

		// Registers the provided trait with the GC system
		// Once registered, IGarbageCollection::AddReferencedObjects will be called on it during GC
		void Register(const FWeakTraitPtr& InTraitPtr);

		// Unregisters the provided trait from the GC system
		void Unregister(const FWeakTraitPtr& InTraitPtr);

		// Called during garbage collection to collect strong object references
		void AddReferencedObjects(FReferenceCollector& Collector) const;

	private:
		// List of trait handles that contain UObject references and implement IGarbageCollection
		TArray<FWeakTraitPtr> TraitsWithReferences;
	};
}
