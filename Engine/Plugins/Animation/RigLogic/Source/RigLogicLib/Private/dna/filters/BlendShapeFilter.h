// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "dna/TypeDefs.h"

#include <cstdint>

namespace dna {

struct RawDefinition;
struct RawMesh;

class BlendShapeFilter {
    public:
        explicit BlendShapeFilter(MemoryResource* memRes_);
        void configure(std::uint16_t blendShapeCount, UnorderedSet<std::uint16_t> allowedBlendShapeIndices);
        void apply(RawDefinition& dest);
        void apply(RawMesh& dest);
        bool passes(std::uint16_t index) const;
        std::uint16_t remapped(std::uint16_t index) const;

    private:
        MemoryResource* memRes;
        UnorderedSet<std::uint16_t> passingIndices;
        UnorderedMap<std::uint16_t, std::uint16_t> remappedIndices;

};

}  // namespace dna
