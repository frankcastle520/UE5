// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"
#include "riglogic/joints/Joints.h"

namespace rl4 {

struct Configuration;
struct RigMetrics;

struct JointsFactory {
    static Joints::Pointer create(const Configuration& config, const dna::Reader* reader, MemoryResource* memRes);
    static Joints::Pointer create(const Configuration& config, const RigMetrics& metrics, MemoryResource* memRes);

};

}  // namespace rl4
