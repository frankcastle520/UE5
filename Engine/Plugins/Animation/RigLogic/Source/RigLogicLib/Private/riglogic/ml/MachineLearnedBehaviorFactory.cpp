// Copyright Epic Games, Inc. All Rights Reserved.

#include "riglogic/ml/MachineLearnedBehaviorFactory.h"

#include "riglogic/TypeDefs.h"
#include "riglogic/utils/Extd.h"
#include "riglogic/ml/MachineLearnedBehaviorEvaluator.h"
#include "riglogic/ml/MachineLearnedBehaviorNullEvaluator.h"
#include "riglogic/ml/cpu/CPUMachineLearnedBehaviorFactory.h"
#include "riglogic/riglogic/Configuration.h"
#include "riglogic/riglogic/RigMetrics.h"
#include "riglogic/system/simd/SIMD.h"

#include <cstdint>

namespace rl4 {

MachineLearnedBehaviorEvaluator::Pointer createMLEvaluator(const Configuration& config,
                                                           const dna::MachineLearnedBehaviorReader* reader,
                                                           MemoryResource* memRes) {
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wunused-local-typedef"
    #endif
    #if !defined(__clang__) && defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wunused-local-typedefs"
    #endif
    #ifdef RL_USE_HALF_FLOATS
        using StorageValueType = std::uint16_t;
    #else
        using StorageValueType = float;
    #endif  // RL_USE_HALF_FLOATS
    #if !defined(__clang__) && defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif
    #if defined(__clang__)
        #pragma clang diagnostic pop
    #endif

    // Work around unused parameter warning when building without SSE and AVX
    static_cast<void>(config);
    #ifdef RL_BUILD_WITH_SSE
        if ((config.calculationType == CalculationType::SSE) || (config.calculationType == CalculationType::AnyVector)) {
            return ml::cpu::Factory<StorageValueType, trimd::sse::F256, trimd::sse::F128>::create(reader, memRes);
        }
    #endif  // RL_BUILD_WITH_SSE
    #ifdef RL_BUILD_WITH_AVX
        if ((config.calculationType == CalculationType::AVX) || (config.calculationType == CalculationType::AnyVector)) {
            // Use 256-bit AVX registers and whatever 128-bit width type is available
            return ml::cpu::Factory<StorageValueType, trimd::avx::F256, trimd::sse::F128>::create(reader, memRes);
        }
    #endif  // RL_BUILD_WITH_AVX
    #ifdef RL_BUILD_WITH_NEON
        if ((config.calculationType == CalculationType::NEON) || (config.calculationType == CalculationType::AnyVector)) {
            return ml::cpu::Factory<StorageValueType, trimd::neon::F256, trimd::neon::F128>::create(reader, memRes);
        }
    #endif  // RL_BUILD_WITH_NEON
    return ml::cpu::Factory<float, trimd::scalar::F256, trimd::scalar::F128>::create(reader, memRes);
}

MachineLearnedBehavior::Pointer MachineLearnedBehaviorFactory::create(const Configuration& config,
                                                                      const dna::MachineLearnedBehaviorReader* reader,
                                                                      MemoryResource* memRes) {
    auto moduleFactory = UniqueInstance<MachineLearnedBehavior>::with(memRes);
    if (!config.loadMachineLearnedBehavior || (reader->getNeuralNetworkCount() == 0u)) {
        auto evaluator =
            UniqueInstance<MachineLearnedBehaviorNullEvaluator, MachineLearnedBehaviorEvaluator>::with(memRes).create();
        return moduleFactory.create(std::move(evaluator), memRes);
    }

    Vector<Matrix<std::uint16_t> > neuralNetworkIndicesPerMeshRegion{memRes};
    neuralNetworkIndicesPerMeshRegion.resize(reader->getMeshCount());
    for (std::uint16_t meshIdx = {}; meshIdx < neuralNetworkIndicesPerMeshRegion.size(); ++meshIdx) {
        neuralNetworkIndicesPerMeshRegion[meshIdx].resize(reader->getMeshRegionCount(meshIdx));
        for (std::uint16_t regionIdx = {}; regionIdx < neuralNetworkIndicesPerMeshRegion[meshIdx].size(); ++regionIdx) {
            const auto netIndices = reader->getNeuralNetworkIndicesForMeshRegion(meshIdx, regionIdx);
            neuralNetworkIndicesPerMeshRegion[meshIdx][regionIdx].assign(netIndices.begin(), netIndices.end());
        }
    }

    return moduleFactory.create(createMLEvaluator(config, reader, memRes), std::move(neuralNetworkIndicesPerMeshRegion));
}

MachineLearnedBehavior::Pointer MachineLearnedBehaviorFactory::create(const Configuration& config,
                                                                      const RigMetrics& metrics,
                                                                      MemoryResource* memRes) {
    auto moduleFactory = UniqueInstance<MachineLearnedBehavior>::with(memRes);
    if (!config.loadMachineLearnedBehavior || (metrics.neuralNetworkCount == 0u)) {
        auto evaluator =
            UniqueInstance<MachineLearnedBehaviorNullEvaluator, MachineLearnedBehaviorEvaluator>::with(memRes).create();
        return moduleFactory.create(std::move(evaluator), memRes);
    }
    return moduleFactory.create(createMLEvaluator(config, nullptr, memRes), memRes);
}

}  // namespace rl4
