// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::AnimNext
{
	// Type alias for a raw trait UID, not typesafe
	using FTraitUIDRaw = uint32;

	/**
	 * FTraitUID
	 *
	 * Encapsulates an trait global UID.
	 * The string is exposed in non-shipping builds for logging and debugging purposes.
	 * The UID should be generated from the provided string using FNV1a with 32 bits.
	 *
	 * The whole struct is meant to be 'constexpr' to allow inlining.
	 */
	struct FTraitUID final
	{
		// Constructs an invalid UID
		constexpr FTraitUID() noexcept
			: UID(INVALID_UID)
#if !UE_BUILD_SHIPPING
			, TraitName(TEXT("<Invalid trait UID>"))
#endif
		{
		}

		// Constructs a trait UID
		explicit constexpr FTraitUID(FTraitUIDRaw InUID, const TCHAR* InTraitName = TEXT("<Unknown Trait Name>")) noexcept
			: UID(InUID)
#if !UE_BUILD_SHIPPING
			, TraitName(InTraitName)
#endif
		{
		}

#if !UE_BUILD_SHIPPING
		// Returns a literal string to the interface name
		constexpr const TCHAR* GetTraitName() const noexcept { return TraitName; }
#endif

		// Returns the trait global UID
		constexpr FTraitUIDRaw GetUID() const noexcept { return UID; }

		// Returns whether this UID is valid or not
		constexpr bool IsValid() const noexcept { return UID != INVALID_UID; }

	private:
		static constexpr FTraitUIDRaw INVALID_UID = 0;

		FTraitUIDRaw	UID;

#if !UE_BUILD_SHIPPING
		const TCHAR*	TraitName;
#endif
	};

	// Compares for equality and inequality
	constexpr bool operator==(FTraitUID LHS, FTraitUID RHS) noexcept { return LHS.GetUID() == RHS.GetUID(); }
	constexpr bool operator!=(FTraitUID LHS, FTraitUID RHS) noexcept { return LHS.GetUID() != RHS.GetUID(); }
	constexpr bool operator==(FTraitUID LHS, FTraitUIDRaw RHS) noexcept { return LHS.GetUID() == RHS; }
	constexpr bool operator!=(FTraitUID LHS, FTraitUIDRaw RHS) noexcept { return LHS.GetUID() != RHS; }
	constexpr bool operator==(FTraitUIDRaw LHS, FTraitUID RHS) noexcept { return LHS == RHS.GetUID(); }
	constexpr bool operator!=(FTraitUIDRaw LHS, FTraitUID RHS) noexcept { return LHS != RHS.GetUID(); }
}
