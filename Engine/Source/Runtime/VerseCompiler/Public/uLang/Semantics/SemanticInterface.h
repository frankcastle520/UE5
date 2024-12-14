// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Containers/SharedPointerSet.h"
#include "uLang/Common/Containers/UniquePointer.h"
#include "uLang/Common/Containers/UniquePointerArray.h"
#include "uLang/Common/Text/Symbol.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/Signature.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/VisitStamp.h"

namespace uLang
{
/** A interface: a named set of function signatures that can be implemented for other types. */
class VERSECOMPILER_API CInterface : public CDefinition, public CNominalType, public CLogicalScope
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Interface;
    static const CDefinition::EKind StaticDefinitionKind = CDefinition::EKind::Interface;

    // Attributes on the interface macro, like "interface<unique>"
    CAttributable _EffectAttributable;
    TOptional<SAccessLevel> _ConstructorAccessLevel;

    TArray<CInterface*> _SuperInterfaces;

    CInterface* _GeneralizedInterface{this};

    TArray<STypeVariableSubstitution> _TypeVariableSubstitutions;

    TURefArray<CInterface> _InstantiatedInterfaces;

    TUPtr<CInterface> _OwnedNegativeInterface;

    CInterface* _NegativeInterface;

    bool _bHasCyclesBroken { false };

    // Construct a generalized positive interface
    CInterface(const CSymbol& Name, CScope& EnclosingScope, const TArray<CInterface*>& SuperInterfaces = {})
        : CInterface(EnclosingScope, Name, SuperInterfaces, this, {}, false)
    {
    }

    // construct a positive interface instantiation
    CInterface(
        CScope& EnclosingScope,
        const CSymbol& Name,
        const TArray<CInterface*>& SuperInterfaces,
        CInterface* GeneralizedInterface,
        TArray<STypeVariableSubstitution> TypeVariableSubstitutions,
        bool bHasCyclesBroken)
        : CDefinition(StaticDefinitionKind, EnclosingScope, Name)
        , CNominalType(StaticTypeKind, EnclosingScope.GetProgram())
        , CLogicalScope(CScope::EKind::Interface, &EnclosingScope, EnclosingScope.GetProgram())
        , _SuperInterfaces(SuperInterfaces)
        , _GeneralizedInterface(GeneralizedInterface)
        , _TypeVariableSubstitutions(Move(TypeVariableSubstitutions))
        , _OwnedNegativeInterface(TUPtr<CInterface>::New(this))
        , _NegativeInterface(_OwnedNegativeInterface.Get())
        , _bHasCyclesBroken(bHasCyclesBroken)
    {
    }

    // Construct a negative interface from a positive interface
    explicit CInterface(CInterface* PositiveInterface);

    SAccessLevel DerivedConstructorAccessLevel() const
    {
        return _ConstructorAccessLevel.Get(SAccessLevel::EKind::Public);
    }

    using CTypeBase::GetProgram;

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override;
    virtual SmallDefinitionArray FindInstanceMember(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier = SQualifier::Unknown(), const CAstPackage* ContextPackage = nullptr, VisitStampType VisitStamp = CScope::GenerateNewVisitStamp()) const override;
    virtual EComparability GetComparability() const override;
    EComparability GetComparability(VisitStampType) const;
    virtual bool CanBeCustomAccessorDataType() const override { return true; }

    // CScope interface.
    virtual CSymbol GetScopeName() const override { return CNamed::GetName(); }
    virtual const CTypeBase* ScopeAsType() const override { return this; }
    virtual const CDefinition* ScopeAsDefinition() const override { return this; }
    virtual void CreateNegativeFunction(const CFunction& PositiveFunction) const override;

    // CLogicalScope interface.
    virtual SmallDefinitionArray FindDefinitions(
        const CSymbol& Name,
        EMemberOrigin Origin = EMemberOrigin::InheritedOrOriginal,
        const SQualifier& Qualifier = SQualifier::Unknown(),
        const CAstPackage* ContextPackage = nullptr,
        VisitStampType VisitStamp = GenerateNewVisitStamp()) const override;

    // CNominalType interface.
    virtual const CDefinition* Definition() const override { return this; }

    // CDefinition interface.
    void SetAstNode(CExprInterfaceDefinition* AstNode) { CDefinition::SetAstNode(AstNode); }
    CExprInterfaceDefinition* GetAstNode() const { return static_cast<CExprInterfaceDefinition*>(CDefinition::GetAstNode()); }

    void SetIrNode(CExprInterfaceDefinition* AstNode) { CDefinition::SetIrNode(AstNode); }
    CExprInterfaceDefinition* GetIrNode(bool bForce = false) const { return static_cast<CExprInterfaceDefinition*>(CDefinition::GetIrNode(bForce)); }

    virtual const CLogicalScope* DefinitionAsLogicalScopeNullable() const override { return this; }

    bool HasCyclesBroken() const { return _GeneralizedInterface->_bHasCyclesBroken; }

    bool IsParametric() const
    {
        return !!(_OwnedNegativeInterface ? _TypeVariableSubstitutions : _NegativeInterface->_TypeVariableSubstitutions).Num();
    }

    virtual bool IsPersistenceCompatConstraint() const override { return false; }

    bool IsUnique() const;
};

class VERSECOMPILER_API CInstantiatedInterface : public CInstantiatedType
{
public:
    CInstantiatedInterface(CSemanticProgram& Program, const CInterface& Interface, ETypePolarity Polarity, TArray<STypeVariableSubstitution> Arguments)
        : CInstantiatedType(Program, Polarity, Move(Arguments))
        , _Interface(&Interface)
    {
    }

    virtual bool CanBeCustomAccessorDataType() const override { return false; };

protected:
    virtual const CNormalType& CreateNormalType() const override;

private:
    const CInterface* _Interface;
};

// Eagerly instantiate an interface.
VERSECOMPILER_API CInterface* InstantiateInterface(
    const CInterface&,
    ETypePolarity,
    const TArray<STypeVariableSubstitution>&);

VERSECOMPILER_API CInterface* InstantiatePositiveInterface(
    const CInterface&,
    const TArray<STypeVariableSubstitution>&);

VERSECOMPILER_API TArray<STypeVariableSubstitution> InstantiateTypeVariableSubstitutions(
    const TArray<STypeVariableSubstitution>&,
    const TArray<STypeVariableSubstitution>&);

VERSECOMPILER_API TArray<CInterface*> InstantiatePositiveInterfaces(
    const TArray<CInterface*>&,
    const TArray<STypeVariableSubstitution>&);

VERSECOMPILER_API TArray<CInterface*> GetNegativeInterfaces(const TArray<CInterface*>& Interfaces);

VERSECOMPILER_API void InstantiatePositiveFunction(
    CLogicalScope& InstScope,
    const CNormalType& InstType,
    const CFunction&,
    const TArray<STypeVariableSubstitution>& Substitutions);

VERSECOMPILER_API void SetInstantiatedOverriddenDefinition(CDefinition& InstDefinition, const CNormalType& InstType, const CDefinition&);

VERSECOMPILER_API TSRef<CFunction> CreateNegativeMemberFunction(CLogicalScope& NegativeScope, const CFunction& PositiveFunction);

VERSECOMPILER_API void SetNegativeInterfaceMemberDefinitionTypes(const CInterface& PositiveInterface);

VERSECOMPILER_API void SetNegativeMemberDefinitionType(CFunction& NegativeFunction, const CFunction& PositiveFunction);
}
