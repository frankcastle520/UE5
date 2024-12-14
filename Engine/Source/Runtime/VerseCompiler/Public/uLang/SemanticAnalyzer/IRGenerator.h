// Copyright Epic Games, Inc. All Rights Reserved.
// uLang IR Generator Public API

#pragma once

#include "uLang/Common/Text/Symbol.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/UniquePointer.h"
#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/CompilerPasses/SemanticAnalyzerPassUtils.h"

namespace uLang
{

class CSemanticProgram;
class CDiagnostics;
class CSyntaxFunction;
class CIrGeneratorImpl;

/// Stand-alone IR generator, use _AstProject to fill in _IrProject in CSemanticProgram
VERSECOMPILER_API bool GenerateIr(const TSRef<CSemanticProgram>& Program, const TSRef<CDiagnostics>& Diagnostics, SBuildParams::EWhichVM TargetVM);

} // namespace uLang