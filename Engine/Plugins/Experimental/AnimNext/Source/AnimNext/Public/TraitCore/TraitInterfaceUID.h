// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::AnimNext
{
	// Type alias for a raw trait UID, not typesafe
	using FTraitInterfaceUIDRaw = uint32;

	/**
	 * FTraitInterfaceUID
	 *
	 * Encapsulates an interface global UID.
	 * The string is exposed in non-shipping builds for logging and debugging purposes.
	 * The UID should be generated from the provided string using FNV1a with 32 bits.
	 *
	 * The whole struct is meant to be 'constexpr' to allow inlining in interface queries.
	 */
	struct FTraitInterfaceUID final
	{
		// Constructs an invalid UID
		constexpr FTraitInterfaceUID()
			: UID(INVALID_UID)
#if !UE_BUILD_SHIPPING
			, InterfaceName(TEXT("<Invalid Interface UID>"))
#endif
		{}

		// Constructs an interface UID
		explicit constexpr FTraitInterfaceUID(FTraitInterfaceUIDRaw InUID, const TCHAR* InInterfaceName = TEXT("<Unknown Trait Interface Name>"))
			: UID(InUID)
#if !UE_BUILD_SHIPPING
			, InterfaceName(InInterfaceName)
#endif
		{
		}

#if !UE_BUILD_SHIPPING
		// Returns a literal string to the interface name
		constexpr const TCHAR* GetInterfaceName() const { return InterfaceName; }
#endif

		// Returns the interface global UID
		constexpr FTraitInterfaceUIDRaw GetUID() const { return UID; }

		// Returns whether this UID is valid or not
		constexpr bool IsValid() const { return UID != INVALID_UID; }

	private:
		static constexpr FTraitInterfaceUIDRaw INVALID_UID = 0;

		FTraitInterfaceUIDRaw		UID;

#if !UE_BUILD_SHIPPING || WITH_EDITOR
		const TCHAR*				InterfaceName = nullptr;
#endif
	};

	// Compares for equality and inequality
	constexpr bool operator==(FTraitInterfaceUID LHS, FTraitInterfaceUID RHS) { return LHS.GetUID() == RHS.GetUID(); }
	constexpr bool operator!=(FTraitInterfaceUID LHS, FTraitInterfaceUID RHS) { return LHS.GetUID() != RHS.GetUID(); }

	// For sorting
	constexpr bool operator<(FTraitInterfaceUID LHS, FTraitInterfaceUID RHS) { return LHS.GetUID() < RHS.GetUID(); }
}
