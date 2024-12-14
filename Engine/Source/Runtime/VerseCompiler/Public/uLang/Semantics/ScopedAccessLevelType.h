// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Semantics/SemanticClass.h"

namespace uLang
{
    class CScopedAccessLevelDefinition;
    class CExprScopedAccessLevelDefinition;

    /**
     * AccessLevelDefinition type
     **/
    // HACK! This is convoluted, but attributes need to be CClass types because the semantic attribute processing demands it right now
    // HACK! CClass expects its associated definition to be a CClassDefinition type, so our CScopedAccessLevelDefinition also needs to be a CClassDefinition type
    // HACK! Ordinarily, we could just use CClassDefinition directly without this extra child type, except the CClassDefinition linkage to the AST demands
    // HACK! that the CExpr* type be CExprClassDefinition even though it ultimately relaxes to CExpressionBase.
    class VERSECOMPILER_API CScopedAccessLevelDefinition : public CClassDefinition
    {
    public:
        CScopedAccessLevelDefinition(TOptional<CSymbol> ClassName, CScope& EnclosingScope);

        TArray<const CScope*> _Scopes;
        bool _IsAnonymous;

        // CDefinition interface glue.
        void SetAstNode(CExprScopedAccessLevelDefinition* AstNode);
        CExprScopedAccessLevelDefinition* GetAstNode() const;

        void SetIrNode(CExprScopedAccessLevelDefinition* AstNode);
        CExprScopedAccessLevelDefinition* GetIrNode(bool bForce = false) const;

        virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override;
    };
}