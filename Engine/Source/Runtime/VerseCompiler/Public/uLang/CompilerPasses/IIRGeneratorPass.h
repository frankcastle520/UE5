// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/Toolchain/ModularFeature.h"

namespace uLang
{

class IIrGeneratorPass : public TModularFeature<IIrGeneratorPass>
{
    ULANG_FEATURE_ID_DECL(IIrGeneratorPass);

public:
    virtual void Initialize(const SBuildContext& BuildContext, const SProgramContext& ProgramContext) = 0;
    virtual void CleanUp(void) = 0;
    virtual void ProcessAst() const = 0;
};

}
