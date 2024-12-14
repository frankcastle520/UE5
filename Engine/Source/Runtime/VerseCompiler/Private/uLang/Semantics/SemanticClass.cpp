// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/SemanticClass.h"

#include "uLang/Common/Algo/FindIf.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/SemanticInterface.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Semantics/VisitStamp.h"
#include "uLang/SourceProject/VerseVersion.h"

namespace uLang
{
CClass::CClass(
    CClassDefinition* Definition,
    CScope& EnclosingScope,
    CClass* Superclass /*= nullptr*/,
    TArray<CInterface*>&& SuperInterfaces /*= {}*/,
    EStructOrClass StructOrClass /*= EStructOrClass::Class*/,
    SEffectSet ConstructorEffects /*= EffectSets::ClassDefault*/)
    : CClass(&EnclosingScope, Definition, StructOrClass, Superclass, Move(SuperInterfaces), ConstructorEffects, this, {})
{
}

CClass::CClass(
    CScope* ParentScope,
    CClassDefinition* Definition,
    EStructOrClass StructOrClass,
    CClass* Superclass,
    TArray<CInterface*>&& SuperInterfaces,
    SEffectSet ConstructorEffects,
    CClass* GeneralizedClass,
    TArray<STypeVariableSubstitution> TypeVariableSubstitutions)
    : CNominalType(StaticTypeKind, ParentScope->GetProgram())
    , CLogicalScope(CScope::EKind::Class, ParentScope, ParentScope->GetProgram())
    , _Definition(Definition)
    , _StructOrClass(StructOrClass)
    , _Superclass(Superclass)
    , _SuperInterfaces(Move(SuperInterfaces))
    , _ConstructorEffects(ConstructorEffects)
    , _GeneralizedClass(GeneralizedClass)
    , _TypeVariableSubstitutions(Move(TypeVariableSubstitutions))
    , _OwnedNegativeClass(TUPtr<CClass>::New(this))
    , _NegativeClass(_OwnedNegativeClass.Get())
{
    _bHasCyclesBroken = Definition->_bHasCyclesBroken;
}

CClass::CClass(CClass* PositiveClass)
    : CNominalType(StaticTypeKind, PositiveClass->GetProgram())
    , CLogicalScope(CScope::EKind::Class, PositiveClass->GetParentScope(), PositiveClass->GetProgram())
    , _Definition(PositiveClass->_Definition)
    , _StructOrClass(PositiveClass->_StructOrClass)
    , _Superclass(PositiveClass->_Superclass? PositiveClass->_Superclass->_NegativeClass : nullptr)
    , _SuperInterfaces(GetNegativeInterfaces(PositiveClass->_SuperInterfaces))
    , _ConstructorEffects(PositiveClass->_ConstructorEffects)
    , _GeneralizedClass(PositiveClass->_GeneralizedClass)
    , _NegativeClass(PositiveClass)
{
}

const CTypeType* CClass::GetTypeType() const
{
    return &GetProgram().GetOrCreateTypeType(this->_NegativeClass, this);
}

bool CClass::IsAbstract() const
{
    return _Definition->_EffectAttributable.HasAttributeClass(GetProgram()._abstractClass, GetProgram());
}

bool CClass::IsPersistent() const
{
    return _Definition->_EffectAttributable.HasAttributeClass(GetProgram()._persistentClass, GetProgram());
}

// Only callable Phase > Deferred_Attributes
bool CClass::IsUnique() const
{
    if (_Definition->_EffectAttributable.HasAttributeClass(GetProgram()._uniqueClass, GetProgram()))
    {
        return true;
    }

    // The <unique> effect is heritable
    if (_Superclass && _Superclass->IsUnique())
    {
        return true;
    }

    for (const CInterface* Interface : _SuperInterfaces)
    {
        if (Interface->IsUnique())
        {
            return true;
        }
    }

    return false;
}

bool CClass::HasConcreteAttribute() const
{
    return _Definition->_EffectAttributable.HasAttributeClass(GetProgram()._concreteClass, GetProgram());
}

const CClass* CClass::FindConcreteBase() const
{
    for (const CClass* Class = this; Class != nullptr; Class = Class->_Superclass)
    {
        if (Class->HasConcreteAttribute())
        {
            return Class;
        }
    }

    return nullptr;
}

const CClass* CClass::FindInitialConcreteBase() const
{
    const CClass* Result = nullptr;
    for (const CClass* Class = this; Class != nullptr; Class = Class->_Superclass)
    {
        if (Class->HasConcreteAttribute())
        {
            Result = Class;
        }
    }
    return Result;
}

SAccessLevel CClass::GetDefaultDefinitionAccessLevel() const
{
    const CAstPackage* Package = GetPackage();
    return IsStruct() && (!Package || Package->_EffectiveVerseVersion >= Verse::Version::StructFieldsMustBePublic)
        ? SAccessLevel::EKind::Public
        : CLogicalScope::GetDefaultDefinitionAccessLevel();
}

void CClass::CreateNegativeDataDefinition(const CDataDefinition& PositiveDataDefinition) const
{
    TSRef<CDataDefinition> NegativeDataDefinition = _NegativeClass->CreateDataDefinition(PositiveDataDefinition.GetName());
    NegativeDataDefinition->SetPrototypeDefinition(*PositiveDataDefinition.GetPrototypeDefinition());
}

void CClass::CreateNegativeFunction(const CFunction& PositiveFunction) const
{
    CreateNegativeMemberFunction(*_NegativeClass, PositiveFunction);
}

CUTF8String CClass::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
{
    if (GetParentScope()->GetKind() != CScope::EKind::Function)
    {
        return CNominalType::AsCodeRecursive(OuterPrecedence, VisitedFlowTypes, bLinkable);
    }
    CUTF8StringBuilder Builder;
    CSymbol Name = GetParentScope()->GetScopeName();
    Builder.Append(Name.AsStringView());

    Builder.Append('(');

    const TArray<STypeVariableSubstitution>* InstTypeVariables;
    if (_OwnedNegativeClass)
    {
        InstTypeVariables = &_TypeVariableSubstitutions;
    }
    else
    {
        InstTypeVariables = &_NegativeClass->_TypeVariableSubstitutions;
    }
    const char* Separator = "";
    for (const STypeVariableSubstitution& InstTypeVariable : *InstTypeVariables)
    {
        if (!InstTypeVariable._TypeVariable->_ExplicitParam || !InstTypeVariable._TypeVariable->_NegativeTypeVariable)
        {
            continue;
        }
        Builder.Append(Separator);
        Separator = ",";
        const CTypeBase* Type;
        if (_OwnedNegativeClass)
        {
            Type = InstTypeVariable._PositiveType;
        }
        else
        {
            Type = InstTypeVariable._NegativeType;
        }
        Builder.Append(Type->AsCodeRecursive(ETypeSyntaxPrecedence::List, VisitedFlowTypes, bLinkable));
    }

    Builder.Append(')');

    return Builder.MoveToString();
}

SmallDefinitionArray CClass::FindDefinitions(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage, VisitStampType VisitStamp) const
{
    SmallDefinitionArray Result = CLogicalScope::FindDefinitions(Name, Origin, Qualifier, ContextPackage, VisitStamp);
    if (Origin != EMemberOrigin::Original)
    {
        Result.Append(FindInstanceMember(Name, EMemberOrigin::Inherited, Qualifier, ContextPackage, VisitStamp));
    }
    return Result;
}

SmallDefinitionArray CClass::FindInstanceMember(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage, VisitStampType VisitStamp) const
{
    SmallDefinitionArray Result;
    // Diamond inheritance (of interfaces) makes it neccessary to 
    // first do class inheritance ...
    const CClass* OriginClass = Origin == EMemberOrigin::Inherited ? this->_Superclass : this;
    while (OriginClass && OriginClass->TryMarkVisited(VisitStamp))
    {
        // FindDefinition will filter on Qualifier
        Result.Append(OriginClass->FindDefinitions(Name, EMemberOrigin::Original, Qualifier, ContextPackage, VisitStamp));
        OriginClass = Origin == EMemberOrigin::Original ? nullptr : OriginClass->_Superclass;
    }

    // .. and then all interfaces.
    if (Origin != EMemberOrigin::Original)
    {
        VisitStampType InterfaceVisitStamp = GenerateNewVisitStamp();
        OriginClass = this;
        while (OriginClass && OriginClass->TryMarkVisited(InterfaceVisitStamp))
        {
            for (CInterface* SuperInterface : OriginClass->_SuperInterfaces)
            {
                Result.Append(SuperInterface->FindInstanceMember(Name, EMemberOrigin::InheritedOrOriginal, Qualifier, ContextPackage, VisitStamp));
            }

            OriginClass = OriginClass->_Superclass;
        }
    }
    return Result;
}

EComparability CClass::GetComparability() const
{
    return GetComparability(GenerateNewVisitStamp());
}

EComparability CClass::GetComparability(VisitStampType VisitStamp) const
{
    const CSemanticProgram& Program = GetProgram();

    if (IsStruct())
    {
        // Structs are only comparable following the versioned change to require struct fields to be public.
        if (GetPackage()->_EffectiveVerseVersion < Verse::Version::StructFieldsMustBePublic)
        {
            return EComparability::Incomparable;
        }

        // Structs with the @import_as attribute are treated as though they have some incomparable field.
        if (Definition()->HasAttributeFunctionHack(Program._import_as.Get(), Program))
        {
            return EComparability::Incomparable;
        }

        // Otherwise, if a struct has only comparable fields, it is considered to be comparable.
        bool bAllDataMembersAreHashable = true;
        for (const CDataDefinition* DataMember : GetDefinitionsOfKind<CDataDefinition>())
        {
            const EComparability DataMemberComparability = DataMember->GetType() ? DataMember->GetType()->GetNormalType().GetComparability() : EComparability::Incomparable;
            switch (DataMemberComparability)
            {
            case EComparability::Comparable:
                bAllDataMembersAreHashable = false;
                break;
            case EComparability::ComparableAndHashable:
                break;
            case EComparability::Incomparable:
                return EComparability::Incomparable;
            default:
                ULANG_UNREACHABLE();
            }
        }

        // The struct is only hashable if all data members are hashable.
        return bAllDataMembersAreHashable ? EComparability::ComparableAndHashable : EComparability::Comparable;
    }

    // The class is only `comparable` if it has the `unique` attribute in its class inheritance chain
    // or its interface inheritance chain.
    for (const CClass* DefinitionClass = this; DefinitionClass; DefinitionClass = DefinitionClass->_Superclass)
    {
        if (!DefinitionClass->TryMarkVisited(VisitStamp))
        {
            break;
        }

        // Should perhaps use DefinitionClass->IsUnique(), but that isn't resolved
        // until the semantic analyzer is past the Deferred_Attributes phase
        if (DefinitionClass->_Definition->_EffectAttributable.HasAttributeClassHack(Program._uniqueClass, Program))
        {
            return EComparability::ComparableAndHashable;
        }

        for (CInterface* Interface : DefinitionClass->_SuperInterfaces)
        {
            if (Interface->GetComparability(VisitStamp) == EComparability::ComparableAndHashable)
            {
                return EComparability::ComparableAndHashable;
            }
        }
    }

    return EComparability::Incomparable;
}

bool CClass::IsPersistable() const
{
    return _Definition->_EffectAttributable.HasAttributeClassHack(GetProgram()._persistableClass, GetProgram());
}

void CClassDefinition::SetAstNode(CExprClassDefinition* AstNode)
{
    CDefinition::SetAstNode(AstNode);
}
CExprClassDefinition* CClassDefinition::GetAstNode() const
{
    return static_cast<CExprClassDefinition*>(CDefinition::GetAstNode());
}

void CClassDefinition::SetIrNode(CExprClassDefinition* AstNode)
{
    CDefinition::SetIrNode(AstNode);
}
CExprClassDefinition* CClassDefinition::GetIrNode(bool bForce) const
{
    return static_cast<CExprClassDefinition*>(CDefinition::GetIrNode(bForce));
}

const CNormalType& CInstantiatedClass::CreateNormalType() const
{
    if (CClass* InstClass = InstantiateClass(*_Class, GetPolarity(), GetSubstitutions()))
    {
        return *InstClass;
    }
    return *_Class;
}

static CClass* FindInstantiatedClass(const TURefArray<CClass>& InstClasses, const TArray<STypeVariableSubstitution>& InstTypeVariables)
{
    auto Last = InstClasses.end();
    auto I = uLang::FindIf(InstClasses.begin(), Last, [&](CClass* InstClass)
    {
        return InstClass->_TypeVariableSubstitutions == InstTypeVariables;
    });
    if (I == Last)
    {
        return nullptr;
    }
    return *I;
}

static void CreateNegativeClassMemberDefinitions(const CClass& PositiveClass)
{
    CClass* NegativeClass = PositiveClass._NegativeClass;
    NegativeClass->_GeneralizedClass = PositiveClass._GeneralizedClass;
    for (const CDefinition* Definition : PositiveClass.GetDefinitions())
    {
        if (const CDataDefinition* PositiveDataDefinition = Definition->AsNullable<CDataDefinition>())
        {
            PositiveClass.CreateNegativeDataDefinition(*PositiveDataDefinition);
        }
        else if (const CFunction* PositiveFunction = Definition->AsNullable<CFunction>())
        {
            PositiveClass.CreateNegativeFunction(*PositiveFunction);
        }
    }
}

static void InstantiatePositiveDataDefinition(
    CLogicalScope& InstScope,
    const CNormalType& InstType,
    const CDataDefinition& DataDefinition,
    const TArray<STypeVariableSubstitution>& Substitutions)
{
    TSRef<CDataDefinition> InstDataMember = InstScope.CreateDataDefinition(DataDefinition.GetName());
    InstDataMember->SetPrototypeDefinition(*DataDefinition.GetPrototypeDefinition());
    SetInstantiatedOverriddenDefinition(*InstDataMember, InstType, DataDefinition);
    const CTypeBase* NegativeDataMemberType = DataDefinition._NegativeType;
    const CTypeBase* PositiveDataMemberType = DataDefinition.GetType();
    NegativeDataMemberType = SemanticTypeUtils::Substitute(*NegativeDataMemberType, ETypePolarity::Negative, Substitutions);
    PositiveDataMemberType = SemanticTypeUtils::Substitute(*PositiveDataMemberType, ETypePolarity::Positive, Substitutions);
    InstDataMember->_NegativeType = NegativeDataMemberType;
    InstDataMember->SetType(PositiveDataMemberType);
}

CClass* InstantiatePositiveClass(const CClass& Class, const TArray<STypeVariableSubstitution>& Substitutions)
{
    if (Class.Definition()->_EnclosingScope.GetKind() != CScope::EKind::Function)
    {
        return nullptr;
    }

    TArray<STypeVariableSubstitution> InstTypeVariables = InstantiateTypeVariableSubstitutions(
        Class._TypeVariableSubstitutions,
        Substitutions);

    CClass* GeneralizedClass = Class._GeneralizedClass;
    TURefArray<CClass>& InstClasses = GeneralizedClass->_InstantiatedClasses;
    if (CClass* InstClass = FindInstantiatedClass(InstClasses, InstTypeVariables))
    {
        return InstClass;
    }

    CClass* Superclass = Class._Superclass;
    if (Superclass)
    {
        if (CClass* InstSuperclass = InstantiatePositiveClass(*Superclass, Substitutions))
        {
            Superclass = InstSuperclass;
        }
    }

    int32_t I = InstClasses.AddNew(
        Class.GetParentScope(),
        Class._Definition,
        Class._StructOrClass,
        Superclass,
        InstantiatePositiveInterfaces(Class._SuperInterfaces, Substitutions),
        Class._ConstructorEffects,
        GeneralizedClass,
        Move(InstTypeVariables));
    CClass* InstClass = InstClasses[I];

    for (const CDefinition* Definition : Class.GetDefinitions())
    {
        if (const CDataDefinition* DataDefinition = Definition->AsNullable<CDataDefinition>())
        {
            InstantiatePositiveDataDefinition(*InstClass, *InstClass, *DataDefinition, Substitutions);
        }
        else if (const CFunction* Function = Definition->AsNullable<CFunction>())
        {
            InstantiatePositiveFunction(*InstClass, *InstClass, *Function, Substitutions);
        }
    }

    CreateNegativeClassMemberDefinitions(*InstClass);

    SetNegativeClassMemberDefinitionTypes(*InstClass);

    return InstClass;
}

CClass* InstantiateClass(const CClass& Class, ETypePolarity Polarity, const TArray<STypeVariableSubstitution>& Substitutions)
{
    switch (Polarity)
    {
    case ETypePolarity::Negative:
        if (CClass* InstClass = InstantiatePositiveClass(*Class._NegativeClass, Substitutions))
        {
            return InstClass->_NegativeClass;
        }
        return nullptr;
    case ETypePolarity::Positive: return InstantiatePositiveClass(Class, Substitutions);
    default: ULANG_UNREACHABLE();
    }
}

void SetNegativeClassMemberDefinitionTypes(const CClass& PositiveClass)
{
    CClass* NegativeClass = PositiveClass._NegativeClass;
    const TArray<TSRef<CDefinition>>& PositiveDefinitions = PositiveClass.GetDefinitions();
    const TArray<TSRef<CDefinition>>& NegativeDefinitions = NegativeClass->GetDefinitions();
    for (auto I = PositiveDefinitions.begin(), J = NegativeDefinitions.begin(), Last = PositiveDefinitions.end(); I != Last; ++I)
    {
        if (const CDataDefinition* PositiveDataDefinition = (*I)->AsNullable<CDataDefinition>())
        {
            CDataDefinition& NegativeDataDefinition = (*J)->AsChecked<CDataDefinition>();
            NegativeDataDefinition._NegativeType = PositiveDataDefinition->GetType();
            NegativeDataDefinition.SetType(PositiveDataDefinition->_NegativeType);
            ++J;
        }
        else if (const CFunction* PositiveFunction = (*I)->AsNullable<CFunction>())
        {
            SetNegativeMemberDefinitionType((*J)->AsChecked<CFunction>(), *PositiveFunction);
            ++J;
        }
    }
}
}
