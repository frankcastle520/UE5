// Copyright Epic Games, Inc. All Rights Reserved.

#include "riglogic/rbf/RBFBehaviorNullOutputInstance.h"

namespace rl4 {

ArrayView<float> RBFBehaviorNullOutputInstance::getInputBuffer() {
    return {};
}

ArrayView<float> RBFBehaviorNullOutputInstance::getIntermediateWeightsBuffer() {
    return {};
}

ArrayView<float> RBFBehaviorNullOutputInstance::getOutputWeightsBuffer() {
    return {};
}

}  // namespace rl4
