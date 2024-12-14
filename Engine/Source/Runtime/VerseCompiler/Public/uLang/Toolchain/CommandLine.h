// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/CompilerPasses/CompilerTypes.h" // for SCommandLine

namespace uLang
{
namespace CommandLine
{
    VERSECOMPILER_API void Init(int ArgC, char* ArgV[]);
    VERSECOMPILER_API void Init(const SCommandLine& Rhs);

    VERSECOMPILER_API bool IsSet();
    VERSECOMPILER_API const SCommandLine& Get();
}
} // namespace uLang
