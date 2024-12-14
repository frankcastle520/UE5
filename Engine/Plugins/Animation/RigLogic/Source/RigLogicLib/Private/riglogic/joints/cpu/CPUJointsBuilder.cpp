// Copyright Epic Games, Inc. All Rights Reserved.

#include "riglogic/joints/cpu/CPUJointsBuilder.h"

#include "riglogic/TypeDefs.h"
#include "riglogic/joints/cpu/CPUJointsEvaluator.h"
#include "riglogic/joints/cpu/CPUJointsOutputInstance.h"
#include "riglogic/joints/cpu/bpcm/BPCMJointsBuilderFactory.h"
#include "riglogic/joints/cpu/quaternions/QuaternionJointsBuilderFactory.h"
#include "riglogic/joints/cpu/twistswing/TwistSwingJointsBuilderFactory.h"
#include "riglogic/riglogic/RigMetrics.h"
#include "riglogic/system/simd/Detect.h"

namespace rl4 {

CPUJointsBuilder::CPUJointsBuilder(const Configuration& config_, MemoryResource* memRes_) :
    memRes{memRes_},
    config{config_},
    bpcmBuilder{},
    quaternionBuilder{},
    twistSwingBuilder{},
    numAttrsPerJoint{},
    jointAttributeCount{} {

    numAttrsPerJoint = static_cast<std::uint16_t>(static_cast<std::uint8_t>(config.translationType) +
                                                  static_cast<std::uint8_t>(config.rotationType) +
                                                  static_cast<std::uint8_t>(config.scaleType));
    bpcmBuilder = BPCMJointsBuilderFactory::create(config, memRes);
    quaternionBuilder = QuaternionJointsBuilderFactory::create(config, memRes);
    twistSwingBuilder = TwistSwingJointsBuilderFactory::create(config, memRes);
}

void CPUJointsBuilder::computeStorageRequirements(const RigMetrics& source) {
    jointAttributeCount = source.jointAttributeCount;
    bpcmBuilder->computeStorageRequirements(source);
    quaternionBuilder->computeStorageRequirements(source);
    twistSwingBuilder->computeStorageRequirements(source);
}

void CPUJointsBuilder::computeStorageRequirements(const JointBehaviorFilter& source) {
    jointAttributeCount = static_cast<std::uint16_t>(source.getJointCount() * numAttrsPerJoint);
    bpcmBuilder->computeStorageRequirements(source.excluded(dna::RotationRepresentation::Quaternion));
    quaternionBuilder->computeStorageRequirements(source.only(dna::RotationRepresentation::Quaternion));
    twistSwingBuilder->computeStorageRequirements(source);
}

void CPUJointsBuilder::allocateStorage(const JointBehaviorFilter& source) {
    bpcmBuilder->allocateStorage(source.excluded(dna::RotationRepresentation::Quaternion));
    quaternionBuilder->allocateStorage(source.only(dna::RotationRepresentation::Quaternion));
    twistSwingBuilder->allocateStorage(source);
}

void CPUJointsBuilder::fillStorage(const JointBehaviorFilter& source) {
    bpcmBuilder->fillStorage(source.excluded(dna::RotationRepresentation::Quaternion));
    quaternionBuilder->fillStorage(source.only(dna::RotationRepresentation::Quaternion));
    twistSwingBuilder->fillStorage(source);
}

JointsEvaluator::Pointer CPUJointsBuilder::build() {
    const auto attrCount = jointAttributeCount;
    const auto translationType = config.translationType;
    const auto rotationType = config.rotationType;
    const auto scaleType = config.scaleType;
    auto instanceFactory = [attrCount, translationType, rotationType, scaleType](MemoryResource* instanceMemRes) {
            return UniqueInstance<CPUJointsOutputInstance, JointsOutputInstance>::with(instanceMemRes).create(attrCount,
                                                                                                              translationType,
                                                                                                              rotationType,
                                                                                                              scaleType,
                                                                                                              instanceMemRes);
        };
    auto bpcmEvaluator = bpcmBuilder->build();
    auto quaternionEvaluator = quaternionBuilder->build();
    auto twistSwingEvaluator = twistSwingBuilder->build();
    auto factory = UniqueInstance<CPUJointsEvaluator, JointsEvaluator>::with(memRes);
    return factory.create(std::move(bpcmEvaluator),
                          std::move(quaternionEvaluator),
                          std::move(twistSwingEvaluator),
                          instanceFactory);
}

}  // namespace rl4
