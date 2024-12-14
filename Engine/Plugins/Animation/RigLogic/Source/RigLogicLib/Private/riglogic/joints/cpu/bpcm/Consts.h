// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>

namespace rl4 {

namespace bpcm {

constexpr std::size_t cacheLineSize = 64ul;  // bytes
constexpr std::size_t lookAheadOffset = cacheLineSize / sizeof(float);  // in number of elements

}  // namespace bpcm

}  // namespace rl4
