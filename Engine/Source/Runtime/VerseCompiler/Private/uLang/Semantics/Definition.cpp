// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/DataDefinition.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SemanticTypes.h"

namespace uLang
{
CDefinition::CDefinition(EKind Kind, CScope& EnclosingScope, const CSymbol& Name)
    : CNamed(Name)
    , _EnclosingScope(EnclosingScope)
    , _ParentScopeOrdinal(EnclosingScope.GetLogicalScope().AllocateNextDefinitionOrdinal())
    , _Qualifier{SQualifier::Unknown()}
    , _Kind(Kind)
{}

CDefinition::~CDefinition()
{
    // Hack to allow CDefinition to reference a default expression for a non CExprDataDefinition named argument.
    // @JIRA SOL-2695 Named parameter needs to detach double link with AST
    //ULANG_ASSERTF(!GetAstNode(), "Expected %s definition AST node link to be cleared before definition is destroyed", DefinitionKindAsCString(GetKind()));
    //ULANG_ASSERTF(!GetIrNode(true), "Expected %s definition IR node link to be cleared before definition is destroyed", DefinitionKindAsCString(GetKind()));
}

SAccessLevel CDefinition::DerivedAccessLevel() const
{
    const CDefinition& DefinitionAccessibilityRoot = GetDefinitionAccessibilityRoot();
    if (DefinitionAccessibilityRoot._AccessLevel.IsSet())
    {
        return DefinitionAccessibilityRoot._AccessLevel.GetValue();
    }
    else
    {
        return DefinitionAccessibilityRoot._EnclosingScope.GetDefaultDefinitionAccessLevel();
    }
}

bool CDefinition::IsInstanceMember() const
{
    return _EnclosingScope.GetKind() == CScope::EKind::Class
        || _EnclosingScope.GetKind() == CScope::EKind::Interface;
}

bool CDefinition::IsDeprecated() const
{
    return HasAttributeClass(_EnclosingScope.GetProgram()._deprecatedClass, _EnclosingScope.GetProgram());
}

bool CDefinition::IsExperimental() const
{
    return HasAttributeClass(_EnclosingScope.GetProgram()._experimentalClass, _EnclosingScope.GetProgram());
}

bool CDefinition::IsFinal() const
{
    return HasAttributeClass(_EnclosingScope.GetProgram()._finalClass, _EnclosingScope.GetProgram());
}

const CExpressionBase* CDefinition::GetNativeSpecifierExpression() const
{
    return FindAttributeExpr(_EnclosingScope.GetProgram()._nativeClass, _EnclosingScope.GetProgram());
}

bool CDefinition::IsNative() const
{
    return GetNativeSpecifierExpression() != nullptr;
}

bool CDefinition::IsBuiltIn() const
{
    return _EnclosingScope.IsBuiltInScope();
}

CUTF8String GetQualifiedNameString(const CDefinition& Definition)
{
    return CUTF8String("(%s:)%s", Definition._EnclosingScope.GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString(), Definition.AsNameCString());
}

const char* DefinitionKindAsCString(CDefinition::EKind Kind)
{
    switch(Kind)
    {
#define VISIT_KIND(Name, String) case CDefinition::EKind::Name: return String;
    VERSE_ENUM_DEFINITION_KINDS(VISIT_KIND)
#undef VISIT_KIND
    default: ULANG_UNREACHABLE();
    };
}

bool CDefinition::IsAccessibleFrom(const CScope& Scope) const
{
    const CDefinition& Definition = GetDefinitionAccessibilityRoot();
    return Scope.CanAccess(Definition, Definition.DerivedAccessLevel());
}

const CDefinition* CDefinition::GetEnclosingDefinition() const
{
    for (const CScope* Scope = &_EnclosingScope; Scope; Scope = Scope->GetParentScope())
    {
        if (const CDefinition* EnclosingScopeDefinition = Scope->ScopeAsDefinition())
        {
            return EnclosingScopeDefinition;
        }
    }
    return nullptr;
}

SQualifier CDefinition::GetImplicitQualifier() const
{
    const CDefinition& BaseDefinition = GetBaseOverriddenDefinition();
    return BaseDefinition._EnclosingScope.GetLogicalScope().AsQualifier();
}

}  // namespace uLang
