// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Semantics/Attributable.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/VisitStamp.h"

namespace uLang
{
class CEnumeration;
class CExprEnumLiteral;
class CExprEnumDefinition;

/**
 * Description for a single enumerator
 **/
class VERSECOMPILER_API CEnumerator : public CDefinition
{
public:

    static constexpr EKind StaticDefinitionKind = EKind::Enumerator;

    /// The integer value denoting this enumerator.
    const int32_t _Value;

    /// Type this enumerator belongs to
    CEnumeration* _Enumeration{nullptr};

    CEnumerator(CEnumeration& Enumeration, const CSymbol& Name, int32_t Value);

    CUTF8String AsCode() const;

    // CDefinition interface.
    void SetAstNode(CExprEnumLiteral* AstNode);
    CExprEnumLiteral* GetAstNode() const;

    void SetIrNode(CExprEnumLiteral* AstNode);
    CExprEnumLiteral* GetIrNode(bool bForce = false) const;

    virtual bool IsPersistenceCompatConstraint() const override;
};


/**
 * Enumeration type
 * @jira SOL-1013 : Make enums derive from Class?
 **/
class VERSECOMPILER_API CEnumeration : public CDefinition, public CLogicalScope, public CNominalType
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Enumeration;
    static const CDefinition::EKind StaticDefinitionKind = CDefinition::EKind::Enumeration;

    CAttributable _EffectAttributable;

    CEnumeration(const CSymbol& Name, CScope& EnclosingScope);

    CEnumerator& CreateEnumerator(const CSymbol& EnumeratorName, int32_t Value);

    // CTypeBase interface.
    using CTypeBase::GetProgram;
    virtual SmallDefinitionArray FindTypeMember(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier = SQualifier::Unknown(), VisitStampType VisitStamp = CScope::GenerateNewVisitStamp()) const override;
    virtual EComparability GetComparability() const override { return EComparability::ComparableAndHashable; }
    virtual bool IsPersistable() const override;
    virtual bool CanBeCustomAccessorDataType() const override { return true; };

    // CNominalType interface.
    virtual const CDefinition* Definition() const override { return this; }

    // CScope interface.
    virtual CSymbol GetScopeName() const override { return GetName(); }
    virtual const CDefinition* ScopeAsDefinition() const override { return this; }
    virtual SAccessLevel GetDefaultDefinitionAccessLevel() const override;

    // CDefinition interface.
    void SetAstNode(CExprEnumDefinition* AstNode);
    CExprEnumDefinition* GetAstNode() const;

    void SetIrNode(CExprEnumDefinition* AstNode);
    CExprEnumDefinition* GetIrNode(bool bForce = false) const;

    virtual const CLogicalScope* DefinitionAsLogicalScopeNullable() const override { return this; }

    virtual bool IsPersistenceCompatConstraint() const override { return IsPersistable(); }
};

}
