// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Semantic Analysis Public API

#pragma once

#include "uLang/CompilerPasses/ISemanticAnalyzerPass.h"
#include "uLang/SemanticAnalyzer/SemanticAnalyzer.h"

namespace uLang
{

/// Converts a CSyntaxProgram to a CSemanticProgram and identifies any semantic issues
class CSemanticAnalyzerPass : public ISemanticAnalyzerPass
{
public:

    ~CSemanticAnalyzerPass()
    {
        ULANG_ASSERTF(!_Context.IsValid(), "Destructor called without clean up.");
    }

    virtual void Initialize(const SBuildContext& BuildContext, const SProgramContext& ProgramContext) override
    {
        ULANG_ASSERTF(!_Context.IsValid(), "Initialize called without a paired clean up.");
        _Context = TUPtr<CSemanticAnalyzer>::New(ProgramContext._Program, BuildContext);
    }

    virtual void CleanUp(void) override
    {
        _Context.Reset();
    }

    //~ Begin ISemanticAnalyzerPass interface
    virtual TSRef<CSemanticProgram> ProcessVst(const Verse::Vst::Project& Vst, const ESemanticPass Pass) const override
    {
        ULANG_ASSERTF(_Context.IsValid(), "ProcessVst called without initializing.");
        _Context->ProcessVst(Vst, Pass);
        return _Context->GetSemanticProgram();
    }
    //~ End ISemanticAnalyzerPass interface
private:

    TUPtr<CSemanticAnalyzer> _Context;
};  // CSemanticAnalyzerPass

}  // namespace uLang
