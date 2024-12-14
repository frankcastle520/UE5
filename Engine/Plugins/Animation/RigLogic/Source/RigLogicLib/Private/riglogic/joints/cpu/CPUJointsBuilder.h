// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"
#include "riglogic/controls/ControlsInputInstance.h"
#include "riglogic/joints/JointBehaviorFilter.h"
#include "riglogic/joints/JointsBuilder.h"
#include "riglogic/joints/JointsEvaluator.h"
#include "riglogic/joints/JointsOutputInstance.h"
#include "riglogic/riglogic/Configuration.h"

#include <cstdint>

namespace rl4 {

class CPUJointsBuilder : public JointsBuilder {
    public:
        CPUJointsBuilder(const Configuration& config_, MemoryResource* memRes_);
        void computeStorageRequirements(const RigMetrics& source) override;
        void computeStorageRequirements(const JointBehaviorFilter& source) override;
        void allocateStorage(const JointBehaviorFilter& source) override;
        void fillStorage(const JointBehaviorFilter& source) override;
        JointsEvaluator::Pointer build() override;

    private:
        MemoryResource* memRes;
        Configuration config;
        UniqueInstance<JointsBuilder>::PointerType bpcmBuilder;
        UniqueInstance<JointsBuilder>::PointerType quaternionBuilder;
        UniqueInstance<JointsBuilder>::PointerType twistSwingBuilder;
        std::uint16_t numAttrsPerJoint;
        std::uint16_t jointAttributeCount;
};

}  // namespace rl4
