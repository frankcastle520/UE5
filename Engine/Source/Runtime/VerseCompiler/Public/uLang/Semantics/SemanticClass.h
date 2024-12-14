// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Containers/SharedPointerSet.h"
#include "uLang/Common/Containers/UniquePointerSet.h"
#include "uLang/Semantics/Attributable.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/DataDefinition.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/SemanticInterface.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/StructOrClass.h"
#include "uLang/Semantics/VisitStamp.h"

namespace uLang
{

// Forward declarations
class CClassDefinition;

/**
 *  Class defining a class instance / object
 *  [Might break off CStructType to differentiate stack based types.]
 **/
class VERSECOMPILER_API CClass : public CNominalType, public CLogicalScope
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Class;
    static const CDefinition::EKind StaticDefinitionKind = CDefinition::EKind::Class;

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Public data
    CClassDefinition* const _Definition;
    const EStructOrClass _StructOrClass;
    CClass* _Superclass;

    TArray<CInterface*> _SuperInterfaces;
    // Flattened array of all interfaces this class inherits (including interfaces from its super-class).
    // Not initially filled out -- cached after we've fully constructed the whole type hierarchy.
    TArray<CInterface*> _AllInheritedInterfaces;

    SEffectSet _ConstructorEffects;

    // Kept alive via _Definition's IrNode's (CExprClassDefinition) Members field.
    // We don't hold a shared reference to this because the Ir tree has to be 
    // destroyed before the AST.
    TArray<CExprCodeBlock*> _IrBlockClauses;

    CClass* _GeneralizedClass{this};

    TArray<STypeVariableSubstitution> _TypeVariableSubstitutions;

    TURefArray<CClass> _InstantiatedClasses;

    TUPtr<CClass> _OwnedNegativeClass;

    CClass* _NegativeClass;

    bool _bHasCyclesBroken{false};

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Methods

    // Construct a generalized positive class
    CClass(
        CClassDefinition*,
        CScope& EnclosingScope,
        CClass* Superclass = nullptr,
        TArray<CInterface*>&& SuperInterfaces = {},
        EStructOrClass = EStructOrClass::Class,
        SEffectSet ConstructorEffects = EffectSets::ClassDefault);

    // Construct a positive class instantiation
    CClass(
        CScope* ParentScope,
        CClassDefinition*,
        EStructOrClass,
        CClass* Superclass,
        TArray<CInterface*>&& SuperInterfaces,
        SEffectSet ConstructorEffects,
        CClass* GeneralizedClass,
        TArray<STypeVariableSubstitution>);

    // Construct a negative class from a positive class
    explicit CClass(CClass* PositiveClass);

    using CTypeBase::GetProgram;

    const CTypeType* GetTypeType() const;

    void SetSuperclass(CClass* SuperClass);

    /// Determine if current class is the same class or a subclass of the specified `Class`
    bool IsClass(const CClass& Class) const;

    /// Determine if current class is a subclass / descendant / child of the specified `Class` (and not the same class!)
    bool IsSubclassOf(const CClass& Superclass) const;

    /// Determine if current class is a superclass / ancestor / parent of the specified `Class` (and not the same class!)
    bool IsSuperclassOf(const CClass& Subclass) const;

    /// Is this class a struct?
    bool IsStruct() const { return _StructOrClass == EStructOrClass::Struct; }

    bool IsNative() const { return Definition()->IsNative(); }

    bool IsAbstract() const;

    bool IsPersistent() const;

    bool IsUnique() const;

    /// Does this class hold a concrete attribute?
    bool HasConcreteAttribute() const;

    /// Return first class in the inheritance chain that contains the concrete attribute or null
    const CClass* FindConcreteBase() const;

    /// Return topmost class in the inheritance chain that contains the concrete attribute or null
    const CClass* FindInitialConcreteBase() const;

    /// Is this class concrete either by having a concrete attribute or inheriting one
    bool IsConcrete() const { return FindConcreteBase() != nullptr; }

    // CScope interface
    virtual CSymbol GetScopeName() const override { return Definition()->GetName(); }
    virtual const CTypeBase* ScopeAsType() const override { return this; }
    virtual const CDefinition* ScopeAsDefinition() const override { return Definition(); }
    virtual SAccessLevel GetDefaultDefinitionAccessLevel() const override;
    virtual void CreateNegativeDataDefinition(const CDataDefinition& PositiveDataDefinition) const override;
    virtual void CreateNegativeFunction(const CFunction& PositiveFunction) const override;

    // CLogicalScope interface.
    virtual SmallDefinitionArray FindDefinitions(
        const CSymbol& Name,
        EMemberOrigin Origin = EMemberOrigin::InheritedOrOriginal,
        const SQualifier& Qualifier = SQualifier::Unknown(),
        const CAstPackage* ContextPackage = nullptr,
        VisitStampType VisitStamp = GenerateNewVisitStamp()) const override;

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override;
    virtual SmallDefinitionArray FindInstanceMember(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier = SQualifier::Unknown(), const CAstPackage* ContextPackage = nullptr, VisitStampType VisitStamp = CScope::GenerateNewVisitStamp()) const override;
    virtual EComparability GetComparability() const override;
    EComparability GetComparability(VisitStampType) const;
    bool IsPersistable() const override;
    virtual void SetRevision(SemanticRevision Revision) override;
    virtual bool CanBeCustomAccessorDataType() const override { return true; }

    // CNominalType interface.
    virtual const CDefinition* Definition() const override;

    template<typename TFunc>
    void ForEachAncestorClassOrInterface(const TFunc& Func)
    {
        for (CInterface* Interface : _AllInheritedInterfaces)
        {
            Func(Interface, nullptr, Interface);
        }
        for (CClass* Class = _Superclass; Class; Class = Class->_Superclass)
        {
            Func(Class, Class, nullptr);
        }
    }

    bool HasAttributeClass(const CClass* AttributeClass, const CSemanticProgram& Program) const;
    void AddAttribute(SAttribute) const;
    TOptional<SAttribute> FindAttribute(const CClass* AttributeClass, const CSemanticProgram& Program) const;
    bool HasCyclesBroken() const;
    bool IsParametric() const
    {
        return !!(_OwnedNegativeClass ? _TypeVariableSubstitutions : _NegativeClass->_TypeVariableSubstitutions).Num();
    }
};  // CClass

class VERSECOMPILER_API CClassDefinition : public CDefinition, public CClass
{
public:
    CAttributable _EffectAttributable;
    TOptional<SAccessLevel> _ConstructorAccessLevel;

    CClassDefinition(const CSymbol& ClassName, CScope& EnclosingScope, CClass* Superclass = nullptr, TArray<CInterface*>&& SuperInterfaces = {}, EStructOrClass StructOrClass = EStructOrClass::Class)
        : CDefinition(StaticDefinitionKind, EnclosingScope, ClassName)
        , CClass(this, EnclosingScope, Superclass, Move(SuperInterfaces), StructOrClass)
    {
    }

    using CAttributable::HasAttributeClass;
    using CAttributable::AddAttribute;
    using CAttributable::FindAttribute;

    SAccessLevel DerivedConstructorAccessLevel() const
    {
        return _ConstructorAccessLevel.Get(SAccessLevel::EKind::Public);
    }

    // CDefinition interface.
    void SetAstNode(CExprClassDefinition* AstNode);
    CExprClassDefinition* GetAstNode() const;

    void SetIrNode(CExprClassDefinition* AstNode);
    CExprClassDefinition* GetIrNode(bool bForce = false) const;

    virtual const CLogicalScope* DefinitionAsLogicalScopeNullable() const override { return this; }

    virtual bool IsPersistenceCompatConstraint() const override { return IsPersistable(); }
};

class VERSECOMPILER_API CInstantiatedClass : public CInstantiatedType
{
public:
    CInstantiatedClass(CSemanticProgram& Program, const CClass& Class, ETypePolarity Polarity, TArray<STypeVariableSubstitution> Arguments)
        : CInstantiatedType(Program, Polarity, Move(Arguments))
        , _Class(&Class)
    {
    }

    virtual bool CanBeCustomAccessorDataType() const override { return false; };

protected:
    virtual const CNormalType& CreateNormalType() const override;

private:
    const CClass* _Class;
};

// Eagerly instantiate a class.
VERSECOMPILER_API CClass* InstantiateClass(const CClass&, ETypePolarity, const TArray<STypeVariableSubstitution>&);

VERSECOMPILER_API void SetNegativeClassMemberDefinitionTypes(const CClass& PositiveClass);

//=======================================================================================
// CClass Inline Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
inline const CDefinition* CClass::Definition() const
{
    return _Definition;
}

//---------------------------------------------------------------------------------------
ULANG_FORCEINLINE void CClass::SetSuperclass(CClass* Superclass)
{
    // TODO-Verse: add _Subclasses
    _Superclass = Superclass;
}

//---------------------------------------------------------------------------------------
ULANG_FORCEINLINE bool CClass::IsClass(const CClass& Class) const
{
    TArrayG<const CClass*, TInlineElementAllocator<16>> SeenClasses;
    const CClass* RelatedClass = this;

    do
    {
        if (SeenClasses.Contains(RelatedClass))
        {
            return false;
        }
        SeenClasses.Push(RelatedClass);

        if (RelatedClass == &Class)
        {
            return true;
        }

        RelatedClass = RelatedClass->_Superclass;
    } while (RelatedClass);

    return false;
}

//---------------------------------------------------------------------------------------
ULANG_FORCEINLINE bool CClass::IsSubclassOf(const CClass& Superclass) const
{
    ULANG_ASSERT(HasCyclesBroken());

    const CClass* RelatedClass = _Superclass;

    while (RelatedClass)
    {
        if (RelatedClass == &Superclass)
        {
            return true;
        }

        RelatedClass = RelatedClass->_Superclass;
    }

    return false;
}

//---------------------------------------------------------------------------------------
ULANG_FORCEINLINE bool CClass::IsSuperclassOf(const CClass& Subclass) const
{
    const CClass* RelatedClass = Subclass._Superclass;

    while (RelatedClass)
    {
        if (RelatedClass == this)
        {
            return true;
        }

        RelatedClass = RelatedClass->_Superclass;
    }

    return false;
}

//---------------------------------------------------------------------------------------
ULANG_FORCEINLINE void CClass::SetRevision(SemanticRevision Revision)
{
    CClass* Class = this;
    do
    {
        ULANG_ENSUREF(Revision >= Class->GetRevision(), "Revision to be set must not be smaller than existing revisions.");
        if (Class->GetRevision() == Revision)
        {
            break;
        }

        Class->CLogicalScope::SetRevision(Revision);
        Class = Class->_Superclass;
    } while (Class);
}

inline bool CClass::HasAttributeClass(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    return _Definition->HasAttributeClass(AttributeClass, Program);
}

inline void CClass::AddAttribute(SAttribute Attribute) const
{
    return _Definition->AddAttribute(Move(Attribute));
}

inline TOptional<SAttribute> CClass::FindAttribute(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    return _Definition->FindAttribute(AttributeClass, Program);
}

inline bool CClass::HasCyclesBroken() const
{
    return _Definition->_bHasCyclesBroken;
}

}  // namespace uLang
