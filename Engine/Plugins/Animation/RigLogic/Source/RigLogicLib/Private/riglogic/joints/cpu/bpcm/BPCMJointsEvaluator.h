// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"
#include "riglogic/controls/ControlsInputInstance.h"
#include "riglogic/joints/JointsEvaluator.h"
#include "riglogic/joints/JointsOutputInstance.h"
#include "riglogic/joints/cpu/bpcm/CalculationStrategy.h"
#include "riglogic/joints/cpu/bpcm/Storage.h"
#include "riglogic/joints/cpu/bpcm/StorageView.h"
#include "riglogic/riglogic/RigInstanceImpl.h"

#include <cstdint>

namespace rl4 {

namespace bpcm {

template<typename TValue>
class Evaluator : public JointsEvaluator {
    public:
        using CalculationStrategyPointer = typename UniqueInstance<JointGroupLinearCalculationStrategy<TValue> >::PointerType;

        struct Accessor;
        friend Accessor;

    public:
        Evaluator(JointStorage<TValue>&& storage_,
                  CalculationStrategyPointer strategy_,
                  JointsOutputInstance::Factory instanceFactory_,
                  MemoryResource* memRes_) :
            memRes{memRes_},
            storage{std::move(storage_)},
            jointGroups{takeStorageSnapshot(storage, memRes)},
            strategy{std::move(strategy_)},
            instanceFactory{instanceFactory_} {
        }

        JointsOutputInstance::Pointer createInstance(MemoryResource* instanceMemRes) const override {
            return instanceFactory(instanceMemRes);
        }

        void calculate(const ControlsInputInstance* inputs, JointsOutputInstance* outputs, std::uint16_t lod) const override {
            assert(strategy != nullptr);
            for (std::size_t i = {}; i < jointGroups.size(); ++i) {
                calculate(inputs, outputs, lod, static_cast<std::uint16_t>(i));
            }
        }

        void calculate(const ControlsInputInstance* inputs,
                       JointsOutputInstance* outputs,
                       std::uint16_t lod,
                       std::uint16_t jointGroupIndex) const override {
            assert(strategy != nullptr);
            strategy->calculate(jointGroups[jointGroupIndex],
                                inputs->getInputBuffer(),
                                outputs->getOutputBuffer(),
                                lod);
        }

        void load(terse::BinaryInputArchive<BoundedIOStream>& archive) override {
            archive(storage);
            jointGroups = takeStorageSnapshot(storage, memRes);
        }

        void save(terse::BinaryOutputArchive<BoundedIOStream>& archive) override {
            archive(storage);
        }

    private:
        MemoryResource* memRes;
        JointStorage<TValue> storage;
        Vector<JointGroupView<TValue> > jointGroups;
        CalculationStrategyPointer strategy;
        JointsOutputInstance::Factory instanceFactory;
};

}  // namespace bpcm

}  // namespace rl4
