// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Version attribute filter 

#pragma once

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/CompilerPasses/IPostVstFilter.h"

namespace uLang
{

struct SBuildVersionInfo
{
    uint32_t UploadedAtFNVersion;
};

// Filter out any symbol with a version attribute that would exclude it from the compile
class VERSECOMPILER_API CAvailableAttributeVstFilter final : public IPostVstFilter
{
public:
    CAvailableAttributeVstFilter() {}

    //~ Begin IPostVstFilter interface
    virtual void Filter(const TSRef<Verse::Vst::Snippet>& VstSnippet, const SBuildContext& BuildParams) override
    {
        StaticFilter(VstSnippet.As<Verse::Vst::Node>(), BuildParams);
    }
    //~ End IPostVstFilter interface

    static void StaticFilter(const TSRef<Verse::Vst::Node>& VstNode, const SBuildContext& BuildParams);

private:
    static void StaticFilterHelper(const TSRef<Verse::Vst::Node>& VstNode,
        const SBuildContext& BuildParams,
        const SBuildVersionInfo& BuildVersion);
};
}