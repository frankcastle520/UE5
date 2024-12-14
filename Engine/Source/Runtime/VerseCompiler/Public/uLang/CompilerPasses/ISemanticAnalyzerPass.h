// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/Toolchain/ModularFeature.h"
#include "uLang/Syntax/VstNode.h" // for Vst::Node
#include "uLang/Common/Containers/SharedPointer.h"

namespace uLang
{

class ISemanticAnalyzerPass : public TModularFeature<ISemanticAnalyzerPass>
{
    ULANG_FEATURE_ID_DECL(ISemanticAnalyzerPass);

public:
    virtual void Initialize(const SBuildContext& BuildContext, const SProgramContext& ProgramContext) = 0;
    virtual void CleanUp(void) = 0;
    virtual TSRef<CSemanticProgram> ProcessVst(const Verse::Vst::Project& Vst, const ESemanticPass Pass) const = 0;
};

}
