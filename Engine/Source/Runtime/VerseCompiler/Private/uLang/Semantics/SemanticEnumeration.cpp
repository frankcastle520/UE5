// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#include "uLang/Semantics/SemanticEnumeration.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/VisitStamp.h"

namespace uLang
{
//=======================================================================================
// CEnumerator
//=======================================================================================

CEnumerator::CEnumerator(CEnumeration& Enumeration, const CSymbol& Name, int32_t Value)
    : CDefinition(StaticDefinitionKind, Enumeration, Name), _Value(Value), _Enumeration(&Enumeration)
{}

CUTF8String CEnumerator::AsCode() const
{
    CUTF8StringBuilder Code;

    Code.Append(_Enumeration->AsNameStringView());
    Code.Append(".");
    Code.Append(_Name.AsStringView());
    return Code.MoveToString();
}

void CEnumerator::SetAstNode(CExprEnumLiteral* AstNode)
{
    CDefinition::SetAstNode(AstNode);
}
CExprEnumLiteral* CEnumerator::GetAstNode() const
{
    return static_cast<CExprEnumLiteral*>(CDefinition::GetAstNode());
}

void CEnumerator::SetIrNode(CExprEnumLiteral* AstNode)
{
    CDefinition::SetIrNode(AstNode);
}
CExprEnumLiteral* CEnumerator::GetIrNode(bool bForce) const
{
    return static_cast<CExprEnumLiteral*>(CDefinition::GetIrNode(bForce));
}

bool CEnumerator::IsPersistenceCompatConstraint() const { return _Enumeration->IsPersistable(); }

//=======================================================================================
// CEnumeration
//=======================================================================================

CEnumeration::CEnumeration(const CSymbol& Name, CScope& EnclosingScope)
    : CDefinition(StaticDefinitionKind, EnclosingScope, Name)
    , CLogicalScope(CScope::EKind::Enumeration, &EnclosingScope, EnclosingScope.GetProgram())
    , CNominalType(ETypeKind::Enumeration, EnclosingScope.GetProgram())
{
}

CEnumerator& CEnumeration::CreateEnumerator(const CSymbol& EnumeratorName, int32_t Value)
{
    TSRef<CEnumerator> NewEnumerator = TSRef<CEnumerator>::New(*this, EnumeratorName, Value);
    CEnumerator& Enumerator = *NewEnumerator;
    _Definitions.Add(Move(NewEnumerator));
    return Enumerator;
}

SmallDefinitionArray CEnumeration::FindTypeMember(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier, VisitStampType VisitStamp) const
{
    return CLogicalScope::FindDefinitions(Name, Origin, Qualifier, nullptr, VisitStamp);
}

bool CEnumeration::IsPersistable() const
{
    return _EffectAttributable.HasAttributeClassHack(GetProgram()._persistableClass, GetProgram());
}

SAccessLevel CEnumeration::GetDefaultDefinitionAccessLevel() const
{
    return SAccessLevel::EKind::Public;
}

void CEnumeration::SetAstNode(CExprEnumDefinition* AstNode)
{
    CDefinition::SetAstNode(AstNode);
}
CExprEnumDefinition* CEnumeration::GetAstNode() const
{
    return static_cast<CExprEnumDefinition*>(CDefinition::GetAstNode());
}

void CEnumeration::SetIrNode(CExprEnumDefinition* AstNode)
{
    CDefinition::SetIrNode(AstNode);
}
CExprEnumDefinition* CEnumeration::GetIrNode(bool bForce) const
{
    return static_cast<CExprEnumDefinition*>(CDefinition::GetIrNode(bForce));
}
}
