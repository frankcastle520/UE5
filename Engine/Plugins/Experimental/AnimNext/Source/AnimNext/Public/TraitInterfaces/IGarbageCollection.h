// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/ITraitInterface.h"
#include "TraitCore/TraitBinding.h"

class FReferenceCollector;

namespace UE::AnimNext
{
	/**
	 * IGarbageCollection
	 * 
	 * This interface exposes garbage collection reference tracking.
	 */
	struct ANIMNEXT_API IGarbageCollection : ITraitInterface
	{
		DECLARE_ANIM_TRAIT_INTERFACE(IGarbageCollection, 0x231a2017)


		// Registers the provided binding for GC callback
		// Once registered, AddReferencedObjects is called during GC to collect references
		static void RegisterWithGC(const FExecutionContext& Context, const FTraitBinding& Binding);

		// Unregisters the provided binding from GC callback
		static void UnregisterWithGC(const FExecutionContext& Context, const FTraitBinding& Binding);

		// Called when garbage collection requests hard/strong object references
		// @see UObject::AddReferencedObjects
		virtual void AddReferencedObjects(const FExecutionContext& Context, const TTraitBinding<IGarbageCollection>& Binding, FReferenceCollector& Collector) const;

#if WITH_EDITOR
		virtual bool IsInternal() const override { return true; }

		virtual const FText& GetDisplayName() const override;
		virtual const FText& GetDisplayShortName() const override;
#endif // WITH_EDITOR
	};

	/**
	 * Specialization for trait binding.
	 */
	template<>
	struct TTraitBinding<IGarbageCollection> : FTraitBinding
	{
		// @see IGarbageCollection::AddReferencedObjects
		void AddReferencedObjects(const FExecutionContext& Context, FReferenceCollector& Collector) const
		{
			GetInterface()->AddReferencedObjects(Context, *this, Collector);
		}

	protected:
		const IGarbageCollection* GetInterface() const { return GetInterfaceTyped<IGarbageCollection>(); }
	};
}
