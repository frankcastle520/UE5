// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"
#include "riglogic/controls/ControlsInputInstance.h"
#include "riglogic/joints/JointsEvaluator.h"
#include "riglogic/joints/JointsOutputInstance.h"
#include "riglogic/joints/cpu/quaternions/CalculationStrategy.h"
#include "riglogic/joints/cpu/quaternions/JointGroup.h"

#include <cstdint>

namespace rl4 {

template<typename TValue>
class QuaternionJointsEvaluator : public JointsEvaluator {
    public:
        struct Accessor;
        friend Accessor;

    private:
        using CalculationStrategyPointer = typename UniqueInstance<JointGroupQuaternionCalculationStrategy<TValue> >::PointerType;

    public:
        QuaternionJointsEvaluator(CalculationStrategyPointer strategy_,
                                  Vector<JointGroup<TValue> >&& jointGroups_,
                                  JointsOutputInstance::Factory instanceFactory_,
                                  MemoryResource* memRes);

        JointsOutputInstance::Pointer createInstance(MemoryResource* instanceMemRes) const override;
        void calculate(const ControlsInputInstance* inputs, JointsOutputInstance* outputs, std::uint16_t lod) const override;
        void calculate(const ControlsInputInstance* inputs,
                       JointsOutputInstance* outputs,
                       std::uint16_t lod,
                       std::uint16_t jointGroupIndex) const override;
        void load(terse::BinaryInputArchive<BoundedIOStream>& archive) override;
        void save(terse::BinaryOutputArchive<BoundedIOStream>& archive) override;

    private:
        CalculationStrategyPointer strategy;
        Vector<JointGroup<TValue> > jointGroups;
        JointsOutputInstance::Factory instanceFactory;

};

template<typename TValue>
QuaternionJointsEvaluator<TValue>::QuaternionJointsEvaluator(CalculationStrategyPointer strategy_,
                                                             Vector<JointGroup<TValue> >&& jointGroups_,
                                                             JointsOutputInstance::Factory instanceFactory_,
                                                             MemoryResource*  /*unused*/) :
    strategy{std::move(strategy_)},
    jointGroups{std::move(jointGroups_)},
    instanceFactory{instanceFactory_} {
}

template<typename TValue>
JointsOutputInstance::Pointer QuaternionJointsEvaluator<TValue>::createInstance(MemoryResource* instanceMemRes) const {
    return instanceFactory(instanceMemRes);
}

template<typename TValue>
void QuaternionJointsEvaluator<TValue>::calculate(const ControlsInputInstance* inputs,
                                                  JointsOutputInstance* outputs,
                                                  std::uint16_t lod) const {
    for (std::size_t i = {}; i < jointGroups.size(); ++i) {
        calculate(inputs, outputs, lod, static_cast<std::uint16_t>(i));
    }
}

template<typename TValue>
void QuaternionJointsEvaluator<TValue>::calculate(const ControlsInputInstance* inputs,
                                                  JointsOutputInstance* outputs,
                                                  std::uint16_t lod,
                                                  std::uint16_t jointGroupIndex) const {
    strategy->calculate(jointGroups[jointGroupIndex], inputs->getInputBuffer(), outputs->getOutputBuffer(), lod);
}

template<typename TValue>
void QuaternionJointsEvaluator<TValue>::load(terse::BinaryInputArchive<BoundedIOStream>& archive) {
    archive(jointGroups);
}

template<typename TValue>
void QuaternionJointsEvaluator<TValue>::save(terse::BinaryOutputArchive<BoundedIOStream>& archive) {
    archive(jointGroups);
}

}  // namespace rl4
