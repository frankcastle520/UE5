// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/rbf/RBFBehaviorOutputInstance.h"

namespace rl4 {

class RBFBehaviorNullOutputInstance : public RBFBehaviorOutputInstance {
    public:
        ArrayView<float> getInputBuffer() override;
        ArrayView<float> getIntermediateWeightsBuffer() override;
        ArrayView<float> getOutputWeightsBuffer() override;

};

}  // namespace rl4
