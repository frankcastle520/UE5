// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#include "uLang/Semantics/ScopedAccessLevelType.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SemanticProgram.h"

namespace uLang
{
    //=======================================================================================
    // CScopedAccessLevelDefinition
    //=======================================================================================

    CScopedAccessLevelDefinition::CScopedAccessLevelDefinition(TOptional<CSymbol> ClassName, CScope& EnclosingScope)
        : CClassDefinition(ClassName.IsSet()?*ClassName:CSymbol(), EnclosingScope, EnclosingScope.GetProgram()._scopedClass)
        , _IsAnonymous(!ClassName.IsSet())
    {
        _bHasCyclesBroken = true;
    }

    // CDefinition interface
    void CScopedAccessLevelDefinition::SetAstNode(CExprScopedAccessLevelDefinition* AstNode)
    {
        CDefinition::SetAstNode(AstNode);
    }

    CExprScopedAccessLevelDefinition* CScopedAccessLevelDefinition::GetAstNode() const
    {
        return static_cast<CExprScopedAccessLevelDefinition*>(CDefinition::GetAstNode());
    }

    void CScopedAccessLevelDefinition::SetIrNode(CExprScopedAccessLevelDefinition* AstNode)
    {
        CDefinition::SetIrNode(AstNode);
    }

    CExprScopedAccessLevelDefinition* CScopedAccessLevelDefinition::GetIrNode(bool bForce) const
    {
        return static_cast<CExprScopedAccessLevelDefinition*>(CDefinition::GetIrNode(bForce));
    }

    CUTF8String CScopedAccessLevelDefinition::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
    {
        CUTF8StringBuilder Builder;
        Builder.Append("scoped{");
        const char* Seperator = "";
        for (const CScope* Scope : _Scopes)
        {
            Builder.AppendFormat("%s%s", Seperator, Scope->GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString());
            Seperator = ", ";
        }
        Builder.Append("}");

        return Builder.MoveToString();
    }
} 
