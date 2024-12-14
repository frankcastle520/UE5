// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

namespace dna {

enum class DataLayer : std::uint32_t {
    Descriptor = 1,
    Definition = 2 | Descriptor,  // Implicitly loads Descriptor
    Behavior = 4 | Definition,  // Implicitly loads Descriptor and Definition
    Geometry = 8 | Definition,  // Implicitly loads Descriptor and Definition
    GeometryWithoutBlendShapes = 16 | Definition,  // Implicitly loads Descriptor and Definition
    MachineLearnedBehavior = 32 | Definition,  // Implicitly loads Definition
    RBFBehavior = 64 | Behavior,  // Implicitly loads Behavior
    JointBehaviorMetadata = 128 | Definition,  // Implicitly loads Definition
    TwistSwingBehavior = 256 | Definition,  // Implicitly loads Definition
    All = RBFBehavior | Geometry | MachineLearnedBehavior | JointBehaviorMetadata | TwistSwingBehavior
};

inline DataLayer operator|(DataLayer lhs, DataLayer rhs) {
    return static_cast<DataLayer>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

enum class UnknownLayerPolicy {
    Preserve,
    Ignore
};

}  // namespace dna
