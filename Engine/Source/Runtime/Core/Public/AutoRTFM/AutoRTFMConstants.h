// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>

// WARNING: Any change in these constants will require a re-patch and re-build of LLVM!

namespace AutoRTFM
{
namespace Constants
{

	inline constexpr uint32_t Major = 0;
	inline constexpr uint32_t Minor = 2;
	inline constexpr uint32_t Patch = 0;

	// The Magic Mike constant - shared with the compiler implementation.
	// Used to ensure that prefix data pointers are what they claim to be
	// and were definitely injected by us.
	inline constexpr uint64_t MagicMike = 0xa273000000000000;

} // namespace Constants
} // namespace AutoRTFM
