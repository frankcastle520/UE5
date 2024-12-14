// Copyright Epic Games, Inc. All Rights Reserved.

#include "riglogic/joints/cpu/twistswing/TwistSwingJointsBuilderFactory.h"

#include "riglogic/joints/cpu/twistswing/TwistSwingJointsBuilder.h"
#include "riglogic/system/simd/Detect.h"

namespace rl4 {

UniqueInstance<JointsBuilder>::PointerType TwistSwingJointsBuilderFactory::create(const Configuration& config,
                                                                                  MemoryResource* memRes) {
    // At the moment, only a Scalar implementation exists for twist and swing evaluation, so there is no need to
    // compile any other configuration.
    /*
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
            using SSETwistSwingJointsBuilder = TwistSwingJointsBuilder<StorageType, trimd::sse::F256, trimd::sse::F128>;
            return UniqueInstance<SSETwistSwingJointsBuilder, JointsBuilder>::with(memRes).create(config, memRes);
        }
    #endif  // RL_BUILD_WITH_SSE
    #ifdef RL_BUILD_WITH_AVX
        if ((config.calculationType == CalculationType::AVX) || (config.calculationType == CalculationType::AnyVector)) {
            using AVXTwistSwingJointsBuilder = TwistSwingJointsBuilder<StorageType, trimd::avx::F256, trimd::sse::F128>;
            return UniqueInstance<AVXTwistSwingJointsBuilder, JointsBuilder>::with(memRes).create(config, memRes);
        }
    #endif  // RL_BUILD_WITH_AVX
    #ifdef RL_BUILD_WITH_NEON
        if ((config.calculationType == CalculationType::NEON) || (config.calculationType == CalculationType::AnyVector)) {
            using NEONTwistSwingJointsBuilder = TwistSwingJointsBuilder<StorageType, trimd::neon::F256, trimd::neon::F128>;
            return UniqueInstance<NEONTwistSwingJointsBuilder, JointsBuilder>::with(memRes).create(config, memRes);
        }
    #endif  // RL_BUILD_WITH_NEON
    */
    using ScalarTwistSwingJointsBuilder = TwistSwingJointsBuilder<float, trimd::scalar::F256, trimd::scalar::F128>;
    return UniqueInstance<ScalarTwistSwingJointsBuilder, JointsBuilder>::with(memRes).create(config, memRes);
}

}  // namespace rl4
