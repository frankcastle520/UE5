// Copyright Epic Games, Inc. All Rights Reserved.

#include "riglogic/joints/cpu/CPUJointsEvaluator.h"

namespace rl4 {

CPUJointsEvaluator::CPUJointsEvaluator(JointsEvaluator::Pointer bpcmEvaluator_,
                                       JointsEvaluator::Pointer quaternionEvaluator_,
                                       JointsEvaluator::Pointer twistSwingEvaluator_,
                                       JointsOutputInstance::Factory instanceFactory_) :
    bpcmEvaluator{std::move(bpcmEvaluator_)},
    quaternionEvaluator{std::move(quaternionEvaluator_)},
    twistSwingEvaluator{std::move(twistSwingEvaluator_)},
    instanceFactory{instanceFactory_} {
}

JointsOutputInstance::Pointer CPUJointsEvaluator::createInstance(MemoryResource* instanceMemRes) const {
    return instanceFactory(instanceMemRes);
}

void CPUJointsEvaluator::calculate(const ControlsInputInstance* inputs, JointsOutputInstance* outputs, std::uint16_t lod) const {
    bpcmEvaluator->calculate(inputs, outputs, lod);
    quaternionEvaluator->calculate(inputs, outputs, lod);
    twistSwingEvaluator->calculate(inputs, outputs, lod);
}

void CPUJointsEvaluator::calculate(const ControlsInputInstance* inputs,
                                   JointsOutputInstance* outputs,
                                   std::uint16_t lod,
                                   std::uint16_t jointGroupIndex) const {
    bpcmEvaluator->calculate(inputs, outputs, lod, jointGroupIndex);
    quaternionEvaluator->calculate(inputs, outputs, lod, jointGroupIndex);
    // No twist swing evaluation per joint group
}

void CPUJointsEvaluator::load(terse::BinaryInputArchive<BoundedIOStream>& archive) {
    bpcmEvaluator->load(archive);
    quaternionEvaluator->load(archive);
    twistSwingEvaluator->load(archive);
}

void CPUJointsEvaluator::save(terse::BinaryOutputArchive<BoundedIOStream>& archive) {
    bpcmEvaluator->save(archive);
    quaternionEvaluator->save(archive);
    twistSwingEvaluator->save(archive);
}

}  // namespace rl4
