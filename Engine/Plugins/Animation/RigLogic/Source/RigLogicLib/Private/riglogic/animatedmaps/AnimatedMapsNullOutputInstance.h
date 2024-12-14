// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"
#include "riglogic/animatedmaps/AnimatedMapsOutputInstance.h"

namespace rl4 {

class AnimatedMapsNullOutputInstance : public AnimatedMapsOutputInstance {
    public:
        ArrayView<float> getOutputBuffer() override;
        void resetOutputBuffer() override;

};

}  // namespace rl4
