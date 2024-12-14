// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/Expression.h"

namespace uLang
{
// Forward declarations.
class CModule;

// An imported module: Alias := import(...)
class VERSECOMPILER_API CModuleAlias : public CDefinition
{
public:
    static const CDefinition::EKind StaticDefinitionKind = CDefinition::EKind::ModuleAlias;

    CModuleAlias(const CSymbol& Name, CScope& EnclosingScope)
        : CDefinition(StaticDefinitionKind, EnclosingScope, Name)
    {}

    const CModule* Module() const { return _Module; }
    void SetModule(const CModule* Module) { _Module = Module; }

    // CDefinition interface.
    void SetAstNode(CExprImport* AstNode) { CDefinition::SetAstNode(AstNode); }
    CExprImport* GetAstNode() const { return static_cast<CExprImport*>(CDefinition::GetAstNode()); }

    void SetIrNode(CExprImport* AstNode) { CDefinition::SetIrNode(AstNode); }
    CExprImport* GetIrNode(bool bForce = false) const { return static_cast<CExprImport*>(CDefinition::GetIrNode(bForce)); }

    virtual bool IsPersistenceCompatConstraint() const override { return false; }

private:

    const CModule* _Module{ nullptr };
};
}