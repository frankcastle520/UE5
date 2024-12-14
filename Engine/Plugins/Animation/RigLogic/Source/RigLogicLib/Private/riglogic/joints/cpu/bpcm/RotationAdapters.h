// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/joints/cpu/bpcm/StorageView.h"
#include "riglogic/system/simd/SIMD.h"
#include "riglogic/types/Aliases.h"
#include "riglogic/utils/Macros.h"

#include <tdm/Quat.h>

namespace rl4 {

namespace bpcm {

struct NoopAdapter {

    template<typename T>
    static FORCE_INLINE void adapt(const JointGroupView<T>&  /*unused*/, ArrayView<float>  /*unused*/,
                                   std::uint16_t  /*unused*/) {
    }

};

template<typename TAngle, tdm::rot_seq Order>
struct EulerAnglesToQuaternions {

    static_assert(std::is_same<TAngle, tdm::fdeg>::value || std::is_same<TAngle, tdm::frad>::value,
                  "TAngle must be either tdm::fdeg or tdm::frad.");

    template<typename T>
    static FORCE_INLINE void adapt(const JointGroupView<T>& jointGroup, ArrayView<float> outputs, std::uint16_t lod) {
        for (std::size_t row = {}; row < jointGroup.outputRotationLODs[lod]; ++row) {
            const auto rotationStartIndex = jointGroup.outputRotationIndices[row];
            tdm::frad3 euler{tdm::frad{TAngle{outputs[rotationStartIndex + 0ul]}},
                             tdm::frad{TAngle{outputs[rotationStartIndex + 1ul]}},
                             tdm::frad{TAngle{outputs[rotationStartIndex + 2ul]}}};
            const tdm::fquat q = tdm::fquat::fromEuler<Order>(euler);
            outputs[rotationStartIndex + 0ul] = q.x;
            outputs[rotationStartIndex + 1ul] = q.y;
            outputs[rotationStartIndex + 2ul] = q.z;
            outputs[rotationStartIndex + 3ul] = q.w;
        }
    }

};

}  // namespace bpcm

}  // namespace rl4
