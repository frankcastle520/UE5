// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"
#include "riglogic/rbf/RBFBehaviorOutputInstance.h"

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <functional>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace rl4 {

namespace rbf {

namespace cpu {

class OutputInstance : public RBFBehaviorOutputInstance {
    public:
        using Factory = std::function<Pointer(std::uint16_t, std::uint16_t, MemoryResource*)>;

    public:
        OutputInstance(std::uint16_t maximumInputCount, std::uint16_t maximumTargetCount, MemoryResource* memRes);
        ArrayView<float> getInputBuffer() override;
        ArrayView<float> getIntermediateWeightsBuffer() override;
        ArrayView<float> getOutputWeightsBuffer() override;

    private:
        AlignedVector<float> inputBuffer;
        AlignedVector<float> intermediateWeightsBuffer;
        AlignedVector<float> outputWeightsBuffer;

};

}  // namespace cpu

}  // namespace rbf

}  // namespace rl4
