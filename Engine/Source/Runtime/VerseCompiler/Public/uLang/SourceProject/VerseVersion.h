// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cstdint>

namespace Verse
{
    namespace Version
    {
        static constexpr uint32_t Primordial = 0; // A retroactively defined version for pre-versioned Verse.

        static constexpr uint32_t V1 = 1;
        // Changes in V1:
        static constexpr uint32_t SetMutatesFallibility = V1;
        static constexpr uint32_t MapLiteralKeysHandleIterationAndFailure = V1;
        static constexpr uint32_t DontMixCommaAndSemicolonInBlocks = V1;
        static constexpr uint32_t UniqueAttributeRequiresAllocatesEffect = V1;
        static constexpr uint32_t LocalQualifiers = V1;
        static constexpr uint32_t StructFieldsMustBePublic = V1;
        // V1 is now stabilized, and further changes should not be added to it!

        static constexpr uint32_t V2 = 2;
        // Changes in V2 (note that more may be added as long as LatestStable < V2):

        static constexpr uint32_t LatestStable = V1;
        static constexpr uint32_t LatestUnstable = V2;

        static constexpr uint32_t Default = LatestStable;

        // The minimum and maximum defined Verse versions: note that this is distinct from the minimum and maximum *allowed* Verse versions.
        static constexpr uint32_t Minimum = Primordial;
        static constexpr uint32_t Maximum = LatestUnstable;
    }
}
