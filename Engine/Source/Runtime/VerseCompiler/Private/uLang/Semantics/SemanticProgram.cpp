// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/SemanticProgram.h"

#include "uLang/Common/Algo/AllOf.h"
#include "uLang/Common/Text/FilePathUtils.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/TypeAlias.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Semantics/VisitStamp.h"
#include "uLang/SourceProject/VerseVersion.h"

#include <initializer_list>

namespace uLang
{
//=======================================================================================
// CModule
//=======================================================================================

CModule::CModule(const CSymbol& Name, CScope& EnclosingScope)
    : CDefinition(StaticDefinitionKind, EnclosingScope, Name)
    , CNominalType(StaticTypeKind, EnclosingScope.GetProgram())
    , CLogicalScope(CScope::EKind::Module, &EnclosingScope, EnclosingScope.GetProgram())
{
}

CModulePart& CModule::CreatePart(CScope* ParentScope, bool bExplicitDefinition)
{
    return *_Parts[_Parts.AddNew(*this, ParentScope, bExplicitDefinition, CScope::GetProgram())];
}

bool CModule::IsExplicitDefinition() const
{
    // A module definition is explicit if any of its parts was explicitly defined
    for (const CModulePart* Part : _Parts)
    {
        if (Part->IsExplicitDefinition())
        {
            return true;
        }
    }

    return false;
}

SmallDefinitionArray CModule::FindInstanceMember(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage, VisitStampType VisitStamp) const
{
    if (TryMarkVisited(VisitStamp))
    {
        return FindDefinitions(Name, Origin, Qualifier, ContextPackage, VisitStamp);
    }
    return SmallDefinitionArray();
}

void CModule::MarkPersistenceCompatConstraint() const
{
    if (IsPersistenceCompatConstraint())
    {
        return;
    }
    _bPersistenceCompatConstraint = true;
    if (_Parent)
    {
        if (const CModule* ParentModule = _Parent->GetModule())
        {
            ParentModule->MarkPersistenceCompatConstraint();
        }
    }
}

SmallDefinitionArray CModule::FindDefinitions(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage, VisitStampType VisitStamp) const
{
    SmallDefinitionArray Definitions = CLogicalScope::FindDefinitions(Name, Origin, Qualifier, ContextPackage, VisitStamp);
    if (GetConstrainedDefinition() && Origin != EMemberOrigin::Original)
    {
        if (const CModule* ConstrainedModule = GetConstrainedDefinition()->AsNullable<CModule>())
        {
            Definitions.Append(ConstrainedModule->FindDefinitions(Name, Origin, Qualifier, ContextPackage, VisitStamp));
        }
    }
    return Definitions;
}

//=======================================================================================
// CIntrinsicSymbols
//=======================================================================================

void CIntrinsicSymbols::Initialize(CSymbolTable& Symbols)
{
#define OPERATOR_OP_NAME_PREFIX "operator'"
#define PREFIX_OP_NAME_PREFIX "prefix'"
#define POSTFIX_OP_NAME_PREFIX "postfix'"
#define OP_NAME_SUFFIX "'"
#define OPERATOR_OP_NAME(NAME) OPERATOR_OP_NAME_PREFIX #NAME OP_NAME_SUFFIX
#define PREFIX_OP_NAME(NAME) PREFIX_OP_NAME_PREFIX #NAME OP_NAME_SUFFIX
    _OperatorOpNamePrefix = OPERATOR_OP_NAME_PREFIX;
    _PrefixOpNamePrefix = PREFIX_OP_NAME_PREFIX;
    _PostfixOpNamePrefix = POSTFIX_OP_NAME_PREFIX;
    _OpNameSuffix = OP_NAME_SUFFIX;
    _OpNameNegate = Symbols.AddChecked(PREFIX_OP_NAME(-));
    _OpNameAdd = Symbols.AddChecked(OPERATOR_OP_NAME(+));
    _OpNameSub = Symbols.AddChecked(OPERATOR_OP_NAME(-));
    _OpNameMul = Symbols.AddChecked(OPERATOR_OP_NAME(*));
    _OpNameDiv = Symbols.AddChecked(OPERATOR_OP_NAME(/));
    _OpNameLess = Symbols.AddChecked(OPERATOR_OP_NAME(<));
    _OpNameLessEqual = Symbols.AddChecked(OPERATOR_OP_NAME(<=));
    _OpNameGreater = Symbols.AddChecked(OPERATOR_OP_NAME(>));
    _OpNameGreaterEqual = Symbols.AddChecked(OPERATOR_OP_NAME(>=));
    _OpNameEqual = Symbols.AddChecked(OPERATOR_OP_NAME(=));
    _OpNameNotEqual = Symbols.AddChecked(OPERATOR_OP_NAME(<>));
    _OpNameAddRMW = Symbols.AddChecked(OPERATOR_OP_NAME(+=));
    _OpNameSubRMW = Symbols.AddChecked(OPERATOR_OP_NAME(-=));
    _OpNameMulRMW = Symbols.AddChecked(OPERATOR_OP_NAME(*=));
    _OpNameDivRMW = Symbols.AddChecked(OPERATOR_OP_NAME(/=));
    _OpNameCall = Symbols.AddChecked(OPERATOR_OP_NAME(()));
    _OpNameQuery = Symbols.AddChecked(OPERATOR_OP_NAME(?));
#undef OPERATOR_OP_NAME
#undef PREFIX_OP_NAME
#undef OPERATOR_OP_NAME_PREFIX
#undef PREFIX_OP_NAME_PREFIX
#undef POSTFIX_OP_NAME_PREFIX
#undef OP_NAME_SUFFIX
    _FuncNameAbs = Symbols.AddChecked("Abs");
    _FuncNameCeil = Symbols.AddChecked("Ceil");
    _FuncNameFloor = Symbols.AddChecked("Floor");
    _FuncNameWeakMap = Symbols.AddChecked("weak_map");
    _FuncNameFitsInPlayerMap = Symbols.AddChecked("FitsInPlayerMap");
    _FieldNameLength = Symbols.AddChecked("Length");
    _Wildcard = Symbols.AddChecked("_");
    _Inf = Symbols.AddChecked("Inf");
    _NaN = Symbols.AddChecked("NaN");
    _ExtensionFieldPrefix = "operator'.";
    _ExtensionFieldSuffix = "'";

    // @available
    _MinUploadedAtFNVersion = Symbols.AddChecked("MinUploadedAtFNVersion");
}

CSymbol CIntrinsicSymbols::GetArithmeticOpName(CExprBinaryArithmetic::EOp Op) const
{
    switch (Op)
    {
    case CExprBinaryArithmetic::EOp::Add: return _OpNameAdd;
    case CExprBinaryArithmetic::EOp::Sub: return _OpNameSub;
    case CExprBinaryArithmetic::EOp::Mul: return _OpNameMul;
    case CExprBinaryArithmetic::EOp::Div: return _OpNameDiv;
    default: ULANG_UNREACHABLE();
    }
}

CSymbol CIntrinsicSymbols::GetComparisonOpName(CExprComparison::EOp Op) const
{
    switch (Op)
    {
    case CExprComparison::EOp::gt:    return _OpNameGreater;
    case CExprComparison::EOp::gteq:  return _OpNameGreaterEqual;
    case CExprComparison::EOp::lt:    return _OpNameLess;
    case CExprComparison::EOp::lteq:  return _OpNameLessEqual;
    case CExprComparison::EOp::eq:    return _OpNameEqual;
    case CExprComparison::EOp::noteq: return _OpNameNotEqual;
    default: ULANG_UNREACHABLE();
    }
}

CSymbol CIntrinsicSymbols::GetAssignmentOpName(CExprAssignment::EOp Op) const
{
    switch (Op)
    {
    case CExprAssignment::EOp::addAssign: return _OpNameAddRMW;
    case CExprAssignment::EOp::subAssign: return _OpNameSubRMW;
    case CExprAssignment::EOp::mulAssign: return _OpNameMulRMW;
    case CExprAssignment::EOp::divAssign: return _OpNameDivRMW;
    case CExprAssignment::EOp::assign: default: ULANG_UNREACHABLE();
    }
}

CUTF8String CIntrinsicSymbols::MakeExtensionFieldOpName(CSymbol Symbol) const
{
    CUTF8StringBuilder Builder;
    Builder.Append(_ExtensionFieldPrefix);
    Builder.Append(Symbol.AsStringView());
    Builder.Append(_ExtensionFieldSuffix);
    return Builder.MoveToString();
}

CUTF8StringView CIntrinsicSymbols::StripExtensionFieldOpName(CSymbol Symbol) const
{
    return Symbol.AsStringView()
        .SubViewTrimBegin(_ExtensionFieldPrefix.ByteLen())
        .SubViewTrimEnd(_ExtensionFieldSuffix.ByteLen());
}

bool CIntrinsicSymbols::IsOperatorOpName(CSymbol Name) const
{
    CUTF8StringView View = Name.AsStringView();
    return View.StartsWith(_OperatorOpNamePrefix) && View.EndsWith(_OpNameSuffix);
}

bool CIntrinsicSymbols::IsPrefixOpName(CSymbol Name) const
{
    CUTF8StringView View = Name.AsStringView();
    return View.StartsWith(_PrefixOpNamePrefix) && View.EndsWith(_OpNameSuffix);
}

bool CIntrinsicSymbols::IsPostfixOpName(CSymbol Name) const
{
    CUTF8StringView View = Name.AsStringView();
    return View.StartsWith(_PostfixOpNamePrefix) && View.EndsWith(_OpNameSuffix);
}

SmallDefinitionArray CCompatConstraintRoot::FindDefinitions(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage, VisitStampType VisitStamp) const
{
    SmallDefinitionArray Definitions = CLogicalScope::FindDefinitions(Name, Origin, Qualifier, ContextPackage, VisitStamp);
    if (Origin != EMemberOrigin::Original)
    {
        Definitions.Append(GetProgram().FindDefinitions(Name, Origin, Qualifier, ContextPackage, VisitStamp));
    }
    return Definitions;
}

//=======================================================================================
// CSemanticProgram
//=======================================================================================

void CSemanticProgram::Initialize(TSPtr<CSymbolTable> Symbols)
{
    if (!Symbols)
    {
        // Create default symbol table since a shared one was not provided
        _Symbols.SetNew();
    }
    else
    {
        _Symbols = Symbols;
    }

    _IntrinsicSymbols.Initialize(*_Symbols);

    ULANG_ASSERTF(_EpicInternalModulePrefixes.IsEmpty(), "`CSemanticProgram` should not be initialized multiple times");
    _EpicInternalModulePrefixes.Add("/Verse.org/");
    _EpicInternalModulePrefixes.Add("/UnrealEngine.com/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/AI/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Abilities/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Animation/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Assets/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/AutomatedTests/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Building/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Characters/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Devices/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Game/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/GameScript/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/FortPlayerUtilities/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/FN1Native/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Playspaces/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Prototypes/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Teams/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/TestDevices/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Vehicles/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/UI/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/Itemization/");
    _EpicInternalModulePrefixes.Add("/Fortnite.com/VerseInternalTesting/");
}

const CFunction* CSemanticProgram::GetTaskFunction() const
{
    if (!_taskFunction)
    {
        _taskFunction = FindDefinitionByVersePath<CFunction>("/Verse.org/Concurrency/task");
    }
    return _taskFunction;
}

const CClass* CSemanticProgram::GetTaskClass() const
{
    const CFunction* TaskFunction = GetTaskFunction();
    if (!TaskFunction)
    {
        return nullptr;
    }
    const CTypeBase& ReturnType = TaskFunction->_Signature.GetFunctionType()->GetReturnType();
    const CTypeType& ReturnTypeType = ReturnType.GetNormalType().AsChecked<CTypeType>();
    return &ReturnTypeType.PositiveType()->GetNormalType().AsChecked<CClass>();
}

const CTypeBase* CSemanticProgram::InstantiateTaskType(const CTypeBase* TypeArgument)
{
    const CFunction* TaskFunction = GetTaskFunction();
    if (!TaskFunction)
    {
        return nullptr;
    }
    const CFunctionType* InstTaskType = SemanticTypeUtils::Instantiate(TaskFunction->_Signature.GetFunctionType());
    // `task` does not make use of the negative part of the type argument.  Any
    // type at or above `TypeArgument` will do.
    bool bConstrained = SemanticTypeUtils::Constrain(&GetOrCreateTypeType(&_anyType, TypeArgument), &InstTaskType->GetParamsType());
    ULANG_ASSERTF(bConstrained, "Expected %s <= t for task(t)", TypeArgument->AsCode(ETypeSyntaxPrecedence::Comparison).AsCString());
    const CTypeType& ParamTypeType = InstTaskType->GetParamsType().GetNormalType().AsChecked<CTypeType>();
    const CFlowType* ParamNegativeFlowType = ParamTypeType.NegativeType()->AsFlowType();
    ULANG_ASSERTF(ParamNegativeFlowType, "Failed to cast Type.");
    // The negative part of the parameter type is now dead.  Prune flow edges
    // to improve instantiation cache.
    ParamNegativeFlowType->EmptyFlowEdges();
    const CTypeType& ReturnTypeType = InstTaskType->GetReturnType().GetNormalType().AsChecked<CTypeType>();
    return ReturnTypeType.PositiveType();
}

CSnippet& CSemanticProgram::GetOrCreateSnippet(const CSymbol& Path, CScope* ParentScope)
{
    // First, try to find the snippet
    CSnippet* Snippet = _Snippets.Find(Path);
    if (Snippet)
    {
        return *Snippet;
    }

    // Create it if not found
    TURef<CSnippet> NewSnippet = TURef<CSnippet>::New(Path, ParentScope, *this);
    Snippet = NewSnippet.Get();
    _Snippets.Add(Move(NewSnippet));
    return *Snippet;
}

CSnippet* CSemanticProgram::FindSnippet(const CUTF8StringView& NameStr) const
{
    CSnippet* FoundSnippet = nullptr;

    TOptional<CSymbol> MaybeSymbol = _Symbols->Find(NameStr);
    if (MaybeSymbol)
    {
        FoundSnippet = _Snippets.Find(*MaybeSymbol);
    }

    return FoundSnippet;
}

CArrayType& CSemanticProgram::GetOrCreateArrayType(const CTypeBase* ElementType)
{
    ULANG_ASSERTF(ElementType, "Unexpected null element type for array type");
    CArrayType* ArrayType = _ArrayTypes.Find(ElementType);
    if (!ArrayType)
    {
        TURef<CArrayType> NewArrayType = TURef<CArrayType>::New(*this, ElementType);
        ArrayType = NewArrayType;
        _ArrayTypes.Add(Move(NewArrayType));
    }
    return *ArrayType;
}

CGeneratorType& CSemanticProgram::GetOrCreateGeneratorType(const CTypeBase* ElementType)
{
    ULANG_ASSERTF(ElementType, "Unexpected null element type for generator type");
    CGeneratorType* GeneratorType = _GeneratorTypes.Find(ElementType);
    if (!GeneratorType)
    {
        TURef<CGeneratorType> NewGeneratorType = TURef<CGeneratorType>::New(*this, ElementType);
        GeneratorType = NewGeneratorType;
        _GeneratorTypes.Add(Move(NewGeneratorType));
    }
    return *GeneratorType;
}

CMapType& CSemanticProgram::GetOrCreateMapType(const CTypeBase* KeyType, const CTypeBase* ValueType)
{
    ULANG_ASSERTF(KeyType, "Unexpected null element type for map key type");
    ULANG_ASSERTF(ValueType, "Unexpected null element type for map value type");
    return GetOrCreateMapType(*KeyType, *ValueType, false);
}

CMapType& CSemanticProgram::GetOrCreateWeakMapType(const CTypeBase& KeyType, const CTypeBase& ValueType)
{
    return GetOrCreateMapType(KeyType, ValueType, true);
}

CMapType& CSemanticProgram::GetOrCreateMapType(const CTypeBase& KeyType, const CTypeBase& ValueType, bool bWeak)
{
    CMapType* MapType = _MapTypes.Find(CMapType::SKey{&KeyType, &ValueType, bWeak});
    if (!MapType)
    {
        TURef<CMapType> NewMapType = TURef<CMapType>::New(*this, KeyType, ValueType, bWeak);
        MapType = NewMapType;
        _MapTypes.Add(Move(NewMapType));
    }
    return *MapType;
}

CPointerType& CSemanticProgram::GetOrCreatePointerType(const CTypeBase* NegativeValueType, const CTypeBase* PositiveValueType)
{
    ULANG_ASSERTF(NegativeValueType, "Unexpected null value type for variable type");
    ULANG_ASSERTF(PositiveValueType, "Unexpected null value type for variable type");
    CPointerType* PointerType = _PointerTypes.Find({NegativeValueType, PositiveValueType});
    if (!PointerType)
    {
        TURef<CPointerType> NewPointerType = TURef<CPointerType>::New(*this, NegativeValueType, PositiveValueType);
        PointerType = NewPointerType;
        _PointerTypes.Add(Move(NewPointerType));
    }
    return *PointerType;
}

CReferenceType& CSemanticProgram::GetOrCreateReferenceType(const CTypeBase* NegativeValueType, const CTypeBase* PositiveValueType)
{
    ULANG_ASSERTF(NegativeValueType, "Unexpected null value type for variable type");
    ULANG_ASSERTF(PositiveValueType, "Unexpected null value type for variable type");
    CReferenceType* ReferenceType = _ReferenceTypes.Find({NegativeValueType, PositiveValueType});
    if (!ReferenceType)
    {
        TURef<CReferenceType> NewReferenceType = TURef<CReferenceType>::New(*this, NegativeValueType, PositiveValueType);
        ReferenceType = NewReferenceType;
        _ReferenceTypes.Add(Move(NewReferenceType));
    }
    return *ReferenceType;
}

COptionType& CSemanticProgram::GetOrCreateOptionType(const CTypeBase* ValueType)
{
    ULANG_ASSERTF(ValueType, "Unexpected null value type for option type");
    COptionType* OptionType = _OptTypes.Find(ValueType);
    if (!OptionType)
    {
        TURef<COptionType> NewOptionType = TURef<COptionType>::New(*this, ValueType);
        OptionType = NewOptionType;
        _OptTypes.Add(Move(NewOptionType));
    }
    return *OptionType;
}

CTypeType& CSemanticProgram::GetOrCreateTypeType(const CTypeBase* NegativeType, const CTypeBase* PositiveType)
{
    ULANG_ASSERTF(NegativeType, "Unexpected null value type for negative type");
    ULANG_ASSERTF(PositiveType, "Unexpected null value type for negative type");
    CTypeType* TypeType = _TypeTypes.Find({NegativeType, PositiveType});
    if (!TypeType)
    {
        TURef<CTypeType> NewTypeType = TURef<CTypeType>::New(*this, NegativeType, PositiveType);
        TypeType = NewTypeType;
        _TypeTypes.Add(Move(NewTypeType));
    }
    return *TypeType;
}

CTypeType& CSemanticProgram::GetOrCreateSubtypeType(const CTypeBase* NegativeType)
{
    return GetOrCreateTypeType(&_falseType, NegativeType);
}

CTupleType& CSemanticProgram::GetOrCreateTupleType(CTupleType::ElementArray&& Elements)
{
    return GetOrCreateTupleType(Move(Elements), Elements.Num());
}

CTupleType& CSemanticProgram::GetOrCreateTupleType(CTupleType::ElementArray&& Elements, int32_t FirstNamedIndex)
{
    ULANG_ASSERT(FirstNamedIndex >= 0 && FirstNamedIndex <= Elements.Num());
    if (!Elements.Num())
    {
        return _EmptyTupleType;
    }

    const CTypeBase* FirstElementType = Elements[0];
    CTupleType* PreexistingTupleType = FirstElementType->_TupleTypesStartingWithThisType.FindByPredicate([&Elements, FirstNamedIndex](const CTupleType* TupleType)->bool
    {
        return TupleType->GetElements() == Elements && TupleType->GetFirstNamedIndex() == FirstNamedIndex;
    });

    if (PreexistingTupleType)
    {
        return *PreexistingTupleType;
    }

    for (const CTypeBase* Element : Elements)
    {
        ULANG_ASSERTF(Element, "Unexpected null element type for tuple type");
    }

    const int32_t TypeIndex = FirstElementType->_TupleTypesStartingWithThisType.AddNew(*this, Move(Elements), FirstNamedIndex);
    return *FirstElementType->_TupleTypesStartingWithThisType[TypeIndex];
}

CNamedType& CSemanticProgram::GetOrCreateNamedType(CSymbol Name, const CTypeBase* ValueType, bool HasValue)
{
    CNamedType* Result = _NamedTypes.Find({Name, ValueType, HasValue});
    if (!Result)
    {
        TURef<CNamedType> NamedType = TURef<CNamedType>::New(
            *this,
            Name,
            ValueType,
            HasValue);
        Result = NamedType.Get();
        _NamedTypes.Add(Move(NamedType));
    }
    return *Result;
}

const CFunctionType& CSemanticProgram::GetOrCreateFunctionType(
    const CTypeBase& ParamsType,
    const CTypeBase& InReturnType,
    SEffectSet Effects,
    TArray<const CTypeVariable*> TypeVariables,
    bool bImplicitlySpecialized)
{
    const CFunctionType* PreexistingFuncType = ParamsType._FunctionTypesWithThisParameterType.FindByPredicate([&](const CFunctionType* FuncType)
    {
        return
            FuncType->GetEffects() == Effects && 
            &FuncType->GetParamsType() == &ParamsType &&
            &FuncType->GetReturnType() == &InReturnType &&
            FuncType->GetTypeVariables() == TypeVariables &&
            FuncType->ImplicitlySpecialized() == bImplicitlySpecialized;
    });

    if (PreexistingFuncType)
    {
        return *PreexistingFuncType;
    }

    const int32_t TypeIndex = ParamsType._FunctionTypesWithThisParameterType.AddNew(*this, ParamsType, InReturnType, Effects, Move(TypeVariables), bImplicitlySpecialized);
    return *ParamsType._FunctionTypesWithThisParameterType[TypeIndex];
}

const CIntType& CSemanticProgram::GetOrCreateConstrainedIntType(CIntType::Constraint Min, CIntType::Constraint Max)
{
    const CIntType* PreexistingConstrainedInt = _ConstrainedIntTypes.FindByPredicate([&](const CIntType* ConstrainedInt)
    {
        return Min == ConstrainedInt->GetMin() && Max == ConstrainedInt->GetMax();
    });

    if (PreexistingConstrainedInt) { return *PreexistingConstrainedInt; }

    const int32_t TypeIndex = _ConstrainedIntTypes.AddNew(*this, Min, Max);
    return *_ConstrainedIntTypes[TypeIndex];
}

const CFloatType& CSemanticProgram::GetOrCreateConstrainedFloatType(double Min, double Max)
{
    int64_t MinRanking = CMath::FloatRanking(Min);
    int64_t MaxRanking = CMath::FloatRanking(Max);
    const CFloatType* PreexistingType = _ConstrainedFloatTypes.FindByPredicate([&](const CFloatType* ConstrainedFloat)
        {
            return MinRanking == ConstrainedFloat->MinRanking() && MaxRanking == ConstrainedFloat->MaxRanking();
        });

    if (PreexistingType) { return *PreexistingType; }

    const int32_t TypeIndex = _ConstrainedFloatTypes.AddNew(*this, Min, Max, MinRanking, MaxRanking);
    return *_ConstrainedFloatTypes[TypeIndex];
}

CFlowType& CSemanticProgram::CreateFlowType(ETypePolarity Polarity)
{
    switch (Polarity)
    {
    case ETypePolarity::Positive:
        return CreatePositiveFlowType();
    case ETypePolarity::Negative:
        return CreateNegativeFlowType();
    default:
        ULANG_UNREACHABLE();
    }
}

CFlowType& CSemanticProgram::CreateFlowType(ETypePolarity Polarity, const CTypeBase* Child)
{
    int32_t I = _FlowTypes.AddNew(*this, Polarity, Child);
    return *_FlowTypes[I];
}

CInstantiatedClass& CSemanticProgram::CreateInstantiatedClass(const CClass& Class, ETypePolarity Polarity, TArray<STypeVariableSubstitution> Arguments)
{
    int32_t I = _InstantiatedClasses.AddNew(*this, Class, Polarity, Move(Arguments));
    return *_InstantiatedClasses[I];
}

CInstantiatedInterface& CSemanticProgram::CreateInstantiatedInterface(const CInterface& Interface, ETypePolarity Polarity, TArray<STypeVariableSubstitution> Arguments)
{
    int32_t I = _InstantiatedInterfaces.AddNew(*this, Interface, Polarity, Move(Arguments));
    return *_InstantiatedInterfaces[I];
}

CSemanticProgram::SExplicitTypeParam CSemanticProgram::CreateExplicitTypeParam(
    CFunction* Function,
    CSymbol DataName,
    CSymbol TypeName,
    CSymbol NegativeTypeName,
    const CTypeType* Type)
{
    // See `SemanticTypeUtils::Instantiate` and `CSemanticAnalyzerImpl::AnalyzeParam`
    // for details of the encoding of explicit type parameters as implicit type
    // parameters.
    TSRef<CTypeVariable> TypeVariable = Function->CreateTypeVariable(
        TypeName,
        Type);
    TypeVariable->_NegativeType = Type;

    CTypeType& NegativeTypeVariableType = GetOrCreateTypeType(&_falseType, TypeVariable.Get());
    TSRef<CTypeVariable> NegativeTypeVariable = Function->CreateTypeVariable(
        NegativeTypeName,
        &NegativeTypeVariableType);
    NegativeTypeVariable->_NegativeType = &NegativeTypeVariableType;

    CTypeType& DataDefinitionType = GetOrCreateTypeType(
        NegativeTypeVariable.Get(),
        NegativeTypeVariable.Get());
    TSRef<CDataDefinition> DataDefinition = Function->CreateDataDefinition(
        DataName,
        &DataDefinitionType);
    DataDefinition->_NegativeType = &DataDefinitionType;

    DataDefinition->_ImplicitParam = TypeVariable.Get();
    TypeVariable->_ExplicitParam = DataDefinition.Get();
    TypeVariable->_NegativeTypeVariable = NegativeTypeVariable;
    NegativeTypeVariable->_ExplicitParam = DataDefinition.Get();

    return {DataDefinition.Get(), TypeVariable.Get(), NegativeTypeVariable.Get()};
}

void CSemanticProgram::AddStandardAccessLevelAttributes(CAttributable* NewAccessLevel) const
{
    NewAccessLevel->AddAttributeClass(_attributeScopeModule);
    NewAccessLevel->AddAttributeClass(_attributeScopeClass);
    NewAccessLevel->AddAttributeClass(_attributeScopeStruct);
    NewAccessLevel->AddAttributeClass(_attributeScopeFunction);
    NewAccessLevel->AddAttributeClass(_attributeScopeData);
    NewAccessLevel->AddAttributeClass(_attributeScopeEnum);
    NewAccessLevel->AddAttributeClass(_attributeScopeEnumerator);
    NewAccessLevel->AddAttributeClass(_attributeScopeAttributeClass);
    NewAccessLevel->AddAttributeClass(_attributeScopeInterface);
    NewAccessLevel->AddAttributeClass(_attributeScopeName);
    NewAccessLevel->AddAttributeClass(_attributeScopeTypeDefinition);
    NewAccessLevel->AddAttributeClass(_attributeScopeClassMacro);
    NewAccessLevel->AddAttributeClass(_attributeScopeStructMacro);
    NewAccessLevel->AddAttributeClass(_attributeScopeInterfaceMacro);
    NewAccessLevel->AddAttributeClass(_attributeScopeEnumMacro);
    NewAccessLevel->AddAttributeClass(_attributeScopeVar);
    NewAccessLevel->AddAttributeClass(_attributeScopeSpecifier);
    NewAccessLevel->AddAttributeClass(_attributeScopeScopedDefinition);
}

void CSemanticProgram::PopulateCoreAPI()
{
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Ensure set up.
    if (!_Symbols)
    {
        Initialize();
    }

    if (_VerseModule)
    {
        return;
    }

    _GeneralCompatConstraintRoot = TSRef<CCompatConstraintRoot>::New(*this);
    _PersistenceCompatConstraintRoot = TSRef<CCompatConstraintRoot>::New(*this);
    _PersistenceSoftCompatConstraintRoot = TSRef<CCompatConstraintRoot>::New(*this);

    _BuiltInPackage = TSRef<CAstPackage>::New(
        "$BuiltIn",
        "/Verse.org",
        EVerseScope::PublicAPI,
        EPackageRole::External,
        Verse::Version::LatestStable,
        VerseFN::UploadedAtFNVersion::Latest,
        false, // bAllowNative
        false, // bTreatDefinitionsAsImplicit
        true   // bAllowExperimental
        );

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Create and cache built-in types

    auto MakeBuiltInModule = [&](const char* Name, CModulePart* ParentScope) -> CModulePart&
    {
        CScope* ModuleParentScope = ParentScope ? static_cast<CScope*>(ParentScope->GetModule()) : this;
        CModule& Module = ModuleParentScope->CreateModule(GetSymbols()->AddChecked(Name));
        Module.SetAccessLevel({ SAccessLevel::EKind::Public });

        CModulePart& ModulePart = Module.CreatePart(ParentScope, true);
        ModulePart.SetAstPackage(_BuiltInPackage.Get());
        return ModulePart;
    };

    CModulePart& VerseDotOrgModuleBuiltInPart = MakeBuiltInModule("Verse.org", nullptr);
    CModulePart& VerseModuleBuiltInPart = MakeBuiltInModule("Verse", &VerseDotOrgModuleBuiltInPart);
    CModulePart& NativeModuleBuiltInPart = MakeBuiltInModule("Native", &VerseDotOrgModuleBuiltInPart);

    _BuiltInPackage->_RootModule = &VerseDotOrgModuleBuiltInPart;
    _VerseModule = VerseModuleBuiltInPart.GetModule();

    AddUsingScope(_VerseModule);

    _GeneralCompatConstraintRoot->AddUsingScope(_VerseModule);
    _PersistenceCompatConstraintRoot->AddUsingScope(_VerseModule);
    _PersistenceSoftCompatConstraintRoot->AddUsingScope(_VerseModule);

    _typeType = &GetOrCreateTypeType(&_falseType, &_anyType);

    _intType = &GetOrCreateConstrainedIntType(INT64_MIN, INT64_MAX);
    _floatType = &GetOrCreateConstrainedFloatType(-INFINITY, NAN);

    // Create type aliases for the global types that are accessible by users.
    auto CreateGlobalTypeAlias = [&](const CTypeBase* Type, const char* NameOverride = nullptr) -> CTypeAlias*
    {
        CSymbol Name = _Symbols->AddChecked(NameOverride ? NameOverride : Type->AsCode());
        CTypeAlias* TypeAlias = VerseModuleBuiltInPart.CreateTypeAlias(Name);
        TypeAlias->InitType(Type, Type);
        TypeAlias->SetAccessLevel({SAccessLevel::EKind::Public});
        return TypeAlias;
    };
    _falseAlias = CreateGlobalTypeAlias(&_falseType);
    _trueAlias = CreateGlobalTypeAlias(&_trueType);
    _voidAlias = CreateGlobalTypeAlias(&_voidType);
    _anyAlias = CreateGlobalTypeAlias(&_anyType);
    _comparableAlias = CreateGlobalTypeAlias(&_comparableType);
    _logicAlias = CreateGlobalTypeAlias(&_logicType);
    _intAlias = CreateGlobalTypeAlias(_intType);
    _floatAlias = CreateGlobalTypeAlias(_floatType);
    _rationalAlias = CreateGlobalTypeAlias(&_rationalType);
    _char8Alias = CreateGlobalTypeAlias(&_char8Type);
    _char32Alias = CreateGlobalTypeAlias(&_char32Type);
    _stringAlias = CreateGlobalTypeAlias(&GetOrCreateArrayType(&_char8Type), "string");

    _typeAlias = CreateGlobalTypeAlias(_typeType);

    _DefaultUnknownType.SetNew(_Symbols->AddChecked("unknown"), VerseModuleBuiltInPart);

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Populate attributes
    
    // TODO-Verse: Consider - C# attributes have `Attribute` suffix though allow just the root. So `nativeAttribute` class would allow `[native]`.
    // Could use prefix which would ensure starting with capital: `Attr_native`
    
    auto CreateAttributeClass = [&](CModulePart& ParentScope, const char* Name, CClass* SuperClass = nullptr, SAccessLevel AccessLevel = SAccessLevel::EKind::Public) -> CClassDefinition*
    {
        CClassDefinition* Class = &ParentScope.CreateClass(_Symbols->AddChecked(Name), SuperClass);
        Class->_ConstructorEffects = EffectSets::Computes;
        Class->_bHasCyclesBroken = true;
        Class->SetAccessLevel(AccessLevel);
        return Class;
    };
    _attributeClass  = CreateAttributeClass(VerseModuleBuiltInPart, "attribute", nullptr, SAccessLevel::EKind::EpicInternal);
    {
        _attributeScopeAttribute      = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_attribute", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeSpecifier      = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_specifier", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeModule         = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_module", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeClass          = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_class", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeStruct         = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_struct", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeData           = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_data", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeFunction       = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_function", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeEnum           = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_enum", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeEnumerator     = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_enumerator", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeAttributeClass = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_attribclass", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeInterface      = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_interface", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeIdentifier     = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_identifier", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeExpression     = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_expression", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeClassMacro     = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_classmacro", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeStructMacro    = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_structmacro", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeInterfaceMacro = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_interfacemacro", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeEnumMacro      = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_enummacro", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeVar            = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_var", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeName           = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_name", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeEffect         = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_effect", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeTypeDefinition = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_typedefinition", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _attributeScopeScopedDefinition = CreateAttributeClass(VerseModuleBuiltInPart, "attribscope_scopeddefinition", _attributeClass, SAccessLevel::EKind::EpicInternal);
        _customAttributeHandler       = CreateAttributeClass(VerseModuleBuiltInPart, "customattribhandler", _attributeClass, SAccessLevel::EKind::EpicInternal);

        auto AddAttribScopeAttributes = [&](CClass* Class) -> void
        {
            Class->_Definition->AddAttributeClass(_attributeScopeAttributeClass);
            Class->_Definition->AddAttributeClass(_attributeScopeAttribute);
        };

        AddAttribScopeAttributes(_attributeScopeAttribute);
        AddAttribScopeAttributes(_attributeScopeSpecifier);
        AddAttribScopeAttributes(_attributeScopeModule);
        AddAttribScopeAttributes(_attributeScopeClass);
        AddAttribScopeAttributes(_attributeScopeStruct);
        AddAttribScopeAttributes(_attributeScopeData);
        AddAttribScopeAttributes(_attributeScopeFunction);
        AddAttribScopeAttributes(_attributeScopeEnum);
        AddAttribScopeAttributes(_attributeScopeEnumerator);
        AddAttribScopeAttributes(_attributeScopeAttributeClass);
        AddAttribScopeAttributes(_attributeScopeInterface);
        AddAttribScopeAttributes(_attributeScopeIdentifier);
        AddAttribScopeAttributes(_attributeScopeExpression);
        AddAttribScopeAttributes(_attributeScopeClassMacro);
        AddAttribScopeAttributes(_attributeScopeStructMacro);
        AddAttribScopeAttributes(_attributeScopeInterfaceMacro);
        AddAttribScopeAttributes(_attributeScopeEnumMacro);
        AddAttribScopeAttributes(_attributeScopeName);
        AddAttribScopeAttributes(_attributeScopeEffect);
        AddAttribScopeAttributes(_attributeScopeTypeDefinition);
        AddAttribScopeAttributes(_attributeScopeScopedDefinition);
        AddAttribScopeAttributes(_customAttributeHandler);
    }
    _abstractClass = CreateAttributeClass(VerseModuleBuiltInPart, "abstract", _attributeClass);
    {
        _abstractClass->_Definition->AddAttributeClass(_attributeScopeClassMacro);
        _abstractClass->_Definition->AddAttributeClass(_attributeScopeClass);
        _abstractClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }
    _finalClass = CreateAttributeClass(VerseModuleBuiltInPart, "final", _attributeClass);
    {
        // It's a bit of a hack that the classmacro scope needs to be used together with the name scope. This is to deal with the
        // fact that final is otherwise used with names.
        _finalClass->_Definition->AddAttributeClass(_attributeScopeFunction);
        _finalClass->_Definition->AddAttributeClass(_attributeScopeData);
        _finalClass->_Definition->AddAttributeClass(_attributeScopeClass);
        _finalClass->_Definition->AddAttributeClass(_attributeScopeClassMacro);
        _finalClass->_Definition->AddAttributeClass(_attributeScopeName);
        _finalClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }
    _concreteClass = CreateAttributeClass(VerseModuleBuiltInPart, "concrete", _attributeClass);
    {
        _concreteClass->_Definition->AddAttributeClass(_attributeScopeClass);
        _concreteClass->_Definition->AddAttributeClass(_attributeScopeClassMacro);
        _concreteClass->_Definition->AddAttributeClass(_attributeScopeStruct);
        _concreteClass->_Definition->AddAttributeClass(_attributeScopeStructMacro);
        _concreteClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }
    _uniqueClass = CreateAttributeClass(VerseModuleBuiltInPart, "unique", _attributeClass);
    {
        _uniqueClass->_Definition->AddAttributeClass(_attributeScopeClass);
        _uniqueClass->_Definition->AddAttributeClass(_attributeScopeClassMacro);
        _uniqueClass->_Definition->AddAttributeClass(_attributeScopeInterface);
        _uniqueClass->_Definition->AddAttributeClass(_attributeScopeInterfaceMacro);
        _uniqueClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }
    _intrinsicClass = CreateAttributeClass(VerseModuleBuiltInPart, "intrinsic", _attributeClass, SAccessLevel::EKind::Private);
    {
        _intrinsicClass->_Definition->AddAttributeClass(_attributeScopeFunction);
        _intrinsicClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }
    _nativeClass = CreateAttributeClass(VerseModuleBuiltInPart, "native", _attributeClass, SAccessLevel::EKind::EpicInternal);
    {
        _nativeClass->_Definition->AddAttributeClass(_attributeScopeClass);
        _nativeClass->_Definition->AddAttributeClass(_attributeScopeStruct);
        _nativeClass->_Definition->AddAttributeClass(_attributeScopeFunction);
        _nativeClass->_Definition->AddAttributeClass(_attributeScopeData);
        _nativeClass->_Definition->AddAttributeClass(_attributeScopeEnum);
        _nativeClass->_Definition->AddAttributeClass(_attributeScopeEnumerator);
        _nativeClass->_Definition->AddAttributeClass(_attributeScopeAttributeClass);
        _nativeClass->_Definition->AddAttributeClass(_attributeScopeInterface);
        _nativeClass->_Definition->AddAttributeClass(_attributeScopeName);
        _nativeClass->_Definition->AddAttributeClass(_attributeScopeTypeDefinition);
        _nativeClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }
    _nativeCallClass = CreateAttributeClass(VerseModuleBuiltInPart, "native_callable", _attributeClass, SAccessLevel::EKind::EpicInternal);
    {
        _nativeCallClass->_Definition->AddAttributeClass(_attributeScopeFunction);
        _nativeCallClass->_Definition->AddAttributeClass(_attributeScopeName);
        _nativeCallClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }
    _constructorClass = CreateAttributeClass(VerseModuleBuiltInPart, "constructor", _attributeClass);
    {
        _constructorClass->_Definition->AddAttributeClass(_attributeScopeFunction);
        _constructorClass->_Definition->AddAttributeClass(_attributeScopeName);
        _constructorClass->_Definition->AddAttributeClass(_attributeScopeIdentifier);
        _constructorClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }
    _overrideClass = CreateAttributeClass(VerseModuleBuiltInPart, "override", _attributeClass);
    {
        _overrideClass->_Definition->AddAttributeClass(_attributeScopeFunction);
        _overrideClass->_Definition->AddAttributeClass(_attributeScopeData);
        _overrideClass->_Definition->AddAttributeClass(_attributeScopeName);
        _overrideClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }

    auto MakeEffectAttributeClass = [&](const char* Name) -> CClassDefinition*
    {
        CClassDefinition* Result = CreateAttributeClass(VerseModuleBuiltInPart, Name, _attributeClass);
        Result->AddAttributeClass(_attributeScopeFunction);
        Result->AddAttributeClass(_attributeScopeClass);
        Result->AddAttributeClass(_attributeScopeStruct);
        Result->AddAttributeClass(_attributeScopeAttributeClass);
        Result->AddAttributeClass(_attributeScopeEffect);
        Result->AddAttributeClass(_attributeScopeSpecifier);
        return Result;
    };
    auto MakeAccessLevelAttributeClass = [&](const char* Name, SAccessLevel AccessLevel = SAccessLevel::EKind::Public) -> CClassDefinition*
    {
        CClassDefinition* Result = CreateAttributeClass(VerseModuleBuiltInPart, Name, _attributeClass, AccessLevel);
        AddStandardAccessLevelAttributes(Result);
        return Result;
    };

    _suspendsClass         = MakeEffectAttributeClass("suspends");
    _decidesClass          = MakeEffectAttributeClass("decides");
    _variesClassDeprecated = MakeEffectAttributeClass("varies");
    _computesClass         = MakeEffectAttributeClass("computes");
    _convergesClass        = MakeEffectAttributeClass("converges");
    _transactsClass        = MakeEffectAttributeClass("transacts");
    _readsClass            = MakeEffectAttributeClass("reads");
    _writesClass           = MakeEffectAttributeClass("writes");
    _allocatesClass        = MakeEffectAttributeClass("allocates");

    _publicClass       = MakeAccessLevelAttributeClass("public");
    _privateClass      = MakeAccessLevelAttributeClass("private");
    _protectedClass    = MakeAccessLevelAttributeClass("protected");
    _internalClass     = MakeAccessLevelAttributeClass("internal");
    _scopedClass       = MakeAccessLevelAttributeClass("scoped");
    _epicInternalClass = MakeAccessLevelAttributeClass("epic_internal", SAccessLevel::EKind::EpicInternal);

    PopulateEffectDescriptorTable();

    _localizes = CreateAttributeClass(VerseModuleBuiltInPart, "localizes", _attributeClass, SAccessLevel::EKind::Public);
    {
        _localizes->_Definition->AddAttributeClass(_attributeScopeName);
        _localizes->_Definition->AddAttributeClass(_attributeScopeData);
        _localizes->_Definition->AddAttributeClass(_attributeScopeSpecifier);
        _localizes->_Definition->AddAttributeClass(_attributeScopeFunction);
    }

    _ignore_unreachable = CreateAttributeClass(VerseModuleBuiltInPart, "ignore_unreachable", _attributeClass, SAccessLevel::EKind::EpicInternal);
    {
        _ignore_unreachable->_Definition->AddAttributeClass(_attributeScopeExpression);
        _ignore_unreachable->_Definition->AddAttributeClass(_attributeScopeAttribute);
    }

    _availableClass = CreateAttributeClass(NativeModuleBuiltInPart, "available", _attributeClass, SAccessLevel::EKind::EpicInternal);
    {
        _availableClass->_Definition->AddAttributeClass(_attributeScopeClass);
        _availableClass->_Definition->AddAttributeClass(_attributeScopeStruct);
        _availableClass->_Definition->AddAttributeClass(_attributeScopeData);
        _availableClass->_Definition->AddAttributeClass(_attributeScopeFunction);
        _availableClass->_Definition->AddAttributeClass(_attributeScopeEnum);
        _availableClass->_Definition->AddAttributeClass(_attributeScopeEnumerator);
        _availableClass->_Definition->AddAttributeClass(_attributeScopeInterface);
        _availableClass->_Definition->AddAttributeClass(_attributeScopeAttribute);
        _availableClass->_Definition->AddAttributeClass(_attributeScopeTypeDefinition);
        // TODO: Modules are unique in that multiple modules with the same name are coalesced (CModulePart),
        // TODO: but the attributes are not combined in a meaningful way. We can't support that until the 
        // TODO: module-parts can retain their own @available versioning.
        //_availableClass->_Definition->AddAttributeClass(_attributeScopeModule);

        TSPtr<CDataDefinition> availableMinUploadedAtFNVersion = _availableClass->_Definition->CreateDataDefinition(_IntrinsicSymbols._MinUploadedAtFNVersion, _intType);
        availableMinUploadedAtFNVersion->_NegativeType = _intType;
        availableMinUploadedAtFNVersion->SetAccessLevel(SAccessLevel(SAccessLevel::EKind::Public));
        availableMinUploadedAtFNVersion->SetHasInitializer();
    }

    _deprecatedClass = CreateAttributeClass(VerseModuleBuiltInPart, "deprecated", _attributeClass, SAccessLevel::EKind::EpicInternal);
    {
        _deprecatedClass->_Definition->AddAttributeClass(_attributeScopeClass);
        _deprecatedClass->_Definition->AddAttributeClass(_attributeScopeStruct);
        _deprecatedClass->_Definition->AddAttributeClass(_attributeScopeData);
        _deprecatedClass->_Definition->AddAttributeClass(_attributeScopeFunction);
        _deprecatedClass->_Definition->AddAttributeClass(_attributeScopeEnum);
        _deprecatedClass->_Definition->AddAttributeClass(_attributeScopeEnumerator);
        _deprecatedClass->_Definition->AddAttributeClass(_attributeScopeInterface);
        _deprecatedClass->_Definition->AddAttributeClass(_attributeScopeAttribute);
        _deprecatedClass->_Definition->AddAttributeClass(_attributeScopeTypeDefinition);
    }

    _experimentalClass = CreateAttributeClass(VerseModuleBuiltInPart, "experimental", _attributeClass, SAccessLevel::EKind::EpicInternal);
    {
        _experimentalClass->_Definition->AddAttributeClass(_attributeScopeClass);
        _experimentalClass->_Definition->AddAttributeClass(_attributeScopeStruct);
        _experimentalClass->_Definition->AddAttributeClass(_attributeScopeData);
        _experimentalClass->_Definition->AddAttributeClass(_attributeScopeFunction);
        _experimentalClass->_Definition->AddAttributeClass(_attributeScopeEnum);
        _experimentalClass->_Definition->AddAttributeClass(_attributeScopeEnumerator);
        _experimentalClass->_Definition->AddAttributeClass(_attributeScopeInterface);
        _experimentalClass->_Definition->AddAttributeClass(_attributeScopeTypeDefinition);
        _experimentalClass->_Definition->AddAttributeClass(_attributeScopeAttribute);
    }

    _persistentClass = CreateAttributeClass(VerseModuleBuiltInPart, "persistent", _attributeClass, SAccessLevel::EKind::EpicInternal);
    {
        _persistentClass->_Definition->AddAttributeClass(_attributeScopeClass);
        _persistentClass->_Definition->AddAttributeClass(_attributeScopeClassMacro);
        _persistentClass->_Definition->AddAttributeClass(_attributeScopeStruct);
        _persistentClass->_Definition->AddAttributeClass(_attributeScopeStructMacro);
        _persistentClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }

    _persistableClass = CreateAttributeClass(VerseModuleBuiltInPart, "persistable", _attributeClass, SAccessLevel::EKind::Public);
    {
        _persistableClass->_Definition->AddAttributeClass(_attributeScopeClass);
        _persistableClass->_Definition->AddAttributeClass(_attributeScopeClassMacro);
        _persistableClass->_Definition->AddAttributeClass(_attributeScopeStruct);
        _persistableClass->_Definition->AddAttributeClass(_attributeScopeStructMacro);
        _persistableClass->_Definition->AddAttributeClass(_attributeScopeEnum);
        _persistableClass->_Definition->AddAttributeClass(_attributeScopeEnumMacro);
        _persistableClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }

    _moduleScopedVarWeakMapKeyClass = CreateAttributeClass(VerseModuleBuiltInPart, "module_scoped_var_weak_map_key", _attributeClass, SAccessLevel::EKind::EpicInternal);
    {
        _moduleScopedVarWeakMapKeyClass->_Definition->AddAttributeClass(_attributeScopeClass);
        _moduleScopedVarWeakMapKeyClass->_Definition->AddAttributeClass(_attributeScopeClassMacro);
        _moduleScopedVarWeakMapKeyClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }

    _getterClass = CreateAttributeClass(VerseModuleBuiltInPart, "getter_attribute", _attributeClass, SAccessLevel::EKind::EpicInternal);
    {
        _getterClass->_Definition->AddAttributeClass(_attributeScopeData);
        _getterClass->_Definition->AddAttributeClass(_attributeScopeName);
        _getterClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }
    _setterClass = CreateAttributeClass(VerseModuleBuiltInPart, "setter_attribute", _attributeClass, SAccessLevel::EKind::EpicInternal);
    {
        _setterClass->_Definition->AddAttributeClass(_attributeScopeData);
        _setterClass->_Definition->AddAttributeClass(_attributeScopeName);
        _setterClass->_Definition->AddAttributeClass(_attributeScopeSpecifier);
    }

    // TODO-Verse: Likely future attributes:
    // - [deprecated] - gives warning when used

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Populate intrinsic operators

    const CSymbol ValName = _Symbols->AddChecked("Value");
    const CSymbol LhsName = _Symbols->AddChecked("Lhs");
    const CSymbol RhsName = _Symbols->AddChecked("Rhs");

    struct STypedName
    {
        CSymbol Name;
        const CTypeBase* Type;
    };

    auto CreateFunction = [this, &VerseModuleBuiltInPart](CSymbol FunctionName, std::initializer_list<STypedName> Params, auto&&... Args) -> CFunction*
    {
        TSRef<CFunction> NewFunction = VerseModuleBuiltInPart.CreateFunction(FunctionName);
        const CTypeBase* ParamsType;
        std::size_t NumParams = Params.size();
        if (NumParams == 1)
        {
            ParamsType = Params.begin()->Type;
        }
        else
        {
            CTupleType::ElementArray ParamTypes;
            ParamTypes.Reserve(static_cast<int32_t>(NumParams));
            for (const STypedName& Param : Params)
            {
                ParamTypes.Add(Param.Type);
            }
            ParamsType = &GetOrCreateTupleType(Move(ParamTypes));
        }
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            *ParamsType,
            uLang::ForwardArg<decltype(Args)>(Args)...);
        TArray<CDataDefinition*> ParamDataDefinitions;
        ParamDataDefinitions.Reserve(static_cast<int32_t>(NumParams));
        for (const STypedName& Param : Params)
        {
            TSRef<CDataDefinition> ParamDataDefinition = NewFunction->CreateDataDefinition(Param.Name);
            ParamDataDefinition->SetType(Param.Type);
            ParamDataDefinitions.Add(ParamDataDefinition.Get());
        }
        NewFunction->_NegativeType = &FunctionType;
        NewFunction->_Signature = SSignature(
            FunctionType,
            Move(ParamDataDefinitions));
        NewFunction->SetAccessLevel({SAccessLevel::EKind::Public});
        return NewFunction.Get();
    };

    auto&& CreateIntrinsicFunction = [this, &CreateFunction](CSymbol FunctionName, std::initializer_list<STypedName> Params, auto&&... Args) -> CFunction* {
			CFunction* NewFunction = CreateFunction(FunctionName, Params, Args...); 
			NewFunction->AddAttributeClass(_intrinsicClass);
			return NewFunction;
		};

    // Create all the effects sets we use in intrinsics here and assert if any of them are technically illegal (ie. couldn't be created from Verse code)
    SEffectSet ConvergesEffectSet             = ConvertEffectClassesToEffectSet({ _convergesClass },                             EffectSets::FunctionDefault).GetValue();
    SEffectSet ConvergesDecidesEffectSet      = ConvertEffectClassesToEffectSet({ _convergesClass, _decidesClass },              EffectSets::FunctionDefault).GetValue();
    SEffectSet ConvergesReadsDecidesEffectSet = ConvertEffectClassesToEffectSet({ _convergesClass, _readsClass, _decidesClass }, EffectSets::FunctionDefault).GetValue();
    SEffectSet ComputesEffectSet              = ConvertEffectClassesToEffectSet({ _computesClass },                              EffectSets::FunctionDefault).GetValue();
    SEffectSet TransactsEffectSet             = ConvertEffectClassesToEffectSet({ _transactsClass },                             EffectSets::FunctionDefault).GetValue();
    SEffectSet TransactsDecidesEffectSet      = ConvertEffectClassesToEffectSet({ _transactsClass, _decidesClass },              EffectSets::FunctionDefault).GetValue();

    // `FunctionName`(`LhsName`:t, `RhsName`:comparable where t:subtype(comparable)):t
    auto ComparableOp = [this, LhsName, RhsName, ConvergesDecidesEffectSet, &VerseModuleBuiltInPart](CSymbol FunctionName)
    {
        TSRef<CFunction> NewFunction = VerseModuleBuiltInPart.CreateFunction(FunctionName);
        TSRef<CTypeVariable> Type = NewFunction->CreateTypeVariable(
            _Symbols->AddChecked("t"),
            &GetOrCreateTypeType(&_falseType, &_comparableType));
        Type->_NegativeType = &GetOrCreateTypeType(&_falseType, &_comparableType);
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            GetOrCreateTupleType({Type.Get(), &_comparableType}),
            *Type,
            ConvergesDecidesEffectSet,
            {Type.Get()});
        NewFunction->_NegativeType = &FunctionType;
        NewFunction->_Signature = SSignature(FunctionType, {
            NewFunction->CreateDataDefinition(LhsName, Type.Get()),
            NewFunction->CreateDataDefinition(RhsName, &_comparableType)});
        NewFunction->SetAccessLevel({SAccessLevel::EKind::Public});
        NewFunction->AddAttributeClass(_intrinsicClass);
        return NewFunction.Get();
    };

    auto AddUnaryOp = [ValName, CreateIntrinsicFunction, ConvergesEffectSet, ConvergesDecidesEffectSet](const CTypeBase* OpType, const CSymbol& FunctionName, bool bFallible = false) -> CFunction*
    {
        return CreateIntrinsicFunction(
            FunctionName,
            /*Params =*/{{ValName, OpType}},
            /*ReturnType =*/*OpType,
            bFallible ? ConvergesDecidesEffectSet : ConvergesEffectSet);
    };

    auto AddBinaryOp = [LhsName, RhsName, CreateIntrinsicFunction, ConvergesEffectSet, ConvergesDecidesEffectSet](const CTypeBase* OpType, const CSymbol& FunctionName, bool bFallible = false) -> CFunction*
    {

        return CreateIntrinsicFunction(
            FunctionName,
            /*Params =*/{{LhsName, OpType}, {RhsName, OpType}},
            /*ReturnType =*/*OpType,
            bFallible ? ConvergesDecidesEffectSet : ConvergesEffectSet);
    };

    auto AddAsymmetricBinaryOp = [LhsName, RhsName, CreateIntrinsicFunction, ConvergesEffectSet, ConvergesDecidesEffectSet](const CTypeBase* LeftType, const CTypeBase* RightType, const CTypeBase* ResultType, const CSymbol& FunctionName, bool bFallible = false) -> CFunction*
    {
        return CreateIntrinsicFunction(
            FunctionName,
            /*Params =*/{{LhsName, LeftType}, {RhsName, RightType}},
            /*ReturnType =*/*ResultType,
            bFallible ? ConvergesDecidesEffectSet : ConvergesEffectSet);
    };

    auto AddAssignOp = [this, LhsName, RhsName, CreateIntrinsicFunction, TransactsDecidesEffectSet, TransactsEffectSet](const CTypeBase* OpType, const CSymbol& FunctionName, bool bFallible = false) -> CFunction*
    {
        const CTypeBase* ReferenceType = &GetOrCreateReferenceType(OpType, OpType);
        return CreateIntrinsicFunction(
            FunctionName,
            /*Params =*/{{LhsName, ReferenceType}, {RhsName, OpType}},
            /*ReturnType =*/*OpType,
            bFallible ? TransactsDecidesEffectSet : TransactsEffectSet);
    };

    auto AddIntDivide = [this, LhsName, RhsName, CreateIntrinsicFunction, ConvergesDecidesEffectSet]() -> CFunction*
    {
        return CreateIntrinsicFunction(
            _IntrinsicSymbols._OpNameDiv,
            /*Params =*/{{LhsName, _intType}, {RhsName, _intType}},
            /*ReturnType =*/_rationalType,
            ConvergesDecidesEffectSet);
    };

    auto AddRationalOp = [this, ValName, CreateIntrinsicFunction, ConvergesEffectSet](CSymbol FunctionName)
    {
        return CreateIntrinsicFunction(
            FunctionName,
            /*Params =*/{{ValName, &_rationalType}},
            /*ReturnType =*/*_intType,
            ConvergesEffectSet);
    };

    _ComparableEqualOp     = ComparableOp(_IntrinsicSymbols._OpNameEqual);
    _ComparableNotEqualOp  = ComparableOp(_IntrinsicSymbols._OpNameNotEqual);

    _IntNegateOp           = AddUnaryOp (_intType, _IntrinsicSymbols._OpNameNegate);
    _IntAddOp              = AddBinaryOp(_intType, _IntrinsicSymbols._OpNameAdd);
    _IntSubtractOp         = AddBinaryOp(_intType, _IntrinsicSymbols._OpNameSub);
    _IntMultiplyOp         = AddBinaryOp(_intType, _IntrinsicSymbols._OpNameMul);
    _IntDivideOp           = AddIntDivide();
    _IntAddAssignOp        = AddAssignOp(_intType, _IntrinsicSymbols._OpNameAddRMW);
    _IntSubtractAssignOp   = AddAssignOp(_intType, _IntrinsicSymbols._OpNameSubRMW);
    _IntMultiplyAssignOp   = AddAssignOp(_intType, _IntrinsicSymbols._OpNameMulRMW);
    _IntAbs                = AddUnaryOp (_intType, _IntrinsicSymbols._FuncNameAbs);

    _IntGreaterOp          = AddBinaryOp(_intType, _IntrinsicSymbols._OpNameGreater, true);
    _IntGreaterEqualOp     = AddBinaryOp(_intType, _IntrinsicSymbols._OpNameGreaterEqual, true);
    _IntLessOp             = AddBinaryOp(_intType, _IntrinsicSymbols._OpNameLess, true);
    _IntLessEqualOp        = AddBinaryOp(_intType, _IntrinsicSymbols._OpNameLessEqual, true);

    _RationalCeil          = AddRationalOp(_IntrinsicSymbols._FuncNameCeil);
    _RationalFloor         = AddRationalOp(_IntrinsicSymbols._FuncNameFloor);

    _FloatNegateOp         = AddUnaryOp (_floatType, _IntrinsicSymbols._OpNameNegate);
    _FloatAddOp            = AddBinaryOp(_floatType, _IntrinsicSymbols._OpNameAdd);
    _FloatSubtractOp       = AddBinaryOp(_floatType, _IntrinsicSymbols._OpNameSub);
    _FloatMultiplyOp       = AddBinaryOp(_floatType, _IntrinsicSymbols._OpNameMul);
    _FloatDivideOp         = AddBinaryOp(_floatType, _IntrinsicSymbols._OpNameDiv);
    _FloatAddAssignOp      = AddAssignOp(_floatType, _IntrinsicSymbols._OpNameAddRMW);
    _FloatSubtractAssignOp = AddAssignOp(_floatType, _IntrinsicSymbols._OpNameSubRMW);
    _FloatMultiplyAssignOp = AddAssignOp(_floatType, _IntrinsicSymbols._OpNameMulRMW);
    _FloatDivideAssignOp   = AddAssignOp(_floatType, _IntrinsicSymbols._OpNameDivRMW);
    _FloatAbs              = AddUnaryOp (_floatType, _IntrinsicSymbols._FuncNameAbs);

    _IntMultiplyFloatOp    = AddAsymmetricBinaryOp(_intType, _floatType, _floatType, _IntrinsicSymbols._OpNameMul);
    _FloatMultiplyIntOp    = AddAsymmetricBinaryOp(_floatType, _intType, _floatType, _IntrinsicSymbols._OpNameMul);
    
    _FloatGreaterOp        = AddBinaryOp(_floatType, _IntrinsicSymbols._OpNameGreater, true);
    _FloatGreaterEqualOp   = AddBinaryOp(_floatType, _IntrinsicSymbols._OpNameGreaterEqual, true);
    _FloatLessOp           = AddBinaryOp(_floatType, _IntrinsicSymbols._OpNameLess, true);
    _FloatLessEqualOp      = AddBinaryOp(_floatType, _IntrinsicSymbols._OpNameLessEqual, true);

    _LogicQueryOp          = AddUnaryOp(&_logicType, _IntrinsicSymbols._OpNameQuery, true);

    //
    // Array generics
    //

    {
        _ArrayAddOp = VerseModuleBuiltInPart.CreateFunction(_IntrinsicSymbols._OpNameAdd);
        TSRef<CTypeVariable> ElementType = _ArrayAddOp->CreateTypeVariable(
            _Symbols->AddChecked("t"),
            _typeType);
        ElementType->_NegativeType = _typeType;
        CArrayType& ArrayType = GetOrCreateArrayType(ElementType.Get());
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            GetOrCreateTupleType({&ArrayType, &ArrayType}),
            ArrayType,
            ConvergesEffectSet,
            {ElementType.Get()},
            true);
        _ArrayAddOp->_NegativeType = &FunctionType;
        _ArrayAddOp->_Signature = SSignature(FunctionType, {
            _ArrayAddOp->CreateDataDefinition(LhsName, &ArrayType),
            _ArrayAddOp->CreateDataDefinition(RhsName, &ArrayType)});
        _ArrayAddOp->SetAccessLevel({SAccessLevel::EKind::Public});
        _ArrayAddOp->AddAttributeClass(_intrinsicClass);
    }

    {
        _ArrayAddAssignOp = VerseModuleBuiltInPart.CreateFunction(_IntrinsicSymbols._OpNameAddRMW);
        TSRef<CTypeVariable> ElementType = _ArrayAddAssignOp->CreateTypeVariable(
            _Symbols->AddChecked("t"),
            _typeType);
        ElementType->_NegativeType = _typeType;
        CArrayType& ArrayType = GetOrCreateArrayType(ElementType.Get());
        CReferenceType& ArrayReferenceType = GetOrCreateReferenceType(&ArrayType, &ArrayType);
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            GetOrCreateTupleType({&ArrayReferenceType, &ArrayType}),
            ArrayType,
            TransactsEffectSet,
            {ElementType.Get()},
            true);
        _ArrayAddAssignOp->_NegativeType = &FunctionType;
        _ArrayAddAssignOp->_Signature = SSignature(FunctionType, {
            _ArrayAddAssignOp->CreateDataDefinition(LhsName, &ArrayReferenceType),
            _ArrayAddAssignOp->CreateDataDefinition(RhsName, &ArrayType)});
        _ArrayAddAssignOp->SetAccessLevel({SAccessLevel::EKind::Public});
        _ArrayAddAssignOp->AddAttributeClass(_intrinsicClass);
    }

    {
        _ArrayLength = CreateIntrinsicFunction(
            _Symbols->AddChecked("operator'array.Length'"),
            /*Params =*/{},
            /*ReturnType =*/*_intType,
            ConvergesEffectSet);
        _ArrayLength->_ExtensionFieldAccessorKind = EExtensionFieldAccessorKind::ExtensionDataMember;
    }

    {
        _ArrayCallOp = VerseModuleBuiltInPart.CreateFunction(_IntrinsicSymbols._OpNameCall);
        TSRef<CTypeVariable> ElementType = _ArrayCallOp->CreateTypeVariable(
            _Symbols->AddChecked("t"),
            _typeType);
        ElementType->_NegativeType = _typeType;
        CArrayType& ArrayType = GetOrCreateArrayType(ElementType.Get());
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            GetOrCreateTupleType({&ArrayType, _intType}),
            *ElementType,
            ConvergesDecidesEffectSet,
            {ElementType.Get()},
            true);
        _ArrayCallOp->_NegativeType = &FunctionType;
        _ArrayCallOp->_Signature = SSignature(FunctionType, {
            _ArrayCallOp->CreateDataDefinition(_Symbols->AddChecked("Array"), &ArrayType),
            _ArrayCallOp->CreateDataDefinition(_Symbols->AddChecked("Index"), _intType)});
        _ArrayCallOp->SetAccessLevel({SAccessLevel::EKind::Public});
        _ArrayCallOp->AddAttributeClass(_intrinsicClass);
    }

    {
        _ArrayRefCallOp = VerseModuleBuiltInPart.CreateFunction(_IntrinsicSymbols._OpNameCall);
        TSRef<CTypeVariable> ElementType = _ArrayRefCallOp->CreateTypeVariable(
            _Symbols->AddChecked("t"),
            _typeType);
        ElementType->_NegativeType = _typeType;
        CArrayType& ArrayType = GetOrCreateArrayType(ElementType.Get());
        CReferenceType& ArrayReferenceType = GetOrCreateReferenceType(&ArrayType, &ArrayType);
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            GetOrCreateTupleType({&ArrayReferenceType, _intType}),
            GetOrCreateReferenceType(ElementType.Get(), ElementType.Get()),
            TransactsDecidesEffectSet,
            {ElementType.Get()},
            true);
        _ArrayRefCallOp->_NegativeType = &FunctionType;
        _ArrayRefCallOp->_Signature = SSignature(FunctionType, {
            _ArrayRefCallOp->CreateDataDefinition(_Symbols->AddChecked("Array"), &ArrayReferenceType),
            _ArrayRefCallOp->CreateDataDefinition(_Symbols->AddChecked("Index"), _intType)});
        _ArrayRefCallOp->SetAccessLevel({SAccessLevel::EKind::Public});
        _ArrayRefCallOp->AddAttributeClass(_intrinsicClass);
    }

    CTypeType& ComparableSubtypeType = GetOrCreateTypeType(&_falseType, &_comparableType);

    //
    // Map generics
    //
    {
        _MapRefCallOp = VerseModuleBuiltInPart.CreateFunction(_IntrinsicSymbols._OpNameCall);
        TSRef<CTypeVariable> KeyType = _MapRefCallOp->CreateTypeVariable(
            _Symbols->AddChecked("t"),
            &ComparableSubtypeType);
        KeyType->_NegativeType = &ComparableSubtypeType;
        TSRef<CTypeVariable> ValueType = _MapRefCallOp->CreateTypeVariable(
            _Symbols->AddChecked("u"),
            _typeType);
        ValueType->_NegativeType = _typeType;
        CMapType& MapType = GetOrCreateMapType(KeyType.Get(), ValueType.Get());
        CReferenceType& MapReferenceType = GetOrCreateReferenceType(&MapType, &MapType);
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            GetOrCreateTupleType({&MapReferenceType, KeyType.Get()}),
            GetOrCreateReferenceType(ValueType.Get(), ValueType.Get()),
            TransactsDecidesEffectSet,
            {KeyType.Get(), ValueType.Get()},
            true);
        _MapRefCallOp->_NegativeType = &FunctionType;
        _MapRefCallOp->_Signature = SSignature(FunctionType, {
            _MapRefCallOp->CreateDataDefinition(_Symbols->AddChecked("Map"), &MapReferenceType),
            _MapRefCallOp->CreateDataDefinition(_Symbols->AddChecked("Key"), KeyType.Get())
        });
        _MapRefCallOp->SetAccessLevel({SAccessLevel::EKind::Public});
        _MapRefCallOp->AddAttributeClass(_intrinsicClass);
    }

    {
        _MapLength = CreateIntrinsicFunction(
            _Symbols->AddChecked("operator'map.Length'"),
            /*Params =*/{},
            /*ReturnType =*/*_intType,
            ConvergesEffectSet);
        _MapLength->_ExtensionFieldAccessorKind = EExtensionFieldAccessorKind::ExtensionDataMember;
    }

    {
        _MapConcatenateMaps = VerseModuleBuiltInPart.CreateFunction(_Symbols->AddChecked("ConcatenateMaps"));
        TSRef<CTypeVariable> KeyType = _MapConcatenateMaps->CreateTypeVariable(
            _Symbols->AddChecked("t"),
            &ComparableSubtypeType);
        KeyType->_NegativeType = &ComparableSubtypeType;
        TSRef<CTypeVariable> ValueType = _MapConcatenateMaps->CreateTypeVariable(
            _Symbols->AddChecked("u"),
            _typeType);
        ValueType->_NegativeType = _typeType;
        CMapType& MapType = GetOrCreateMapType(KeyType.Get(), ValueType.Get());
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            GetOrCreateTupleType({&MapType, &MapType}),
            MapType,
            ConvergesEffectSet,
            {KeyType.Get(), ValueType.Get()},
            true);
        _MapConcatenateMaps->_NegativeType = &FunctionType;
        _MapConcatenateMaps->_Signature = SSignature(FunctionType, {
            _MapConcatenateMaps->CreateDataDefinition(LhsName, &MapType),
            _MapConcatenateMaps->CreateDataDefinition(RhsName, &MapType)});
        _MapConcatenateMaps->SetAccessLevel({SAccessLevel::EKind::Public});
        _MapConcatenateMaps->AddAttributeClass(_intrinsicClass);
    }

    //
    // Weak map generics
    //
    {
        _WeakMapCallOp = VerseModuleBuiltInPart.CreateFunction(_IntrinsicSymbols._OpNameCall);
        TSRef<CTypeVariable> KeyType = _WeakMapCallOp->CreateTypeVariable(
            _Symbols->AddChecked("t"),
            &ComparableSubtypeType);
        KeyType->_NegativeType = &ComparableSubtypeType;
        TSRef<CTypeVariable> ValueType = _WeakMapCallOp->CreateTypeVariable(
            _Symbols->AddChecked("u"),
            _typeType);
        ValueType->_NegativeType = _typeType;
        CMapType& MapType = GetOrCreateWeakMapType(*KeyType, *ValueType);
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            GetOrCreateTupleType({&MapType, KeyType.Get()}),
            *ValueType,
            ConvergesDecidesEffectSet,
            {KeyType.Get(), ValueType.Get()},
            true);
        _WeakMapCallOp->_NegativeType = &FunctionType;
        _WeakMapCallOp->_Signature = SSignature(FunctionType, {
            _WeakMapCallOp->CreateDataDefinition(_Symbols->AddChecked("Map"), &MapType),
            _WeakMapCallOp->CreateDataDefinition(_Symbols->AddChecked("Key"), KeyType.Get())});
        _WeakMapCallOp->SetAccessLevel({SAccessLevel::EKind::Public});
        _WeakMapCallOp->AddAttributeClass(_intrinsicClass);
    }

    {
        _WeakMapRefCallOp = VerseModuleBuiltInPart.CreateFunction(_IntrinsicSymbols._OpNameCall);
        TSRef<CTypeVariable> KeyType = _WeakMapRefCallOp->CreateTypeVariable(
            _Symbols->AddChecked("t"),
            &ComparableSubtypeType);
        KeyType->_NegativeType = &ComparableSubtypeType;
        TSRef<CTypeVariable> ValueType = _WeakMapRefCallOp->CreateTypeVariable(
            _Symbols->AddChecked("u"),
            _typeType);
        ValueType->_NegativeType = _typeType;
        CMapType& MapType = GetOrCreateWeakMapType(*KeyType, *ValueType);
        CReferenceType& MapReferenceType = GetOrCreateReferenceType(&MapType, &MapType);
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            GetOrCreateTupleType({&MapReferenceType, KeyType.Get()}),
            GetOrCreateReferenceType(ValueType.Get(), ValueType.Get()),
            TransactsDecidesEffectSet,
            {KeyType.Get(), ValueType.Get()},
            true);
        _WeakMapRefCallOp->_NegativeType = &FunctionType;
        _WeakMapRefCallOp->_Signature = SSignature(FunctionType, {
            _WeakMapRefCallOp->CreateDataDefinition(_Symbols->AddChecked("Map"), &MapReferenceType),
            _WeakMapRefCallOp->CreateDataDefinition(_Symbols->AddChecked("Key"), KeyType.Get())});
        _WeakMapRefCallOp->SetAccessLevel({SAccessLevel::EKind::Public});
        _WeakMapRefCallOp->AddAttributeClass(_intrinsicClass);
    }

    {
        // @code
        // weak_map(t:subtype(comparable), u:type) := intrinsic{}
        // @endcode
        _WeakMapOp = VerseModuleBuiltInPart.CreateFunction(_IntrinsicSymbols._FuncNameWeakMap);
        
        auto [ExplicitKeyType, KeyType, NegativeKeyType] = CreateExplicitTypeParam(
            _WeakMapOp,
            _Symbols->AddChecked("KeyType"),
            _Symbols->AddChecked("t"),
            _Symbols->AddChecked("u"),
            &ComparableSubtypeType);

        auto [ExplicitValueType, ValueType, NegativeValueType] = CreateExplicitTypeParam(
            _WeakMapOp,
            _Symbols->AddChecked("ValueType"),
            _Symbols->AddChecked("v"),
            _Symbols->AddChecked("w"),
            _typeType);

        CMapType& MapType = GetOrCreateWeakMapType(*KeyType, *ValueType);

        CMapType& NegativeMapType = MapType;

        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            GetOrCreateTupleType({ExplicitKeyType->GetType(), ExplicitValueType->GetType()}),
            GetOrCreateTypeType(&MapType, &NegativeMapType),
            ConvergesEffectSet,
            {KeyType, NegativeKeyType, ValueType, NegativeValueType},
            true);

        _WeakMapOp->_NegativeType = &FunctionType;
        _WeakMapOp->_Signature = SSignature(FunctionType, {
            ExplicitKeyType,
            ExplicitValueType});
        _WeakMapOp->SetAccessLevel({SAccessLevel::EKind::Public});
        _WeakMapOp->AddAttributeClass(_intrinsicClass);
    }

    //
    // Option generics
    //

    {
        _OptionQueryOp = VerseModuleBuiltInPart.CreateFunction(_IntrinsicSymbols._OpNameQuery);
        TSRef<CTypeVariable> ValueType = _OptionQueryOp->CreateTypeVariable(
            _Symbols->AddChecked("t"),
            _typeType);
        ValueType->_NegativeType = _typeType;
        COptionType& OptionType = GetOrCreateOptionType(ValueType.Get());
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            OptionType,
            *ValueType,
            ConvergesDecidesEffectSet,
            {ValueType.Get()},
            true);
        _OptionQueryOp->_NegativeType = &FunctionType;
        _OptionQueryOp->_Signature = SSignature(
            FunctionType,
            {_OptionQueryOp->CreateDataDefinition(ValName, &OptionType)});
        _OptionQueryOp->SetAccessLevel({SAccessLevel::EKind::Public});
        _OptionQueryOp->AddAttributeClass(_intrinsicClass);
    }

    //
    // `FitsInPlayer`
    //
    {
        const CTypeType& PersistableSubtypeType = GetOrCreateTypeType(&_falseType, &_persistableType);
        _FitsInPlayerMap = VerseModuleBuiltInPart.CreateFunction(_IntrinsicSymbols._FuncNameFitsInPlayerMap);
        TSRef<CTypeVariable> ValType = _FitsInPlayerMap->CreateTypeVariable(
            _Symbols->AddChecked("t"),
            &PersistableSubtypeType);
        ValType->_NegativeType = &PersistableSubtypeType;
        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            *ValType,
            *ValType,
            ConvergesReadsDecidesEffectSet,
            {ValType.Get()},
            true);
        _FitsInPlayerMap->_NegativeType = &FunctionType;
        _FitsInPlayerMap->_Signature = SSignature(
            FunctionType,
            {_FitsInPlayerMap->CreateDataDefinition(ValName, ValType.Get())});
        _FitsInPlayerMap->SetAccessLevel({SAccessLevel::EKind::Public});
        _FitsInPlayerMap->AddAttributeClass(_intrinsicClass);
    }

    //
    // getter/setter (for use in attributes)
    //
    {
        _Getter = CreateFunction(
          _Symbols->AddChecked("getter"), {{_Symbols->AddChecked("_"), &_anyType}}, *_getterClass, ComputesEffectSet
        );
        _Getter->SetAccessLevel({SAccessLevel::EKind::EpicInternal});

        _Setter = CreateFunction(
          _Symbols->AddChecked("setter"), {{_Symbols->AddChecked("_"), &_anyType}}, *_setterClass, ComputesEffectSet
        );
        _Setter->SetAccessLevel({SAccessLevel::EKind::EpicInternal});        
    }

    {
        // UnsafeCast(X:any, t:type):t = intrinsic{}
        _UnsafeCast = VerseModuleBuiltInPart.CreateFunction(_Symbols->AddChecked("UnsafeCast"));
        auto [ExplicitType, ResultType, NegativeResultType] = CreateExplicitTypeParam(
            _UnsafeCast,
            _Symbols->AddChecked("T"),
            _Symbols->AddChecked("t"),
            _Symbols->AddChecked("u"),
            _typeType
        );

        const CFunctionType& FunctionType = GetOrCreateFunctionType(
            GetOrCreateTupleType({&_anyType, ExplicitType->GetType()}),
            *ResultType,
            ConvergesEffectSet,
            {ResultType, NegativeResultType},
            false
        );
        _UnsafeCast->_NegativeType = &FunctionType;
        _UnsafeCast->_Signature = SSignature(
            FunctionType,
            {_UnsafeCast->CreateDataDefinition(_Symbols->AddChecked("Value"), &_anyType),
             ExplicitType}
        );
        _UnsafeCast->SetAccessLevel({SAccessLevel::EKind::EpicInternal});
        _UnsafeCast->AddAttributeClass(_intrinsicClass);
    }

    //
    // Intrinsic data definitions
    // 

    //
    // Floats
    //

    {
        const CFloatType& InfType = GetOrCreateConstrainedFloatType(INFINITY, INFINITY);
        _InfDefinition = VerseModuleBuiltInPart.CreateDataDefinition(_IntrinsicSymbols._Inf, &InfType);
        _InfDefinition->_NegativeType = &InfType;
        _InfDefinition->SetAccessLevel({SAccessLevel::EKind::Public});
        _InfDefinition->AddAttributeClass(_intrinsicClass);
    }

    {
        const CFloatType& NaNType = GetOrCreateConstrainedFloatType(NAN, NAN);
        _NaNDefinition = VerseModuleBuiltInPart.CreateDataDefinition(_IntrinsicSymbols._NaN, &NaNType);
        _NaNDefinition->_NegativeType = &NaNType;
        _NaNDefinition->SetAccessLevel({SAccessLevel::EKind::Public});
        _NaNDefinition->AddAttributeClass(_intrinsicClass);
    }
}

void CSemanticProgram::PopulateEffectDescriptorTable()
{
    ULANG_ASSERTF(!bEffectsTablePopulated, "Reinitializing the effects table is not allowed!");
    bEffectsTablePopulated = true;

    //                            Effect Key              EffectSet to apply              Effect bits to rescind before applying effect                                                           Effect classes that can't coexist or are considered redundant with the key class                               Allow in decomposition
    _EffectDescriptorTable.Insert(_readsClass,            { EffectSets::Reads,            EEffect::reads | EEffect::writes | EEffect::allocates | EEffect::no_rollback,                           { _transactsClass } });
    _EffectDescriptorTable.Insert(_writesClass,           { EffectSets::Writes,           EEffect::reads | EEffect::writes | EEffect::allocates | EEffect::no_rollback,                           { _transactsClass } });
    _EffectDescriptorTable.Insert(_allocatesClass,        { EffectSets::Allocates,        EEffect::reads | EEffect::writes | EEffect::allocates | EEffect::no_rollback,                           { _transactsClass, _variesClassDeprecated } });
    _EffectDescriptorTable.Insert(_transactsClass,        { EffectSets::Transacts,        EEffect::reads | EEffect::writes | EEffect::allocates | EEffect::no_rollback,                           { _readsClass, _writesClass, _allocatesClass, _variesClassDeprecated, _computesClass, _convergesClass } });
    _EffectDescriptorTable.Insert(_computesClass,         { EffectSets::Computes,         EEffect::diverges | EEffect::reads | EEffect::writes | EEffect::allocates | EEffect::no_rollback,       { _transactsClass, _variesClassDeprecated, _convergesClass } });
    _EffectDescriptorTable.Insert(_convergesClass,        { EffectSets::Converges,        EEffect::diverges | EEffect::reads | EEffect::writes | EEffect::allocates | EEffect::no_rollback,       { _transactsClass, _variesClassDeprecated, _computesClass  } });
    _EffectDescriptorTable.Insert(_suspendsClass,         { EffectSets::Suspends,         EEffect::suspends,                                                                                      {} });
    _EffectDescriptorTable.Insert(_decidesClass,          { EffectSets::Decides,          EEffect::decides,                                                                                       {} });

    _EffectDescriptorTable.Insert(_variesClassDeprecated, { EffectSets::VariesDeprecated, EEffect::reads | EEffect::writes | EEffect::allocates | EEffect::no_rollback,                           { _transactsClass, _allocatesClass, _computesClass, _convergesClass },                                         false });

    // Create any legacy effects tables that might come up
    {
        // Duplicate the latest table and augment the meaning of decides to imply diverges as this aligns with the legacy effects (pre-CL33775275)
        for (const auto& DescPair : _EffectDescriptorTable)
        {
            _EffectDescriptorTable_Pre3100.Insert(DescPair._Key, DescPair._Value);
        }

        _EffectDescriptorTable_Pre3100[_decidesClass]._EffectSet |= EffectSets::Computes;
    }

    for (const auto& DescPair : _EffectDescriptorTable)
    {
        _AllEffectClasses.Add(DescPair._Key);
    }

    for (const auto& DescPair : _EffectDescriptorTable)
    {
        if (DescPair._Value._AllowInDecomposition)
        {
            _OrderedDecompositionData.Add({ DescPair._Value._EffectSet, DescPair._Key });
        }
    }

    // Sort the decomp table to give us the heaviest effect classes (eg. transacts) first.
    //  This will naturally favor the aggregate effect classes over many singles - ie. <transacts> instead of something like <reads><writes><allocates><computes>
    _OrderedDecompositionData.Sort([](const SDecompositionMapping& A, const SDecompositionMapping& B) 
        {
            if (A.Effects.Num() == B.Effects.Num())
            {
				// alphabetical sorting is important because this code is indirectly used in mangled symbol generation
                return A.Class->Definition()->AsNameStringView() < B.Class->Definition()->AsNameStringView();
            }
            
            return A.Effects.Num() > B.Effects.Num();
        });

    ValidateEffectDescriptorTable(_EffectDescriptorTable);
    ValidateEffectDescriptorTable(_EffectDescriptorTable_Pre3100);
}

void CSemanticProgram::ValidateEffectDescriptorTable(const TMap<const CClass*, SEffectDescriptor>& DescriptorTable) const
{
    ULANG_ASSERTF(bEffectsTablePopulated, "Effects descriptor table not populated!");

    for (const auto& DescPair : DescriptorTable)
    {
        const CClass* SourceClass = DescPair._Key;
        const SEffectDescriptor& SourceDescriptor = DescPair._Value;
        ULANG_ASSERTF(SourceClass != nullptr, "Null keys are not allowed inside the effect descriptor table");

        for (const CClass* TargetClass : SourceDescriptor._MutualExclusions)
        {
            ULANG_ASSERTF(TargetClass != nullptr, "Null references are not allowed inside the effect descriptor table - mutual exclusion list for `%s`", SourceClass->Definition()->AsNameCString());
            ULANG_ASSERTF(SourceClass != TargetClass, "Effect classes cannot be mutually exclusive with themselves - `%s`", SourceClass->Definition()->AsNameCString());

            const SEffectDescriptor* TargetDescriptor = DescriptorTable.Find(TargetClass);
            ULANG_ASSERTF(TargetDescriptor != nullptr, "All mutually exclusive effect classes must also have a descriptor in the table - `%s` is missing", TargetClass->Definition()->AsNameCString());
            ULANG_ASSERTF(TargetDescriptor->_MutualExclusions.Contains(SourceClass), "All mutual exlusion relationships must be reciprocated - `%s` lacks `%s`", TargetClass->Definition()->AsNameCString(), SourceClass->Definition()->AsNameCString());
        }

        ULANG_ASSERTF(_AllEffectClasses.Contains(SourceClass), "All effect classes must be in both the descriptor table and the all-effects list", SourceClass->Definition()->AsNameCString());
    }

    for (const CClass* EffectClass : _AllEffectClasses)
    {
        ULANG_ASSERTF(DescriptorTable.Contains(EffectClass), "All effect classes must be in both the descriptor table and the all-effects list", EffectClass->Definition()->AsNameCString());
    }
}

const TMap<const CClass*, SEffectDescriptor>& CSemanticProgram::GetEffectDescriptorTableForVersion(uint32_t UploadedAtFNVersion) const
{
    if (!VerseFN::UploadedAtFNVersion::DecidesEffectNoLongerImpliesComputes(UploadedAtFNVersion))
    {
        return _EffectDescriptorTable_Pre3100;
    }

    return _EffectDescriptorTable;
}

const SEffectDescriptor& CSemanticProgram::FindEffectDescriptorChecked(const CClass* effectClass, uint32_t UploadedAtFNVersion /*= Latest*/ ) const
{
    const SEffectDescriptor* ResultDescriptor = GetEffectDescriptorTableForVersion(UploadedAtFNVersion).Find(effectClass);

    ULANG_ASSERTF(ResultDescriptor != nullptr, "Failed to find an effect descriptor for the `%s` effect class", effectClass->Definition()->AsNameCString());
    
    return *ResultDescriptor;
}

TOptional<SEffectSet> CSemanticProgram::ConvertEffectClassesToEffectSet(const TArray<const CClass*>& EffectClasses, const SEffectSet& DefaultEffectSet, uint32_t UploadedAtFNVersion /*=Latest*/) const
{
    ULANG_ASSERTF(bEffectsTablePopulated, "Effects descriptor table not populated!");

    // Check that all these effect classes can coexist
    for (int i = 0; i < EffectClasses.Num(); ++i)
    {
        const uLang::SEffectDescriptor& OuterDesc = FindEffectDescriptorChecked(EffectClasses[i], UploadedAtFNVersion);
        for (int j = i + 1; j < EffectClasses.Num(); ++j)
        {
            if (OuterDesc._MutualExclusions.Contains(EffectClasses[j]))
            {
                return TOptional<SEffectSet>();
            }
        }
    }

    SEffectSet Result = DefaultEffectSet;
    SEffectSet AddedEffects = SEffectSet{};

    const TMap<const CClass*, SEffectDescriptor>& EffectDescriptorTable = GetEffectDescriptorTableForVersion(UploadedAtFNVersion);

    for (const CClass* EffectClass : EffectClasses)
    {
        if (const uLang::SEffectDescriptor* EffectDesc = EffectDescriptorTable.Find(EffectClass))
        {
            Result &= ~EffectDesc->_RescindFromDefault;
            AddedEffects |= EffectDesc->_EffectSet;
        }
    }

    return TOptional<SEffectSet>(Result | AddedEffects);
}

// Convert an effect set into a reasonable set of code-side effect classes
TOptional<TArray<const CClass*>> CSemanticProgram::ConvertEffectSetToEffectClasses(const SEffectSet& TargetSet, const SEffectSet& DefaultEffectSet) const
{
    ULANG_ASSERTF(bEffectsTablePopulated, "Effects descriptor table not populated!");

    // Early out if they're just asking for the default
    if (TargetSet == DefaultEffectSet)
    {
        return TOptional<TArray<const CClass*>>(TArray<const CClass*>());
    }

    // It is currently not necessary to support the Effect-set to Classes conversion with versioned effect tables.
    // That's only used for digest creation and some current-version-only cases like the LSP.
    const TMap<const CClass*, SEffectDescriptor>& EffectDescriptorTable = GetEffectDescriptorTableForVersion(VerseFN::UploadedAtFNVersion::Latest);

    // This decomposition is an incomplete solution. In reality, this is something akin to a knapsack problem or the coins problem, but 
    //  the standard dp solution doesn't have an obvious mapping. At a minimum, we should cache solutions instead of recalculating them
    //  all the time. It might be practical to just brute-force the solutions into a table, but the table shifts depending on what the
    //  specified default values are.
    for (const SDecompositionMapping& OuterMapping : _OrderedDecompositionData)
    {
        if (const uLang::SEffectDescriptor* OuterDesc = EffectDescriptorTable.Find(OuterMapping.Class))
        {
            SEffectSet SubtractiveSet = DefaultEffectSet & ~OuterDesc->_RescindFromDefault;
            SEffectSet AdditiveSet = OuterDesc->_EffectSet;

            SEffectSet ResultSet = SubtractiveSet | AdditiveSet;

            TArray<const CClass*> WorkingEffectClasses;

			// Only add the effect if it's not already the default effect. This prevents over-reporting for classes and modules in some cases
            if (OuterDesc->_EffectSet != DefaultEffectSet)
            {
                WorkingEffectClasses.Add(OuterMapping.Class);
            }

            for (auto InnerMapping : _OrderedDecompositionData)
            {
                if (InnerMapping.Class == OuterMapping.Class)
                {
                    continue;
                }

                if (OuterDesc->_MutualExclusions.Contains(InnerMapping.Class))
                {
                    continue;
                }

                if (const uLang::SEffectDescriptor* InnerDesc = EffectDescriptorTable.Find(InnerMapping.Class))
                {
                    // Recompute the result if this effect was added
                    SEffectSet TestSet = (SubtractiveSet & ~InnerDesc->_RescindFromDefault) | AdditiveSet | InnerDesc->_EffectSet;

                    if (TargetSet.HasAll(TestSet)
                        && !ResultSet.HasAll(TestSet))
                    {
                        SubtractiveSet &= ~InnerDesc->_RescindFromDefault;
                        AdditiveSet |= InnerDesc->_EffectSet;

			            if (InnerDesc->_EffectSet != DefaultEffectSet)
						{
                        	WorkingEffectClasses.Add(InnerMapping.Class);
						}

                        ResultSet = SubtractiveSet | AdditiveSet;
                    }
                }
            }

            // Did we get there?
            if (TargetSet == ResultSet)
            {
                return TOptional<TArray<const CClass*>>(WorkingEffectClasses);
            }
        }
    }

    // Failure
    return TOptional<TArray<const CClass*>>();
}

CDefinition* CSemanticProgram::FindDefinitionByVersePathInternal(CUTF8StringView VersePath) const
{
    const CLogicalScope* Scope = this;
    CDefinition* Result = nullptr;
    bool bError = false;
    FilePathUtils::ForeachPartOfPath(VersePath, [this, &VersePath, &Scope, &Result, &bError](const uLang::CUTF8StringView& Part)
        {
            if (Part.IsFilled() && !bError)
            {
                TOptional<CSymbol> PartSymbol = _Symbols->Find(Part);
                if (!PartSymbol)
                {
                    bError = true;
                }
                else
                {
                    for (CDefinition* Definition : Scope->GetDefinitions())
                    {
                        if (Definition->GetName() == PartSymbol)
                        {
                            // Is this the leaf of the VersePath?
                            if (Part._End == VersePath._End)
                            {
                                // Yes, then that's the definition we want
                                Result = Definition;
                            }
                            else if (CModule* Module = Definition->AsNullable<CModule>())
                            {
                                // Otherwise it better be a module
                                Scope = Module;
                            }
                            else
                            {
                                bError = true;
                            }
                            break;
                        }
                    }
                }
            }
        });

    return Result;
}

} // namespace uLang
