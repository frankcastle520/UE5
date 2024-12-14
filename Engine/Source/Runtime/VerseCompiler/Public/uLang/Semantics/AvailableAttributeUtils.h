// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Misc/MathUtils.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/SourceProject/UploadedAtFNVersion.h"
#include <cstdint>

namespace uLang
{
    class CDefinition;
    class CSemanticProgram;

    TOptional<uint64_t> GetAvailableAttributeVersion(const CDefinition& Definition, CSemanticProgram& SemanticProgram);
    TOptional<uint64_t> CalculateCombinedAvailableAttributeVersion(const CDefinition& Definition, CSemanticProgram& SemanticProgram);
    bool IsDefinitionAvailableAtVersion(const CDefinition& Definition, uint64_t Version, CSemanticProgram& SemanticProgram);
}