// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Semantics/SemanticScope.h"

namespace uLang
{
/**
 * The implicit scope of a type.
 */
class CTypeScope : public CLogicalScope, public CSharedMix
{
public:

    CTypeScope(CScope& EnclosingScope)
    : CLogicalScope(CScope::EKind::Type, &EnclosingScope, EnclosingScope.GetProgram())
    {}

    // CScope interface.
    virtual CSymbol GetScopeName() const override { return CSymbol(); }
};

}  // namespace uLang
