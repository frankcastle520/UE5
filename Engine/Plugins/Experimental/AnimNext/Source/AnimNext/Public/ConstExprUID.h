// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::AnimNext
{
	// Type alias for a raw UID, not typesafe
	using FFConstExprUIDRaw = uint32;

	// Implements a constexpr usable version of FNVa32 for ANSICHAR string literals
	constexpr uint32 ConstexprStringFnv32(const ANSICHAR* StringLiteral)
	{
		constexpr uint32 Offset = 0x811c9dc5;
		constexpr uint32 Prime = 0x01000193;

		const char* CharPtr = StringLiteral;

		uint32 Fnv = Offset;
		while (*CharPtr != 0)
		{
			Fnv ^= *CharPtr++;
			Fnv *= Prime;
		}

		return Fnv;
	}

	// Implements a constexpr usable version of FNVa32 for WIDECHAR string literals
	constexpr uint32 ConstexprStringFnv32(const WIDECHAR* StringLiteral)
	{
		constexpr uint32 Offset = 0x811c9dc5;
		constexpr uint32 Prime = 0x01000193;

		const WIDECHAR* CharPtr = StringLiteral;

		uint32 Fnv = Offset;
		while (*CharPtr != 0)
		{
			const uint32 HighByte = (*CharPtr >> 8) & 0xff;
			Fnv ^= HighByte;
			Fnv *= Prime;

			const uint32 LowByte = *CharPtr & 0xff;
			Fnv ^= LowByte;
			Fnv *= Prime;

			CharPtr++;
		}

		return Fnv;
	}

	/**
	 * A constexpr safe UID
	 *
	 * Encapsulates an constexpr UID.
	 * The string is exposed in non-shipping builds for logging and debugging purposes.
	 * The UID should be generated from the provided string using FNV1a with 32 bits.
	 *
	 * The whole struct is meant to be 'constexpr' to allow inlining.
	 */
	struct FConstExprUID final
	{
		// Constructs an invalid UID
		constexpr FConstExprUID() noexcept
			: UID(INVALID_UID)
#if !UE_BUILD_SHIPPING
			, DebugName(TEXT("<Invalid ConstExprUID UID>"))
#endif
		{
		}

		// Constructs a UID
		explicit constexpr FConstExprUID(FFConstExprUIDRaw InUID, const TCHAR* InDebugName = TEXT("<Unknown ConstExprUID Name>")) noexcept
			: UID(InUID)
#if !UE_BUILD_SHIPPING
			, DebugName(InDebugName)
#endif
		{
		}

		// Constructs a UID
		static constexpr FConstExprUID MakeFromString(const TCHAR* InName) noexcept
		{
			return FConstExprUID(ConstexprStringFnv32(InName), InName);
		}

#if !UE_BUILD_SHIPPING
		// Returns a literal string to the debug name
		constexpr const TCHAR* GetDebugName() const noexcept { return DebugName; }
#endif

		// Returns the raw UID
		constexpr FFConstExprUIDRaw GetUID() const noexcept { return UID; }

		// Returns whether this UID is valid or not
		constexpr bool IsValid() const noexcept { return UID != INVALID_UID; }

	private:
		static constexpr FFConstExprUIDRaw INVALID_UID = 0;

		FFConstExprUIDRaw	UID;

#if !UE_BUILD_SHIPPING
		const TCHAR*		DebugName;
#endif
	};

	// Compares for equality and inequality
	constexpr bool operator==(FConstExprUID LHS, FConstExprUID RHS) noexcept { return LHS.GetUID() == RHS.GetUID(); }
	constexpr bool operator!=(FConstExprUID LHS, FConstExprUID RHS) noexcept { return LHS.GetUID() != RHS.GetUID(); }
	constexpr bool operator==(FConstExprUID LHS, FFConstExprUIDRaw RHS) noexcept { return LHS.GetUID() == RHS; }
	constexpr bool operator!=(FConstExprUID LHS, FFConstExprUIDRaw RHS) noexcept { return LHS.GetUID() != RHS; }
	constexpr bool operator==(FFConstExprUIDRaw LHS, FConstExprUID RHS) noexcept { return LHS == RHS.GetUID(); }
	constexpr bool operator!=(FFConstExprUIDRaw LHS, FConstExprUID RHS) noexcept { return LHS != RHS.GetUID(); }

	// For sorting
	constexpr bool operator<(FConstExprUID LHS, FConstExprUID RHS) { return LHS.GetUID() < RHS.GetUID(); }
}
