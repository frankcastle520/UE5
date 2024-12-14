// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/SemanticInterface.h"

#include "uLang/Common/Algo/FindIf.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Semantics/VisitStamp.h"

namespace uLang
{
CInterface::CInterface(CInterface* PositiveInterface)
    : CDefinition(StaticDefinitionKind, PositiveInterface->_EnclosingScope, PositiveInterface->GetName())
    , CNominalType(StaticTypeKind, PositiveInterface->GetProgram())
    , CLogicalScope(CScope::EKind::Interface, PositiveInterface->GetParentScope(), PositiveInterface->GetProgram())
    , _SuperInterfaces(GetNegativeInterfaces(PositiveInterface->_SuperInterfaces))
    , _GeneralizedInterface(PositiveInterface->_GeneralizedInterface)
    , _NegativeInterface(PositiveInterface)
{
}

CUTF8String CInterface::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
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
    if (_OwnedNegativeInterface)
    {
        InstTypeVariables = &_TypeVariableSubstitutions;
    }
    else
    {
        InstTypeVariables = &_NegativeInterface->_TypeVariableSubstitutions;
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
        if (_OwnedNegativeInterface)
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

SmallDefinitionArray CInterface::FindDefinitions(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage, VisitStampType VisitStamp) const
{
    SmallDefinitionArray Result = CLogicalScope::FindDefinitions(Name, Origin, Qualifier, ContextPackage, VisitStamp);
    if (Origin != EMemberOrigin::Original)
    {
        Result.Append(FindInstanceMember(Name, EMemberOrigin::Inherited, Qualifier, ContextPackage, VisitStamp));
    }
    return Result;
}

SmallDefinitionArray CInterface::FindInstanceMember(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage, VisitStampType VisitStamp) const
{
    SmallDefinitionArray Result;
    if (Origin == EMemberOrigin::Inherited || TryMarkVisited(VisitStamp))
    {
        if (Origin != EMemberOrigin::Inherited)
        {
            // FindDefinition will filter on Qualifier
            Result.Append(FindDefinitions(Name, EMemberOrigin::Original, Qualifier, ContextPackage, VisitStamp));
        }

        if (Origin != EMemberOrigin::Original)
        {
            for (const CInterface* SuperInterface : _SuperInterfaces)
            {
                Result.Append(SuperInterface->FindInstanceMember(Name, EMemberOrigin::InheritedOrOriginal, Qualifier, ContextPackage, VisitStamp));
            }
        }
    }

    return Result;
}

EComparability CInterface::GetComparability() const
{
    return GetComparability(GenerateNewVisitStamp());
}

EComparability CInterface::GetComparability(VisitStampType VisitStamp) const
{
    if (!TryMarkVisited(VisitStamp))
    {
        return EComparability::Incomparable;
    }

    const CSemanticProgram& Program = _GeneralizedInterface->GetProgram();

    // Should perhaps use _GeneralizedInterface->IsUnique(), but that isn't resolved
    // until the semantic analyzer is past the Deferred_Attributes phase
    if (_GeneralizedInterface->_EffectAttributable.HasAttributeClassHack(Program._uniqueClass, Program))
    {
        return EComparability::ComparableAndHashable;
    }

    for (const CInterface* Interface : _SuperInterfaces)
    {
        if (Interface->GetComparability(VisitStamp) == EComparability::ComparableAndHashable)
        {
            return EComparability::ComparableAndHashable;
        }
    }

    return EComparability::Incomparable;
}

void CInterface::CreateNegativeFunction(const CFunction& PositiveFunction) const
{
    CreateNegativeMemberFunction(*_NegativeInterface, PositiveFunction);
}

// Only callable Phase > Deferred_Attributes
bool CInterface::IsUnique() const
{
    if (_EffectAttributable.HasAttributeClass(GetProgram()._uniqueClass, GetProgram()))
    {
        return true;
    }

    // The <unique> effect is heritable
    for (const CInterface* Interface : _SuperInterfaces)
    {
        if (Interface->IsUnique())
        {
            return true;
        }
    }

    return false;
}

const CNormalType& CInstantiatedInterface::CreateNormalType() const
{
    if (CInterface* InstInterface = InstantiateInterface(*_Interface, GetPolarity(), GetSubstitutions()))
    {
        return *InstInterface;
    }
    return *_Interface;
}

static CInterface* FindInstantiatedInterface(const TURefArray<CInterface>& InstInterfaces, const TArray<STypeVariableSubstitution>& InstTypeVariables)
{
    auto Last = InstInterfaces.end();
    auto I = uLang::FindIf(InstInterfaces.begin(), Last, [&](CInterface* InstInterface)
    {
        return InstInterface->_TypeVariableSubstitutions == InstTypeVariables;
    });
    if (I == Last)
    {
        return nullptr;
    }
    return *I;
}

static void CreateNegativeInterfaceMemberDefinitions(const CInterface& PositiveInterface)
{
    for (const CFunction* PositiveFunction : PositiveInterface.GetDefinitionsOfKind<CFunction>())
    {
        PositiveInterface.CreateNegativeFunction(*PositiveFunction);
    }
}

CInterface* InstantiatePositiveInterface(const CInterface& Interface, const TArray<STypeVariableSubstitution>& Substitutions)
{
    if (Interface.GetParentScope()->GetKind() != CScope::EKind::Function)
    {
        return nullptr;
    }

    TArray<STypeVariableSubstitution> InstTypeVariables = InstantiateTypeVariableSubstitutions(
        Interface._TypeVariableSubstitutions,
        Substitutions);

    CInterface* GeneralizedInterface = Interface._GeneralizedInterface;
    TURefArray<CInterface>& InstInterfaces = GeneralizedInterface->_InstantiatedInterfaces;
    if (CInterface* InstInterface = FindInstantiatedInterface(InstInterfaces, InstTypeVariables))
    {
        return InstInterface;
    }

    int32_t I = InstInterfaces.AddNew(
        *Interface.GetParentScope(),
        Interface.GetName(),
        InstantiatePositiveInterfaces(Interface._SuperInterfaces, Substitutions),
        GeneralizedInterface,
        Move(InstTypeVariables),
        Interface._bHasCyclesBroken);
    CInterface* InstInterface = InstInterfaces[I];

    for (const CFunction* Function : Interface.GetDefinitionsOfKind<CFunction>())
    {
        InstantiatePositiveFunction(*InstInterface, *InstInterface, *Function, Substitutions);
    }

    CreateNegativeInterfaceMemberDefinitions(*InstInterface);

    SetNegativeInterfaceMemberDefinitionTypes(*InstInterface);

    return InstInterface;
}

TArray<STypeVariableSubstitution> InstantiateTypeVariableSubstitutions(
    const TArray<STypeVariableSubstitution>& TypeVariables,
    const TArray<STypeVariableSubstitution>& Substitutions)
{
    TArray<STypeVariableSubstitution> InstTypeVariables;
    InstTypeVariables.Reserve(TypeVariables.Num());
    for (const STypeVariableSubstitution& TypeVariable : TypeVariables)
    {
        const CTypeBase* NegativeType = TypeVariable._NegativeType;
        const CTypeBase* PositiveType = TypeVariable._PositiveType;
        NegativeType = SemanticTypeUtils::Substitute(*NegativeType, ETypePolarity::Negative, Substitutions);
        PositiveType = SemanticTypeUtils::Substitute(*PositiveType, ETypePolarity::Positive, Substitutions);
        InstTypeVariables.Add({TypeVariable._TypeVariable, NegativeType, PositiveType});
    }
    return InstTypeVariables;
}

CInterface* InstantiateInterface(const CInterface& Interface, ETypePolarity Polarity, const TArray<STypeVariableSubstitution>& Substitutions)
{
    switch (Polarity)
    {
    case ETypePolarity::Negative:
        if (CInterface* InstInterface = InstantiatePositiveInterface(*Interface._NegativeInterface, Substitutions))
        {
            return InstInterface->_NegativeInterface;
        }
        return nullptr;
    case ETypePolarity::Positive: return InstantiatePositiveInterface(Interface, Substitutions);
    default: ULANG_UNREACHABLE();
    }
}

TArray<CInterface*> InstantiatePositiveInterfaces(const TArray<CInterface*>& Interfaces, const TArray<STypeVariableSubstitution>& Substitutions)
{
    TArray<CInterface*> InstInterfaces;
    InstInterfaces.Reserve(Interfaces.Num());
    for (CInterface* Interface : Interfaces)
    {
        if (CInterface* InstInterface = InstantiatePositiveInterface(*Interface, Substitutions))
        {
            Interface = InstInterface;
        }
        InstInterfaces.Add(Interface);
    }
    return InstInterfaces;
}

TArray<CInterface*> GetNegativeInterfaces(const TArray<CInterface*>& Interfaces)
{
    TArray<CInterface*> NegativeInterfaces;
    NegativeInterfaces.Reserve(Interfaces.Num());
    for (CInterface* Interface : Interfaces)
    {
        NegativeInterfaces.Add(Interface->_NegativeInterface);
    }
    return NegativeInterfaces;
}

void SetInstantiatedOverriddenDefinition(CDefinition& InstDefinition, const CNormalType& InstType, const CDefinition& Definition)
{
    const CDefinition* OverriddenDefinition = Definition.GetOverriddenDefinition();
    if (!OverriddenDefinition)
    {
        return;
    }
    for (const CDefinition* SuperDefinition : InstType.FindInstanceMember(Definition.GetName(), EMemberOrigin::Inherited, Definition._Qualifier))
    {
        if (OverriddenDefinition->GetPrototypeDefinition() != SuperDefinition->GetPrototypeDefinition())
        {
            continue;
        }
        InstDefinition.SetOverriddenDefinition(SuperDefinition);
    }
}

void InstantiatePositiveFunction(
    CLogicalScope& InstScope,
    const CNormalType& InstType,
    const CFunction& Function,
    const TArray<STypeVariableSubstitution>& Substitutions)
{
    TSRef<CFunction> InstFunction = InstScope.CreateFunction(Function.GetName());
    InstFunction->_ExtensionFieldAccessorKind = Function._ExtensionFieldAccessorKind;
    InstFunction->SetPrototypeDefinition(*Function.GetPrototypeDefinition());
    SetInstantiatedOverriddenDefinition(*InstFunction, InstType, Function);
    const CFunctionType* NegativeFunctionType = Function._NegativeType;
    const CFunctionType* FunctionType = Function._Signature.GetFunctionType();
    NegativeFunctionType = &SemanticTypeUtils::Substitute(*NegativeFunctionType, ETypePolarity::Negative, Substitutions)->GetNormalType().AsChecked<CFunctionType>();
    FunctionType = &SemanticTypeUtils::Substitute(*FunctionType, ETypePolarity::Positive, Substitutions)->GetNormalType().AsChecked<CFunctionType>();
    InstFunction->_NegativeType = NegativeFunctionType;
    InstFunction->SetSignature(
        SSignature(
            FunctionType->GetNormalType().AsChecked<CFunctionType>(),
            TArray<CDataDefinition*>(Function._Signature.GetParams())),
        Function.GetSignatureRevision());
}

TSRef<CFunction> CreateNegativeMemberFunction(CLogicalScope& NegativeScope, const CFunction& PositiveFunction)
{
    TSRef<CFunction> NegativeFunction = NegativeScope.CreateFunction(PositiveFunction.GetName());
    NegativeFunction->_ExtensionFieldAccessorKind = PositiveFunction._ExtensionFieldAccessorKind;
    NegativeFunction->SetPrototypeDefinition(*PositiveFunction.GetPrototypeDefinition());
    return NegativeFunction;
}

void SetNegativeInterfaceMemberDefinitionTypes(const CInterface& PositiveInterface)
{
    CInterface* NegativeInterface = PositiveInterface._NegativeInterface;
    const TArray<TSRef<CDefinition>>& PositiveDefinitions = PositiveInterface.GetDefinitions();
    const TArray<TSRef<CDefinition>>& NegativeDefinitions = NegativeInterface ->GetDefinitions();
    for (auto I = PositiveDefinitions.begin(), J = NegativeDefinitions.begin(), Last = PositiveDefinitions.end(); I != Last; ++I)
    {
        if (const CFunction* PositiveFunction = (*I)->AsNullable<CFunction>())
        {
            SetNegativeMemberDefinitionType((*J)->AsChecked<CFunction>(), *PositiveFunction);
            ++J;
        }
    }
}

void SetNegativeMemberDefinitionType(CFunction& NegativeFunction, const CFunction& PositiveFunction)
{
    NegativeFunction._NegativeType = PositiveFunction._Signature.GetFunctionType();
    NegativeFunction.SetSignature(
        SSignature(
            *PositiveFunction._NegativeType,
            TArray<CDataDefinition*>(PositiveFunction._Signature.GetParams())),
        PositiveFunction.GetSignatureRevision());
}
}
