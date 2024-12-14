// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Semantic Analyzer Public API

#pragma once

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/UniquePointer.h"
#include "uLang/Common/Text/Symbol.h"
#include "uLang/CompilerPasses/SemanticAnalyzerPassUtils.h"

namespace Verse { namespace Vst { struct Project; } }

namespace uLang
{
class CSemanticProgram;
class CSemanticAnalyzerImpl;
struct SBuildContext;

constexpr int32_t MaxNumPersistentVarsDefault = 2;

/// Stand-alone semantic analyzer, converts from a syntax program to a semantic program
class CSemanticAnalyzer
{
public:
    VERSECOMPILER_API CSemanticAnalyzer(const TSRef<CSemanticProgram>&, const SBuildContext&);
    VERSECOMPILER_API ~CSemanticAnalyzer();

    VERSECOMPILER_API bool ProcessVst(const Verse::Vst::Project& Vst, const ESemanticPass Stage) const;

    VERSECOMPILER_API const TSRef<CSemanticProgram>& GetSemanticProgram() const;

private:
    TURef<CSemanticAnalyzerImpl> _SemaImpl;
};

} // namespace uLang
