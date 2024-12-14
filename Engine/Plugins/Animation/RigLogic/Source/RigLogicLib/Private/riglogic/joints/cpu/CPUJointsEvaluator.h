// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"
#include "riglogic/controls/ControlsInputInstance.h"
#include "riglogic/joints/JointsEvaluator.h"
#include "riglogic/joints/JointsOutputInstance.h"

#include <cstdint>

namespace rl4 {

class CPUJointsEvaluator : public JointsEvaluator {
    public:
        struct Accessor;
        friend Accessor;

    public:
        CPUJointsEvaluator(JointsEvaluator::Pointer bpcmEvaluator_,
                           JointsEvaluator::Pointer quaternionEvaluator_,
                           JointsEvaluator::Pointer twistSwingEvaluator_,
                           JointsOutputInstance::Factory instanceFactory_);
        JointsOutputInstance::Pointer createInstance(MemoryResource* instanceMemRes) const override;
        void calculate(const ControlsInputInstance* inputs, JointsOutputInstance* outputs, std::uint16_t lod) const override;
        void calculate(const ControlsInputInstance* inputs,
                       JointsOutputInstance* outputs,
                       std::uint16_t lod,
                       std::uint16_t jointGroupIndex) const override;
        void load(terse::BinaryInputArchive<BoundedIOStream>& archive) override;
        void save(terse::BinaryOutputArchive<BoundedIOStream>& archive) override;

    private:
        JointsEvaluator::Pointer bpcmEvaluator;
        JointsEvaluator::Pointer quaternionEvaluator;
        JointsEvaluator::Pointer twistSwingEvaluator;
        JointsOutputInstance::Factory instanceFactory;

};

}  // namespace rl4
