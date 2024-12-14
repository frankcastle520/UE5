// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Semantic Analyzer Public API

#pragma once

#include "uLang/Common/Text/Symbol.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Containers/SharedPointer.h"

namespace uLang
{

class CSemanticProgram;
class CAstPackage;
class CDiagnostics;

namespace DigestGenerator
{

/// Generates a text digest for a given program and a package within it
VERSECOMPILER_API bool Generate(
    const CSemanticProgram& Program,
    const CAstPackage& Package,
    bool bIncludeInternalDefinitions,
    bool bIncludeEpicInternalDefinitions,
    const TSRef<CDiagnostics>& Diagnostics,
    const CUTF8String* Notes,
    CUTF8String& OutDigestCode);

} // namespace DigestGenerator
} // namespace uLang