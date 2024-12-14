// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"

namespace rl4 {

class RBFBehaviorOutputInstance {
    public:
        using Pointer = UniqueInstance<RBFBehaviorOutputInstance>::PointerType;

    public:
        virtual ~RBFBehaviorOutputInstance();
        virtual ArrayView<float> getInputBuffer() = 0;
        virtual ArrayView<float> getIntermediateWeightsBuffer() = 0;
        virtual ArrayView<float> getOutputWeightsBuffer() = 0;

};

}  // namespace rl4
