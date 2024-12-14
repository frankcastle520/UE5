// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"
#include "riglogic/rbf/RBFBehavior.h"

namespace rl4 {

struct Configuration;
struct RigMetrics;

struct RBFBehaviorFactory {
    static RBFBehavior::Pointer create(const Configuration& config, const dna::Reader* reader, MemoryResource* memRes);
    static RBFBehavior::Pointer create(const Configuration& config, const RigMetrics& metrics, MemoryResource* memRes);

};

}  // namespace rl4
