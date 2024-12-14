// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/CompilerPasses/IParserPass.h"

namespace uLang
{

/// Used as the identifiers for any string interpolation expressions.
static constexpr char ConcatenateIdentifierText[] = "Concatenate";
static constexpr char ToStringIdentifierText[] = "ToString";

class VERSECOMPILER_API CParserPass : public IParserPass
{
public:
    //~ Begin IParserPass interface
    virtual void ProcessSnippet(const uLang::TSRef<Verse::Vst::Snippet>& OutVst, const CUTF8StringView& TextSnippet, const SBuildContext& BuildContext) const override;
    //~ End IParserPass interface
};

} // namespace uLang
