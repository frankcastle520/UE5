// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/Toolchain/ModularFeature.h"
#include "uLang/Syntax/VstNode.h" // for Vst::Node
#include "uLang/Common/Containers/Array.h"

namespace uLang
{

class IParserPass : public TModularFeature<IParserPass>
{
    ULANG_FEATURE_ID_DECL(IParserPass);

public:
    virtual void ProcessSnippet(const uLang::TSRef<Verse::Vst::Snippet>& OutVst, const CUTF8StringView& TextSnippet, const SBuildContext& BuildContext) const = 0;
};

}
