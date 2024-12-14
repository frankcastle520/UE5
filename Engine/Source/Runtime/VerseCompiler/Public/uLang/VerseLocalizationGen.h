// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Diagnostics/Diagnostics.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Common/Text/UTF8StringBuilder.h"

namespace uLang
{
struct FSolLocalizationInfo 
{
    FSolLocalizationInfo(const CUTF8String& Path,
        const CUTF8String& Default,
        const CUTF8String& Where)
        : Path(Path)
        , Default(Default)
        , Where(Where)
    {}
    FSolLocalizationInfo(const CUTF8String& Default,
        const CUTF8String& Where)
        : Path("")
        , Default(Default)
        , Where(Where)
    {}
    CUTF8String Path;
    CUTF8String Default;
    CUTF8String Where;
};

struct FVerseLocalizationGen
{
    VERSECOMPILER_API void operator()(const uLang::CSemanticProgram& Program, 
        uLang::CDiagnostics& Diagnostics,
        TArray<FSolLocalizationInfo>& LocalizationInfo, 
        TArray<FSolLocalizationInfo>& StringInfo) const;
};
}
