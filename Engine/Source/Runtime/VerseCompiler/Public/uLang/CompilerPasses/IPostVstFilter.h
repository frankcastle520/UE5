// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/Toolchain/ModularFeature.h"

namespace uLang
{

class IPostVstFilter : public TModularFeature<IPostVstFilter>
{
    ULANG_FEATURE_ID_DECL(IPostVstFilter);

public:
    virtual void Filter(const uLang::TSRef<Verse::Vst::Snippet>& VstSnippet, const SBuildContext& BuildParams) = 0;
};

}
