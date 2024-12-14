// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/Toolchain/ModularFeature.h"

namespace uLang
{
    class CExpressionBase;

class IPostSemanticAnalysisFilter : public TModularFeature<IPostSemanticAnalysisFilter>
{
    ULANG_FEATURE_ID_DECL(IPostSemanticAnalysisFilter);

public:
    virtual void FilterAst(const TSRef<CSemanticProgram>& SemanticResult, const SBuildContext& BuildParams, const SProgramContext& ProgramContext) = 0;
};

}
