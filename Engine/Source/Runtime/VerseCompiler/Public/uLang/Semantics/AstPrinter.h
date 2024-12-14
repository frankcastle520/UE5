// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Text/UTF8String.h"

namespace uLang
{
    class CAstNode;
    class CSemanticProgram;
    VERSECOMPILER_API CUTF8String PrintAst(const CSemanticProgram& Program, const CAstNode& RootNode);
}