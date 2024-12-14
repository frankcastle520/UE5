// Copyright Epic Games, Inc. All Rights Reserved.

#include "riglogic/rbf/cpu/CPURBFBehaviorOutputInstance.h"

namespace rl4 {

namespace rbf {

namespace cpu {

OutputInstance::OutputInstance(std::uint16_t maximumInputCount, std::uint16_t maximumTargetCount, MemoryResource* memRes) :
    inputBuffer{maximumInputCount, {}, memRes},
    intermediateWeightsBuffer{maximumTargetCount, {}, memRes},
    outputWeightsBuffer{maximumTargetCount, {}, memRes} {
}

ArrayView<float> OutputInstance::getInputBuffer() {
    return inputBuffer;
}

ArrayView<float> OutputInstance::getIntermediateWeightsBuffer() {
    return intermediateWeightsBuffer;
}

ArrayView<float> OutputInstance::getOutputWeightsBuffer() {
    return outputWeightsBuffer;
}

}  // namespace cpu

}  // namespace rbf

}  // namespace rl4
