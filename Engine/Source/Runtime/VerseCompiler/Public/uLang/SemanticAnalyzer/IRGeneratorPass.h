// Copyright Epic Games, Inc. All Rights Reserved.
// uLang IR Analysis Public API

#pragma once

#include "uLang/CompilerPasses/IIRGeneratorPass.h"
#include "uLang/SemanticAnalyzer/IRGenerator.h"

namespace uLang
{

/// Generates an IR from the Ast in the CSemanticProgram. The CSemanticProgram is updated with the generated IR.
class CIrGeneratorPass : public IIrGeneratorPass
{
public:

    ~CIrGeneratorPass()
    {
        ULANG_ASSERTF(!_Program, "Destructor called without clean up.");
    }

    virtual void Initialize(const SBuildContext& BuildContext, const SProgramContext& ProgramContext) override
    {
        ULANG_ASSERTF(!_Program, "Initialize called without a paired clean up.");
        _Program = ProgramContext._Program;
        _Diagnostics = BuildContext._Diagnostics;
        _TargetVM = BuildContext._Params._TargetVM;
    }

    virtual void CleanUp(void) override
    {
        _Program.Reset();
        _Diagnostics.Reset();
    }

    //~ Begin IIrGeneratorPass interface
    virtual void ProcessAst() const override
    {
        ULANG_ASSERTF(_Program, "ProcesAst called without initializing.");

        GenerateIr(_Program.AsRef(), _Diagnostics.AsRef(), _TargetVM);
    }
    //~ End IIrGeneratorPass interface
private:
    TSPtr<CSemanticProgram> _Program;
    TSPtr<CDiagnostics> _Diagnostics;
    SBuildParams::EWhichVM _TargetVM;
};  // CIrGeneratorPass

}  // namespace uLang
