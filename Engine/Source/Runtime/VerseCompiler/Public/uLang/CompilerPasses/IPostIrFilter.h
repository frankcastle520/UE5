// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/Toolchain/ModularFeature.h"

namespace uLang
{
class CExpressionBase;

class IPostIrFilter : public TModularFeature<IPostIrFilter>
{
    ULANG_FEATURE_ID_DECL(IPostIrFilter);

public:
    virtual void FilterIr(const TSRef<CSemanticProgram>& IrResult, const SBuildContext& BuildParams, const SProgramContext& ProgramContext) = 0;
};

}
