// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/SemanticTypes.h"

#include "uLang/Common/Algo/AllOf.h"
#include "uLang/Common/Algo/AnyOf.h"
#include "uLang/Common/Algo/Cases.h"
#include "uLang/Common/Algo/Contains.h"
#include "uLang/Common/Algo/FindIf.h"
#include "uLang/Common/Containers/Set.h"
#include "uLang/Common/Containers/ValueRange.h"
#include "uLang/Common/Misc/MathUtils.h"
#include "uLang/Common/Templates/References.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/SemanticEnumeration.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/TypeAlias.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Semantics/UnknownType.h"

namespace uLang
{
const char* TypeKindAsCString(ETypeKind Type)
{
    switch (Type)
    {
#define VISIT_KIND(Name, CppType) case ETypeKind::Name: return #Name;
        VERSE_ENUM_SEMANTIC_TYPE_KINDS(VISIT_KIND)
#undef VISIT_KIND
    default:
        ULANG_UNREACHABLE();
    }
}

//=======================================================================================
// CNormalType
//=======================================================================================

SmallDefinitionArray CNormalType::FindInstanceMember(const CSymbol& MemberName, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage) const
{
    return FindInstanceMember(MemberName, Origin, Qualifier, ContextPackage, CScope::GenerateNewVisitStamp());
}

SmallDefinitionArray CNormalType::FindTypeMember(const CSymbol& MemberName, EMemberOrigin Origin, const SQualifier& Qualifier) const
{
    return FindTypeMember(MemberName, Origin, Qualifier, CScope::GenerateNewVisitStamp());
}

//=======================================================================================
// CNominalType
//=======================================================================================

CUTF8String CNominalType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
{
    return Definition()->AsNameStringView();
}


//=======================================================================================
// CPointerType
//=======================================================================================

CPointerType::CPointerType(CSemanticProgram& Program, const CTypeBase* NegativeValueType, const CTypeBase* PositiveValueType)
    : CInvariantValueType(ETypeKind::Pointer, Program, NegativeValueType, PositiveValueType)
{
}

//=======================================================================================
// CReferenceType
//=======================================================================================

CReferenceType::CReferenceType(CSemanticProgram& Program, const CTypeBase* NegativeValueType, const CTypeBase* PositiveValueType)
    : CInvariantValueType(ETypeKind::Reference, Program, NegativeValueType, PositiveValueType)
{
}

//=======================================================================================
// COptionType
//=======================================================================================

COptionType::COptionType(CSemanticProgram& Program, const CTypeBase* ValueType)
    : CValueType(ETypeKind::Option, Program, ValueType)
{}


//=======================================================================================
// CTypeType
//=======================================================================================
CUTF8String CTypeType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
{
    const CNormalType& NegativeType = _NegativeType->GetNormalType();
    const CNormalType& PositiveType = _PositiveType->GetNormalType();
    if (!bLinkable && &NegativeType == &PositiveType)
    {
        return NegativeType.AsCodeRecursive(OuterPrecedence, VisitedFlowTypes, bLinkable).AsCString();
    }
    if (&NegativeType == &GetProgram()._falseType)
    {
        if (&PositiveType == &GetProgram()._anyType)
        {
            return "type";
        }
        return CUTF8String("subtype(%s)", PositiveType.AsCodeRecursive(ETypeSyntaxPrecedence::List, VisitedFlowTypes, bLinkable).AsCString());
    }
    if (_PositiveType == &GetProgram()._anyType)
    {
        return CUTF8String("supertype(%s)", NegativeType.AsCodeRecursive(ETypeSyntaxPrecedence::List, VisitedFlowTypes, bLinkable).AsCString());
    }
    // There isn't a good single expression to represent this.
    return CUTF8String(
        "type(%s, %s)",
        NegativeType.AsCodeRecursive(ETypeSyntaxPrecedence::List, VisitedFlowTypes, bLinkable).AsCString(),
        PositiveType.AsCodeRecursive(ETypeSyntaxPrecedence::List, VisitedFlowTypes, bLinkable).AsCString());
}


//=======================================================================================
// CTupleType
//=======================================================================================

CUTF8String CTupleType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
{
    CUTF8StringBuilder DestCode;
    DestCode.Append("tuple(");
    DestCode.Append(AsParamsCode(OuterPrecedence, VisitedFlowTypes, false, bLinkable));
    DestCode.Append(')');
    return DestCode.MoveToString();
}

CUTF8String CTupleType::AsParamsCode(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool WithColon) const
{
    return AsParamsCode(OuterPrecedence, VisitedFlowTypes, WithColon, false);
}

CUTF8String CTupleType::AsParamsCode(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool WithColon, bool bLinkable) const
{
    CUTF8StringBuilder DestCode;
    for (int32_t ElementIndex = 0; ElementIndex < _Elements.Num(); ++ElementIndex)
    {
        const CTypeBase* Element = _Elements[ElementIndex];
        if (WithColon)
        {
            DestCode.Append(':');
        }
        DestCode.Append(Element->AsCodeRecursive(ETypeSyntaxPrecedence::List, VisitedFlowTypes, bLinkable));
        if (ElementIndex + 1 < _Elements.Num())
        {
            DestCode.Append(',');
        }
    }
    return DestCode.MoveToString();
}

EComparability CTupleType::GetComparability() const
{
    // Use the comparability of the least comparable element of tuple.
    bool bAllDataMembersAreHashable = true;
    for (const CTypeBase* Element : _Elements)
    {
        switch (Element->GetNormalType().GetComparability())
        {
        case EComparability::Incomparable: return EComparability::Incomparable;
        case EComparability::Comparable: bAllDataMembersAreHashable = false; break;
        case EComparability::ComparableAndHashable: break;
        default: ULANG_UNREACHABLE();
        }
    }
    return bAllDataMembersAreHashable ? EComparability::ComparableAndHashable : EComparability::Comparable;
}

bool CTupleType::IsPersistable() const
{
    for (const CTypeBase* Element : _Elements)
    {
        if (!Element->GetNormalType().IsPersistable())
        {
            return false;
        }
    }
    return true;
}

CTupleType::ElementArray CTupleType::ElementsWithSortedNames() const
{
    ElementArray Elements = GetElements();
    Algo::Sort(TRangeView{Elements.begin() + GetFirstNamedIndex(), Elements.end()}, 
        [] (const CTypeBase* Type1, const CTypeBase* Type2)
        {
            const CNamedType& NamedType1 = Type1->GetNormalType().AsChecked<CNamedType>();
            const CNamedType& NamedType2 = Type2->GetNormalType().AsChecked<CNamedType>();
            return NamedType1.GetName() < NamedType2.GetName();
        });
    return Elements;
}

const CNamedType* CTupleType::FindNamedType(CSymbol Name) const
{
    for (int32_t I = GetFirstNamedIndex(); I < Num(); ++I)
    {
        const CNamedType& MaybeMatch = _Elements[I]->GetNormalType().AsChecked<CNamedType>();
        if (MaybeMatch.GetName() == Name)
        {
            return &MaybeMatch;
        }
    }
    return nullptr;
}

//=======================================================================================
// CFunctionType
//=======================================================================================

template <typename... ArgTypes>
static const CTypeBase* GetOrCreateParamTypeImpl(CSemanticProgram& Program, CTupleType::ElementArray&& ParamTypes, ArgTypes&&... Args)
{
    if (ParamTypes.Num() == 1)
    {
        return ParamTypes[0];
    }
    return &Program.GetOrCreateTupleType(Move(ParamTypes), uLang::ForwardArg<ArgTypes>(Args)...);
}

const CTypeBase* CFunctionType::GetOrCreateParamType(CSemanticProgram& Program, CTupleType::ElementArray&& ParamTypes)
{
    return GetOrCreateParamTypeImpl(Program, uLang::Move(ParamTypes));
}

const CTypeBase* CFunctionType::GetOrCreateParamType(CSemanticProgram& Program, CTupleType::ElementArray&& ParamTypes, int32_t FirstNamedIndex)
{
    return GetOrCreateParamTypeImpl(Program, uLang::Move(ParamTypes), FirstNamedIndex);
}

void CFunctionType::BuildTypeVariableCode(CUTF8StringBuilder& Builder, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
{
    const char* TypeVariableSeparator = " where ";
    for (const CTypeVariable* TypeVariable : GetTypeVariables())
    {
        if (TypeVariable->_ExplicitParam && TypeVariable->_ExplicitParam->_ImplicitParam != TypeVariable)
        {
            continue;
        }
        Builder.Append(TypeVariableSeparator);
        TypeVariableSeparator = ",";
        Builder.Append(TypeVariable->AsCodeRecursive(ETypeSyntaxPrecedence::Min, VisitedFlowTypes, bLinkable).AsCString());
    }
}

void CFunctionType::BuildEffectAttributeCode(CUTF8StringBuilder& Builder) const
{
    if (TOptional<TArray<const CClass*>> EffectClasses = GetProgram().ConvertEffectSetToEffectClasses(_Effects, EffectSets::FunctionDefault))
    {
        for (const CClass* EffectClass : EffectClasses.GetValue())
        {
            Builder.Append('<');
            Builder.Append(EffectClass->AsCode());
            Builder.Append('>');
        }
    }
}

void CFunctionType::BuildParameterBlockCode(CUTF8StringBuilder& Builder, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
{
    Builder.Append('(');

    const char* ParamSeparator = "";
    for (const CTypeBase* ParamType : GetParamTypes())
    {
        Builder.Append(ParamSeparator);
        ParamSeparator = ",";

        Builder.Append(':');
        Builder.Append(ParamType->AsCodeRecursive(ETypeSyntaxPrecedence::Definition, VisitedFlowTypes, false));
    }

    BuildTypeVariableCode(Builder, VisitedFlowTypes, bLinkable);

    Builder.Append(')');

    BuildEffectAttributeCode(Builder);
}

CUTF8String CFunctionType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
{
    CUTF8StringBuilder DestCode;
    if (_TypeVariables.Num() || _Effects != EffectSets::FunctionDefault)
    {
        DestCode.Append("type{_");
        BuildParameterBlockCode(DestCode, VisitedFlowTypes, bLinkable);
        DestCode.Append(':');
        DestCode.Append(_ReturnType.AsCodeRecursive(ETypeSyntaxPrecedence::Definition, VisitedFlowTypes, bLinkable));
        DestCode.Append('}');
    }
    else
    {
        const bool bNeedsParentheses = OuterPrecedence >= ETypeSyntaxPrecedence::To;
        if (bNeedsParentheses)
        {
            DestCode.Append('(');
        }
        DestCode.Append(_ParamsType->AsCodeRecursive(ETypeSyntaxPrecedence::To, VisitedFlowTypes, bLinkable));
        DestCode.Append("->");
        DestCode.Append(_ReturnType.AsCodeRecursive(ETypeSyntaxPrecedence::To, VisitedFlowTypes, bLinkable));
        if (bNeedsParentheses)
        {
            DestCode.Append(')');
        }
    }

    return DestCode.MoveToString();
}

//=======================================================================================
// CIntType
//=======================================================================================

CUTF8String CIntType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
{
    if ((!GetMin() || GetMin() == INT64_MIN) &&
        (!GetMax() || GetMax() == INT64_MAX))
    {
        return "int";
    }

    CUTF8StringBuilder DestCode;
    if (GetMin() == GetMax())
    {
        DestCode.AppendFormat("type{%ld}", *GetMin());
        return DestCode.MoveToString();
    }

    DestCode.Append("type{_X:int where ");
    const char* Separator = "";

    if (GetMin() && GetMin() != INT64_MIN)
    {
        DestCode.AppendFormat("%ld <= _X", *GetMin());
        Separator = ", ";
    }

    if (GetMax() && GetMax() != INT64_MAX)
    {
        DestCode.Append(Separator);
        DestCode.AppendFormat("_X <= %ld", *GetMax());
    }
    DestCode.Append("}");
    return DestCode.MoveToString();
}

//=======================================================================================
// CFloatType
//=======================================================================================

CUTF8String CFloatType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
{
    if (GetMin() == -INFINITY && std::isnan(GetMax()))
    {
        return "float";
    }

    CUTF8StringBuilder DestCode;
    auto AppendFloat = [&DestCode](double Value)
    {
        if (Value == INFINITY)
        {
            DestCode.Append("Inf");
        }
        else if (Value == -INFINITY)
        {
            DestCode.Append("-Inf");
        }
        else if (std::isnan(Value))
        {
            DestCode.Append("NaN");
        }
        else
        {
            int Exponent;
            double Unused = std::frexp(Value, &Exponent);
            // Supress unused result warning.
            (void)Unused;
            if (std::abs(Exponent) > 5)
            {
                DestCode.AppendFormat("%e", Value);
            }
            else
            {
                DestCode.AppendFormat("%f", Value);
            }
        }
    };

    if (GetMin() == GetMax() || std::isnan(GetMax()))
    {
        DestCode.Append("type{");
        AppendFloat(GetMin());
        DestCode.Append("}");
        return DestCode.MoveToString();
    }

    ULANG_ASSERTF(!std::isnan(GetMin()) && !std::isnan(GetMax()), "only the intrinsic float type / type{NaN} should contain nan");
    // Unlike with ints we always print the upper and lower bound this is because
    // 1) it's actually always possible to have an upper and lower bound in MaxVerse
    // 2) floats are not totally ordered and have unintuitive semantics for new programmers so both bounds might help more.
    DestCode.Append("type{_X:float where ");
    AppendFloat(GetMin());
    DestCode.Append(" <= _X, _X <= ");
    AppendFloat(GetMax());
    DestCode.Append("}");
    return DestCode.MoveToString();
}

ETypePolarity CFlowType::Polarity() const
{
    return _Polarity;
}

const CTypeBase* CFlowType::GetChild() const
{
    return _Child;
}

void CFlowType::SetChild(const CTypeBase* Child) const
{
    _Child = Child;
}

void CFlowType::AddFlowEdge(const CFlowType* FlowType) const
{
    if (_FlowEdges.Contains(FlowType))
    {
        return;
    }
    _FlowEdges.Insert(FlowType);
}

void CFlowType::EmptyFlowEdges() const
{
    for (const CFlowType* NegativeFlowType : _FlowEdges)
    {
        NegativeFlowType->_FlowEdges.Remove(this);
    }
    _FlowEdges.Empty();
}

namespace {
    void MergeChild(const CFlowType& Dest, const CTypeBase* Src, ETypePolarity Polarity)
    {
        ULANG_ASSERTF(Dest.Polarity() == Polarity, "`Dest`'s polarity must match `Polarity`");
        const CTypeBase* DestChild = Dest.GetChild();
        switch (Polarity)
        {
        case ETypePolarity::Negative:
            Dest.SetChild(SemanticTypeUtils::Meet(DestChild, Src));
            break;
        case ETypePolarity::Positive:
            Dest.SetChild(SemanticTypeUtils::Join(DestChild, Src));
            break;
        default:
            ULANG_UNREACHABLE();
        }
    }

    void Merge(const CFlowType& Dest, const CFlowType& Src, ETypePolarity Polarity)
    {
        ULANG_ASSERTF(Dest.Polarity() == Polarity, "`Dest`'s polarity must match `Polarity`");
        ULANG_ASSERTF(Src.Polarity() == Polarity, "`Src`'s polarity must match `Polarity`");
        MergeChild(Dest, Src.GetChild(), Polarity);
        for (const CFlowType* FlowType : Src.FlowEdges())
        {
            Dest.AddFlowEdge(FlowType);
            FlowType->AddFlowEdge(&Dest);
        }
    }

    void MergeNegativeChild(const CFlowType& Dest, const CTypeBase* Src)
    {
        MergeChild(Dest, Src, ETypePolarity::Negative);
    }

    void MergeNegative(const CFlowType& Dest, const CFlowType& Src)
    {
        Merge(Dest, Src, ETypePolarity::Negative);
    }

    void MergePositiveChild(const CFlowType& Dest, const CTypeBase* Src)
    {
        MergeChild(Dest, Src, ETypePolarity::Positive);
    }

    void MergePositive(const CFlowType& Dest, const CFlowType& Src)
    {
        Merge(Dest, Src, ETypePolarity::Positive);
    }
}

const CNormalType& CFlowType::GetNormalType() const
{
    return GetChild()->GetNormalType();
}

CUTF8String CFlowType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
{
    // Guard against trying to print types that have cycles via flow types.
    if (VisitedFlowTypes.Contains(this))
    {
        ULANG_ASSERT(!bLinkable);
        return "...";
    }
    else
    {
        const int32_t Index = VisitedFlowTypes.Add(this);
        const CUTF8String Result = GetChild()->AsCodeRecursive(OuterPrecedence, VisitedFlowTypes, bLinkable);
        ULANG_ASSERT(Index == VisitedFlowTypes.Num() - 1);
        VisitedFlowTypes.Pop();
        return Result;
    }
}

CUTF8String CNamedType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const
{
    CUTF8StringBuilder Builder;
    bool bNeedsParentheses = OuterPrecedence >= ETypeSyntaxPrecedence::Definition;
    if (bNeedsParentheses)
    {
        Builder.Append('(');
    }
    Builder.Append('?')
           .Append(_Name.AsStringView())
           .Append(':')
           .Append(_ValueType->AsCodeRecursive(ETypeSyntaxPrecedence::Definition, VisitedFlowTypes, bLinkable));
    if (_HasValue)
    {
        Builder.Append(" = ...");
    }
    if (bNeedsParentheses)
    {
        Builder.Append(')');
    }
    return Builder.MoveToString();
}

const CTupleType& CNamedType::ToTupleType() const
{
    int32_t FirstNamedIndex = 0;
    return GetProgram().GetOrCreateTupleType({ this }, FirstNamedIndex);
}

//=======================================================================================
// SemanticTypeUtils
//=======================================================================================

const CClass* SemanticTypeUtils::AsSingleClass(const CNormalType& NegativeType, const CNormalType& PositiveType)
{
    const CClass* NegativeClass = NegativeType.AsNullable<CClass>();
    if (!NegativeClass)
    {
        return nullptr;
    }
    const CClass* PositiveClass = PositiveType.AsNullable<CClass>();
    if (!PositiveClass)
    {
        return nullptr;
    }
    if (NegativeClass != PositiveClass->_NegativeClass)
    {
        return nullptr;
    }
    return PositiveClass;
}

const CInterface* SemanticTypeUtils::AsSingleInterface(const CNormalType& NegativeType, const CNormalType& PositiveType)
{
    const CInterface* NegativeInterface = NegativeType.AsNullable<CInterface>();
    if (!NegativeInterface)
    {
        return nullptr;
    }
    const CInterface* PositiveInterface = PositiveType.AsNullable<CInterface>();
    if (!PositiveInterface)
    {
        return nullptr;
    }
    if (NegativeInterface != PositiveInterface->_NegativeInterface)
    {
        return nullptr;
    }
    return PositiveInterface;
}

static const CTypeBase* SubstituteMapType(const CMapType& MapType, ETypePolarity Polarity, const TArray<STypeVariableSubstitution>& InstTypeVariables)
{
    CSemanticProgram& Program = MapType.GetProgram();
    const CTypeBase* KeyType = MapType.GetKeyType();
    const CTypeBase* ValueType = MapType.GetValueType();
    const CTypeBase* InstKeyType = SemanticTypeUtils::Substitute(*KeyType, Polarity, InstTypeVariables);
    const CTypeBase* InstValueType = SemanticTypeUtils::Substitute(*ValueType, Polarity, InstTypeVariables);
    if (KeyType == InstKeyType && ValueType == InstValueType)
    {
        return &MapType;
    }
    return &Program.GetOrCreateMapType(*InstKeyType, *InstValueType, MapType.IsWeak());
}

const CTypeBase* SemanticTypeUtils::Substitute(const CTypeBase& Type, ETypePolarity Polarity, const TArray<STypeVariableSubstitution>& InstTypeVariables)
{
    if (const CFlowType* FlowType = Type.AsFlowType())
    {
        const CTypeBase* Child = FlowType->GetChild();
        const CTypeBase* InstChild = Substitute(*Child, Polarity, InstTypeVariables);
        // Unchecked invariant: flow edges of generalized types point to dead
        // types and need not be instantiated.  This will cease to be true once
        // non-constructor closed-world functions are supported (the result type
        // of such a function may point to a negative type if the result is an
        // instantiated parameteric function); or if the `type` macro is
        // supported with arbitrary values.  For example,
        // @code
        // Identity(X:t):t = X
        // F() := Identity
        // @endcode
        // or
        // @code
        // Identity(X:t):t = X
        // class1(t:type) := class:
        //     Property:t
        // MakeIdentityClass1<constructor>() := class1(type{Identity})
        //     Property := Identity
        // @endcode
        // Both of these cases can be handled if all live flow types (through
        // the type graph) are marked.  Flow edges pointing to live flow types
        // should be recreated in the instantiated type (and point to
        // instantiated flow types).  However, this will cease to work correctly
        // once nested closed-world functions are supported.  For example,
        // @code
        // Identity(X:t):t = X
        // F():int =
        //     G := Identity
        //     H() := G
        // @endcode
        return InstChild;
    }

    CSemanticProgram& Program = Type.GetProgram();
    const CNormalType& NormalType = Type.GetNormalType();
    switch (NormalType.GetKind())
    {
    case ETypeKind::Array:
    {
        const CArrayType& ArrayType = NormalType.AsChecked<CArrayType>();
        const CTypeBase* InstElementType = Substitute(*ArrayType.GetElementType(), Polarity, InstTypeVariables);
        return ArrayType.GetElementType() == InstElementType
            ? &ArrayType
            : &Program.GetOrCreateArrayType(InstElementType);
    }
    case ETypeKind::Generator:
    {
        const CGeneratorType& GeneratorType = NormalType.AsChecked<CGeneratorType>();
        const CTypeBase* InstElementType = Substitute(*GeneratorType.GetElementType(), Polarity, InstTypeVariables);
        return GeneratorType.GetElementType() == InstElementType
            ? &GeneratorType
            : &Program.GetOrCreateGeneratorType(InstElementType);
    }
    case ETypeKind::Map:
        return SubstituteMapType(NormalType.AsChecked<CMapType>(), Polarity, InstTypeVariables);
    case ETypeKind::Pointer:
    {
        const CPointerType& PointerType = NormalType.AsChecked<CPointerType>();
        const CTypeBase* NegativeValueType = PointerType.NegativeValueType();
        const CTypeBase* PositiveValueType = PointerType.PositiveValueType();
        const CTypeBase* InstNegativeValueType = Substitute(*NegativeValueType, FlipPolarity(Polarity), InstTypeVariables);
        const CTypeBase* InstPositiveValueType = Substitute(*PositiveValueType, Polarity, InstTypeVariables);
        return NegativeValueType == InstNegativeValueType && PositiveValueType == InstPositiveValueType
            ? &PointerType
            : &Program.GetOrCreatePointerType(InstNegativeValueType, InstPositiveValueType);
    }
    case ETypeKind::Reference:
    {
        const CReferenceType& ReferenceType = NormalType.AsChecked<CReferenceType>();
        const CTypeBase* NegativeValueType = ReferenceType.NegativeValueType();
        const CTypeBase* PositiveValueType = ReferenceType.PositiveValueType();
        const CTypeBase* InstNegativeValueType = Substitute(*NegativeValueType, FlipPolarity(Polarity), InstTypeVariables);
        const CTypeBase* InstPositiveValueType = Substitute(*PositiveValueType, Polarity, InstTypeVariables);
        return NegativeValueType == InstNegativeValueType && PositiveValueType == InstPositiveValueType
            ? &ReferenceType
            : &Program.GetOrCreateReferenceType(InstNegativeValueType, InstPositiveValueType);
    }
    case ETypeKind::Option:
    {
        const COptionType& OptionType = NormalType.AsChecked<COptionType>();
        const CTypeBase* InstValueType = Substitute(*OptionType.GetValueType(), Polarity, InstTypeVariables);
        return OptionType.GetValueType() == InstValueType
            ? &OptionType
            : &Program.GetOrCreateOptionType(InstValueType);
    }
    case ETypeKind::Type:
    {
        const CTypeType& TypeType = NormalType.AsChecked<CTypeType>();
        const CTypeBase* NegativeType = TypeType.NegativeType();
        const CTypeBase* PositiveType = TypeType.PositiveType();
        const CTypeBase* InstNegativeType = Substitute(*NegativeType, FlipPolarity(Polarity), InstTypeVariables);
        const CTypeBase* InstPositiveType = Substitute(*PositiveType, Polarity, InstTypeVariables);
        return NegativeType == InstNegativeType && PositiveType == InstPositiveType
            ? &TypeType
            : &Program.GetOrCreateTypeType(InstNegativeType, InstPositiveType);
    }
    case ETypeKind::Class:
        return &Program.CreateInstantiatedClass(
            NormalType.AsChecked<CClass>(),
            Polarity,
            InstTypeVariables);
    case ETypeKind::Interface:
        return &Program.CreateInstantiatedInterface(
            NormalType.AsChecked<CInterface>(),
            Polarity,
            InstTypeVariables);
    case ETypeKind::Tuple:
    {
        const CTupleType& TupleType = NormalType.AsChecked<CTupleType>();
        CTupleType::ElementArray InstantiatedElements;
        bool bInstantiated = false;
        for (const CTypeBase* Element : TupleType.GetElements())
        {
            const CTypeBase* InstElement = Substitute(*Element, Polarity, InstTypeVariables);
            InstantiatedElements.Add(InstElement);
            bInstantiated |= Element != InstElement;
        }
        return !bInstantiated
            ? &TupleType
            : &Program.GetOrCreateTupleType(Move(InstantiatedElements), TupleType.GetFirstNamedIndex());
    }
    case ETypeKind::Function:
    {
        const CFunctionType& FunctionType = NormalType.AsChecked<CFunctionType>();
        const CTypeBase* ParamsType = &FunctionType.GetParamsType();
        const CTypeBase* ReturnType = &FunctionType.GetReturnType();
        const CTypeBase* InstParamsType = Substitute(*ParamsType, FlipPolarity(Polarity), InstTypeVariables);
        const CTypeBase* InstReturnType = Substitute(*ReturnType, Polarity, InstTypeVariables);
        // Note, the type variables' types may need to be instantiated if an
        // inner function type's type variables' types refer to an outer
        // function's now-instantiated type variables.  For example,
        // assuming `where` nests when inside a function type,
        // `type{_(:t, F(:u where u:subtype(t)):u where t:type)}`.
        // However, this requires higher rank types, which are currently
        // unimplemented.
        return ParamsType == InstParamsType && ReturnType == InstReturnType
            ? &FunctionType
            : &Program.GetOrCreateFunctionType(
                *InstParamsType,
                *InstReturnType,
                FunctionType.GetEffects(),
                FunctionType.GetTypeVariables(),
                FunctionType.ImplicitlySpecialized());
    }
    case ETypeKind::Variable:
    {
        const CTypeVariable* TypeVariable = &NormalType.AsChecked<CTypeVariable>();
        if (auto Last = InstTypeVariables.end(), I = FindIf(InstTypeVariables.begin(), Last, [=](const STypeVariableSubstitution& Arg) { return Arg._TypeVariable == TypeVariable; }); I != Last)
        {
            switch (Polarity)
            {
            case ETypePolarity::Negative: return I->_NegativeType;
            case ETypePolarity::Positive: return I->_PositiveType;
            default: ULANG_UNREACHABLE();
            }
        }
        return &NormalType;
    }
    case ETypeKind::Named:
    {
        const CNamedType& NamedType = NormalType.AsChecked<CNamedType>();
        const CTypeBase* InstValueType = Substitute(*NamedType.GetValueType(), Polarity, InstTypeVariables);
        return NamedType.GetValueType() == InstValueType
            ? &NamedType
            : &Program.GetOrCreateNamedType(
                NamedType.GetName(),
                InstValueType,
                NamedType.HasValue());
    }
    case ETypeKind::Unknown:
    case ETypeKind::False:
    case ETypeKind::True:
    case ETypeKind::Void:
    case ETypeKind::Any:
    case ETypeKind::Comparable:
    case ETypeKind::Persistable:
    case ETypeKind::Logic:
    case ETypeKind::Int:
    case ETypeKind::Rational:
    case ETypeKind::Float:
    case ETypeKind::Char8:
    case ETypeKind::Char32:
    case ETypeKind::Path:
    case ETypeKind::Range:
    case ETypeKind::Module:
    case ETypeKind::Enumeration:
    default:
        return &NormalType;
    }
}

static TArray<STypeVariableSubstitution> Compose(TArray<STypeVariableSubstitution> First, TArray<STypeVariableSubstitution> Second)
{
    TArray<STypeVariableSubstitution> Result;
    for (const STypeVariableSubstitution& Substitution : First)
    {
        const CTypeBase* NegativeType = SemanticTypeUtils::Substitute(*Substitution._NegativeType, ETypePolarity::Negative, Second);
        const CTypeBase* PositiveType = SemanticTypeUtils::Substitute(*Substitution._PositiveType, ETypePolarity::Positive, Second);
        Result.Emplace(Substitution._TypeVariable, NegativeType, PositiveType);
    }
    return Result;
}

// See `CTypeVariable` and `AnalyzeParam` for an explanation of why this
// substitution is necessary.
static TArray<STypeVariableSubstitution> ExplicitTypeVariableSubsitutions(const TArray<const CTypeVariable*> TypeVariables)
{
    TArray<STypeVariableSubstitution> Result;
    Result.Reserve(TypeVariables.Num());
    for (const CTypeVariable* TypeVariable : TypeVariables)
    {
        const CTypeVariable* NegativeTypeVariable;
        const CTypeVariable* PositiveTypeVariable;
        if (TypeVariable->_ExplicitParam)
        {
            if (TypeVariable->_NegativeTypeVariable)
            {
                NegativeTypeVariable = TypeVariable->_NegativeTypeVariable;
            }
            else
            {
                NegativeTypeVariable = TypeVariable->_ExplicitParam->_ImplicitParam;
            }
        }
        else
        {
            NegativeTypeVariable = TypeVariable;
        }
        PositiveTypeVariable = TypeVariable;
        Result.Emplace(TypeVariable, NegativeTypeVariable, PositiveTypeVariable);
    }
    return Result;
}

static TArray<STypeVariableSubstitution> FlowTypeVariableSubsitutions(const TArray<const CTypeVariable*> TypeVariables)
{
    TArray<STypeVariableSubstitution> Result;
    Result.Reserve(TypeVariables.Num());
    for (const CTypeVariable* TypeVariable : TypeVariables)
    {
        CSemanticProgram& Program = TypeVariable->GetProgram();
        CFlowType& NegativeFlowType = Program.CreateNegativeFlowType();
        CFlowType& PositiveFlowType = Program.CreatePositiveFlowType();
        NegativeFlowType.AddFlowEdge(&PositiveFlowType);
        PositiveFlowType.AddFlowEdge(&NegativeFlowType);
        Result.Emplace(TypeVariable, &NegativeFlowType, &PositiveFlowType);
    }
    for (auto [TypeVariable, NegativeType, PositiveType] : Result)
    {
        auto NegativeFlowType = NegativeType->AsFlowType();
        ULANG_ASSERT(NegativeFlowType);
        auto PositiveFlowType = PositiveType->AsFlowType();
        ULANG_ASSERT(PositiveFlowType);

        const CTypeType* NegativeTypeType = TypeVariable->_NegativeType->GetNormalType().AsNullable<CTypeType>();
        if (!NegativeTypeType)
        {
            continue;
        }

        const CTypeBase* InstNegativeType = SemanticTypeUtils::Substitute(
            *NegativeTypeType->PositiveType(),
            ETypePolarity::Negative,
            Result);
        if (const CFlowType* InstNegativeFlowType = InstNegativeType->AsFlowType())
        {
            // Maintain invariant that a `CFlowType`'s child is not a `CFlowType`.
            Merge(*NegativeFlowType, *InstNegativeFlowType, ETypePolarity::Negative);
        }
        else
        {
            NegativeFlowType->SetChild(InstNegativeType);
        }

        const CTypeBase* InstPositiveType = SemanticTypeUtils::Substitute(
            *NegativeTypeType->NegativeType(),
            ETypePolarity::Positive,
            Result);
        if (const CFlowType* InstPositiveFlowType = InstPositiveType->AsFlowType())
        {
            // Maintain invariant that a `CFlowType`'s child is not a `CFlowType`.
            Merge(*PositiveFlowType, *InstPositiveFlowType, ETypePolarity::Positive);
        }
        else
        {
            PositiveFlowType->SetChild(InstPositiveType);
        }
    }
    return Result;
}

TArray<STypeVariableSubstitution> SemanticTypeUtils::Instantiate(const TArray<const CTypeVariable*>& TypeVariables)
{
    return Compose(ExplicitTypeVariableSubsitutions(TypeVariables), FlowTypeVariableSubsitutions(TypeVariables));
}

const CFunctionType* SemanticTypeUtils::Instantiate(const CFunctionType* FunctionType)
{
    if (!FunctionType)
    {
        return nullptr;
    }
    const TArray<const CTypeVariable*>& TypeVariables = FunctionType->GetTypeVariables();
    if (TypeVariables.IsEmpty())
    {
        return FunctionType;
    }
    const CTypeBase* ParamsType = &FunctionType->GetParamsType();
    const CTypeBase* ReturnType = &FunctionType->GetReturnType();
    TArray<STypeVariableSubstitution> InstTypeVariables = Instantiate(FunctionType->GetTypeVariables());
    const CTypeBase* InstParamsType = Substitute(*ParamsType, ETypePolarity::Negative, InstTypeVariables);
    const CTypeBase* InstReturnType = Substitute(*ReturnType, ETypePolarity::Positive, InstTypeVariables);
    return ParamsType == InstParamsType && ReturnType == InstReturnType
        ? FunctionType
        : &FunctionType->GetProgram().GetOrCreateFunctionType(
            *InstParamsType,
            *InstReturnType,
            FunctionType->GetEffects(),
            {},
            FunctionType->ImplicitlySpecialized());
}

namespace {
    struct SInvariantType
    {
        const CTypeBase* _NegativeType;
        const CTypeBase* _PositiveType;
    };

    template <typename Function>
    TOptional<SInvariantType> TransformInvariant(const CTypeBase* NegativeType, const CTypeBase* PositiveType, Function F)
    {
        bool bChanged = false;
        if (const CTypeBase* NewNegativeType = uLang::Invoke(F, *NegativeType))
        {
            NegativeType = NewNegativeType;
            bChanged = true;
        }
        if (const CTypeBase* NewPositiveType = uLang::Invoke(F, *PositiveType))
        {
            PositiveType = NewPositiveType;
            bChanged = true;
        }
        if (!bChanged)
        {
            return {};
        }
        return {{NegativeType, PositiveType}};
    }

    template <typename Function>
    const CTupleType* TransformTuple(const CTupleType& Type, Function F)
    {
        CTupleType::ElementArray Elements = Type.GetElements();
        bool bChanged = false;
        for (const CTypeBase*& Element : Elements)
        {
            if (const CTypeBase* NewElement = uLang::Invoke(F, *Element))
            {
                Element = NewElement;
                bChanged = true;
            }
        }
        if (!bChanged)
        {
            return nullptr;
        }
        return &Type.GetProgram().GetOrCreateTupleType(Move(Elements), Type.GetFirstNamedIndex());
    }

    template <typename Function>
    const CFunctionType* TransformFunction(const CFunctionType& Type, Function F)
    {
        bool bChanged = false;
        const CTypeBase* ParamsType = &Type.GetParamsType();
        if (const CTypeBase* NewParamsType = uLang::Invoke(F, *ParamsType))
        {
            ParamsType = NewParamsType;
            bChanged = true;
        }
        const CTypeBase* ReturnType = &Type.GetReturnType();
        if (const CTypeBase* NewReturnType = uLang::Invoke(F, *ReturnType))
        {
            ReturnType = NewReturnType;
            bChanged = true;
        }
        if (!bChanged)
        {
            return nullptr;
        }
        return &Type.GetProgram().GetOrCreateFunctionType(
            *ParamsType,
            *ReturnType,
            Type.GetEffects(),
            Type.GetTypeVariables(),
            Type.ImplicitlySpecialized());
    }

    template <typename Function>
    const CTypeBase* Transform(const CTypeBase&, Function);

    template <typename Function>
    const CTypeBase* TransformMapType(const CMapType& MapType, Function F)
    {
        bool bChanged = false;
        const CTypeBase* KeyType = MapType.GetKeyType();
        if (const CTypeBase* NewKeyType = uLang::Invoke(F, *KeyType))
        {
            KeyType = NewKeyType;
            bChanged = true;
        }
        const CTypeBase* ValueType = MapType.GetValueType();
        if (const CTypeBase* NewValueType = uLang::Invoke(F, *ValueType))
        {
            ValueType = NewValueType;
            bChanged = true;
        }
        if (!bChanged)
        {
            return nullptr;
        }
        return &MapType.GetProgram().GetOrCreateMapType(*KeyType, *ValueType, MapType.IsWeak());
    }

    template <typename Function>
    const CTypeBase* Transform(const CNormalType& Type, Function F)
    {
        switch (Type.GetKind())
        {
        case ETypeKind::Array:
        {
            const CArrayType& ArrayType = Type.AsChecked<CArrayType>();
            const CTypeBase* NewType = uLang::Invoke(F, *ArrayType.GetElementType());
            if (!NewType)
            {
                return nullptr;
            }
            return &ArrayType.GetProgram().GetOrCreateArrayType(NewType);
        }
        case ETypeKind::Generator:
        {
            const CGeneratorType& GeneratorType = Type.AsChecked<CGeneratorType>();
            const CTypeBase* NewType = uLang::Invoke(F, *GeneratorType.GetElementType());
            if (!NewType)
            {
                return nullptr;
            }
            return &GeneratorType.GetProgram().GetOrCreateGeneratorType(NewType);
        }
        case ETypeKind::Map:
            return TransformMapType(Type.AsChecked<CMapType>(), F);
        case ETypeKind::Pointer:
        {
            const CPointerType& PointerType = Type.AsChecked<CPointerType>();
            TOptional<SInvariantType> Result = TransformInvariant(
                PointerType.NegativeValueType(),
                PointerType.PositiveValueType(),
                F);
            if (!Result)
            {
                return nullptr;
            }
            return &PointerType.GetProgram().GetOrCreatePointerType(Result->_NegativeType, Result->_PositiveType);
        }
        case ETypeKind::Reference:
        {
            const CReferenceType& ReferenceType = Type.AsChecked<CReferenceType>();
            TOptional<SInvariantType> Result = TransformInvariant(
                ReferenceType.NegativeValueType(),
                ReferenceType.PositiveValueType(),
                F);
            if (!Result)
            {
                return nullptr;
            }
            return &ReferenceType.GetProgram().GetOrCreateReferenceType(Result->_NegativeType, Result->_PositiveType);
        }
        case ETypeKind::Option:
        {
            const COptionType& OptionType = Type.AsChecked<COptionType>();
            const CTypeBase* NewValueType = uLang::Invoke(F, *OptionType.GetValueType());
            if (!NewValueType)
            {
                return nullptr;
            }
            return &OptionType.GetProgram().GetOrCreateOptionType(NewValueType);
        }
        case ETypeKind::Type:
        {
            const CTypeType& TypeType = Type.AsChecked<CTypeType>();
            TOptional<SInvariantType> Result = TransformInvariant(
                TypeType.NegativeType(),
                TypeType.PositiveType(),
                F);
            if (!Result)
            {
                return nullptr;
            }
            return &TypeType.GetProgram().GetOrCreateTypeType(Result->_NegativeType, Result->_PositiveType);
        }
        case ETypeKind::Tuple:
            return TransformTuple(Type.AsChecked<CTupleType>(), F);
        case ETypeKind::Function:
            return TransformFunction(Type.AsChecked<CFunctionType>(), F);
        case ETypeKind::Named:
        {
            const CNamedType& NamedType = Type.AsChecked<CNamedType>();
            const CTypeBase* NewValueType = uLang::Invoke(F, *NamedType.GetValueType());
            if (!NewValueType)
            {
                return nullptr;
            }
            return &NamedType.GetProgram().GetOrCreateNamedType(
                NamedType.GetName(),
                NewValueType,
                NamedType.HasValue());
        }
        case ETypeKind::Comparable:
        case ETypeKind::Persistable:
        case ETypeKind::Class:
        case ETypeKind::Interface:
        case ETypeKind::Unknown:
        case ETypeKind::False:
        case ETypeKind::True:
        case ETypeKind::Void:
        case ETypeKind::Any:
        case ETypeKind::Logic:
        case ETypeKind::Int:
        case ETypeKind::Rational:
        case ETypeKind::Float:
        case ETypeKind::Char8:
        case ETypeKind::Char32:
        case ETypeKind::Path:
        case ETypeKind::Range:
        case ETypeKind::Module:
        case ETypeKind::Enumeration:
        case ETypeKind::Variable:
        default:
            return nullptr;
        }
    }

    template <typename Function>
    const CTypeBase* Transform(const CTypeBase& Type, Function F)
    {
        return Transform(Type.GetNormalType(), Move(F));
    }

    const CTypeBase* CanonicalizeImpl(const CTypeBase&);

    const CFunctionType* CanonicalizeFunctionImpl(const CFunctionType& Type)
    {
        bool bChanged = false;
        const CTypeBase* ParamsType = &Type.GetParamsType();
        if (const CTypeBase* NewParamsType = CanonicalizeImpl(*ParamsType))
        {
            ParamsType = NewParamsType;
            bChanged = true;
        }
        const CTypeBase* ReturnType = &Type.GetReturnType();
        if (const CTypeBase* NewReturnType = CanonicalizeImpl(*ReturnType))
        {
            ReturnType = NewReturnType;
            bChanged = true;
        }
        if (!Type.GetTypeVariables().IsEmpty())
        {
            bChanged = true;
        }
        if (!bChanged)
        {
            return nullptr;
        }
        return &Type.GetProgram().GetOrCreateFunctionType(
            *ParamsType,
            *ReturnType,
            Type.GetEffects(),
            {},
            Type.ImplicitlySpecialized());
    }

    const CTypeBase* CanonicalizeImpl(const CTypeBase& Type)
    {
        if (const CFlowType* FlowType = Type.AsFlowType())
        {
            return &SemanticTypeUtils::Canonicalize(*FlowType->GetChild());
        }
        if (const CAliasType* AliasType = Type.AsAliasType())
        {
            const CTypeBase& AliasedType = *AliasType->GetAliasedType();
            const CTypeBase* CanonicalizedAliasedType = CanonicalizeImpl(AliasedType);
            return CanonicalizedAliasedType ? CanonicalizedAliasedType : &AliasedType;
        }
        const CNormalType& NormalType = Type.GetNormalType();
        if (const CFunctionType* FunctionType = NormalType.AsNullable<CFunctionType>())
        {
            return CanonicalizeFunctionImpl(*FunctionType);
        }
        if (const CTypeVariable* TypeVariable = NormalType.AsNullable<CTypeVariable>())
        {
            // Canonicalize a type variable by rewriting to the upper bound (the
            // lower bound will currently always be `false`).  This ensures
            // multiple uses of different type variables that are represented as
            // the same type (`any` or some other upper bound) have the same
            // representation.  Additionally, this ensures multiple type variables
            // with the same name and bound (and thus the same mangled name) do
            // not collide when generating `UStruct`s for tuples containing such
            // type variables.
            if (const CTypeType* TypeType = TypeVariable->GetType()->GetNormalType().AsNullable<CTypeType>())
            {
                return &SemanticTypeUtils::Canonicalize(*TypeType->PositiveType());
            }
            return &TypeVariable->GetProgram()._anyType;
        }
        if (NormalType.GetKind() == ETypeKind::Comparable)
        {
            return &NormalType.GetProgram()._anyType;
        }
        if (const CClass* Class = NormalType.AsNullable<CClass>())
        {
            if (Class->_GeneralizedClass == Class)
            {
                return nullptr;
            }
            return Class->_GeneralizedClass;
        }
        if (const CInterface* Interface = NormalType.AsNullable<CInterface>())
        {
            if (Interface->_GeneralizedInterface == Interface)
            {
                return nullptr;
            }
            return Interface->_GeneralizedInterface;
        }
        if (const CMapType* MapType = NormalType.AsNullable<CMapType>())
        {
            return &MapType->GetProgram().GetOrCreateMapType(
                SemanticTypeUtils::Canonicalize(*MapType->GetKeyType()),
                SemanticTypeUtils::Canonicalize(*MapType->GetValueType()),
                false);
        }
        return Transform(NormalType, CanonicalizeImpl);
    }
}

const CTypeBase& SemanticTypeUtils::Canonicalize(const CTypeBase& Type)
{
    const CTypeBase* NewType = CanonicalizeImpl(Type);
    return NewType? *NewType : Type;
}

const CTupleType& SemanticTypeUtils::Canonicalize(const CTupleType& Type)
{
    const CTupleType* NewType = TransformTuple(Type, CanonicalizeImpl);
    return NewType? *NewType : Type;
}

const CFunctionType& SemanticTypeUtils::Canonicalize(const CFunctionType& Type)
{
    const CFunctionType* NewType = CanonicalizeFunctionImpl(Type);
    return NewType? *NewType : Type;
}

namespace {
    const CClass* AsPolarityClassImpl(const CClass& Class, ETypePolarity DesiredPolarity)
    {
        switch (DesiredPolarity)
        {
        case ETypePolarity::Positive:
            if (Class._OwnedNegativeClass)
            {
                return nullptr;
            }
            return Class._NegativeClass;
        case ETypePolarity::Negative:
            if (Class._OwnedNegativeClass)
            {
                return Class._NegativeClass;
            }
            return nullptr;
        default:
            ULANG_UNREACHABLE();
        }
    }

    const CClass& AsPositiveClass(const CClass& Class)
    {
        const CClass* NewClass = AsPolarityClassImpl(Class, ETypePolarity::Positive);
        return NewClass? *NewClass : Class;
    }

    const CInterface* AsPolarityInterfaceImpl(const CInterface& Interface, ETypePolarity DesiredPolarity)
    {
        switch (DesiredPolarity)
        {
        case ETypePolarity::Positive:
            if (Interface._OwnedNegativeInterface)
            {
                return nullptr;
            }
            return Interface._NegativeInterface;
        case ETypePolarity::Negative:
            if (Interface._OwnedNegativeInterface)
            {
                return Interface._OwnedNegativeInterface;
            }
            return nullptr;
        default:
            ULANG_UNREACHABLE();
        }
    }

    const CInterface& AsPositiveInterface(const CInterface& Interface)
    {
        const CInterface* NewInterface = AsPolarityInterfaceImpl(Interface, ETypePolarity::Positive);
        return NewInterface? *NewInterface : Interface;
    }

    const CTypeBase* AsPolarityImpl(const CTypeBase& Type, const TArray<SInstantiatedTypeVariable>& Substitutions, ETypePolarity DesiredPolarity)
    {
        if (const CFlowType* FlowType = Type.AsFlowType())
        {
            for (auto [NegativeFlowType, PositiveFlowType] : Substitutions)
            {
                if (DesiredPolarity == ETypePolarity::Positive && FlowType == NegativeFlowType)
                {
                    return PositiveFlowType;
                }
                else if (DesiredPolarity == ETypePolarity::Negative && FlowType == PositiveFlowType)
                {
                    return NegativeFlowType;
                }
            }
        }
        const CNormalType& NormalType = Type.GetNormalType();
        if (const CClass* Class = NormalType.AsNullable<CClass>())
        {
            return AsPolarityClassImpl(*Class, DesiredPolarity);
        }
        if (const CInterface* Interface = NormalType.AsNullable<CInterface>())
        {
           return AsPolarityInterfaceImpl(*Interface, DesiredPolarity);
        }
        return Transform(Type, [&](const CTypeBase& ChildType)
        {
            return AsPolarityImpl(ChildType, Substitutions, DesiredPolarity);
        });
    }
}

const CTypeBase& SemanticTypeUtils::AsPolarity(const CTypeBase& Type, const TArray<SInstantiatedTypeVariable>& Substitutions, ETypePolarity DesiredPolarity)
{
    if (const CTypeBase* NewType = AsPolarityImpl(Type, Substitutions, DesiredPolarity))
    {
        return *NewType;
    }
    return Type;
}

const CTypeBase& SemanticTypeUtils::AsPositive(const CTypeBase& Type, const TArray<SInstantiatedTypeVariable>& Substitutions)
{
    return AsPolarity(Type, Substitutions, ETypePolarity::Positive);
}

const CTypeBase& SemanticTypeUtils::AsNegative(const CTypeBase& Type, const TArray<SInstantiatedTypeVariable>& Substitutions)
{
    return AsPolarity(Type, Substitutions, ETypePolarity::Negative);
}

namespace {
    using TInterfaceSet = TArrayG<const CInterface*, TInlineElementAllocator<8>>;

    // Utility functions for collecting all interfaces implemented by a class or interface (including the interface itself).
    // A set might be a better type for FoundInterfaces (but probably not if they are small).
    void CollectAllInterfaces(TInterfaceSet& FoundInterfaces, const CInterface* Interface)
    {
        if (!FoundInterfaces.Contains(Interface))
        {
            FoundInterfaces.Add(Interface);
            const TArray<CInterface*>& SuperInterfaces = Interface->_SuperInterfaces;
            for (const CInterface* SuperInterface : SuperInterfaces)
            {
                CollectAllInterfaces(FoundInterfaces, SuperInterface);
            }
        }
    }

    void CollectAllInterfaces(TInterfaceSet& FoundInterfaces, const CClass* Class, VisitStampType VisitStamp)
    {
        for (const CClass* SuperClass = Class;
            SuperClass;
            SuperClass = SuperClass->_Superclass)
        {
            if (!SuperClass->TryMarkVisited(VisitStamp))
            {
                break;
            }
            const TArray<CInterface*>& SuperInterfaces = SuperClass->_SuperInterfaces;
            for (const CInterface* SuperInterface : SuperInterfaces)
            {
                CollectAllInterfaces(FoundInterfaces, SuperInterface);
            }
        }
    }

    void CollectAllInterfaces(TInterfaceSet& FoundInterfaces, const CClass* Class)
    {
        CollectAllInterfaces(FoundInterfaces, Class, CScope::GenerateNewVisitStamp());
    }

    TArray<STypeVariableSubstitution> JoinTypeVariableSubstitutions(
        const TArray<STypeVariableSubstitution>& TypeVariables,
        const TArray<STypeVariableSubstitution>& InstantiatedTypeVariables1,
        const TArray<STypeVariableSubstitution>& InstantiatedTypeVariables2)
    {
        TArray<STypeVariableSubstitution> TypeVariableSubstitutions;
        using NumType = decltype(TypeVariables.Num());
        NumType NumInstantiatedTypeVariables = TypeVariables.Num();
        ULANG_ASSERT(NumInstantiatedTypeVariables == InstantiatedTypeVariables1.Num());
        ULANG_ASSERT(NumInstantiatedTypeVariables == InstantiatedTypeVariables2.Num());
        for (NumType J = 0; J != NumInstantiatedTypeVariables; ++J)
        {
            TypeVariableSubstitutions.Emplace(
                TypeVariables[J]._TypeVariable,
                SemanticTypeUtils::Meet(
                    InstantiatedTypeVariables1[J]._NegativeType,
                    InstantiatedTypeVariables2[J]._NegativeType),
                SemanticTypeUtils::Join(
                    InstantiatedTypeVariables1[J]._PositiveType,
                    InstantiatedTypeVariables2[J]._PositiveType));
        }
        return TypeVariableSubstitutions;
    }

    // Utility function that takes two containers with interfaces and returns container with the interfaces that are common to both.
    // If a interface is included in the result, then none of its super_interfaces are.
    TInterfaceSet FindCommonInterfaces(const TInterfaceSet& LhsInterfaces, const TInterfaceSet& RhsInterfaces)
    {
        TInterfaceSet CommonInterfaces;
        for (const CInterface* LhsInterface : LhsInterfaces)
        {
            const CInterface* GeneralizedInterface = LhsInterface->_GeneralizedInterface;
            for (const CInterface* RhsInterface : RhsInterfaces)
            {
                if (GeneralizedInterface != RhsInterface->_GeneralizedInterface)
                {
                    continue;
                }
                TArray<STypeVariableSubstitution> TypeVariableSubstitutions = JoinTypeVariableSubstitutions(
                    GeneralizedInterface->_TypeVariableSubstitutions,
                    LhsInterface->_TypeVariableSubstitutions,
                    RhsInterface->_TypeVariableSubstitutions);
                const CInterface* Interface;
                if (auto InstantiatedInterface = InstantiateInterface(*GeneralizedInterface, ETypePolarity::Positive, TypeVariableSubstitutions))
                {
                    Interface = InstantiatedInterface;
                }
                else
                {
                    Interface = GeneralizedInterface;
                }
                if (CommonInterfaces.ContainsByPredicate([Interface](const CInterface* CommonInterface) { return SemanticTypeUtils::IsSubtype(CommonInterface, Interface); }))
                {
                    continue;
                }
                // Need to add, but first remove things implemented by the new interface
                using NumType = decltype(CommonInterfaces.Num());
                for (NumType I = 0, Last = CommonInterfaces.Num(); I != Last; ++I)
                {
                    if (SemanticTypeUtils::IsSubtype(Interface, CommonInterfaces[I]))
                    {
                        CommonInterfaces.RemoveAtSwap(I);
                        --I;
                    }
                }
                CommonInterfaces.Add(Interface);
            }
        }
        return CommonInterfaces;
    }

    // A simple, O(n^2) check that two arrays contain the same elements in any order, assuming that each array contains a distinct element at most once.
    template<typename ElementType, typename AllocatorType>
    bool ArraysHaveSameElementsInAnyOrder(const TArrayG<ElementType, AllocatorType>& A, const TArrayG<ElementType, AllocatorType>& B)
    {
        if (A.Num() != B.Num())
        {
            return false;
        }

        for (const ElementType& Element : A)
        {
            if (!B.Contains(Element))
            {
                return false;
            }
        }

        return true;
    }

    /// Compute the join of a Interface and a Interface/Class: the "least" unique interface that is implemented by both the Interface and the Interface/Class.
    /// Return AnyType if no suitable unique interface is found.
    template<typename TClassOrInterface>
    const CTypeBase* JoinInterfaces(const CInterface* Interface, const TClassOrInterface* ClassOrInterface)
    {
        TInterfaceSet Interfaces1;
        CollectAllInterfaces(Interfaces1, Interface);
        TInterfaceSet Interfaces2;
        CollectAllInterfaces(Interfaces2, ClassOrInterface);
        TInterfaceSet Common = FindCommonInterfaces(Interfaces1, Interfaces2);
        if (1 == Common.Num())
        {
            return Common[0];
        }
        else
        {   // No common interface or more than one distinct common interfaces
            return &Interface->CTypeBase::GetProgram()._anyType;
        }
    }

    template <typename Function>
    bool MatchDataDefinition(const CDataDefinition& DataDefinition1, const CDataDefinition& DataDefinition2, Function F)
    {
        const CTypeBase* Type1 = DataDefinition1.GetType();
        if (!Type1)
        {
            return true;
        }
        const CTypeBase* Type2 = DataDefinition2.GetType();
        if (!Type2)
        {
            return true;
        }
        return uLang::Invoke(F, Type1, Type2);
    }

    template <typename Function>
    bool MatchFunction(const CFunction& Function1, const CFunction& Function2, Function F)
    {
        const CFunctionType* FunctionType1 = Function1._Signature.GetFunctionType();
        if (!FunctionType1)
        {
            return true;
        }
        const CFunctionType* FunctionType2 = Function2._Signature.GetFunctionType();
        if (!FunctionType2)
        {
            return true;
        }
        return uLang::Invoke(F, Function1._Signature.GetFunctionType(), Function2._Signature.GetFunctionType());
    }

    template <typename Function>
    bool MatchClassClass(const CClass& Class1, const CClass& Class2, Function F)
    {
        if (Class1._GeneralizedClass != Class2._GeneralizedClass)
        {
            if (const CClass* Superclass = Class1._Superclass)
            {
                return uLang::Invoke(F, Superclass, &Class2);
            }
            return false;
        }
        if (&AsPositiveClass(Class1) == &AsPositiveClass(Class2))
        {
            return true;
        }
        int32_t NumDefinitions = Class1.GetDefinitions().Num();
        ULANG_ASSERTF(
            NumDefinitions == Class2.GetDefinitions().Num(),
            "Classes with same definition should have the same number of members");
        for (int32_t DefinitionIndex = 0; DefinitionIndex != NumDefinitions; ++DefinitionIndex)
        {
            const CDefinition* Definition1 = Class1.GetDefinitions()[DefinitionIndex];
            const CDefinition* Definition2 = Class2.GetDefinitions()[DefinitionIndex];
            const CDefinition::EKind DefinitionKind = Definition1->GetKind();
            ULANG_ASSERTF(DefinitionKind == Definition2->GetKind(), "Expected instantiated class members to have the same kind.");
            // The definition types may be `nullptr` if there was an earlier error.
            if (DefinitionKind == CDefinition::EKind::Data)
            {
                const CDataDefinition& DataMember1 = Definition1->AsChecked<CDataDefinition>();
                const CDataDefinition& DataMember2 = Definition2->AsChecked<CDataDefinition>();
                if (!MatchDataDefinition(DataMember1, DataMember2, F))
                {
                    return false;
                }
            }
            else if (DefinitionKind == CDefinition::EKind::Function)
            {
                const CFunction& Function1 = Definition1->AsChecked<CFunction>();
                const CFunction& Function2 = Definition2->AsChecked<CFunction>();
                if (!MatchFunction(Function1, Function2, F))
                {
                    return false;
                }
            }
            else
            {
                ULANG_ERRORF("Did not expect class to contain definitions other than methods and data, but found %s '%s'.",
                    DefinitionKindAsCString(Definition1->GetKind()),
                    Definition1->AsNameCString());
                return false;
            }
        }
        return true;
    }

    template <typename Function>
    bool MatchInterfaceInterface(const CInterface& Interface1, const CInterface& Interface2, Function F)
    {
        if (Interface1._GeneralizedInterface != Interface2._GeneralizedInterface)
        {
            for (const CInterface* SuperInterface : Interface1._SuperInterfaces)
            {
                if (uLang::Invoke(F, SuperInterface, &Interface2))
                {
                    return true;
                }
            }
            return false;
        }
        if (&AsPositiveInterface(Interface1) == &AsPositiveInterface(Interface2))
        {
            return true;
        }
        int32_t NumDefinitions = Interface1.GetDefinitions().Num();
        ULANG_ASSERTF(
            NumDefinitions == Interface2.GetDefinitions().Num(),
            "Interfaces with same definition should have the same number of members");
        for (int32_t DefinitionIndex = 0; DefinitionIndex != NumDefinitions; ++DefinitionIndex)
        {
            const CDefinition* Definition1 = Interface1.GetDefinitions()[DefinitionIndex];
            const CDefinition* Definition2 = Interface2.GetDefinitions()[DefinitionIndex];
            const CDefinition::EKind DefinitionKind = Definition1->GetKind();
            ULANG_ASSERTF(DefinitionKind == Definition2->GetKind(), "Expected instantiated class members to have the same kind.");
            // The definition types may be `nullptr` if there was an earlier error.
            if (DefinitionKind == CDefinition::EKind::Function)
            {
                const CFunction& Function1 = Definition1->AsChecked<CFunction>();
                const CFunction& Function2 = Definition2->AsChecked<CFunction>();
                if (!MatchFunction(Function1, Function2, F))
                {
                    return false;
                }
            }
            else
            {
                ULANG_ERRORF("Did not expect interface to contain definitions other than methods, but found %s '%s'.",
                    DefinitionKindAsCString(Definition1->GetKind()),
                    Definition1->AsNameCString());
                return false;
            }
        }
        return true;
    }

    template <typename Function>
    bool MatchClassInterface(const CClass& Class1, const CInterface& Interface2, Function F)
    {
        for (const CClass* I = &Class1; I; I = I->_Superclass)
        {
            for (const CInterface* SuperInterface : I->_SuperInterfaces)
            {
                if (uLang::Invoke(F, SuperInterface, &Interface2))
                {
                    return true;
                }
            }
        }
        return false;
    }

    template <typename Function>
    bool MatchNamed(const CNamedType& Type1, const CNamedType& Type2, Function F)
    {
        if (Type1.GetName() != Type2.GetName())
        {
            return false;
        }
        if (!uLang::Invoke(F, Type1.GetValueType(), Type2.GetValueType()))
        {
            return false;
        }
        if (Type1.HasValue() && !Type2.HasValue())
        {
            return false;
        }
        return true;
    }

    template <typename FirstIterator1, typename LastIterator1, typename FirstIterator2, typename LastIterator2, typename Function>
    bool MatchElements(FirstIterator1 First1, LastIterator1 Last1, FirstIterator2 First2, LastIterator2 Last2, Function F)
    {
        if (Last1 - First1 != Last2 - First2)
        {
            return false;
        }
        for (; First1 != Last1; ++First1, ++First2)
        {
            if (!uLang::Invoke(F, *First1, *First2))
            {
                return false;
            }
        }
        return true;
    }

    template <typename FirstIterator1, typename LastIterator1, typename FirstIterator2, typename LastIterator2, typename Function>
    bool MatchNamedElements(FirstIterator1 First1, LastIterator1 Last1, FirstIterator2 First2, LastIterator2 Last2, Function F)
    {
        while(First1 != Last1 && First2 != Last2)
        {
            const CNamedType& NamedElementType1 = (*First1)->GetNormalType().template AsChecked<CNamedType>();
            const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
            if (NamedElementType1.GetName() < NamedElementType2.GetName())
            {
                return false;
            }
            else if (NamedElementType2.GetName() < NamedElementType1.GetName())
            {
                if (!NamedElementType2.HasValue())
                {
                    return false;
                }
                ++First2;
            }
            else
            {
                if (!uLang::Invoke(F, NamedElementType1.GetValueType(), NamedElementType2.GetValueType()))
                {
                    return false;
                }
                if (NamedElementType1.HasValue() && !NamedElementType2.HasValue())
                {
                    return false;
                }
                ++First1;
                ++First2;
            }
        }
        if (First1 != Last1)
        {
            return false;
        }
        for (; First2 != Last2; ++First2)
        {
            const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
            if (!NamedElementType2.HasValue())
            {
                return false;
            }
        }
        return true;
    }

    template <typename Range1, typename Range2, typename Function>
    bool MatchElements(Range1&& ElementTypes1, int32_t FirstNamedIndex1, Range2&& ElementTypes2, int32_t FirstNamedIndex2, Function F)
    {
        if (!MatchElements(ElementTypes1.begin(), ElementTypes1.begin() + FirstNamedIndex1, ElementTypes2.begin(), ElementTypes2.begin() + FirstNamedIndex2, F))
        {
            return false;
        }
        if (!MatchNamedElements(ElementTypes1.begin() + FirstNamedIndex1, ElementTypes1.end(), ElementTypes2.begin() + FirstNamedIndex2, ElementTypes2.end(), F))
        {
            return false;
        }
        return true;
    }

    template <typename Function>
    bool MatchElements(const CTupleType& Type1, const CTupleType& Type2, Function F)
    {
        return MatchElements(Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex(), F);
    }

    template <typename Function>
    bool MatchElements(const CTypeBase* Type1, const CTupleType& Type2, Function F)
    {
        TRangeView ElementTypes1{&Type1, &Type1 + 1};
        int32_t FirstNamedIndex1 = Type1->GetNormalType().IsA<CNamedType>()? 0 : 1;
        return MatchElements(ElementTypes1, FirstNamedIndex1, Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex(), F);
    }

    template <typename Function>
    bool MatchElements(const CTupleType& Type1, const CTypeBase* Type2, Function F)
    {
        TRangeView ElementTypes2{&Type2, &Type2 + 1};
        int32_t FirstNamedIndex2 = Type2->GetNormalType().IsA<CNamedType>() ? 0 : 1;
        return MatchElements(Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), ElementTypes2, FirstNamedIndex2, F);
    }

    template <typename Function>
    bool Match(
        const CNormalType& NormalType1,
        ETypePolarity Type1Polarity,
        const CNormalType& NormalType2,
        ETypePolarity Type2Polarity,
        Function F)
    {
        if (&NormalType1 == &NormalType2)
        {
            return true;
        }
        if (NormalType1.IsA<CUnknownType>())
        {
            return true;
        }
        if (NormalType1.IsA<CFalseType>())
        {
            return true;
        }
        if (NormalType2.IsA<CAnyType>())
        {
            return true;
        }
        // `void` in the negative position is equivalent to `any`
        if (Type1Polarity == ETypePolarity::Negative && NormalType2.IsA<CVoidType>() && NormalType2.IsA<CAnyType>())
        {
            return true;
        }
        if (Type2Polarity == ETypePolarity::Negative && NormalType2.IsA<CVoidType>())
        {
            return true;
        }
        // `void` in the positive position is equivalent to `true`
        if (Type1Polarity == ETypePolarity::Positive && NormalType1.IsA<CVoidType>() && NormalType2.IsA<CTrueType>())
        {
            return true;
        }
        if (NormalType1.IsA<CTrueType>() && Type2Polarity == ETypePolarity::Positive && NormalType2.IsA<CVoidType>())
        {
            return true;
        }
        if (NormalType2.IsA<CComparableType>() && NormalType1.GetComparability() != EComparability::Incomparable)
        {
            return true;
        }
        if (NormalType2.IsA<CPersistableType>() && NormalType1.IsPersistable())
        {
            return true;
        }
        if (const CTupleType* TupleType1 = NormalType1.AsNullable<CTupleType>(); TupleType1 && NormalType2.IsA<CArrayType>() && TupleType1->GetFirstNamedIndex() == TupleType1->Num())
        {
            const CArrayType& ArrayType2 = NormalType2.AsChecked<CArrayType>();
            const CTypeBase* ElementType2 = ArrayType2.GetElementType();
            for (const CTypeBase* ElementType1 : TupleType1->GetElements())
            {
                if (!uLang::Invoke(F, ElementType1, ElementType2))
                {
                    return false;
                }
            }
            return true;
        }
        if (const CTupleType* TupleType1 = NormalType1.AsNullable<CTupleType>())
        {
            if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
            {
                return MatchElements(*TupleType1, *TupleType2, F);
            }
            if (TupleType1->Num() == 1)
            {
                // A singleton tuple is not a subtype of a single type
                return false;
            }
            // A non-singleton tuple type containing named types with values may be a subtype of a single type
            return MatchElements(*TupleType1, &NormalType2, F);
        }
        if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
        {
            if (TupleType2->Num() == 1)
            {
                // A single type is not a subtype of a singleton tuple type
                return false;
            }
            // A single type may be a subtype of a non-singleton tuple type containing named types with values
            return MatchElements(&NormalType1, *TupleType2, F);
        }
        if (NormalType1.IsA<CClass>() && NormalType2.IsA<CInterface>())
        {
            // Classes that implement a interface are subtypes of the interface type.
            return MatchClassInterface(
                NormalType1.AsChecked<CClass>(),
                NormalType2.AsChecked<CInterface>(), F);
        }
        if (const CTypeVariable* TypeVariable1 = NormalType1.AsNullable<CTypeVariable>())
        {
            if (const CDataDefinition* ExplicitParam = TypeVariable1->_ExplicitParam)
            {
                TypeVariable1 = ExplicitParam->_ImplicitParam;
            }
            const CTypeBase* Type2 = &NormalType2;
            if (const CTypeVariable* TypeVariable2 = NormalType2.AsNullable<CTypeVariable>())
            {
                if (const CDataDefinition* ExplicitParam = TypeVariable2->_ExplicitParam)
                {
                    Type2 = ExplicitParam->_ImplicitParam;
                    if (TypeVariable1 == Type2)
                    {
                        return true;
                    }
                }
            }
            const CTypeType* TypeType1;
            if (Type1Polarity == ETypePolarity::Negative)
            {
                TypeType1 = TypeVariable1->_NegativeType->GetNormalType().AsNullable<CTypeType>();
            }
            else
            {
                TypeType1 = TypeVariable1->GetType()->GetNormalType().AsNullable<CTypeType>();
            }
            if (!TypeType1)
            {
                return false;
            }
            return uLang::Invoke(F, TypeType1->PositiveType(), Type2);
        }
        if (const CTypeVariable* TypeVariable2 = NormalType2.AsNullable<CTypeVariable>())
        {
            if (const CDataDefinition* ExplicitParam = TypeVariable2->_ExplicitParam)
            {
                TypeVariable2 = ExplicitParam->_ImplicitParam;
            }
            const CTypeType* TypeType2;
            if (Type2Polarity == ETypePolarity::Negative)
            {
                TypeType2 = TypeVariable2->GetType()->GetNormalType().AsNullable<CTypeType>();
            }
            else
            {
                TypeType2 = TypeVariable2->_NegativeType->GetNormalType().AsNullable<CTypeType>();
            }
            if (!TypeType2)
            {
                return false;
            }
            return uLang::Invoke(F, &NormalType1, TypeType2->NegativeType());
        }
        const ETypeKind Kind = NormalType1.GetKind();
        if (Kind != NormalType2.GetKind())
        {
            return false;
        }
        switch (Kind)
        {
        case ETypeKind::Module:
        case ETypeKind::Enumeration:
            // Different module and enumeration types don't have any values in common.
            return false;

        case ETypeKind::Class:
        {
            const CClass& Class1 = NormalType1.AsChecked<CClass>();
            const CClass& Class2 = NormalType2.AsChecked<CClass>();
            return MatchClassClass(Class1, Class2, F);
        }
        case ETypeKind::Interface:
        {
            const CInterface& Interface1 = NormalType1.AsChecked<CInterface>();
            const CInterface& Interface2 = NormalType2.AsChecked<CInterface>();
            return MatchInterfaceInterface(Interface1, Interface2, F);
        }

        case ETypeKind::Array:
        {
            const CArrayType& ArrayType1 = NormalType1.AsChecked<CArrayType>();
            const CArrayType& ArrayType2 = NormalType2.AsChecked<CArrayType>();
            return uLang::Invoke(F, ArrayType1.GetElementType(), ArrayType2.GetElementType());
        }
        case ETypeKind::Generator:
        {
            const CGeneratorType& GeneratorType1 = NormalType1.AsChecked<CGeneratorType>();
            const CGeneratorType& GeneratorType2 = NormalType2.AsChecked<CGeneratorType>();
            return uLang::Invoke(F, GeneratorType1.GetElementType(), GeneratorType2.GetElementType());
        }
        case ETypeKind::Map:
        {
            const CMapType& MapType1 = static_cast<const CMapType&>(NormalType1);
            const CMapType& MapType2 = static_cast<const CMapType&>(NormalType2);
            if (MapType1.IsWeak() && !MapType2.IsWeak())
            {
                return false;
            }
            if (!uLang::Invoke(F, MapType1.GetKeyType(), MapType2.GetKeyType()))
            {
                return false;
            }
            if (!uLang::Invoke(F, MapType1.GetValueType(), MapType2.GetValueType()))
            {
                return false;
            }
            return true;
        }
        case ETypeKind::Pointer:
        {
            const CPointerType& PointerType1 = NormalType1.AsChecked<CPointerType>();
            const CPointerType& PointerType2 = NormalType2.AsChecked<CPointerType>();
            if (!uLang::Invoke(F, PointerType2.NegativeValueType(), PointerType1.NegativeValueType()))
            {
                return false;
            }
            if (!uLang::Invoke(F, PointerType1.PositiveValueType(), PointerType2.PositiveValueType()))
            {
                return false;
            }
            return true;
        }
        case ETypeKind::Reference:
        {
            const CReferenceType& ReferenceType1 = NormalType1.AsChecked<CReferenceType>();
            const CReferenceType& ReferenceType2 = NormalType2.AsChecked<CReferenceType>();
            if (!uLang::Invoke(F, ReferenceType2.NegativeValueType(), ReferenceType1.NegativeValueType()))
            {
                return false;
            }
            if (!uLang::Invoke(F, ReferenceType1.PositiveValueType(), ReferenceType2.PositiveValueType()))
            {
                return false;
            }
            return true;
        }
        case ETypeKind::Option:
        {
            const COptionType& OptionType1 = NormalType1.AsChecked<COptionType>();
            const COptionType& OptionType2 = NormalType2.AsChecked<COptionType>();
            return uLang::Invoke(F, OptionType1.GetValueType(), OptionType2.GetValueType());
        }
        case ETypeKind::Type:
        {
            const CTypeType& TypeType1 = NormalType1.AsChecked<CTypeType>();
            const CTypeType& TypeType2 = NormalType2.AsChecked<CTypeType>();
            if (!uLang::Invoke(F, TypeType2.NegativeType(), TypeType1.NegativeType()))
            {
                return false;
            }
            if (!uLang::Invoke(F, TypeType1.PositiveType(), TypeType2.PositiveType()))
            {
                return false;
            }
            return true;
        }
        case ETypeKind::Function:
        {
            const CFunctionType& FunctionType1 = NormalType1.AsChecked<CFunctionType>();
            const CFunctionType& FunctionType2 = NormalType2.AsChecked<CFunctionType>();
            if (!FunctionType2.GetEffects().HasAll(FunctionType1.GetEffects()))
            {
                return false;
            }
            // Function types are co-variant in return and contra-variant in parameter.
            if (!uLang::Invoke(F, &FunctionType2.GetParamsType(), &FunctionType1.GetParamsType()))
            {
                return false;
            }
            if (!uLang::Invoke(F, &FunctionType1.GetReturnType(), &FunctionType2.GetReturnType()))
            {
                return false;
            }
            return true;
        }
        case ETypeKind::Variable:
            // Only identical generalized type variables have a subtyping relationship.
            return false;
        case ETypeKind::Named:
            return MatchNamed(NormalType1.AsChecked<CNamedType>(), NormalType2.AsChecked<CNamedType>(), F);

        case ETypeKind::Int:
        {
            const CIntType& IntType1 = NormalType1.AsChecked<CIntType>();
            const CIntType& IntType2 = NormalType2.AsChecked<CIntType>();

            if (IntType1.GetMin() < IntType2.GetMin())
            {
                return false;
            }

            if (IntType1.GetMax() > IntType2.GetMax())
            {
                return false;
            }

            return true;
        }

        case ETypeKind::Float:
        {
            const CFloatType& FloatType1 = NormalType1.AsChecked<CFloatType>();
            const CFloatType& FloatType2 = NormalType2.AsChecked<CFloatType>();

            if (FloatType1.MinRanking() < FloatType2.MinRanking())
            {
                return false;
            }

            if (FloatType1.MaxRanking() > FloatType2.MaxRanking())
            {
                return false;
            }

            return true;
        }

        // These cases should be handled by the conditions before the switch.
        case ETypeKind::Unknown:
        case ETypeKind::False:
        case ETypeKind::True:
        case ETypeKind::Void:
        case ETypeKind::Any:
        case ETypeKind::Comparable:
        case ETypeKind::Persistable:
        case ETypeKind::Logic:
        case ETypeKind::Rational:
        case ETypeKind::Char8:
        case ETypeKind::Char32:
        case ETypeKind::Path:
        case ETypeKind::Range:
        case ETypeKind::Tuple:
        default:
            ULANG_UNREACHABLE();
        };
    }

    template <typename T, typename U>
    struct TPair
    {
        T First;
        U Second;

        friend bool operator==(const TPair& Left, const TPair& Right)
        {
            return Left.First == Right.First && Left.Second == Right.Second;
        }
    };

    using SConstrainedTypes = TPair<const CTypeBase*, const CTypeBase*>;

    /// Require `Type1` to be a subtype of `Type2`
    /// @returns false if `Type1` cannot be constrained to be a subtype of `Type2`
    bool Constrain(const CTypeBase* Type1, const CTypeBase* Type2, TArrayG<SConstrainedTypes, TInlineElementAllocator<16>>& Visited);

    bool Constrain(const CTypeBase& Type1, const CTypeBase& Type2, TArrayG<SConstrainedTypes, TInlineElementAllocator<16>>& Visited)
    {
        if (Contains(Visited, SConstrainedTypes{&Type1, &Type2}))
        {
            return true;
        }
        Visited.Add({&Type1, &Type2});
        if (const CFlowType* FlowType1 = Type1.AsFlowType())
        {
            ULANG_ASSERTF(FlowType1->Polarity() == ETypePolarity::Positive, "`Type1` must be positive");
            if (const CFlowType* FlowType2 = Type2.AsFlowType())
            {
                ULANG_ASSERTF(FlowType2->Polarity() == ETypePolarity::Negative, "`Type2` must be negative");
                if (!Constrain(FlowType1->GetChild(), FlowType2->GetChild(), Visited))
                {
                    return false;
                }
                for (const CFlowType* NegativeFlowType1 : FlowType1->FlowEdges())
                {
                    MergeNegative(*NegativeFlowType1, *FlowType2);
                }
                for (const CFlowType* PositiveFlowType2 : FlowType2->FlowEdges())
                {
                    MergePositive(*PositiveFlowType2, *FlowType1);
                }
                return true;
            }
            if (!Constrain(FlowType1->GetChild(), &Type2, Visited))
            {
                return false;
            }
            for (const CFlowType* NegativeFlowType1 : FlowType1->FlowEdges())
            {
                MergeNegativeChild(*NegativeFlowType1, &Type2);
            }
            return true;
        }
        else if (const CFlowType* FlowType2 = Type2.AsFlowType())
        {
            ULANG_ASSERTF(FlowType2->Polarity() == ETypePolarity::Negative, "`Type2` must be negative");
            if (!Constrain(&Type1, FlowType2->GetChild(), Visited))
            {
                return false;
            }
            for (const CFlowType* PositiveFlowType2 : FlowType2->FlowEdges())
            {
                MergePositiveChild(*PositiveFlowType2, &Type1);
            }
            return true;
        }

        const CNormalType& NormalType1 = Type1.GetNormalType();
        const CNormalType& NormalType2 = Type2.GetNormalType();
        return Match(NormalType1, ETypePolarity::Positive, NormalType2, ETypePolarity::Negative, [&](const CTypeBase* ElementType1, const CTypeBase* ElementType2)
        {
            return Constrain(ElementType1, ElementType2, Visited);
        });
    }

    bool Constrain(const CTypeBase* Type1, const CTypeBase* Type2, TArrayG<SConstrainedTypes, TInlineElementAllocator<16>>& Visited)
    {
        ULANG_ASSERTF(Type1, "Expected non-`nullptr` `Type1`");
        ULANG_ASSERTF(Type2, "Expected non-`nullptr` `Type2`");
        return Constrain(*Type1, *Type2, Visited);
    }

    using SSubsumedTypes = TPair<const CTypeBase*, const CTypeBase*>;

    using SSubsumedFlowTypes = TPair<const CFlowType*, const CFlowType*>;

    /// @returns true if all instances of `Type1` ignoring flow types are subtypes of `Type2`
    /// @see Algebraic Subtyping, chapter 8
    bool Subsumes(
        const CTypeBase& Type1,
        const CTypeBase& Type2,
        TArrayG<SSubsumedFlowTypes, TInlineElementAllocator<16>>& NegativeFlowTypes,
        TArrayG<SSubsumedFlowTypes, TInlineElementAllocator<16>>& PositiveFlowTypes,
        TArrayG<SSubsumedTypes, TInlineElementAllocator<16>>& Visited)
    {
        if (Contains(Visited, SSubsumedTypes{&Type1, &Type2}))
        {
            return true;
        }
        Visited.Add({&Type1, &Type2});
        if (const CFlowType* FlowType1 = Type1.AsFlowType())
        {
            if (const CFlowType* FlowType2 = Type2.AsFlowType())
            {
                switch (FlowType1->Polarity())
                {
                case ETypePolarity::Negative:
                    NegativeFlowTypes.Add({FlowType1, FlowType2});
                    break;
                case ETypePolarity::Positive:
                    PositiveFlowTypes.Add({FlowType1, FlowType2});
                    break;
                default:
                    ULANG_UNREACHABLE();
                }
            }
        }

        const CNormalType& NormalType1 = Type1.GetNormalType();
        const CNormalType& NormalType2 = Type2.GetNormalType();
        return Match(NormalType1, ETypePolarity::Positive, NormalType2, ETypePolarity::Positive, [&](const CTypeBase* ElementType1, const CTypeBase* ElementType2)
        {
            return Subsumes(*ElementType1, *ElementType2, NegativeFlowTypes, PositiveFlowTypes, Visited);
        });
    }

    bool Subsumes(
        const CTypeBase& Type1,
        const CTypeBase& Type2,
        TArrayG<SSubsumedFlowTypes, TInlineElementAllocator<16>>& NegativeFlowTypes,
        TArrayG<SSubsumedFlowTypes, TInlineElementAllocator<16>>& PositiveFlowTypes)
    {
        TArrayG<SSubsumedTypes, TInlineElementAllocator<16>> Visited;
        return Subsumes(Type1, Type2, NegativeFlowTypes, PositiveFlowTypes, Visited);
    }

    bool ConnectedFlowTypes(const CTypeBase& Type1, const CTypeBase& Type2)
    {
        if (const CFlowType* FlowType1 = Type1.AsFlowType())
        {
            if (const CFlowType* FlowType2 = Type2.AsFlowType())
            {
                if (FlowType1->FlowEdges().Num() < FlowType2->FlowEdges().Num())
                {
                    return Contains(FlowType1->FlowEdges(), FlowType2);
                }
                return Contains(FlowType2->FlowEdges(), FlowType1);
            }
        }
        return false;
    }

    using SAdmissableTypes = TPair<const CTypeBase*, const CTypeBase*>;

    /// @see Algebraic Subtyping, chapter 8
    bool Admissable(const CTypeBase& NegativeType, const CTypeBase& PositiveType, TArrayG<SAdmissableTypes, TInlineElementAllocator<16>>& Visited)
    {
        if (Contains(Visited, SAdmissableTypes{&NegativeType, &PositiveType}))
        {
            return true;
        }
        Visited.Add({&NegativeType, &PositiveType});
        if (ConnectedFlowTypes(NegativeType, PositiveType))
        {
            return true;
        }
        const CNormalType& NegativeNormalType = NegativeType.GetNormalType();
        const CNormalType& PositiveNormalType = PositiveType.GetNormalType();
        return Match(NegativeNormalType, ETypePolarity::Negative, PositiveNormalType, ETypePolarity::Positive, [&](const CTypeBase* NegativeElementType, const CTypeBase* PositiveElementType)
        {
            return Admissable(*NegativeElementType, *PositiveElementType, Visited);
        });
    }

    bool Admissable(const CTypeBase& NegativeType, const CTypeBase& PositiveType)
    {
        TArrayG<SAdmissableTypes, TInlineElementAllocator<16>> Visited;
        return Admissable(NegativeType, PositiveType, Visited);
    }

    bool Admissable(const TArrayG<SSubsumedFlowTypes, TInlineElementAllocator<16>>& NegativeFlowTypes, const TArrayG<SSubsumedFlowTypes, TInlineElementAllocator<16>>& PositiveFlowTypes)
    {
        for (auto&& [NegativeFlowType2, NegativeFlowType1] : NegativeFlowTypes)
        {
            for (const CFlowType* PositiveFlowType1 : NegativeFlowType1->FlowEdges())
            {
                if (auto Last = PositiveFlowTypes.end(), I = FindIf(PositiveFlowTypes.begin(), Last, [=](auto&& Arg) { return Arg.First == PositiveFlowType1; }); I != Last)
                {
                    const CFlowType* PositiveFlowType2 = I->Second;
                    if (!Admissable(*NegativeFlowType2, *PositiveFlowType2))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    /// @returns true if all instances of `Type1` are subtypes of `Type2`
    /// @see Algebraic Subtyping, chapter 8
    bool IsSubtype(const CTypeBase* Type1, const CTypeBase* Type2);

    bool IsSubtype(const CTypeBase& Type1, const CTypeBase& Type2)
    {
        TArrayG<SSubsumedFlowTypes, TInlineElementAllocator<16>> NegativeFlowTypes;
        TArrayG<SSubsumedFlowTypes, TInlineElementAllocator<16>> PositiveFlowTypes;
        if (!Subsumes(Type1, Type2, NegativeFlowTypes, PositiveFlowTypes))
        {
            return false;
        }
        if (!Admissable(NegativeFlowTypes, PositiveFlowTypes))
        {
            return false;
        }
        return true;
    }

    bool IsSubtype(const CTypeBase* Type1, const CTypeBase* Type2)
    {
        ULANG_ASSERTF(Type1, "Expected non-`nullptr` `Type1`");
        ULANG_ASSERTF(Type2, "Expected non-`nullptr` `Type2`");
        return IsSubtype(*Type1, *Type2);
    }
    
    bool IsEquivalent(const CTypeBase& Type1, const CTypeBase& Type2)
    {
        TArrayG<SSubsumedFlowTypes, TInlineElementAllocator<16>> NegativeFlowTypes;
        TArrayG<SSubsumedFlowTypes, TInlineElementAllocator<16>> PositiveFlowTypes;
        if (!Subsumes(Type1, Type2, NegativeFlowTypes, PositiveFlowTypes))
        {
            return false;
        }
        if (!Subsumes(Type2, Type1, NegativeFlowTypes, PositiveFlowTypes))
        {
            return false;
        }
        if (!Admissable(NegativeFlowTypes, PositiveFlowTypes))
        {
            return false;
        }
        return true;
    }

    bool IsEquivalent(const CTypeBase* Type1, const CTypeBase* Type2)
    {
        ULANG_ASSERTF(Type1, "Expected non-`nullptr` `Type1`");
        ULANG_ASSERTF(Type2, "Expected non-`nullptr` `Type2`");
        return IsEquivalent(*Type1, *Type2);
    }

    using SMatchedTypes = TPair<const CNormalType*, const CNormalType*>;

    bool Matches(const CTypeBase* Type1, const CTypeBase* Type2, TArrayG<SMatchedTypes, TInlineElementAllocator<16>>& Visited)
    {
        // `nullptr` `Type1` or `Type2` may be possible due to a preceding
        // error, though latent bugs may result in this as well.  In such cases,
        // the most conservative type should be used - perhaps resulting in an
        // error cascade, but better than being erroneously permissive.
        if (!Type1)
        {
            if (!Type2)
            {
                // If both `Type1` and `Type2` are `nullptr`, indicate no match.
                return false;
            }
            // Interpret `nullptr` `Type1` as the most restrictive alternative.
            // For positive types, this is `any`.
            Type1 = &Type2->GetProgram()._anyType;
        }
        else if (!Type2)
        {
            // Intepret `nullptr` `Type2` as the most restrictive alternative.
            // For negative types, this is `false`.
            Type2 = &Type1->GetProgram()._falseType;
        }
        const CNormalType& NormalType1 = Type1->GetNormalType();
        const CNormalType& NormalType2 = Type2->GetNormalType();
        if (Contains(Visited, SMatchedTypes{&NormalType1, &NormalType2}))
        {
            return true;
        }
        Visited.Add({&NormalType1, &NormalType2});
        return Match(NormalType1, ETypePolarity::Positive, NormalType2, ETypePolarity::Negative, [&](const CTypeBase* ElementType1, const CTypeBase* ElementType2)
        {
            return Matches(ElementType1, ElementType2, Visited);
        });
    }
}

bool SemanticTypeUtils::Constrain(const CTypeBase* Type1, const CTypeBase* Type2)
{
    TArrayG<SConstrainedTypes, TInlineElementAllocator<16>> Visited;
    return uLang::Constrain(Type1, Type2, Visited);
}

bool SemanticTypeUtils::IsSubtype(const CTypeBase* Type1, const CTypeBase* Type2)
{
    return uLang::IsSubtype(Type1, Type2);
}

bool SemanticTypeUtils::IsEquivalent(const CTypeBase* Type1, const CTypeBase* Type2)
{
    return uLang::IsEquivalent(Type1, Type2);
}

bool SemanticTypeUtils::Matches(const CTypeBase* Type1, const CTypeBase* Type2)
{
    TArrayG<SMatchedTypes, TInlineElementAllocator<16>> Visited;
    return uLang::Matches(Type1, Type2, Visited);
}

namespace {
void RemoveAdmissableFlowEdges(const CFlowType& FlowType, ETypePolarity Polarity)
{
    TArrayG<SAdmissableTypes, TInlineElementAllocator<16>> Visited;
    const CTypeBase* Child = FlowType.GetChild();
    TSet<const CFlowType*>& NegativeFlowTypes = FlowType.FlowEdges();
    auto Last = NegativeFlowTypes.end();
    for (auto I = NegativeFlowTypes.begin(); I != Last;)
    {
        const CFlowType* NegativeFlowType = *I;
        const CTypeBase* NegativeChild = NegativeFlowType->GetChild();
        bool bAdmissable;
        switch (Polarity)
        {
        case ETypePolarity::Negative:
            bAdmissable = Admissable(*Child, *NegativeChild, Visited);
            break;
        case ETypePolarity::Positive:
            bAdmissable = Admissable(*NegativeChild, *Child, Visited);
            break;
        default:
            ULANG_UNREACHABLE();
        }
        if (bAdmissable)
        {
            NegativeFlowType->FlowEdges().Remove(&FlowType);
            NegativeFlowTypes.Remove(NegativeFlowType);
            // Rely on backwards shifting of elements in `TSet`.
        }
        else
        {
            ++I;
        }
    }
}

const CTypeBase* SkipIdentityFlowType(const CTypeBase&, ETypePolarity);

const CTypeBase* SkipIdentityFlowTypeImpl(const CFlowType& FlowType, ETypePolarity Polarity)
{
    RemoveAdmissableFlowEdges(FlowType, Polarity);
    if (FlowType.FlowEdges().IsEmpty())
    {
        if (const CTypeBase* NewChild = SkipIdentityFlowType(*FlowType.GetChild(), Polarity))
        {
            FlowType.SetChild(NewChild);
        }
        return FlowType.GetChild();
    }
    return nullptr;
}

const CTypeBase* SkipIdentityFlowType(const CTypeBase& Type, ETypePolarity Polarity)
{
    const CFlowType* FlowType = Type.AsFlowType();
    if (!FlowType)
    {
        return nullptr;
    }
    return SkipIdentityFlowTypeImpl(*FlowType, Polarity);
}
}

const CTypeBase& SemanticTypeUtils::SkipIdentityFlowType(const CFlowType& FlowType, ETypePolarity Polarity)
{
    if (const CTypeBase* NewType = SkipIdentityFlowTypeImpl(FlowType, Polarity))
    {
        return *NewType;
    }
    return FlowType;
}

const CTypeBase& SemanticTypeUtils::SkipIdentityFlowType(const CTypeBase& Type, ETypePolarity Polarity)
{
    const CFlowType* FlowType = Type.AsFlowType();
    if (!FlowType)
    {
        return Type;
    }
    return SkipIdentityFlowType(*FlowType, Polarity);
}

const CTypeBase& SemanticTypeUtils::SkipEmptyFlowType(const CTypeBase& Type)
{
    const CFlowType* FlowType = Type.AsFlowType();
    if (!FlowType)
    {
        return Type;
    }
    if (!FlowType->FlowEdges().IsEmpty())
    {
        return Type;
    }
    return *FlowType->GetChild();
}

namespace {
const CNamedType& GetOrCreateNamedType(CSemanticProgram& Program, const CNamedType& Type, bool HasValue)
{
    if (Type.HasValue() == HasValue)
    {
        return Type;
    }
    return Program.GetOrCreateNamedType(
        Type.GetName(),
        Type.GetValueType(),
        true);
}

const CTypeBase& JoinNamed(CSemanticProgram& Program, const CNamedType& Type1, const CNamedType& Type2)
{
    CSymbol Name = Type1.GetName();
    if (Name != Type2.GetName())
    {
        CTupleType::ElementArray JoinedElements;
        JoinedElements.Add(&GetOrCreateNamedType(Program, Type1, true));
        JoinedElements.Add(&GetOrCreateNamedType(Program, Type2, true));
        return Program.GetOrCreateTupleType(Move(JoinedElements), 0);
    }
    return Program.GetOrCreateNamedType(
        Name,
        SemanticTypeUtils::Join(Type1.GetValueType(), Type2.GetValueType()),
        Type1.HasValue() || Type2.HasValue());
}

template <typename FirstIterator1, typename LastIterator1, typename FirstIterator2, typename LastIterator2>
bool JoinElements(FirstIterator1 First1, LastIterator1 Last1, FirstIterator2 First2, LastIterator2 Last2, CTupleType::ElementArray& Result)
{
    if (Last1 - First1 != Last2 - First2)
    {
        return false;
    }
    for (; First1 != Last1; ++First1, ++First2)
    {
        Result.Add(SemanticTypeUtils::Join(*First1, *First2));
    }
    return true;
}

template <typename FirstIterator1, typename LastIterator1, typename FirstIterator2, typename LastIterator2>
void JoinNamedElements(CSemanticProgram& Program, FirstIterator1 First1, LastIterator1 Last1, FirstIterator2 First2, LastIterator2 Last2, CTupleType::ElementArray& Result)
{
    while (First1 != Last1 && First2 != Last2)
    {
        const CNamedType& NamedElementType1 = (*First1)->GetNormalType().template AsChecked<CNamedType>();
        const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
        if (NamedElementType1.GetName() < NamedElementType2.GetName())
        {
            Result.Add(&GetOrCreateNamedType(Program, NamedElementType1, true));
            ++First1;
        }
        else if (NamedElementType2.GetName() < NamedElementType1.GetName())
        {
            Result.Add(&GetOrCreateNamedType(Program, NamedElementType2, true));
            ++First2;
        }
        else
        {
            Result.Add(&Program.GetOrCreateNamedType(
                NamedElementType1.GetName(),
                SemanticTypeUtils::Join(NamedElementType1.GetValueType(), NamedElementType2.GetValueType()),
                NamedElementType1.HasValue() || NamedElementType2.HasValue()));
            ++First1;
            ++First2;
        }
    }
    for (; First1 != Last1; ++First1)
    {
        const CNamedType& NamedElementType1 = (*First1)->GetNormalType().template AsChecked<CNamedType>();
        Result.Add(&GetOrCreateNamedType(Program, NamedElementType1, true));
    }
    for (; First2 != Last2; ++First2)
    {
        const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
        Result.Add(&GetOrCreateNamedType(Program, NamedElementType2, true));
    }
}

template <typename Range1, typename Range2>
TOptional<CTupleType::ElementArray> JoinElements(CSemanticProgram& Program, Range1&& ElementTypes1, int32_t FirstNamedIndex1, Range2&& ElementTypes2, int32_t FirstNamedIndex2)
{
    CTupleType::ElementArray Result;
    if (!JoinElements(ElementTypes1.begin(), ElementTypes1.begin() + FirstNamedIndex1, ElementTypes2.begin(), ElementTypes2.begin() + FirstNamedIndex2, Result))
    {
        return {};
    }
    JoinNamedElements(Program, ElementTypes1.begin() + FirstNamedIndex1, ElementTypes1.end(), ElementTypes2.begin() + FirstNamedIndex2, ElementTypes2.end(), Result);
    return Result;
}

TOptional<CTupleType::ElementArray> JoinElements(CSemanticProgram& Program, const CTupleType& Type1, const CTupleType& Type2)
{
    return JoinElements(Program, Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex());
}

TOptional<CTupleType::ElementArray> JoinElements(CSemanticProgram& Program, const CTypeBase* Type1, const CTupleType& Type2)
{
    TRangeView ElementTypes1{&Type1, &Type1 + 1};
    int32_t FirstNamedIndex1 = Type1->GetNormalType().IsA<CNamedType>() ? 0 : 1;
    return JoinElements(Program, ElementTypes1, FirstNamedIndex1, Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex());
}

TOptional<CTupleType::ElementArray> JoinElements(CSemanticProgram& Program, const CTupleType& Type1, const CTypeBase* Type2)
{
    TRangeView ElementTypes2{&Type2, &Type2 + 1};
    int32_t FirstNamedIndex2 = Type2->GetNormalType().IsA<CNamedType>() ? 0 : 1;
    return JoinElements(Program, Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), ElementTypes2, FirstNamedIndex2);
}

const CClass* JoinClasses(const CClass& Class1, const CClass& Class2)
{
    auto CollectHierarchy = [] (const CClass* Class) -> TArray<const CClass*>
    {
        TArray<const CClass*> Hierarchy;
        VisitStampType VisitStamp = CScope::GenerateNewVisitStamp();
        while (Class)
        {
            if (!Class->TryMarkVisited(VisitStamp))
            {
                return {};
            }
            Hierarchy.Push(Class);
            Class = Class->_Superclass;
        }
        return Hierarchy;
    };

    TArray<const CClass*> Hierarchy1 = CollectHierarchy(&Class1);
    TArray<const CClass*> Hierarchy2 = CollectHierarchy(&Class2);
    if (Hierarchy1.Num() > Hierarchy2.Num())
    {
        uLang::Swap(Hierarchy1, Hierarchy2);
    }

    using NumType = decltype(Hierarchy1.Num());
    NumType Offset = Hierarchy2.Num() - Hierarchy1.Num();
    for (NumType I = 0, NumHierarchy1 = Hierarchy1.Num(); I != NumHierarchy1; ++I)
    {
        const CClass* HierarchyClass1 = Hierarchy1[I];
        const CClass* HierarchyClass2 = Hierarchy2[I + Offset];
        const CClass* GeneralizedClass = HierarchyClass1->_GeneralizedClass;
        if (GeneralizedClass == HierarchyClass2->_GeneralizedClass)
        {
            TArray<STypeVariableSubstitution> TypeVariableSubstitutions = JoinTypeVariableSubstitutions(
                GeneralizedClass->_TypeVariableSubstitutions,
                HierarchyClass1->_TypeVariableSubstitutions,
                HierarchyClass2->_TypeVariableSubstitutions);
            if (auto InstantiatedClass = InstantiateClass(*GeneralizedClass, ETypePolarity::Positive, TypeVariableSubstitutions))
            {
                return InstantiatedClass;
            }
            return GeneralizedClass;
        }
    }

    return nullptr;
}

}

const CTypeBase* SemanticTypeUtils::Join(const CTypeBase* Type1, const CTypeBase* Type2)
{
    ULANG_ASSERTF(Type1 && Type2, "Expected non-null arguments to Join");
    ULANG_ASSERTF(
        &Type1->GetProgram() == &Type2->GetProgram(),
        "Types '%s' and '%s' are from different programs",
        Type1->AsCode().AsCString(),
        Type2->AsCode().AsCString());
    CSemanticProgram& Program = Type1->GetProgram();
    
    if (const CFlowType* FlowType1 = Type1->AsFlowType())
    {
        const ETypePolarity Polarity = FlowType1->Polarity();
        CFlowType& Result = Program.CreateFlowType(Polarity);
        Merge(Result, *FlowType1, Polarity);
        if (const CFlowType* FlowType2 = Type2->AsFlowType())
        {
            Merge(Result, *FlowType2, Polarity);
        }
        else
        {
            MergeChild(Result, Type2, Polarity);
        }
        return &Result;
    }
    if (const CFlowType* FlowType2 = Type2->AsFlowType())
    {
        const ETypePolarity Polarity = FlowType2->Polarity();
        CFlowType& Result = Program.CreateFlowType(Polarity);
        MergeChild(Result, Type1, Polarity);
        Merge(Result, *FlowType2, Polarity);
        return &Result;
    }

    const CNormalType& NormalType1 = Type1->GetNormalType();
    const CNormalType& NormalType2 = Type2->GetNormalType();
    if (&NormalType1 == &NormalType2)
    {
        return Type1;
    }
    else if ((NormalType1.IsA<CTupleType>() && NormalType2.IsA<CArrayType>())
          || (NormalType2.IsA<CTupleType>() && NormalType1.IsA<CArrayType>()))
    {
        const CTupleType* TupleType = &(NormalType1.IsA<CTupleType>() ? NormalType1 : NormalType2).AsChecked<CTupleType>();
        const CArrayType* ArrayType = &(NormalType1.IsA<CArrayType>() ? NormalType1 : NormalType2).AsChecked<CArrayType>();
        const CTupleType::ElementArray& TupleElementTypes = TupleType->GetElements();
        if (TupleType->NumNonNamedElements() == TupleElementTypes.Num())
        {
            // If there are no named elements of the tuple, the join is the
            // array of joined elements.
            const CTypeBase* ResultElementType = ArrayType->GetElementType();
            for (auto I = TupleElementTypes.begin(), Last = TupleElementTypes.begin() + TupleType->NumNonNamedElements(); I != Last; ++I)
            {
                ResultElementType = Join(ResultElementType, *I);
            }
            return &Program.GetOrCreateArrayType(ResultElementType);
        }
        // If there are any named elements, then the join must also allow for
        // them.  However, given one argument to the join certainly does not
        // have them (the array type), they mustn't be required (i.e. must have
        // defaults).  Furthermore, any number of unnamed elements must be
        // allowed when no named elements exist.  This is impossible to
        // represent with the current vocabulary of types.  Approximate with
        // `any`.
        return &Program._anyType;
    }
    // If one type is $class, and the other is $interface, the result is $interface if $class implements its, otherwise try to find a common $interface.
    else if ((NormalType1.IsA<CClass>() && NormalType2.IsA<CInterface>())
          || (NormalType2.IsA<CClass>() && NormalType1.IsA<CInterface>()))
    {
        const CInterface* Interface = &(NormalType1.IsA<CInterface>() ? NormalType1 : NormalType2).AsChecked<CInterface>();
        const CClass* Class         = &(NormalType1.IsA<CClass>()     ? NormalType1 : NormalType2).AsChecked<CClass>();
        return JoinInterfaces(Interface, Class);
    }
    else if (NormalType1.IsA<CVoidType>() && NormalType2.IsA<CTrueType>()) { return Type2; }
    else if (NormalType1.IsA<CTrueType>() && NormalType2.IsA<CVoidType>()) { return Type1; }
    // If either type is unknown or false, the result is the other type.
    else if (NormalType1.IsA<CUnknownType>()) { return Type2; }
    else if (NormalType2.IsA<CUnknownType>()) { return Type1; }
    else if (NormalType1.IsA<CFalseType>()) { return Type2; }
    else if (NormalType2.IsA<CFalseType>()) { return Type1; }
    else if (const CTypeVariable* TypeVariable1 = NormalType1.AsNullable<CTypeVariable>())
    {
        if (const CDataDefinition* ExplicitParam = TypeVariable1->_ExplicitParam)
        {
            TypeVariable1 = ExplicitParam->_ImplicitParam;
        }
        if (const CTypeVariable* TypeVariable2 = NormalType2.AsNullable<CTypeVariable>())
        {
            if (const CDataDefinition* ExplicitParam = TypeVariable2->_ExplicitParam)
            {
                Type2 = ExplicitParam->_ImplicitParam;
                if (Type1 == Type2)
                {
                    return Type1;
                }
            }
        }
        const CTypeType* NegativeTypeType1 = TypeVariable1->_NegativeType->GetNormalType().AsNullable<CTypeType>();
        if (!NegativeTypeType1)
        {
            NegativeTypeType1 = Program._typeType;
        }
        if (Join(NegativeTypeType1->NegativeType(), Type2) == NegativeTypeType1->NegativeType())
        {
            return Type1;
        }
        const CTypeType* PositiveTypeType1 = TypeVariable1->GetType()->GetNormalType().AsNullable<CTypeType>();
        if (!PositiveTypeType1)
        {
            PositiveTypeType1 = Program._typeType;
        }
        return Join(PositiveTypeType1->PositiveType(), Type2);
    }
    else if (const CTypeVariable* TypeVariable2 = NormalType2.AsNullable<CTypeVariable>())
    {
        if (const CDataDefinition* ExplicitParam = TypeVariable2->_ExplicitParam)
        {
            TypeVariable2 = ExplicitParam->_ImplicitParam;
        }
        const CTypeType* NegativeTypeType2 = TypeVariable2->_NegativeType->GetNormalType().AsNullable<CTypeType>();
        if (!NegativeTypeType2)
        {
            NegativeTypeType2 = Program._typeType;
        }
        if (Join(&NormalType1, NegativeTypeType2->NegativeType()) == NegativeTypeType2->NegativeType())
        {
            return Type2;
        }
        const CTypeType* PositiveTypeType2 = TypeVariable2->GetType()->GetNormalType().AsNullable<CTypeType>();
        if (!PositiveTypeType2)
        {
            PositiveTypeType2 = Program._typeType;
        }
        return Join(Type1, PositiveTypeType2->PositiveType());
    }
    else if (const CTupleType* TupleType1 = NormalType1.AsNullable<CTupleType>())
    {
        if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
        {
            if (TOptional<CTupleType::ElementArray> Elements = JoinElements(Program, *TupleType1, *TupleType2))
            {
                return &Program.GetOrCreateTupleType(Move(*Elements), TupleType1->GetFirstNamedIndex());
            }
        }
        else if (TupleType1->Num() != 1)
        {
            if (TOptional<CTupleType::ElementArray> Elements = JoinElements(Program, *TupleType1, Type2))
            {
                return &Program.GetOrCreateTupleType(Move(*Elements), TupleType1->GetFirstNamedIndex());
            }
        }
        if (TupleType1->GetComparability() != EComparability::Incomparable && NormalType2.GetComparability() != EComparability::Incomparable)
        {
            return &Program._comparableType;
        }
        return &Program._anyType;
    }
    else if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
    {
        if (TupleType2->Num() != 1)
        {
            if (TOptional<CTupleType::ElementArray> Elements = JoinElements(Program, Type1, *TupleType2))
            {
                return &Program.GetOrCreateTupleType(Move(*Elements), TupleType2->GetFirstNamedIndex());
            }
        }
        if (NormalType1.GetComparability() != EComparability::Incomparable && TupleType2->GetComparability() != EComparability::Incomparable)
        {
            return &Program._comparableType;
        }
        return &Program._anyType;
    }
    else if (NormalType1.GetKind() != NormalType2.GetKind())
    {
        if (NormalType1.GetComparability() != EComparability::Incomparable && NormalType2.GetComparability() != EComparability::Incomparable)
        {
            return &Program._comparableType;
        }
        return &Program._anyType;
    }
    else
    {
        const ETypeKind CommonKind = NormalType1.GetKind();
        switch (CommonKind)
        {
        case ETypeKind::Module:
            // These types have no join less than any.
            return &Program._anyType;

        case ETypeKind::Enumeration:
            return &Program._comparableType;

        case ETypeKind::Class:
        {
            const CClass& Class1 = NormalType1.AsChecked<CClass>();
            const CClass& Class2 = NormalType2.AsChecked<CClass>();

            // For classes, find the most derived common ancestor
            const CClass* CommonClass = JoinClasses(Class1, Class2);

            // Find the set of interfaces both classes implement.
            TInterfaceSet Interfaces1;
            CollectAllInterfaces(Interfaces1, &Class1);
            TInterfaceSet Interfaces2;
            CollectAllInterfaces(Interfaces2, &Class2);
            TInterfaceSet CommonInterfaces = FindCommonInterfaces(Interfaces1, Interfaces2);

            // If there is a join of the two classes ignoring interfaces and it
            // is a subtype of the joins of the interfaces, use it.
            if (CommonClass)
            {
                if (AllOf(CommonInterfaces, [=](const CInterface* CommonInterface) { return IsSubtype(CommonClass, CommonInterface); }))
                {
                    return CommonClass;
                }
            }
            // If there is no join of the two classes ignoring interfaces, if
            // there is a single interface join, use it.  Note if there is a
            // join of the two classes ignoring interfaces and a single
            // interface join, but the class join is not a subtype of the
            // interface join, neither should be used.
            else if (CommonInterfaces.Num() == 1)
            {
                return CommonInterfaces[0];
            }

            if (Class1.GetComparability() != EComparability::Incomparable && Class2.GetComparability() != EComparability::Incomparable)
            {
                return &Program._comparableType;
            }
            return &Program._anyType;
        }
        case ETypeKind::Type:
        {
            const CTypeType& TypeType1 = NormalType1.AsChecked<CTypeType>();
            const CTypeType& TypeType2 = NormalType2.AsChecked<CTypeType>();
            const CTypeBase* MeetNegativeType = Meet(TypeType1.NegativeType(), TypeType2.NegativeType());
            const CTypeBase* JoinPositiveType = Join(TypeType1.PositiveType(), TypeType2.PositiveType());
            return &Program.GetOrCreateTypeType(MeetNegativeType, JoinPositiveType);
        }
        case ETypeKind::Interface:
        {
            // For interfaces, find the most derived common ancestor
            const CInterface* Interface1 = &NormalType1.AsChecked<CInterface>();
            const CInterface* Interface2 = &NormalType2.AsChecked<CInterface>();
            return JoinInterfaces(Interface1, Interface2);
        }
        case ETypeKind::Array:
        {
            // For array types, return an array type with the join of both element types.
            const CArrayType& ArrayType1 = NormalType1.AsChecked<CArrayType>();
            const CArrayType& ArrayType2 = NormalType2.AsChecked<CArrayType>();
            const CTypeBase* JoinElementType = Join(ArrayType1.GetElementType(), ArrayType2.GetElementType());
            return &Program.GetOrCreateArrayType(JoinElementType);
        }
        case ETypeKind::Generator:
        {
            // For generator types, return an generator type with the join of both element types.
            const CGeneratorType& GeneratorType1 = NormalType1.AsChecked<CGeneratorType>();
            const CGeneratorType& GeneratorType2 = NormalType2.AsChecked<CGeneratorType>();
            const CTypeBase* JoinElementType = Join(GeneratorType1.GetElementType(), GeneratorType2.GetElementType());
            return &Program.GetOrCreateGeneratorType(JoinElementType);
        }
        case ETypeKind::Map:
        {
            // The join of two map types is a map with the join (union) of their key type and the join (union) of their value type.
            const CMapType& MapType1 = NormalType1.AsChecked<CMapType>();
            const CMapType& MapType2 = NormalType2.AsChecked<CMapType>();
            const CTypeBase* JoinKeyType = Join(MapType1.GetKeyType(), MapType2.GetKeyType());
            const CTypeBase* JoinValueType = Join(MapType1.GetValueType(), MapType2.GetValueType());
            return &Program.GetOrCreateMapType(*JoinKeyType, *JoinValueType, MapType1.IsWeak() || MapType2.IsWeak());
        }
        case ETypeKind::Pointer:
        {
            const CPointerType& PointerType1 = NormalType1.AsChecked<CPointerType>();
            const CPointerType& PointerType2 = NormalType2.AsChecked<CPointerType>();
            const CTypeBase* MeetNegativeValueType = Meet(PointerType1.NegativeValueType(), PointerType2.NegativeValueType());
            const CTypeBase* JoinPositiveValueType = Join(PointerType1.PositiveValueType(), PointerType2.PositiveValueType());
            return &Program.GetOrCreatePointerType(MeetNegativeValueType, JoinPositiveValueType);
        }
        case ETypeKind::Reference:
        {
            const CReferenceType& ReferenceType1 = NormalType1.AsChecked<CReferenceType>();
            const CReferenceType& ReferenceType2 = NormalType2.AsChecked<CReferenceType>();
            const CTypeBase* MeetNegativeValueType = Meet(ReferenceType1.NegativeValueType(), ReferenceType2.NegativeValueType());
            const CTypeBase* JoinPositiveValueType = Join(ReferenceType1.PositiveValueType(), ReferenceType2.PositiveValueType());
            return &Program.GetOrCreateReferenceType(MeetNegativeValueType, JoinPositiveValueType);
        }
        case ETypeKind::Option:
        {
            // For option types, return an option type with the join of both value types.
            const COptionType& OptionType1 = NormalType1.AsChecked<COptionType>();
            const COptionType& OptionType2 = NormalType2.AsChecked<COptionType>();

            const CTypeBase* CommonValueType = Join(OptionType1.GetValueType(), OptionType2.GetValueType());
            return &Program.GetOrCreateOptionType(CommonValueType);
        }
        case ETypeKind::Function:
        {
            const CFunctionType& FunctionType1 = NormalType1.AsChecked<CFunctionType>();
            const CFunctionType& FunctionType2 = NormalType2.AsChecked<CFunctionType>();
            // The join of two function types is the meet (intersection) of their parameter type and the join (union) of their return type.
            const CTypeBase* MeetParamsType = Meet(&FunctionType1.GetParamsType(), &FunctionType2.GetParamsType());
            const CTypeBase* JoinReturnType = Join(&FunctionType1.GetReturnType(), &FunctionType2.GetReturnType());
            SEffectSet JoinEffects = FunctionType1.GetEffects() | FunctionType2.GetEffects();
            return &Program.GetOrCreateFunctionType(*MeetParamsType, *JoinReturnType, JoinEffects);
        }
        case ETypeKind::Named:
            return &JoinNamed(Program, NormalType1.AsChecked<CNamedType>(), NormalType2.AsChecked<CNamedType>());

        case ETypeKind::Int:
        {
            const CIntType& IntType1 = NormalType1.AsChecked<CIntType>();
            const CIntType& IntType2 = NormalType2.AsChecked<CIntType>();

            CIntType::Constraint Min = CMath::Min(IntType1.GetMin(), IntType2.GetMin());
            CIntType::Constraint Max = CMath::Max(IntType1.GetMax(), IntType2.GetMax());

            return &Program.GetOrCreateConstrainedIntType(Min, Max);
        }

        case ETypeKind::Float:
        {
            const CFloatType& FloatType1 = NormalType1.AsChecked<CFloatType>();
            const CFloatType& FloatType2 = NormalType2.AsChecked<CFloatType>();

            double Min = (FloatType1.MinRanking() <= FloatType2.MinRanking()) ? FloatType1.GetMin() : FloatType2.GetMin();
            double Max = (FloatType1.MaxRanking() >= FloatType2.MaxRanking()) ? FloatType1.GetMax() : FloatType2.GetMax();

            return &Program.GetOrCreateConstrainedFloatType(Min, Max);
        }

        case ETypeKind::Unknown:
        case ETypeKind::False:
        case ETypeKind::True:
        case ETypeKind::Void:
        case ETypeKind::Any:
        case ETypeKind::Comparable:
        case ETypeKind::Persistable:
        case ETypeKind::Logic:
        case ETypeKind::Rational:
        case ETypeKind::Char8:
        case ETypeKind::Char32:
        case ETypeKind::Path:
        case ETypeKind::Range:
            // It shouldn't be possible to reach here for one of the global types; it should be
            // handled by the first Type1==Type2 case.
            ULANG_FALLTHROUGH;
        case ETypeKind::Tuple:
        case ETypeKind::Variable:
        default:
            ULANG_UNREACHABLE();
        }
    }
}

namespace {
const CTypeBase& MeetNamed(CSemanticProgram& Program, const CNamedType& Type1, const CNamedType& Type2)
{
    CSymbol Name = Type1.GetName();
    if (Name != Type2.GetName())
    {
        if (!Type1.HasValue())
        {
            return Program._falseType;
        }
        if (!Type2.HasValue())
        {
            return Program._falseType;
        }
        return Program.GetOrCreateTupleType({});
    }
    return Program.GetOrCreateNamedType(
        Name,
        SemanticTypeUtils::Meet(Type1.GetValueType(), Type2.GetValueType()),
        Type1.HasValue() && Type2.HasValue());
}

template <typename FirstIterator1, typename LastIterator1, typename FirstIterator2, typename LastIterator2>
bool MeetElements(FirstIterator1 First1, LastIterator1 Last1, FirstIterator2 First2, LastIterator2 Last2, CTupleType::ElementArray& Result)
{
    if (Last1 - First1 != Last2 - First2)
    {
        return false;
    }
    for (; First1 != Last1; ++First1, ++First2)
    {
        Result.Add(SemanticTypeUtils::Meet(*First1, *First2));
    }
    return true;
}

template <typename FirstIterator1, typename LastIterator1, typename FirstIterator2, typename LastIterator2>
bool MeetNamedElements(CSemanticProgram& Program, FirstIterator1 First1, LastIterator1 Last1, FirstIterator2 First2, LastIterator2 Last2, CTupleType::ElementArray& Result)
{
    while (First1 != Last1 && First2 != Last2)
    {
        const CNamedType& NamedElementType1 = (*First1)->GetNormalType().template AsChecked<CNamedType>();
        const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
        if (NamedElementType1.GetName() < NamedElementType2.GetName())
        {
            if (!NamedElementType1.HasValue())
            {
                return false;
            }
            ++First1;
        }
        else if (NamedElementType2.GetName() < NamedElementType1.GetName())
        {
            if (!NamedElementType2.HasValue())
            {
                return false;
            }
            ++First2;
        }
        else
        {
            Result.Add(&Program.GetOrCreateNamedType(
                NamedElementType1.GetName(),
                SemanticTypeUtils::Meet(NamedElementType1.GetValueType(), NamedElementType2.GetValueType()),
                NamedElementType1.HasValue() && NamedElementType2.HasValue()));
            ++First1;
            ++First2;
        }
    }
    for (; First1 != Last1; ++First1)
    {
        const CNamedType& NamedElementType1 = (*First1)->GetNormalType().template AsChecked<CNamedType>();
        if (!NamedElementType1.HasValue())
        {
            return false;
        }
    }
    for (; First2 != Last2; ++First2)
    {
        const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
        if (!NamedElementType2.HasValue())
        {
            return false;
        }
    }
    return true;
}

template <typename Range1, typename Range2>
TOptional<CTupleType::ElementArray> MeetElements(CSemanticProgram& Program, Range1&& ElementTypes1, int32_t FirstNamedIndex1, Range2&& ElementTypes2, int32_t FirstNamedIndex2)
{
    CTupleType::ElementArray Result;
    if (!MeetElements(ElementTypes1.begin(), ElementTypes1.begin() + FirstNamedIndex1, ElementTypes2.begin(), ElementTypes2.begin() + FirstNamedIndex2, Result))
    {
        return {};
    }
    if (!MeetNamedElements(Program, ElementTypes1.begin() + FirstNamedIndex1, ElementTypes1.end(), ElementTypes2.begin() + FirstNamedIndex2, ElementTypes2.end(), Result))
    {
        return {};
    }
    return Result;
}

TOptional<CTupleType::ElementArray> MeetElements(CSemanticProgram& Program, const CTupleType& Type1, const CTupleType& Type2)
{
    return MeetElements(Program, Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex());
}

TOptional<CTupleType::ElementArray> MeetElements(CSemanticProgram& Program, const CTypeBase* Type1, const CTupleType& Type2)
{
    TRangeView ElementTypes1{&Type1, &Type1 + 1};
    int32_t FirstNamedIndex1 = Type1->GetNormalType().IsA<CNamedType>() ? 0 : 1;
    return MeetElements(Program, ElementTypes1, FirstNamedIndex1, Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex());
}

TOptional<CTupleType::ElementArray> MeetElements(CSemanticProgram& Program, const CTupleType& Type1, const CTypeBase* Type2)
{
    TRangeView ElementTypes2{&Type2, &Type2 + 1};
    int32_t FirstNamedIndex2 = Type2->GetNormalType().IsA<CNamedType>() ? 0 : 1;
    return MeetElements(Program, Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), ElementTypes2, FirstNamedIndex2);
}
}

const CTypeBase* SemanticTypeUtils::Meet(const CTypeBase* Type1, const CTypeBase* Type2)
{
    ULANG_ASSERTF(Type1 && Type2, "Expected non-null arguments to Meet");
    ULANG_ASSERTF(
        &Type1->GetProgram() == &Type2->GetProgram(),
        "Types '%s' and '%s' are from different programs",
        Type1->AsCode().AsCString(),
        Type2->AsCode().AsCString());
    CSemanticProgram& Program = Type1->GetProgram();


    if (const CFlowType* FlowType1 = Type1->AsFlowType())
    {
        const ETypePolarity Polarity = FlowType1->Polarity();
        CFlowType& Result = Program.CreateFlowType(Polarity);
        Merge(Result, *FlowType1, Polarity);
        if (const CFlowType* FlowType2 = Type2->AsFlowType())
        {
            Merge(Result, *FlowType2, Polarity);
        }
        else
        {
            MergeChild(Result, Type2, Polarity);
        }
        return &Result;
    }
    if (const CFlowType* FlowType2 = Type2->AsFlowType())
    {
        const ETypePolarity Polarity = FlowType2->Polarity();
        CFlowType& Result = Program.CreateFlowType(Polarity);
        MergeChild(Result, Type1, Polarity);
        Merge(Result, *FlowType2, Polarity);
        return &Result;
    }

    const CNormalType& NormalType1 = Type1->GetNormalType();
    const CNormalType& NormalType2 = Type2->GetNormalType();

    if (&NormalType1 == &NormalType2)
    {
        return Type1;
    }
    else if (NormalType1.IsA<CComparableType>() && NormalType2.GetComparability() != EComparability::Incomparable) { return Type2; }
    else if (NormalType2.IsA<CComparableType>() && NormalType1.GetComparability() != EComparability::Incomparable) { return Type1; }
    else if (NormalType1.IsA<CPersistableType>() && NormalType2.IsPersistable()) { return Type2; }
    else if (NormalType2.IsA<CPersistableType>() && NormalType1.IsPersistable()) { return Type1; }
    // If either type is any, the result is the other type.
    else if (NormalType1.IsA<CAnyType>()) { return Type2; }
    else if (NormalType2.IsA<CAnyType>()) { return Type1; }
    else if (NormalType1.IsA<CVoidType>()) { return Type2; }
    else if (NormalType2.IsA<CVoidType>()) { return Type1; }
    else if ((NormalType1.IsA<CTupleType>() && NormalType2.IsA<CArrayType>())
          || (NormalType2.IsA<CTupleType>() && NormalType1.IsA<CArrayType>()))
    {
        const CTupleType& TupleType = (NormalType1.IsA<CTupleType>() ? NormalType1 : NormalType2).AsChecked<CTupleType>();
        const CArrayType& ArrayType = (NormalType1.IsA<CArrayType>() ? NormalType1 : NormalType2).AsChecked<CArrayType>();
        const CTupleType::ElementArray& TupleElementTypes = TupleType.GetElements();
        if (!AllOf(TupleElementTypes.begin() + TupleType.GetFirstNamedIndex(), TupleElementTypes.end(),
            [](const CTypeBase* Element) { return Element->GetNormalType().AsChecked<CNamedType>().HasValue(); }))
        {
            // An array cannot provide named elements.  If any are present
            // lacking a default in the tuple, the meet is `false`.
            return &Program._falseType;
        }
        if (TupleType.NumNonNamedElements() == 1 && TupleElementTypes.Num() != 1)
        {
            // If named elements are present in the tuple and there is a single
            // unnamed tuple element, the meet may be the meet of the single
            // unnamed tuple element and the array.
            const CTypeBase* ResultType = Meet(TupleElementTypes[0], &ArrayType);
            if (!ResultType->GetNormalType().IsA<CFalseType>())
            {
                return ResultType;
            }
            // However, if `false`, a higher (non-`false`) type will certainly
            // be found via element-wise meet on the tuple, as such a type will
            // at least be `tuple(false)`, which is (arguably) higher than
            // `false`.  Note the element-wise case may also produce lower types,
            // e.g. `[]any \/ tuple([]any, ?X:int = 0)` would produce
            // `tuple([]any)`, which is lower than what is produced by the
            // above (`[]any`).
        }
        CTupleType::ElementArray ResultElements;
        ResultElements.Reserve(TupleType.NumNonNamedElements());
        for (auto I = TupleElementTypes.begin(), Last = TupleElementTypes.begin() + TupleType.NumNonNamedElements(); I != Last; ++I)
        {
            ResultElements.Add(Meet(*I, ArrayType.GetElementType()));
        }
        return &Program.GetOrCreateTupleType(Move(ResultElements));
    }
    // If one type is a class, and the other is a interface, the result is a class if the class implements the interface, otherwise false.
    else if ((NormalType1.IsA<CClass>() && NormalType2.IsA<CInterface>())
          || (NormalType2.IsA<CClass>() && NormalType1.IsA<CInterface>()))
    {
        const CInterface& Interface = (NormalType1.IsA<CInterface>() ? NormalType1 : NormalType2).AsChecked<CInterface>();
        const CClass&     Class     = (NormalType1.IsA<CClass>()     ? NormalType1 : NormalType2).AsChecked<CClass>();
        if (SemanticTypeUtils::IsSubtype(&Class, &Interface))
        {
            return &Class;
        }
        return &Program._falseType;
    }
    // If either type is false or unknown, the result is that type.
    else if (NormalType1.IsA<CFalseType>()) { return Type1; }
    else if (NormalType2.IsA<CFalseType>()) { return Type2; }
    else if (NormalType1.IsA<CUnknownType>()) { return Type1; }
    else if (NormalType2.IsA<CUnknownType>()) { return Type2; }
    else if (const CTypeVariable* TypeVariable1 = NormalType1.AsNullable<CTypeVariable>())
    {
        if (const CDataDefinition* ExplicitParam = TypeVariable1->_ExplicitParam)
        {
            TypeVariable1 = ExplicitParam->_ImplicitParam;
        }
        if (const CTypeVariable* TypeVariable2 = NormalType2.AsNullable<CTypeVariable>())
        {
            if (const CDataDefinition* ExplicitParam = TypeVariable2->_ExplicitParam)
            {
                Type2 = ExplicitParam->_ImplicitParam;
                if (Type1 == Type2)
                {
                    return Type1;
                }
            }
        }
        const CTypeType* NegativeTypeType1 = TypeVariable1->_NegativeType->GetNormalType().AsNullable<CTypeType>();
        if (!NegativeTypeType1)
        {
            NegativeTypeType1 = Program._typeType;
        }
        if (Meet(NegativeTypeType1->PositiveType(), Type2) == NegativeTypeType1->PositiveType())
        {
            return TypeVariable1;
        }
        return Meet(NegativeTypeType1->NegativeType(), Type2);
    }
    else if (const CTypeVariable* TypeVariable2 = NormalType2.AsNullable<CTypeVariable>())
    {
        if (const CDataDefinition* ExplicitParam = TypeVariable2->_ExplicitParam)
        {
            TypeVariable2 = ExplicitParam->_ImplicitParam;
        }
        const CTypeType* NegativeTypeType2 = TypeVariable2->_NegativeType->GetNormalType().AsNullable<CTypeType>();
        if (!NegativeTypeType2)
        {
            NegativeTypeType2 = Program._typeType;
        }
        if (Meet(&NormalType1, NegativeTypeType2->PositiveType()) == NegativeTypeType2->PositiveType())
        {
            return TypeVariable2;
        }
        return Meet(&NormalType1, NegativeTypeType2->NegativeType());
    }
    else if (const CTupleType* TupleType1 = NormalType1.AsNullable<CTupleType>())
    {
        if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
        {
            TOptional<CTupleType::ElementArray> Elements = MeetElements(Program, *TupleType1, *TupleType2);
            if (!Elements)
            {
                return &Program._falseType;
            }
            return &Program.GetOrCreateTupleType(Move(*Elements), TupleType1->GetFirstNamedIndex());
        }
        if (TupleType1->Num() == 1)
        {
            return &Program._falseType;
        }
        TOptional<CTupleType::ElementArray> Elements = MeetElements(Program, *TupleType1, Type2);
        if (!Elements)
        {
            return &Program._falseType;
        }
        if (Elements->Num() == 1)
        {
            // For `TupleType1` of size != 1, this may only hold if `TupleType1`'s
            // named elements all have values and `TupleType1` has a single unnamed
            // element.
            return (*Elements)[0];
        }
        return &Program.GetOrCreateTupleType(Move(*Elements), TupleType1->GetFirstNamedIndex());
    }
    else if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
    {
        if (TupleType2->Num() == 1)
        {
            return &Program._falseType;
        }
        TOptional<CTupleType::ElementArray> Elements = MeetElements(Program, Type1, *TupleType2);
        if (!Elements)
        {
            return &Program._falseType;
        }
        if (Elements->Num() == 1)
        {
            // For `TupleType2` of size != 1, this may only hold if `TupleType2`'s
            // named elements all have values and `TupleType2` has a single unnamed
            // element.
            return (*Elements)[0];
        }
        return &Program.GetOrCreateTupleType(Move(*Elements), TupleType2->GetFirstNamedIndex());
    }
    else if (NormalType1.GetKind() != NormalType2.GetKind())
    {
        return &Program._falseType;
    }
    else
    {
        const ETypeKind CommonKind = NormalType1.GetKind();
        switch(CommonKind)
        {
        case ETypeKind::Module:
        case ETypeKind::Enumeration:
            // These types have no meet greater than false.
            return &Program._falseType;

        case ETypeKind::Class:
        {
            // For classes, if one is a subclass of the other, that is the meet of the two classes.
            const CClass& Class1 = NormalType1.AsChecked<CClass>();
            const CClass& Class2 = NormalType2.AsChecked<CClass>();
            if (SemanticTypeUtils::IsSubtype(&Class1, &Class2)) { return Type1; }
            if (SemanticTypeUtils::IsSubtype(&Class2, &Class1)) { return Type2; }
            return &Program._falseType;
        }
        case ETypeKind::Interface:
        {
            // For interfaces, if one is a subinterface of the other, that is the meet of the two interfaces.
            const CInterface& Interface1 = NormalType1.AsChecked<CInterface>();
            const CInterface& Interface2 = NormalType2.AsChecked<CInterface>();
            if (SemanticTypeUtils::IsSubtype(&Interface2, &Interface1)) { return Type2; }
            if (SemanticTypeUtils::IsSubtype(&Interface1, &Interface2)) { return Type1; }
            return &Program._falseType;
        }
        case ETypeKind::Type:
        {
            const CTypeType& TypeType1 = NormalType1.AsChecked<CTypeType>();
            const CTypeType& TypeType2 = NormalType2.AsChecked<CTypeType>();
            const CTypeBase* JoinNegativeType = Join(TypeType1.NegativeType(), TypeType2.NegativeType());
            const CTypeBase* MeetPositiveType = Meet(TypeType1.PositiveType(), TypeType2.PositiveType());
            return &Program.GetOrCreateTypeType(JoinNegativeType, MeetPositiveType);
        }
        case ETypeKind::Array:
        {
            // For array types, return an array type with the meet of both element types.
            const CArrayType& ArrayType1 = NormalType1.AsChecked<CArrayType>();
            const CArrayType& ArrayType2 = NormalType2.AsChecked<CArrayType>();
            const CTypeBase* MeetElementType = Meet(ArrayType1.GetElementType(), ArrayType2.GetElementType());
            return &Program.GetOrCreateArrayType(MeetElementType);
        }
        case ETypeKind::Generator:
        {
            // For generator types, return an generator type with the meet of both element types.
            const CGeneratorType& GeneratorType1 = NormalType1.AsChecked<CGeneratorType>();
            const CGeneratorType& GeneratorType2 = NormalType2.AsChecked<CGeneratorType>();
            const CTypeBase* MeetElementType = Meet(GeneratorType1.GetElementType(), GeneratorType2.GetElementType());
            return &Program.GetOrCreateGeneratorType(MeetElementType);
        }
        case ETypeKind::Map:
        {
            // The meet of two map types is a map with the meet (intersection) of their key type and the meet (intersection) of their value type.
            const CMapType& MapType1 = NormalType1.AsChecked<CMapType>();
            const CMapType& MapType2 = NormalType2.AsChecked<CMapType>();
            const CTypeBase* MeetKeyType   = Meet(MapType1.GetKeyType()  , MapType2.GetKeyType());
            const CTypeBase* MeetValueType = Meet(MapType1.GetValueType(), MapType2.GetValueType());
            return &Program.GetOrCreateMapType(*MeetKeyType, *MeetValueType, MapType1.IsWeak() && MapType2.IsWeak());
        }
        case ETypeKind::Pointer:
        {
            const CPointerType& PointerType1 = NormalType1.AsChecked<CPointerType>();
            const CPointerType& PointerType2 = NormalType2.AsChecked<CPointerType>();
            const CTypeBase* JoinNegativeValueType = Join(PointerType1.NegativeValueType(), PointerType2.NegativeValueType());
            const CTypeBase* MeetPositiveValueType = Meet(PointerType1.PositiveValueType(), PointerType2.PositiveValueType());
            return &Program.GetOrCreatePointerType(JoinNegativeValueType, MeetPositiveValueType);
        }
        case ETypeKind::Reference:
        {
            const CReferenceType& ReferenceType1 = NormalType1.AsChecked<CReferenceType>();
            const CReferenceType& ReferenceType2 = NormalType2.AsChecked<CReferenceType>();
            const CTypeBase* JoinNegativeValueType = Join(ReferenceType1.NegativeValueType(), ReferenceType2.NegativeValueType());
            const CTypeBase* MeetPositiveValueType = Meet(ReferenceType1.PositiveValueType(), ReferenceType2.PositiveValueType());
            return &Program.GetOrCreateReferenceType(JoinNegativeValueType, MeetPositiveValueType);
        }
        case ETypeKind::Option:
        {
            // For option types, return an option type with the meet of both value types.
            const COptionType& OptionType1 = NormalType1.AsChecked<COptionType>();
            const COptionType& OptionType2 = NormalType2.AsChecked<COptionType>();
            const CTypeBase* MeetValueType = Meet(OptionType1.GetValueType(), OptionType2.GetValueType());
            return &Program.GetOrCreateOptionType(MeetValueType);
        }
        case ETypeKind::Function:
        {
            const CFunctionType& FunctionType1 = NormalType1.AsChecked<CFunctionType>();
            const CFunctionType& FunctionType2 = NormalType2.AsChecked<CFunctionType>();
            // The meet type of two functions is the join (union) of their parameter type and the meet (intersection) of their return type.
            const CTypeBase* JoinParamsType = Join(&FunctionType1.GetParamsType(), &FunctionType2.GetParamsType());
            const CTypeBase* MeetReturnType = Meet(&FunctionType1.GetReturnType(), &FunctionType2.GetReturnType());
            SEffectSet MeetEffects = FunctionType1.GetEffects() & FunctionType2.GetEffects();
            return &Program.GetOrCreateFunctionType(*JoinParamsType, *MeetReturnType, MeetEffects);
        }
        case ETypeKind::Named:
            return &MeetNamed(Program, NormalType1.AsChecked<CNamedType>(), NormalType2.AsChecked<CNamedType>());

        case ETypeKind::Int:
        {
            const CIntType& IntType1 = NormalType1.AsChecked<CIntType>();
            const CIntType& IntType2 = NormalType2.AsChecked<CIntType>();

            CIntType::Constraint Min = CMath::Max(IntType1.GetMin(), IntType2.GetMin());
			CIntType::Constraint Max = CMath::Min(IntType1.GetMax(), IntType2.GetMax());

            return &Program.GetOrCreateConstrainedIntType(Min, Max);
        }

        case ETypeKind::Float:
        {
            const CFloatType& FloatType1 = NormalType1.AsChecked<CFloatType>();
            const CFloatType& FloatType2 = NormalType2.AsChecked<CFloatType>();

            double Min = (FloatType1.MinRanking() >= FloatType2.MinRanking()) ? FloatType1.GetMin() : FloatType2.GetMin();
            double Max = (FloatType1.MaxRanking() <= FloatType2.MaxRanking()) ? FloatType1.GetMax() : FloatType2.GetMax();

            return &Program.GetOrCreateConstrainedFloatType(Min, Max);
        }

        case ETypeKind::Unknown:
        case ETypeKind::False:
        case ETypeKind::True:
        case ETypeKind::Void:
        case ETypeKind::Any:
        case ETypeKind::Comparable:
        case ETypeKind::Persistable:
        case ETypeKind::Logic:
        case ETypeKind::Rational:
        case ETypeKind::Char8:
        case ETypeKind::Char32:
        case ETypeKind::Path:
        case ETypeKind::Range:
            // It shouldn't be possible to reach here for one of the global types; it should be
            // handled by the first Type1==Type2 case.
            ULANG_FALLTHROUGH;
        case ETypeKind::Tuple:
        case ETypeKind::Variable:
        default:
            ULANG_UNREACHABLE();
        }
    }
} 

namespace {

bool AreDomainsDistinct(const CNormalType&, const CNormalType&);
bool AreDomainsDistinct(const CTypeBase*, const CTypeBase*);

bool IsDomainTop(const CNormalType& DomainType)
{
    return DomainType.GetKind() == Cases<ETypeKind::Void, ETypeKind::Any>;
}

bool IsBottom(const CNormalType& Type)
{
    return Type.GetKind() == Cases<ETypeKind::Unknown, ETypeKind::False>;
}

// Note, all specific `Is<...>Distinct` predicates assume `IsBottom` and `IsDomainTop`
// have already been checked by `AreDomainsDistinct`.

bool IsDomainTrueDistinct(const CNormalType& DomainType)
{
    if (DomainType.GetKind() == Cases<
        ETypeKind::Function,
        ETypeKind::Map,
        ETypeKind::Array,
        ETypeKind::Generator,
        ETypeKind::Logic,
        ETypeKind::Option,
        ETypeKind::True>)
    {
        return false;
    }
    if (const CTupleType* DomainTupleType = DomainType.AsNullable<CTupleType>())
    {
        return DomainTupleType->Num() != 0;
    }
    return true;
}

bool IsNamedTypeDistinct(const CNamedType& NamedType1, const CNormalType& Type2)
{
    // We need to handle cases like this, which are not distinct. The first F
    // has a single parameter which is a named type, and the second F has a tuple
    // with the named types in it.
    // F(?X:int=42)
    // F(?X:int=10, ?Y:int=42)
    //
    // Also, note that this works as expected when explicitly declaring
    // a tuple as a singular parameter. These should not be distinct:
    // F(?X:int)
    // F(P:tuple(?X:int))
    //
    // Also, if we had a syntax to write this, this property nests, so this
    // wouldn't be distinct:
    // 
    // F(A:tuple(int, ?X:int))
    // F(A:tuple(int, tuple(?X:int, ?Y:int=42)))
    if (const CTupleType* Tuple = Type2.AsNullable<CTupleType>())
    {
        return AreDomainsDistinct(NamedType1.ToTupleType(), *Tuple);
    }

    const CNamedType* NamedType2 = Type2.AsNullable<CNamedType>();
    if (!NamedType2)
    {
        return true;
    }

    if (NamedType1.HasValue() && NamedType2->HasValue())
    {
        return false;
    }

    if (NamedType1.GetName() != NamedType2->GetName())
    {
        return true;
    }

    // If only one or neither named types have a default value, then we are
    // distinct if the types are distinct. Consider the example:
    // F(X?:int=42) 
    // F(X?:float)
    // when deciding which function to invoke. If a value is provided for X, we can 
    // decide based on that value's type. If a value isn't provided for X, we can
    // decide based only having one function with a default value. Naturally, if 
    // neither has a default value, the only way to tell a difference is via their types.
    return AreDomainsDistinct(NamedType1.GetValueType(), NamedType2->GetValueType());
}

bool IsDomainNonEmptyTupleDistinct(const CNormalType& DomainType1, const CTupleType& DomainTupleType2)
{
    if (DomainType1.IsA<CFunctionType>())
    {
        return false;
    }
    if (const CMapType* MapType = DomainType1.AsNullable<CMapType>())
    {
        if (!MapType->GetKeyType()->GetNormalType().IsA<CIntType>())
        {
            return true;
        }
        const CNormalType& ValueType = MapType->GetValueType()->GetNormalType();
        return AnyOf(
            DomainTupleType2.GetElements(),
            [&](const CTypeBase* Arg) { return AreDomainsDistinct(ValueType, Arg->GetNormalType()); });
    }
    if (const CArrayType* ArrayType = DomainType1.AsNullable<CArrayType>())
    {
        const CNormalType& ElementType = ArrayType->GetElementType()->GetNormalType();
        return AnyOf(
            DomainTupleType2.GetElements(),
            [&](const CTypeBase* Arg) { return AreDomainsDistinct(ElementType, Arg->GetNormalType()); });
    }
    if (DomainType1.IsA<CLogicType>())
    {
        return DomainTupleType2.Num() != 1;
    }
    if (const COptionType* OptionType = DomainType1.AsNullable<COptionType>())
    {
        if (DomainTupleType2.Num() != 1)
        {
            return true;
        }
        const CNormalType& ValueType = OptionType->GetValueType()->GetNormalType();
        return
            !ValueType.IsA<CIntType>() ||
            AreDomainsDistinct(ValueType, DomainTupleType2[0]->GetNormalType());
    }
    if (const CTupleType* DomainTupleType1 = DomainType1.AsNullable<CTupleType>())
    {
        auto NumNonNamedElements = DomainTupleType1->NumNonNamedElements();
        if (NumNonNamedElements != DomainTupleType2.NumNonNamedElements())
        {
            return true;
        }

        bool bAreAnyNonNamedDistinct = AnyOf(TUntil{NumNonNamedElements},
            [&](auto I) { return AreDomainsDistinct((*DomainTupleType1)[I], DomainTupleType2[I]); });
        if (bAreAnyNonNamedDistinct)
        {
            return true;
        }

        // The ways named sections of tuples can be distinct:
        // - If the named value shows up in both tuples and is distinct.
        // - If a named value is present in one tuple, but not the other, and is a required value in one. 
        //   Notably, if it's optional in one, but not present in the other, then we can't use it as
        //   a form of distinction.
        ULANG_ASSERTF(DomainTupleType1->GetFirstNamedIndex() == DomainTupleType2.GetFirstNamedIndex(), "Otherwise we would've already said they're distinct.");
        TArray<CSymbol> SeenNames;
        for (int32_t I = DomainTupleType1->GetFirstNamedIndex(); I < DomainTupleType1->Num(); ++I)
        {
            const CNamedType& NamedType = (*DomainTupleType1)[I]->GetNormalType().AsChecked<CNamedType>();
            SeenNames.Push(NamedType.GetName());

            if (const CNamedType* Match = DomainTupleType2.FindNamedType(NamedType.GetName()))
            {
                if (IsNamedTypeDistinct(NamedType, *Match))
                {
                    return true;
                }
            }
            else if (!NamedType.HasValue())
            {
                return true;
            }
        }

        for (int32_t I = DomainTupleType2.GetFirstNamedIndex(); I < DomainTupleType2.Num(); ++I)
        {
            const CNamedType& NamedType = DomainTupleType2[I]->GetNormalType().AsChecked<CNamedType>();
            if (!SeenNames.Contains(NamedType.GetName()) && !NamedType.HasValue())
            {
                return true;
            }
        }

        return false;
    }
    return true;
}

bool IsAnyClassDistinct(const CNormalType& Type)
{
    if (Type.GetKind() == ETypeKind::Interface)
    {
        return false;
    }
    return true;
}

bool IsClassDistinct(const CNormalType& Type1, const CClass& Class2)
{
    if (Type1.GetKind() == ETypeKind::Interface)
    {
        return false;
    }
    if (const CClass* Class1 = Type1.AsNullable<CClass>())
    {
        if (Class1->IsStruct())
        {
            return true;
        }
        const CClass& PositiveClass1 = AsPositiveClass(*Class1);
        const CClass& PositiveClass2 = AsPositiveClass(Class2);
        return !PositiveClass1.IsClass(PositiveClass2) && !PositiveClass2.IsClass(PositiveClass1);
    }
    return true;
}

bool IsStructDistinct(const CNormalType& Type1, const CClass& Struct2)
{
    if (Type1.IsA<CClass>())
    {
        return &Type1 != &Struct2;
    }
    return true;
}

bool IsDomainTypeDistinct(const CNormalType& DomainType1, const CTypeType& DomainTypeType2)
{
    if (const CTypeType* DomainTypeType1 = DomainType1.AsNullable<CTypeType>())
    {
        if (!AreDomainsDistinct(DomainTypeType1->NegativeType(), DomainTypeType2.NegativeType()))
        {
            return false;
        }
        if (!AreDomainsDistinct(DomainTypeType1->PositiveType(), DomainTypeType2.PositiveType()))
        {
            return false;
        }
    }
    return true;
}

bool IsPointerDistinct(const CNormalType& Type1, const CPointerType& PointerType2)
{
    if (const CPointerType* PointerType1 = Type1.AsNullable<CPointerType>())
    {
        if (!AreDomainsDistinct(PointerType1->NegativeValueType(), PointerType2.NegativeValueType()))
        {
            return false;
        }
        if (!AreDomainsDistinct(PointerType1->PositiveValueType(), PointerType2.PositiveValueType()))
        {
            return false;
        }
    }
    return true;
}

bool IsReferenceDistinct(const CNormalType& Type1, const CReferenceType& ReferenceType2)
{
    if (const CReferenceType* ReferenceType1 = Type1.AsNullable<CReferenceType>())
    {
        if (!AreDomainsDistinct(ReferenceType1->NegativeValueType(), ReferenceType2.NegativeValueType()))
        {
            return false;
        }
        if (!AreDomainsDistinct(ReferenceType1->PositiveValueType(), ReferenceType2.PositiveValueType()))
        {
            return false;
        }
    }
    return true;
}

bool IsEnumerationDistinct(const CNormalType& Type1, const CEnumeration& Enumeration2)
{
    if (Type1.IsA<CEnumeration>())
    {
        return &Type1 != &Enumeration2;
    }
    return true;
}

bool IsIntDistinct(const CNormalType& Type1, const CIntType& Int2)
{
    if (const CIntType* Int1 = Type1.AsNullable<CIntType>())
    {
        return !Int1->IsInhabitable() || !Int2.IsInhabitable()
            || (Int1->GetMin() < Int2.GetMin() && Int1->GetMax() < Int2.GetMin())
            || (Int2.GetMin() < Int1->GetMin() && Int2.GetMax() < Int1->GetMin());
    }
    return true;
}

bool IsFloatDistinct(const CNormalType& Type1, const CFloatType& Float2)
{
    if (const CFloatType* Float1 = Type1.AsNullable<CFloatType>())
    {

        return !Float1->IsInhabitable() || !Float2.IsInhabitable()
            || (Float1->MinRanking() < Float2.MinRanking() && Float1->MaxRanking() < Float2.MinRanking())
            || (Float2.MinRanking() < Float1->MinRanking() && Float2.MaxRanking() < Float1->MinRanking());
    }
    return true;
}

bool AreDomainsDistinct(const CNormalType& DomainType1, const CNormalType& DomainType2)
{
    // If two types do not share a subtype above `false`, they are distinct. In
    // other words, if the intersection of the sets of values contained in two
    // types is empty, they are distinct. All types other than `false` must
    // reach a type just above `false` (where the lattice edges point down), so
    // if two types reach the same type above `false`, they may share a
    // possibly-inhabited subtype, i.e. they are not distinct. In terms of sets,
    // if two sets of values contain the same subset, they are not distinct.
    // Importantly, the subtype need not currently exist, just be possible to
    // exist. Furthermore, this means the problem can be reduced to checking
    // subtyping of the types just above `false` against the argument types.
    if (&DomainType1 == &DomainType2)
    {
        return false;
    }
    if (IsDomainTop(DomainType1) || IsDomainTop(DomainType2))
    {
        return false;
    }
    if (DomainType1.GetKind() == ETypeKind::Comparable)
    {
        return DomainType2.GetComparability() == EComparability::Incomparable;
    }
    if (DomainType2.GetKind() == ETypeKind::Comparable)
    {
        return DomainType1.GetComparability() == EComparability::Incomparable;
    }
    if (DomainType1.GetKind() == ETypeKind::Persistable)
    {
        return !DomainType2.IsPersistable();
    }
    if (DomainType2.GetKind() == ETypeKind::Persistable)
    {
        return !DomainType1.IsPersistable();
    }
    if (DomainType1.GetKind() == ETypeKind::Variable || DomainType2.GetKind() == ETypeKind::Variable)
    {
        return false;
    }
    if (IsBottom(DomainType1) || IsBottom(DomainType2))
    {
        return false;
    }
    // Types for which `true` is a subtype (`true` being just above `false`)
    if (!IsDomainTrueDistinct(DomainType1) && !IsDomainTrueDistinct(DomainType2))
    {
        return false;
    }
    // Names types. 
    // We put this before tuples so that at the top level, named type 
    // comparison has special handling when compared against a tuple. 
    // No need to implement the same logic both in named type comparison 
    // and tuple comparison, so we do named types first.
    if (const CNamedType* NamedType1 = DomainType1.AsNullable<CNamedType>())
    {
        return IsNamedTypeDistinct(*NamedType1, DomainType2);
    }
    if (const CNamedType* NamedType2 = DomainType2.AsNullable<CNamedType>())
    {
        return IsNamedTypeDistinct(*NamedType2, DomainType1);
    }
    // Tuples for which `true` is not a subtype, i.e. non-empty tuples
    //
    // Note, only non-empty tuples are compared with other types.  Types above
    // non-empty tuples are not compared to one another, as all such types are
    // also above `true` and are handled by `IsTrueDistinct`.
    if (const CTupleType* DomainTupleType1 = DomainType1.AsNullable<CTupleType>(); DomainTupleType1 && DomainTupleType1->Num() != 0)
    {
        return IsDomainNonEmptyTupleDistinct(DomainType2, *DomainTupleType1);
    }
    if (const CTupleType* DomainTupleType2 = DomainType2.AsNullable<CTupleType>(); DomainTupleType2 && DomainTupleType2->Num() != 0)
    {
        return IsDomainNonEmptyTupleDistinct(DomainType1, *DomainTupleType2);
    }
    // Types strictly above classes
    if (!IsAnyClassDistinct(DomainType1) && !IsAnyClassDistinct(DomainType2))
    {
        return false;
    }
    // Classes and structs
    if (const CClass* DomainClass1 = DomainType1.AsNullable<CClass>())
    {
        return DomainClass1->IsStruct() ? IsStructDistinct(DomainType2, *DomainClass1) : IsClassDistinct(DomainType2, *DomainClass1);
    }
    if (const CClass* DomainClass2 = DomainType2.AsNullable<CClass>())
    {
        return DomainClass2->IsStruct() ? IsStructDistinct(DomainType1, *DomainClass2) : IsClassDistinct(DomainType1, *DomainClass2);
    }
    // Subtype types
    if (const CTypeType* DomainTypeType1 = DomainType1.AsNullable<CTypeType>())
    {
        return IsDomainTypeDistinct(DomainType2, *DomainTypeType1);
    }
    if (const CTypeType* DomainTypeType2 = DomainType2.AsNullable<CTypeType>())
    {
        return IsDomainTypeDistinct(DomainType1, *DomainTypeType2);
    }
    // Pointer types
    if (const CPointerType* DomainPointerType1 = DomainType1.AsNullable<CPointerType>())
    {
        return IsPointerDistinct(DomainType2, *DomainPointerType1);
    }
    if (const CPointerType* DomainPointerType2 = DomainType2.AsNullable<CPointerType>())
    {
        return IsPointerDistinct(DomainType1, *DomainPointerType2);
    }
    // Reference types
    if (const CReferenceType* DomainReferenceType1 = DomainType1.AsNullable<CReferenceType>())
    {
        return IsReferenceDistinct(DomainType2, *DomainReferenceType1);
    }
    if (const CReferenceType* DomainReferenceType2 = DomainType2.AsNullable<CReferenceType>())
    {
        return IsReferenceDistinct(DomainType1, *DomainReferenceType2);
    }
    // Enumerations
    if (const CEnumeration* DomainEnumeration1 = DomainType1.AsNullable<CEnumeration>())
    {
        return IsEnumerationDistinct(DomainType2, *DomainEnumeration1);
    }
    if (const CEnumeration* DomainEnumeration2 = DomainType2.AsNullable<CEnumeration>())
    {
        return IsEnumerationDistinct(DomainType1, *DomainEnumeration2);
    }
    // Ints
    if (const CIntType* DomainInt1 = DomainType1.AsNullable<CIntType>())
    {
        return IsIntDistinct(DomainType2, *DomainInt1);
    }
    if (const CIntType* DomainInt2 = DomainType2.AsNullable<CIntType>())
    {
        return IsIntDistinct(DomainType1, *DomainInt2);
    }
    // Floats
    if (const CFloatType* DomainFloat1 = DomainType1.AsNullable<CFloatType>())
    {
        return IsFloatDistinct(DomainType2, *DomainFloat1);
    }
    if (const CFloatType* DomainFloat2 = DomainType2.AsNullable<CFloatType>())
    {
        return IsFloatDistinct(DomainType1 , *DomainFloat2);
    }

    return true;
}

bool AreDomainsDistinct(const CTypeBase* DomainType1, const CTypeBase* DomainType2)
{
    ULANG_ASSERTF(DomainType1 && DomainType2, "Expected non-null arguments to AreTypesDistinct");
    return AreDomainsDistinct(DomainType1->GetNormalType(), DomainType2->GetNormalType());
}
}

bool SemanticTypeUtils::AreDomainsDistinct(const CTypeBase* DomainType1, const CTypeBase* DomainType2)
{
    ULANG_ASSERTF(DomainType1 && DomainType2, "Expected non-null arguments to AreTypesDistinct");
    ULANG_ASSERTF(
        &DomainType1->GetProgram() == &DomainType2->GetProgram(),
        "Types '%s' and '%s' are from different programs",
        DomainType1->AsCode().AsCString(),
        DomainType2->AsCode().AsCString());
    return uLang::AreDomainsDistinct(DomainType1, DomainType2);
}

namespace
{
bool IsUnknownTypeImpl(const CTypeBase* Type, TSet<const CFlowType*>& VisitedFlowTypes)
{
    ULANG_ASSERTF(Type, "Queried for types should never be null -- we should be using CUnknownType instead.");
    if (const CFlowType* FlowType = Type->AsFlowType())
    {
        if (VisitedFlowTypes.Contains(FlowType))
        {
            return false;
        }
        VisitedFlowTypes.Insert(FlowType);
        if (IsUnknownTypeImpl(FlowType->GetChild(), VisitedFlowTypes))
        {
            return true;
        }
        return false;
    }
    const CNormalType& NormalType = Type->GetNormalType();
    if (const CPointerType* PointerType = NormalType.AsNullable<CPointerType>())
    {
        // A pointer type is an unknown type if its value type is an unknown type.
        return IsUnknownTypeImpl(PointerType->NegativeValueType(), VisitedFlowTypes)
            || IsUnknownTypeImpl(PointerType->PositiveValueType(), VisitedFlowTypes);
    }
    else if (const CReferenceType* ReferenceType = NormalType.AsNullable<CReferenceType>())
    {
        // A reference type is an unknown type if its value type is an unknown type.
        return IsUnknownTypeImpl(ReferenceType->NegativeValueType(), VisitedFlowTypes)
            || IsUnknownTypeImpl(ReferenceType->PositiveValueType(), VisitedFlowTypes);
    }
    else if (const CArrayType* ArrayType = NormalType.AsNullable<CArrayType>())
    {
        // An array type is an unknown type if its element type is an unknown type.
        return IsUnknownTypeImpl(ArrayType->GetElementType(), VisitedFlowTypes);
    }
    else if (const CMapType* MapType = NormalType.AsNullable<CMapType>())
    {
        // An map type is an unknown type if either its key type or element type is an unknown type.
        return IsUnknownTypeImpl(MapType->GetKeyType(), VisitedFlowTypes)
            || IsUnknownTypeImpl(MapType->GetValueType(), VisitedFlowTypes);
    }
    else if (const COptionType* OptionType = NormalType.AsNullable<COptionType>())
    {
        // An option type is an unknown type if its value type is an unknown type.
        return IsUnknownTypeImpl(OptionType->GetValueType(), VisitedFlowTypes);
    }
    else if (const CTupleType* TupleType = NormalType.AsNullable<CTupleType>())
    {
        // A tuple type is an unknown type if any of its elements is an unknown type.
        for (int32_t ParamIndex = 0; ParamIndex < TupleType->Num(); ++ParamIndex)
        {
            if (IsUnknownTypeImpl((*TupleType)[ParamIndex], VisitedFlowTypes))
            {
                return true;
            }
        }
        return false;
    }
    else if (const CFunctionType* FunctionType = NormalType.AsNullable<CFunctionType>())
    {
        // A function type is an unknown type if either its return or parameter type is an unknown type.
        return IsUnknownTypeImpl(&FunctionType->GetParamsType(), VisitedFlowTypes)
            || IsUnknownTypeImpl(&FunctionType->GetReturnType(), VisitedFlowTypes);
    }
    else
    {
        return NormalType.IsA<CUnknownType>();
    }
}
}

bool SemanticTypeUtils::IsUnknownType(const CTypeBase* Type)
{
    TSet<const CFlowType*> VisitedFlowTypes;
    return IsUnknownTypeImpl(Type, VisitedFlowTypes);
}

bool SemanticTypeUtils::IsAttributeType(const CTypeBase* Type)
{
    if (const CClass* Class = Type->GetNormalType().AsNullable<CClass>())
    {
        return Class->IsClass(*Type->GetProgram()._attributeClass);
    }
    else
    {
        return false;
    }
}

void SemanticTypeUtils::VisitAllDefinitions(const CTypeBase* Type, const TFunction<void(const CDefinition&,const CSymbol&)>& Functor)
{
    const CNormalType& NormalType = Type->GetNormalType();
    switch (NormalType.GetKind())
    {
    case ETypeKind::Unknown:
    case ETypeKind::False:
    case ETypeKind::True:
    case ETypeKind::Void:
    case ETypeKind::Logic:
    case ETypeKind::Int:
    case ETypeKind::Rational:
    case ETypeKind::Float:
    case ETypeKind::Char8:
    case ETypeKind::Char32:
    case ETypeKind::Path:
    case ETypeKind::Range:
    case ETypeKind::Any:
    case ETypeKind::Comparable:
    case ETypeKind::Persistable:
        return;

    case ETypeKind::Interface:
    {
        const CInterface* Interface = NormalType.AsChecked<CInterface>()._GeneralizedInterface;
        Functor(*Interface->Definition(), Interface->Definition()->GetName());
        return;
    }

    case ETypeKind::Class:
    case ETypeKind::Module:
    case ETypeKind::Enumeration:
    case ETypeKind::Variable:
    {
        const CNominalType& NominalType = *NormalType.AsNominalType();
        Functor(*NominalType.Definition(), NominalType.Definition()->GetName());
        return;
    }

    case ETypeKind::Array:
        VisitAllDefinitions(NormalType.AsChecked<CArrayType>().GetElementType(), Functor);
        return;

    case ETypeKind::Generator:
        VisitAllDefinitions(NormalType.AsChecked<CGeneratorType>().GetElementType(), Functor);
        return;

    case ETypeKind::Map:
    {
        const CMapType& MapType = NormalType.AsChecked<CMapType>();
        VisitAllDefinitions(MapType.GetKeyType(), Functor);
        VisitAllDefinitions(MapType.GetValueType(), Functor);
        return;
    }

    case ETypeKind::Pointer:
    {
        const CPointerType& PointerType = NormalType.AsChecked<CPointerType>();
        VisitAllDefinitions(PointerType.NegativeValueType(), Functor);
        VisitAllDefinitions(PointerType.PositiveValueType(), Functor);
        return;
    }

    case ETypeKind::Reference:
    {
        const CReferenceType& ReferenceType = NormalType.AsChecked<CReferenceType>();
        VisitAllDefinitions(ReferenceType.NegativeValueType(), Functor);
        VisitAllDefinitions(ReferenceType.PositiveValueType(), Functor);
        return;
    }

    case ETypeKind::Option:
        VisitAllDefinitions(NormalType.AsChecked<COptionType>().GetValueType(), Functor);
        return;

    case ETypeKind::Type:
    {
        const CTypeType& TypeType = NormalType.AsChecked<CTypeType>();
        if (TypeType.PositiveType() == &TypeType.GetProgram()._anyType)
        {
            // If `supertype`, visit the negative type.
            VisitAllDefinitions(TypeType.NegativeType(), Functor);
        }
        else
        {
            // Otherwise, assume either the negative type is `false` or the
            // negative equivalent of `PositiveType`.
            VisitAllDefinitions(TypeType.PositiveType(), Functor);
        }
        return;
    }

    case ETypeKind::Tuple:
    {
        const CTupleType& TupleType = NormalType.AsChecked<CTupleType>();
        for (const CTypeBase* ElementType : TupleType.GetElements())
        {
            VisitAllDefinitions(ElementType, Functor);
        }
        return;
    }

    case ETypeKind::Function:
    {
        const CFunctionType& FunctionType = NormalType.AsChecked<CFunctionType>();
        VisitAllDefinitions(&FunctionType.GetParamsType(), Functor);
        VisitAllDefinitions(&FunctionType.GetReturnType(), Functor);
        return;
    }

    case ETypeKind::Named:
    {
        const CNamedType& NamedType = NormalType.AsChecked<CNamedType>();
        VisitAllDefinitions(NamedType.GetValueType(), Functor);
        return;
    }

    default:
        ULANG_UNREACHABLE();
    }
}

void SemanticTypeUtils::ForEachDataType(const CTypeBase* Type, const TFunction<void(const CTypeBase*)>& F)
{
    const CNormalType& NormalType = Type->GetNormalType();
    switch (NormalType.GetKind())
    {
    case ETypeKind::Unknown:
    case ETypeKind::False:
    case ETypeKind::True:
    case ETypeKind::Void:
    case ETypeKind::Any:
    case ETypeKind::Comparable:
    case ETypeKind::Logic:
    case ETypeKind::Int:
    case ETypeKind::Rational:
    case ETypeKind::Float:
    case ETypeKind::Char8:
    case ETypeKind::Char32:
    case ETypeKind::Path:
    case ETypeKind::Range:
    case ETypeKind::Type:
    case ETypeKind::Enumeration:
    case ETypeKind::Function:
    case ETypeKind::Variable:
    case ETypeKind::Persistable:
        return;

    case ETypeKind::Class:
    case ETypeKind::Module:
    case ETypeKind::Interface:
    {
        const CNominalType* NominalType = NormalType.AsNominalType();
        ULANG_ASSERTF(NominalType, "Failed to cast to NominalType.");
        const CLogicalScope* LogicalScope = NominalType->Definition()->DefinitionAsLogicalScopeNullable();
        ULANG_ASSERTF(LogicalScope, "Failed to cast to LogicalScope");
        for (const TSRef<CDefinition>& Definition : LogicalScope->GetDefinitions())
        {
            if (const CDataDefinition* DataDefinition = Definition->AsNullable<CDataDefinition>())
            {
                F(DataDefinition->GetType());
            }
        }
        return;
    }

    case ETypeKind::Array:
        F(NormalType.AsChecked<CArrayType>().GetElementType());
        return;

    case ETypeKind::Generator:
        F(NormalType.AsChecked<CGeneratorType>().GetElementType());
        return;

    case ETypeKind::Map:
    {
        const CMapType& MapType = NormalType.AsChecked<CMapType>();
        F(MapType.GetKeyType());
        F(MapType.GetValueType());
        return;
    }

    case ETypeKind::Pointer:
    {
        const CPointerType& PointerType = NormalType.AsChecked<CPointerType>();
        F(PointerType.NegativeValueType());
        F(PointerType.PositiveValueType());
        return;
    }

    case ETypeKind::Reference:
    {
        const CReferenceType& ReferenceType = NormalType.AsChecked<CReferenceType>();
        F(ReferenceType.NegativeValueType());
        F(ReferenceType.PositiveValueType());
        return;
    }

    case ETypeKind::Option:
        F(NormalType.AsChecked<COptionType>().GetValueType());
        return;

    case ETypeKind::Tuple:
    {
        const CTupleType& TupleType = NormalType.AsChecked<CTupleType>();
        for (const CTypeBase* ElementType : TupleType.GetElements())
        {
            F(ElementType);
        }
        return;
    }

    case ETypeKind::Named:
    {
        const CNamedType& NamedType = NormalType.AsChecked<CNamedType>();
        F(NamedType.GetValueType());
        return;
    }

    default:
        ULANG_UNREACHABLE();
    }
}

static void ForEachDataTypeRecursiveImpl(const CTypeBase* Type, const TFunction<void(const CTypeBase*)>& F, TArray<const CTypeBase*>& Visited)
{
    if (Visited.Contains(Type))
    {
        return;
    }
    Visited.Add(Type);
    F(Type);
    SemanticTypeUtils::ForEachDataType(Type, [&](const CTypeBase* DataType)
    {
        ForEachDataTypeRecursiveImpl(DataType, F, Visited);
    });
}

void SemanticTypeUtils::ForEachDataTypeRecursive(const CTypeBase* Type, const TFunction<void(const CTypeBase*)>& F)
{
    TArray<const CTypeBase*> Visited;
    ForEachDataTypeRecursiveImpl(Type, F, Visited);
}

namespace
{
SemanticTypeUtils::EIsEditable Combine(SemanticTypeUtils::EIsEditable lhs, SemanticTypeUtils::EIsEditable rhs)
{
    return (lhs != SemanticTypeUtils::EIsEditable::Yes ? lhs : rhs);
}
}

const char* SemanticTypeUtils::IsEditableToCMessage(EIsEditable IsEditable)
{
    switch (IsEditable)
    {
    case SemanticTypeUtils::EIsEditable::NotEditableType:
        return "The editable attribute is not supported for data definitions of this type.";
    case SemanticTypeUtils::EIsEditable::MissingConcrete:
        return "The editable attribute is not supported for classes that aren't concrete.";
    case SemanticTypeUtils::EIsEditable::MissingConcreteOrEditable:
        return "The editable_non_concrete attribute is not supported for classes that are either not concrete or have uninitialized members that are not editable.";
    case SemanticTypeUtils::EIsEditable::Yes:
        return "The editable attribute can be used here.";
    default:
        ULANG_UNREACHABLE();
    }
}

SemanticTypeUtils::EIsEditable SemanticTypeUtils::IsEditableType(const uLang::CTypeBase* Type, bool bAllowNonConcreteClasses)
{
    using namespace uLang;

    const CNormalType& NormalType = Type->GetNormalType();
    if (NormalType.GetKind() == Cases<
        ETypeKind::Logic,
        //		ETypeKind::Char8,   Not supported since it would show up as unsigned 8-bit integer
        //		ETypeKind::Char32,  Not supported since it would show up as unsigned 32-bit integer
        ETypeKind::Int,
        ETypeKind::Float,
        ETypeKind::Enumeration>)
    {
        return EIsEditable::Yes;
    }
    else if (IsStringType(NormalType))
    {
        return EIsEditable::Yes;
    }
    else if (const CArrayType* ArrayType = NormalType.AsNullable<CArrayType>())
    {
        return IsEditableType(ArrayType->GetElementType(), bAllowNonConcreteClasses);
    }
    else if (const CMapType* MapType = NormalType.AsNullable<CMapType>())
    {
        return Combine(IsEditableType(MapType->GetKeyType(), bAllowNonConcreteClasses),
            IsEditableType(MapType->GetValueType(), bAllowNonConcreteClasses));
    }
    else if (const CPointerType* PointerType = NormalType.AsNullable<CPointerType>())
    {
        return Combine(IsEditableType(PointerType->PositiveValueType(), bAllowNonConcreteClasses),
            IsEditableType(PointerType->NegativeValueType(), bAllowNonConcreteClasses));
    }
    else if (const CTypeType* TypeType = NormalType.AsNullable<CTypeType>())
    {
        // We don't allow the type of `any` as this doesn't have clear use cases as an @editable yet (ie: identifier:type is not @editable)
        return TypeType->PositiveType()->GetNormalType().GetKind() != ETypeKind::Any ? EIsEditable::Yes : EIsEditable::NotEditableType;
    }
    else if (const CClass* Class = NormalType.AsNullable<CClass>())
    {
        return (bAllowNonConcreteClasses || Class->IsConcrete()) ? EIsEditable::Yes : EIsEditable::MissingConcrete;
    }
    else if (NormalType.IsA<CInterface>())
    {
        return bAllowNonConcreteClasses ? EIsEditable::Yes : EIsEditable::NotEditableType;
    }
    else if (const CTupleType* Tuple = NormalType.AsNullable<CTupleType>())
    {
        for (const CTypeBase* ElementType : Tuple->GetElements())
        {
            EIsEditable Result = IsEditableType(ElementType, bAllowNonConcreteClasses);
            if (Result != EIsEditable::Yes)
            {
                return Result;
            }
        }
        return EIsEditable::Yes;
    }
    else if (const COptionType* OptionType = NormalType.AsNullable<COptionType>())
    {
        // Optional types are allowed-editable if their internal value type is allowed.
        if (const uLang::CTypeBase* ValueType = OptionType->GetValueType())
        {
            return IsEditableType(ValueType, bAllowNonConcreteClasses);
        }
    }
    return EIsEditable::NotEditableType;
}

} // namespace uLang
