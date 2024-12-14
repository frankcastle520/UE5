// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Toolchain/ModularFeature.h"
#include "uLang/CompilerPasses/SemanticAnalyzerPassUtils.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Semantics/SemanticProgram.h"

namespace Verse { namespace Vst { struct Node; struct Snippet; struct Package; struct Project; } }

namespace uLang
{

// Forward declarations
struct SBuildContext;
struct SProgramContext;
class  CUTF8StringView;
class  CSemanticProgram;

struct SIntraSemInjectArgs
{
    SIntraSemInjectArgs(const TSRef<CSemanticProgram>& Program)
        : _Program(Program)
    {}
    const TSRef<CSemanticProgram>& _Program;

    ESemanticPass _InjectionPass = ESemanticPass::SemanticPass_Invalid;
};

template<class FeatureType, typename... InjectionArgsType>
class TApiLayerInjection : public TModularFeature<FeatureType>
{
public:
    /** @return True to halt the toolchain from continuing to build -- NOTE: could be ignored (depending on current build settings). */
    virtual bool Ingest(InjectionArgsType... Args, const SBuildContext& BuildParams) = 0;
};

class IPreParseInjection         : public TApiLayerInjection<IPreParseInjection, const CUTF8StringView&>                                            { ULANG_FEATURE_ID_DECL(IPreParseInjection); };
class IPostParseInjection        : public TApiLayerInjection<IPostParseInjection, const TSRef<Verse::Vst::Snippet>&>                                { ULANG_FEATURE_ID_DECL(IPostParseInjection); };
class IPreSemAnalysisInjection   : public TApiLayerInjection<IPreSemAnalysisInjection, const TSRef<Verse::Vst::Project>&, const SProgramContext&>   { ULANG_FEATURE_ID_DECL(IPreSemAnalysisInjection); };
class IIntraSemAnalysisInjection : public TApiLayerInjection<IIntraSemAnalysisInjection, const SIntraSemInjectArgs&, const SProgramContext&>        { ULANG_FEATURE_ID_DECL(IIntraSemAnalysisInjection); };
class IPostSemAnalysisInjection  : public TApiLayerInjection<IPostSemAnalysisInjection, const TSRef<CSemanticProgram>&, const SProgramContext&>     { ULANG_FEATURE_ID_DECL(IPostSemAnalysisInjection); };
class IPreTranslateInjection     : public TApiLayerInjection<IPreTranslateInjection, const TSRef<CSemanticProgram>&, const SProgramContext&>        { ULANG_FEATURE_ID_DECL(IPreTranslateInjection); };
class IPreLinkInjection          : public TApiLayerInjection<IPreLinkInjection, const SProgramContext&>                                             { ULANG_FEATURE_ID_DECL(IPreLinkInjection); };
}
