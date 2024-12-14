// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"
#include "riglogic/types/PaddedBlockView.h"

#include <cstdint>

namespace rl4 {

using LODRegion = PaddedBlockView;

template<typename TValue>
struct JointGroup {
    // All non-zero values
    AlignedVector<TValue> values;
    // Sub-matrix col -> input vector
    AlignedVector<std::uint16_t> inputIndices;
    // Sub-matrix row -> output vector
    AlignedVector<std::uint16_t> outputIndices;
    // Output index boundaries for each LOD
    Vector<LODRegion> lods;

    explicit JointGroup(MemoryResource* memRes) :
        values{memRes},
        inputIndices{memRes},
        outputIndices{memRes},
        lods{memRes} {
    }

    template<class Archive>
    void serialize(Archive& archive) {
        archive(values, inputIndices, outputIndices, lods);
    }

};

}  // namespace rl4
