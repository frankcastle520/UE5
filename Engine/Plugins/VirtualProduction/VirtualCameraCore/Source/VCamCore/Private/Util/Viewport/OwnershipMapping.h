// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Delegates/Delegate.h"
#include "HAL/Platform.h"
#include "Misc/Optional.h"

#include <type_traits>

namespace UE::VCamCore
{
	// Work around missing header/implementations on some platforms
	template<typename From, typename To>
	concept convertible_to = std::is_convertible_v<From, To> && requires { static_cast<To>(std::declval<From>()); };
	
	template<typename TThing>
	concept CThing = std::is_copy_constructible_v<TThing>;

	template<typename TOwner>
	concept COwner = std::is_copy_constructible_v<TOwner> && requires(TOwner A, TOwner B) { { A == B } -> convertible_to<bool>; };
	
	/**
	 * Abstracts the concept of ownership.
	 *
	 * Multiple agents can try to take and release ownership over a thing. The first one to request it gains the owner.
	 * If another agent requests ownership over something that's already owned, then it will get ownership if the current owner releases ownership.
	 * 
	 * @tparam TThing The type of the thing being owned
	 * @tparam TOwner The type of whatever owns the thing
	 */
	template<CThing TThing, COwner TOwner>
	class TOwnershipMapping
	{
	public:

		~TOwnershipMapping() { Clear(false); }

		/**
		 * Attemps to assign owner ship for Thing to Owner, or enqueues it.
		 * @return Whether Owner now has ownership over Thing.
		 */
		bool TryTakeOwnership(const TOwner& Owner, const TThing& Thing);

		/** Releases Owner's ownership over all its registered things. */
		void ReleaseOwnership(const TOwner& Owner) { RemoveOwnershipAll(Owner); }
		/** Releases Owner's ownership over Thing.*/
		void ReleaseOwnership(const TOwner& Owner, const TThing& Thing) { RemoveOwnershipSingle(Owner, Thing); }

		/** Clears all ownership. */
		void Clear(bool bSilent = true);

		/** @return Gets the owner of Thing or nullptr. */
		const TOwner* GetOwner(const TThing& Thing) const;
		/** @return Whether Thing is owned by Owner. */
		bool IsOwnedBy(const TThing& Thing, const TOwner& TestOwner) const { return GetOwner(Thing) && TestOwner == *GetOwner(Thing); }
		/** @return Whether Thing has an Owner. */
		bool HasOwner(const TThing& Thing) const { return GetOwner(Thing) != nullptr; }

		/**
		 * Removes every TOwner that has called TryTakeOwnership for Thing.
		 * The callback returns true if it wants to remove the object.
		 */
		template<typename TLambda> requires std::is_invocable_r_v<bool, TLambda, const TOwner&>
		void RemovePotentialOwnerIf(const TThing& Thing, const TLambda& Callback);

		DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOwnershipChanged, const TThing& Thing, const TOptional<TOwner> NewOwner);
		/** Called when ownership changes for a thing. */
		FOnOwnershipChanged& OnOwnershipChanged() { return OnOwnershipChangedDelegate; }

	private:

		struct FOwnershipData
		{
			/** Sorted by the order in which ownership was registered. */
			TArray<TOwner> OwnershipPriority;
			
			const TOwner* GetOwner() const { return OwnershipPriority.IsEmpty() ? nullptr : &OwnershipPriority[0]; }
		};
		/** Stores the ownership state. */
		TMap<TThing, FOwnershipData> Ownership;

		/** Called when ownership changes for a thing. */
		FOnOwnershipChanged OnOwnershipChangedDelegate;

		template<typename TRemoveLambda>
		void RemoveOwnershipSingle(const TOwner& Owner, const TThing& Thing, FOwnershipData* OwnershipData, const TRemoveLambda& RemoveLambda);
		void RemoveOwnershipSingle(const TOwner& Owner, const TThing& Thing)
		{
			RemoveOwnershipSingle(Owner, Thing, Ownership.Find(Thing), [this, &Thing](){ Ownership.Remove(Thing); });
		}
		void RemoveOwnershipAll(const TOwner& Owner);
	};

	template <CThing TThing, COwner TOwner> 
	bool TOwnershipMapping<TThing, TOwner>::TryTakeOwnership(const TOwner& Owner, const TThing& Thing)
	{
		FOwnershipData& OwnershipData = Ownership.FindOrAdd(Thing);
		const int32 NumBefore = OwnershipData.OwnershipPriority.Num();
		OwnershipData.OwnershipPriority.AddUnique(Owner);
		
		if (NumBefore == 0)
		{
			OnOwnershipChangedDelegate.Broadcast(Thing, { Owner });
			return true;
		}

		return *OwnershipData.GetOwner() == Owner;
	}

	template <CThing TThing, COwner TOwner>
	void TOwnershipMapping<TThing, TOwner>::Clear(bool bSilent)
	{
		if (bSilent)
		{
			Ownership.Empty();
		}
		else
		{
			TArray<TThing> OldThings;
			Ownership.GetKeys(OldThings);
			Ownership.Empty();
			
			for (const TThing& Thing : OldThings)
			{
				// Previous OnOwnershipChangedDelegate call may have changed the ownership
				if (!HasOwner(Thing))
				{
					OnOwnershipChangedDelegate.Broadcast(Thing, {});
				}
			}
		}
	}

	template <CThing TThing, COwner TOwner> 
	const TOwner* TOwnershipMapping<TThing, TOwner>::GetOwner(const TThing& Thing) const
	{
		const FOwnershipData* OwnershipData = Ownership.Find(Thing);
		return OwnershipData ? OwnershipData->GetOwner() : nullptr;
	}

	template <CThing TThing, COwner TOwner>
	template <typename TLambda> requires std::is_invocable_r_v<bool, TLambda, const TOwner&>
	void TOwnershipMapping<TThing, TOwner>::RemovePotentialOwnerIf(const TThing& Thing, const TLambda& Callback)
	{
		FOwnershipData* OwnershipData = Ownership.Find(Thing);
		if (!OwnershipData)
		{
			return;
		}

		const TOwner* PreviousOwnerPointer = GetOwner(Thing);
		const TOptional<TOwner> PreviousOwner = PreviousOwnerPointer ? *PreviousOwnerPointer : TOptional<TOwner>{};
		for (auto It = OwnershipData->OwnershipPriority.CreateIterator(); It; ++It)
		{
			const bool bRemoveItem = Callback(*It);
			if (bRemoveItem)
			{
				It.RemoveCurrent();
			}
		}

		if (OwnershipData->OwnershipPriority.IsEmpty())
		{
			Ownership.Remove(Thing);
			OnOwnershipChangedDelegate.Broadcast(Thing, {});
			return;
		}

		const TOwner* NewOwner = GetOwner(Thing);
		const bool bHasChangedOwner =  PreviousOwner && ensureMsgf(NewOwner, TEXT("NewOwner should not be null if OwnershipPriority is non-empty")) && *NewOwner != *PreviousOwner;
		if (bHasChangedOwner)
		{
			OnOwnershipChangedDelegate.Broadcast(Thing, *NewOwner);
		}
	}

	template <CThing TThing, COwner TOwner>
	template <typename TRemoveLambda>
	void TOwnershipMapping<TThing, TOwner>::RemoveOwnershipSingle(const TOwner& Owner, const TThing& Thing, FOwnershipData* OwnershipData, const TRemoveLambda& RemoveLambda)
	{
		const int32 NumRemoved = OwnershipData ? OwnershipData->OwnershipPriority.RemoveSingle(Owner) : 0;
		if (NumRemoved == 0)
		{
			return;
		}

		if (const TOwner* NewOwner = OwnershipData->GetOwner())
		{
			OnOwnershipChangedDelegate.Broadcast(Thing, { *NewOwner });
		}
		else
		{
			const TThing OldThing = Thing; // RemoveLambda may leave Thing dangling
			RemoveLambda();
			OnOwnershipChangedDelegate.Broadcast(OldThing, {});
		}
	}

	template <CThing TThing, COwner TOwner>
	void TOwnershipMapping<TThing, TOwner>::RemoveOwnershipAll(const TOwner& Owner)
	{
		for (auto OwnershipIt = Ownership.CreateIterator(); OwnershipIt; ++OwnershipIt)
		{
			RemoveOwnershipSingle(Owner, OwnershipIt->Key, &OwnershipIt->Value, [&OwnershipIt]()
			{
				OwnershipIt.RemoveCurrent();
			});
		}
	}
}

