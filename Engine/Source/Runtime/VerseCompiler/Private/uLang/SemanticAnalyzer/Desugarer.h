// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Text/Symbol.h"
#include "uLang/Diagnostics/Diagnostics.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Syntax/VstNode.h"

namespace uLang
{
    TSRef<CAstProject> DesugarVstToAst(const Verse::Vst::Project& VstProject, CSymbolTable& Symbols, CDiagnostics& Diagnostics);
}