// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"
#include "riglogic/joints/JointBehaviorFilter.h"
#include "riglogic/joints/JointsBuilder.h"
#include "riglogic/joints/cpu/bpcm/BPCMJointsEvaluator.h"
#include "riglogic/joints/cpu/bpcm/CalculationStrategy.h"
#include "riglogic/joints/cpu/bpcm/RotationAdapters.h"
#include "riglogic/joints/cpu/bpcm/Storage.h"
#include "riglogic/joints/cpu/bpcm/StorageSize.h"
#include "riglogic/riglogic/Configuration.h"
#include "riglogic/riglogic/RigMetrics.h"
#include "riglogic/types/Aliases.h"
#include "riglogic/types/bpcm/Optimizer.h"
#include "riglogic/utils/Extd.h"

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace rl4 {

namespace bpcm {

template<typename TIterator>
static void remapOutputIndicesForQuaternions(TIterator begin, TIterator end) {
    for (auto it = begin; it != end; ++it) {
        const auto absAttrIndex = *it;
        const auto jointIndex = static_cast<std::uint16_t>(absAttrIndex / 9u);
        const auto relAttrIndex = static_cast<std::uint16_t>(absAttrIndex % 9u);
        const auto newAttrBase = static_cast<std::uint16_t>(jointIndex * 10u);
        // Only scale relative attribute index is offset by one when output is in quaternions
        const auto newRelAttrIndex = (relAttrIndex < 6u ? relAttrIndex : static_cast<std::uint16_t>(relAttrIndex + 1u));
        *it = static_cast<std::uint16_t>(newAttrBase + newRelAttrIndex);
    }
}

template<typename TValue, typename TFVec>
class BPCMJointsBuilder : public JointsBuilder {
    public:
        BPCMJointsBuilder(const Configuration& config_, MemoryResource* memRes_);

        void computeStorageRequirements(const RigMetrics& source) override;
        void computeStorageRequirements(const JointBehaviorFilter& source) override;
        void allocateStorage(const JointBehaviorFilter& source) override;
        void fillStorage(const JointBehaviorFilter& source) override;
        JointsEvaluator::Pointer build() override;

    private:
        void setValues(const JointBehaviorFilter& source);
        void setInputIndices(const JointBehaviorFilter& source);
        void setOutputIndices(const JointBehaviorFilter& source);
        void setLODs(const JointBehaviorFilter& source);
        void setOutputRotationIndices(const JointBehaviorFilter& source);
        void setOutputRotationLODs(const JointBehaviorFilter& source,
                                   ConstArrayView<std::uint16_t> outputRotationIndices,
                                   std::uint32_t outputOffset,
                                   std::uint16_t jointGroupIndex);
        static constexpr std::uint32_t BlockHeight() {
            return static_cast<std::uint32_t>(TFVec::size() * 2ul);
        }

        static constexpr std::uint32_t PadTo() {
            return static_cast<std::uint32_t>(TFVec::size());
        }

    private:
        Configuration config;
        MemoryResource* memRes;
        StorageSize sizeReqs;
        JointStorage<TValue> storage;
        dna::RotationUnit rotationUnit;
};

template<typename TValue, typename TFVec>
BPCMJointsBuilder<TValue, TFVec>::BPCMJointsBuilder(const Configuration& config_, MemoryResource* memRes_) :
    config{config_},
    memRes{memRes_},
    sizeReqs{memRes},
    storage{memRes},
    rotationUnit{} {
}

template<typename TValue, typename TFVec>
void BPCMJointsBuilder<TValue, TFVec>::computeStorageRequirements(const RigMetrics&  /*unused*/) {
}

template<typename TValue, typename TFVec>
void BPCMJointsBuilder<TValue, TFVec>::computeStorageRequirements(const JointBehaviorFilter& source) {
    sizeReqs.computeFrom(source, PadTo());
}

template<typename TValue, typename TFVec>
void BPCMJointsBuilder<TValue, TFVec>::allocateStorage(const JointBehaviorFilter&  /*unused*/) {
    storage.values.resize(sizeReqs.valueCount);
    storage.inputIndices.resize(sizeReqs.inputIndexCount);
    storage.outputIndices.resize(sizeReqs.outputIndexCount);
    storage.lodRegions.reserve(sizeReqs.lodRegionCount);
    storage.jointGroups.resize(sizeReqs.jointGroups.size());
    if (config.rotationType == RotationType::Quaternions) {
        storage.outputRotationLODs.resize(sizeReqs.jointGroups.size() * sizeReqs.lodCount);
    }
}

template<typename TValue, typename TFVec>
void BPCMJointsBuilder<TValue, TFVec>::fillStorage(const JointBehaviorFilter& source) {
    rotationUnit = source.getRotationUnit();
    setValues(source);
    setInputIndices(source);
    setOutputIndices(source);
    setLODs(source);
    if (config.rotationType == RotationType::Quaternions) {
        // Remap output indices from 9-attribute joints to 10-attribute joints
        remapOutputIndicesForQuaternions(storage.outputIndices.begin(), storage.outputIndices.end());
        const JointBehaviorFilter filtered = source.only(dna::RotationRepresentation::EulerAngles);
        setOutputRotationIndices(filtered);
    }
}

template<typename TValue, typename TFVec>
void BPCMJointsBuilder<TValue, TFVec>::setValues(const JointBehaviorFilter& source) {
    std::uint32_t offset = {};
    Vector<float> buffer{memRes};
    for (std::uint16_t i = {}; i < source.getJointGroupCount(); ++i) {
        const auto jointGroupSize = sizeReqs.getJointGroupSize(i);
        storage.jointGroups[i].valuesOffset = offset;
        storage.jointGroups[i].valuesSize = jointGroupSize.padded.size();

        buffer.resize(jointGroupSize.padded.size());
        source.copyValues(i, buffer);

        offset += Optimizer<TFVec, BlockHeight(), PadTo(), 1u>::optimize(storage.values.data() + offset,
                                                                         buffer.data(),
                                                                         jointGroupSize.original);
    }
}

template<typename TValue, typename TFVec>
void BPCMJointsBuilder<TValue, TFVec>::setInputIndices(const JointBehaviorFilter& source) {
    std::uint32_t offset = {};
    for (std::uint16_t i = {}; i < source.getJointGroupCount(); ++i) {
        const auto jointGroupSize = sizeReqs.getJointGroupSize(i);
        source.copyInputIndices(i, {storage.inputIndices.data() + offset, jointGroupSize.padded.cols});
        storage.jointGroups[i].inputIndicesOffset = offset;
        storage.jointGroups[i].inputIndicesSize = jointGroupSize.padded.cols;
        storage.jointGroups[i].inputIndicesSizeAlignedTo4 = jointGroupSize.padded.cols - (jointGroupSize.padded.cols % 4u);
        storage.jointGroups[i].inputIndicesSizeAlignedTo8 = jointGroupSize.padded.cols - (jointGroupSize.padded.cols % 8u);
        offset += jointGroupSize.padded.cols;
    }
}

template<typename TValue, typename TFVec>
void BPCMJointsBuilder<TValue, TFVec>::setOutputIndices(const JointBehaviorFilter& source) {
    std::uint32_t offset = {};
    for (std::uint16_t i = {}; i < source.getJointGroupCount(); ++i) {
        const auto jointGroupSize = sizeReqs.getJointGroupSize(i);
        source.copyOutputIndices(i, {storage.outputIndices.data() + offset, jointGroupSize.padded.rows});
        storage.jointGroups[i].outputIndicesOffset = offset;
        offset += jointGroupSize.padded.rows;
    }
}

template<typename TValue, typename TFVec>
void BPCMJointsBuilder<TValue, TFVec>::setLODs(const JointBehaviorFilter& source) {
    std::uint32_t offset = {};
    for (std::uint16_t i = {}; i < source.getJointGroupCount(); ++i) {
        const auto jointGroupSize = sizeReqs.getJointGroupSize(i);
        const auto paddedRowCount = jointGroupSize.padded.rows;
        for (std::uint16_t lod = {}; lod < source.getLODCount(); ++lod) {
            storage.lodRegions.emplace_back(source.getRowCountForLOD(i, lod),
                                            paddedRowCount,
                                            BlockHeight(),
                                            PadTo());
        }
        storage.jointGroups[i].lodsOffset = offset;
        offset += source.getLODCount();
    }
}

template<typename TValue, typename TFVec>
void BPCMJointsBuilder<TValue, TFVec>::setOutputRotationIndices(const JointBehaviorFilter& source) {
    std::uint32_t outputOffset = {};
    for (std::uint16_t jgi = {}; jgi < source.getJointGroupCount(); ++jgi) {
        const auto rowCount = source.getRowCount(jgi);
        Vector<std::uint16_t> outputRotationIndices{rowCount, {}, memRes};
        source.copyOutputIndices(jgi, {outputRotationIndices.data(), rowCount});
        // Remap output indices from 9-attribute joints to 10-attribute joints rx -> qx
        remapOutputIndicesForQuaternions(outputRotationIndices.begin(), outputRotationIndices.end());
        // Given any rotation indices (qx, qy, qz), return only qx indices for all joints in the group
        #if !defined(__clang__) && defined(__GNUC__)
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wattributes"
        #endif
        auto deduplicate = [this](Vector<std::uint16_t>& v) {
                UnorderedSet<std::uint16_t> deduplicator{memRes};
                v.erase(v.rend().base(), std::remove_if(v.rbegin(), v.rend(), [&deduplicator](const std::uint16_t value) {
                        return !deduplicator.insert(value).second;
                    }).base());
            };
        #if !defined(__clang__) && defined(__GNUC__)
            #pragma GCC diagnostic pop
        #endif
        Vector<std::uint16_t> outputRotationBaseIndices{memRes};
        outputRotationBaseIndices.reserve(outputRotationIndices.size() / 3ul);
        std::transform(outputRotationIndices.begin(),
                       outputRotationIndices.end(),
                       std::back_inserter(outputRotationBaseIndices),
                       [](std::uint16_t outputIndex) {
                    return static_cast<std::uint16_t>((outputIndex / 10) * 10 + 3);
                });
        deduplicate(outputRotationBaseIndices);
        // Copy remapped qx indices into destination storage
        std::copy(outputRotationBaseIndices.begin(),
                  outputRotationBaseIndices.end(),
                  std::back_inserter(storage.outputRotationIndices));
        storage.jointGroups[jgi].outputRotationIndicesOffset = outputOffset;
        // Must be called before outputOffset is adjusted
        setOutputRotationLODs(source, outputRotationIndices, outputOffset, jgi);
        outputOffset += static_cast<std::uint32_t>(outputRotationBaseIndices.size());
    }
}

template<typename TValue, typename TFVec>
void BPCMJointsBuilder<TValue, TFVec>::setOutputRotationLODs(const JointBehaviorFilter& source,
                                                             ConstArrayView<std::uint16_t> outputRotationIndices,
                                                             std::uint32_t outputOffset,
                                                             std::uint16_t jointGroupIndex) {
    const auto lodCount = source.getLODCount();
    const auto offset = static_cast<std::uint32_t>(jointGroupIndex * lodCount);
    for (std::uint16_t lod = {}; lod < lodCount; ++lod) {
        const auto oldLODRowCount = source.getRowCountForLOD(jointGroupIndex, lod);
        if (oldLODRowCount == 0) {
            storage.outputRotationLODs[offset + lod] = 0;
            continue;
        }

        assert(oldLODRowCount <= outputRotationIndices.size());
        const auto qxRotationIndexAtOldLODRowCount = (outputRotationIndices[oldLODRowCount - 1ul] / 10) * 10 + 3;
        auto start = extd::advanced(storage.outputRotationIndices.begin(), outputOffset);
        auto it = std::find(start, storage.outputRotationIndices.end(), qxRotationIndexAtOldLODRowCount);
        assert(it != storage.outputRotationIndices.end());
        const auto newLODRowCount = static_cast<std::uint16_t>(std::distance(start, it) + 1);
        storage.outputRotationLODs[offset + lod] = newLODRowCount;
    }
    storage.jointGroups[jointGroupIndex].outputRotationLODsOffset = offset;
}

template<typename T, typename TFVec>
typename UniqueInstance<JointGroupLinearCalculationStrategy<T> >::PointerType createJointGroupLinearStrategy(
    RotationType rotationType,
    RotationOrder rotationOrder,
    dna::RotationUnit rotationUnit,
    MemoryResource* memRes) {

    if (rotationType == RotationType::EulerAngles) {
        using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec, NoopAdapter>;
        return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
    }

    #ifdef RL_BUILD_WITH_XYZ_ROTATION_ORDER
        if (rotationOrder == RotationOrder::XYZ) {
            if (rotationUnit == dna::RotationUnit::degrees) {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::fdeg,
                                                                                                                   tdm::rot_seq::xyz> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            } else {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::frad,
                                                                                                                   tdm::rot_seq::xyz> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            }
        }
    #endif  // RL_BUILD_WITH_XYZ_ROTATION_ORDER

    #ifdef RL_BUILD_WITH_XZY_ROTATION_ORDER
        if (rotationOrder == RotationOrder::XZY) {
            if (rotationUnit == dna::RotationUnit::degrees) {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::fdeg,
                                                                                                                   tdm::rot_seq::xzy> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            } else {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::frad,
                                                                                                                   tdm::rot_seq::xzy> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            }
        }
    #endif  // RL_BUILD_WITH_XZY_ROTATION_ORDER

    #ifdef RL_BUILD_WITH_YXZ_ROTATION_ORDER
        if (rotationOrder == RotationOrder::YXZ) {
            if (rotationUnit == dna::RotationUnit::degrees) {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::fdeg,
                                                                                                                   tdm::rot_seq::yxz> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            } else {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::frad,
                                                                                                                   tdm::rot_seq::yxz> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            }
        }
    #endif  // RL_BUILD_WITH_YXZ_ROTATION_ORDER

    #ifdef RL_BUILD_WITH_YZX_ROTATION_ORDER
        if (rotationOrder == RotationOrder::YZX) {
            if (rotationUnit == dna::RotationUnit::degrees) {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::fdeg,
                                                                                                                   tdm::rot_seq::yzx> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            } else {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::frad,
                                                                                                                   tdm::rot_seq::yzx> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            }
        }
    #endif  // RL_BUILD_WITH_YZX_ROTATION_ORDER

    #ifdef RL_BUILD_WITH_ZXY_ROTATION_ORDER
        if (rotationOrder == RotationOrder::ZXY) {
            if (rotationUnit == dna::RotationUnit::degrees) {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::fdeg,
                                                                                                                   tdm::rot_seq::zxy> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            } else {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::frad,
                                                                                                                   tdm::rot_seq::zxy> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            }
        }
    #endif  // RL_BUILD_WITH_ZXY_ROTATION_ORDER

    #ifdef RL_BUILD_WITH_ZYX_ROTATION_ORDER
        if (rotationOrder == RotationOrder::ZYX) {
            if (rotationUnit == dna::RotationUnit::degrees) {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::fdeg,
                                                                                                                   tdm::rot_seq::zyx> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            } else {
                using CalculationStrategy = VectorizedJointGroupLinearCalculationStrategy<T, TFVec,
                                                                                          EulerAnglesToQuaternions<tdm::frad,
                                                                                                                   tdm::rot_seq::zyx> >;
                return UniqueInstance<CalculationStrategy, JointGroupLinearCalculationStrategy<T> >::with(memRes).create();
            }
        }
    #endif  // RL_BUILD_WITH_ZYX_ROTATION_ORDER

    return nullptr;
}

template<typename TValue, typename TFVec>
JointsEvaluator::Pointer BPCMJointsBuilder<TValue, TFVec>::build() {
    auto strategy =
        createJointGroupLinearStrategy<TValue, TFVec>(config.rotationType, config.rotationOrder, rotationUnit, memRes);
    auto factory = UniqueInstance<Evaluator<TValue>, JointsEvaluator>::with(memRes);
    return factory.create(std::move(storage), std::move(strategy), nullptr, memRes);
}

}  // namespace bpcm

}  // namespace rl4
