// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/Toolchain/ModularFeature.h"

namespace uLang
{

enum class ELinkerResult
{
    Link_Skipped = -1,
    Link_Success = 0,
    Link_Failure,
    Link_Skipped_ByInjection,
    Link_Skipped_ByEmptyPass
};
ULANG_FORCEINLINE bool operator! (ELinkerResult E) { return !(int32_t)E; }


class IAssemblerPass : public TModularFeature<IAssemblerPass>
{
    ULANG_FEATURE_ID_DECL(IAssemblerPass);

public:
    virtual void TranslateExpressions(const TSRef<CSemanticProgram>& SemanticResult, const SBuildContext& BuildContext, const SProgramContext& ProgramContext) const = 0;
    virtual ELinkerResult Link(const SBuildContext& BuildContext, const SProgramContext& ProgramContext) const = 0;
};

}
