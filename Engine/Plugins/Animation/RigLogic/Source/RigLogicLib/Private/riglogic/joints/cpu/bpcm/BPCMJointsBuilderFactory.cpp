// Copyright Epic Games, Inc. All Rights Reserved.

#include "riglogic/joints/cpu/bpcm/BPCMJointsBuilderFactory.h"

#include "riglogic/joints/cpu/bpcm/BPCMJointsBuilder.h"
#include "riglogic/system/simd/Detect.h"

namespace rl4 {

UniqueInstance<JointsBuilder>::PointerType BPCMJointsBuilderFactory::create(const Configuration& config, MemoryResource* memRes) {
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wunused-local-typedef"
    #endif
    #if !defined(__clang__) && defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wunused-local-typedefs"
    #endif
    #ifdef RL_USE_HALF_FLOATS
        using StorageType = std::uint16_t;
    #else
        using StorageType = float;
    #endif  // RL_USE_HALF_FLOATS
    #if !defined(__clang__) && defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif
    #if defined(__clang__)
        #pragma clang diagnostic pop
    #endif

    #ifdef RL_BUILD_WITH_SSE
        if ((config.calculationType == CalculationType::SSE) || (config.calculationType == CalculationType::AnyVector)) {
            using SSEBPCMJointsBuilder = bpcm::BPCMJointsBuilder<StorageType, trimd::sse::F128>;
            return UniqueInstance<SSEBPCMJointsBuilder, JointsBuilder>::with(memRes).create(config, memRes);
        }
    #endif  // RL_BUILD_WITH_SSE
    #ifdef RL_BUILD_WITH_AVX
        if ((config.calculationType == CalculationType::AVX) || (config.calculationType == CalculationType::AnyVector)) {
            using AVXBPCMJointsBuilder = bpcm::BPCMJointsBuilder<StorageType, trimd::avx::F256>;
            return UniqueInstance<AVXBPCMJointsBuilder, JointsBuilder>::with(memRes).create(config, memRes);
        }
    #endif  // RL_BUILD_WITH_AVX
    #ifdef RL_BUILD_WITH_NEON
        if ((config.calculationType == CalculationType::NEON) || (config.calculationType == CalculationType::AnyVector)) {
            using NEONBPCMJointsBuilder = bpcm::BPCMJointsBuilder<StorageType, trimd::neon::F128>;
            return UniqueInstance<NEONBPCMJointsBuilder, JointsBuilder>::with(memRes).create(config, memRes);
        }
    #endif  // RL_BUILD_WITH_NEON
    using ScalarBPCMJointsBuilder = bpcm::BPCMJointsBuilder<float, trimd::scalar::F128>;
    return UniqueInstance<ScalarBPCMJointsBuilder, JointsBuilder>::with(memRes).create(config, memRes);
}

}  // namespace rl4
