// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/Expression.h"

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/SharedPointerArray.h"
#include "uLang/Semantics/ModuleAlias.h"
#include "uLang/Semantics/ScopedAccessLevelType.h"
#include "uLang/Semantics/SemanticEnumeration.h"
#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Semantics/TypeAlias.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Semantics/UnknownType.h"
#include "uLang/SourceProject/PackageRole.h"
#include "uLang/Syntax/VstNode.h"

namespace uLang
{
SAstNodeTypeInfo GetAstNodeTypeInfo(EAstNodeType NodeType)
{
    switch(NodeType)
    {
    #define VISIT_AST_NODE_TYPE(Name, Class) case EAstNodeType::Name: return {#Name, #Class};
        VERSE_VISIT_AST_NODE_TYPES(VISIT_AST_NODE_TYPE)
    #undef VISIT_AST_NODE_TYPE
    default: ULANG_UNREACHABLE();
    };
}

//=======================================================================================
// CAstNode Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CAstNode::~CAstNode()
{
    if (IsVstMappingReciprocal())
    {
        if (_MappedVstNode && _MappedVstNode->_MappedAstNode == this)
        {
            _MappedVstNode->_MappedAstNode = nullptr;
        }
    }
}

//=======================================================================================
// CExpressionBase Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CTypeBase* CExpressionBase::GetResultType(const CSemanticProgram& Program) const
{
    return _Report.IsSet()
        ? _Report.GetValue().ResultType
        : nullptr;
}

//---------------------------------------------------------------------------------------
void CExpressionBase::SetResultType(const CTypeBase* InResultType)
{
    ULANG_ENSUREF(!_Report.IsSet(), "Node was previously analyzed!");
    ULANG_ENSUREF(InResultType!=nullptr, "Type must be non-null");

    _Report = SAnalysisResult{ InResultType };
}

void CExpressionBase::RefineResultType(const CTypeBase* RefinedResultType)
{
    ULANG_ENSUREF(_Report.IsSet(), "Node was not previously analyzed!");
    ULANG_ENSUREF(RefinedResultType != nullptr, "RefinedType must be non-null");
    // TODO: We should check IsSubtype(_Report.GetValue(), RefinedResultType) but then we'd need to take a scope here.

    _Report = SAnalysisResult { RefinedResultType };
}

//=======================================================================================
// CExprCompoundBase Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprCompoundBase::CanFail() const
{
    for (CExpressionBase* SubExpr : _SubExprs)
    {
        if (SubExpr->CanFail())
        {
            return true;
        }
    }

    return false;
}

//---------------------------------------------------------------------------------------
const CExpressionBase* CExprCompoundBase::FindFirstAsyncSubExpr(const CSemanticProgram& Program) const
{
    const CExpressionBase* AsyncExpr;

    for (CExpressionBase* SubExpr : _SubExprs)
    {
        AsyncExpr = SubExpr->FindFirstAsyncSubExpr(Program);

        if (AsyncExpr)
        {
            return AsyncExpr;
        }
    }

    return nullptr;
}

//---------------------------------------------------------------------------------------
bool CExprCompoundBase::operator==(const CExpressionBase& Other) const
{
    return BaseCompare(*this, Other)
        && AreSubExprsEqual(_SubExprs, static_cast<const CExprCompoundBase&>(Other)._SubExprs);
}


//=======================================================================================
// CExprExternal Methods
//=======================================================================================

CExprExternal::CExprExternal(const CSemanticProgram& Program)
    : CExpressionBase(&Program._falseType)
{
}

//=======================================================================================
// CExprLogic Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprLogic::CExprLogic(const CSemanticProgram& Program, bool Value)
    : CExpressionBase(&Program._logicType)
    , _Value(Value)
{}


//=======================================================================================
// CExprNumber Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprNumber::CExprNumber(CSemanticProgram& Program, Integer IntValue)
    : _IntValue(IntValue)
    , _bIsFloat(false)
{
    SetResultType(&Program.GetOrCreateConstrainedIntType(IntValue, IntValue));
}

CExprNumber::CExprNumber(CSemanticProgram& Program, Float FloatValue)
    : _FloatValue(FloatValue)
    , _bIsFloat(true)
{
    SetResultType(&Program.GetOrCreateConstrainedFloatType(FloatValue, FloatValue));
}

void CExprNumber::SetIntValue(CSemanticProgram& Program, Integer IntValue)
{
    _IntValue = IntValue;
    _bIsFloat = false;
    SetResultType(&Program.GetOrCreateConstrainedIntType(IntValue, IntValue));
}

void CExprNumber::SetFloatValue(CSemanticProgram& Program, Float FloatValue)
{
    _FloatValue = FloatValue;
    _bIsFloat = true;
    SetResultType(&Program.GetOrCreateConstrainedFloatType(FloatValue, FloatValue));
}

//=======================================================================================
// CExprEnumLiteral Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CTypeBase* CExprEnumLiteral::GetResultType(const CSemanticProgram& Program) const
{
    ULANG_ASSERT(_Enumerator);
    return _Enumerator->_Enumeration;
}

void CExprEnumLiteral::VisitImmediates(SAstVisitor& Visitor) const
{
    CExpressionBase::VisitImmediates(Visitor);
    Visitor.VisitImmediate("Enumerator", _Enumerator->AsCode());
}

//=======================================================================================
// CExprDefinition Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprDefinition::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() == EAstNodeType::Definition)
    {
        const CExprDefinition& OthrDef = static_cast<const CExprDefinition&>(Other);
        const CExprDefinition& ThisDef = *this;

        return Other.GetNodeType() == EAstNodeType::Definition
            && (ThisDef._Name == OthrDef._Name)
            && (IsSubExprEqual(ThisDef.Element(), OthrDef.Element()))
            && (IsSubExprEqual(ThisDef.ValueDomain(), OthrDef.ValueDomain()))
            && (IsSubExprEqual(ThisDef.Value(), OthrDef.Value()));
    }
    else
    {
        return false;
    }

}

//---------------------------------------------------------------------------------------
bool CExprDefinition::CanFail() const
{
    return Value() && Value()->CanFail();
}

//---------------------------------------------------------------------------------------
const CExpressionBase* CExprDefinition::FindFirstAsyncSubExpr(const CSemanticProgram& Program) const
{
    return Value()
        ? Value()->FindFirstAsyncSubExpr(Program)
        : nullptr;
}

//=======================================================================================
// CExprIdentifierClass Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprIdentifierClass::CExprIdentifierClass(const CTypeType* Type, TSPtr<CExpressionBase>&& Context, TSPtr<CExpressionBase>&& Qualifier)
    : CExprIdentifierBase(Move(Context), Move(Qualifier))
{
    SetResultType(Type);
}

//---------------------------------------------------------------------------------------
const CTypeType* CExprIdentifierClass::GetTypeType(const CSemanticProgram& Program) const
{
    return &GetResultType(Program)->GetNormalType().AsChecked<CTypeType>();
}

const CClass* CExprIdentifierClass::GetClass(const CSemanticProgram& Program) const
{
    const CTypeType* TypeType = GetTypeType(Program);
    return &TypeType->PositiveType()->GetNormalType().AsChecked<CClass>();
}

CUTF8String CExprIdentifierClass::GetErrorDesc() const
{
    if (const CTypeBase* ResultType = IrGetResultType())
    {
        return ResultType->AsCode();
    }
    else
    {
        return "class identifier";
    }
}

//=======================================================================================
// CExprIdentifierModule Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprIdentifierModule::CExprIdentifierModule(const CModule* Module, TSPtr<CExpressionBase>&& Context, TSPtr<CExpressionBase>&& Qualifier)
    : CExprIdentifierBase(Move(Context), Move(Qualifier))
{
    SetResultType(Module);
}

//---------------------------------------------------------------------------------------
CModule const* CExprIdentifierModule::GetModule(const CSemanticProgram& Program) const
{
    return &GetResultType(Program)->GetNormalType().AsChecked<CModule>();
}

//=======================================================================================
// CExprEnumerationType Methods
//=======================================================================================

const CTypeType* CExprEnumerationType::GetTypeType(const CSemanticProgram& Program) const
{
    return &GetResultType(Program)->GetNormalType().AsChecked<CTypeType>(); 
}

CEnumeration const* CExprEnumerationType::GetEnumeration(const CSemanticProgram& Program) const
{
    const CTypeType* TypeType = GetTypeType(Program);
    return &TypeType->PositiveType()->GetNormalType().AsChecked<CEnumeration>();
}

//=======================================================================================
// CExprInterfaceType Methods
//=======================================================================================

CInterface const* CExprInterfaceType::GetInterface(const CSemanticProgram& Program) const
{
    const CTypeType* TypeType = GetTypeType(Program);
    return &TypeType->PositiveType()->GetNormalType().AsChecked<CInterface>();
}

//=======================================================================================
// CExprIdentifierData Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprIdentifierData::CExprIdentifierData(const CSemanticProgram& Program, const CDataDefinition& DataDefinition, TSPtr<CExpressionBase>&& Context, TSPtr<CExpressionBase>&& Qualifier )
    : CExprIdentifierBase(Move(Context), Move(Qualifier))
    , _DataDefinition(DataDefinition)
{
}

//---------------------------------------------------------------------------------------
const CTypeBase* CExprIdentifierData::GetResultType(const CSemanticProgram& Program) const
{
    const CTypeBase* DataDefinitionPositiveValueType = _DataDefinition.GetType() ? _DataDefinition.GetType() : Program.GetDefaultUnknownType();

    // this identifier expression result type needs to be
    // wrapped in a reference if it has a context and the 
    // context is a reference type

    if (Context())
    {
        const CTypeBase* ContextType = Context()->GetResultType(Program);
        if (const CReferenceType* ReferenceType = ContextType->GetNormalType().AsNullable<CReferenceType>())
        {
            // Only allow forming a reference to a data member of a pure struct.
            const CTypeBase* ReferenceValueType = ReferenceType->PositiveValueType();
            if (const CClass* Class = ReferenceValueType->GetNormalType().AsNullable<CClass>())
            {
                if(Class->IsStruct() && Class->_ConstructorEffects == EffectSets::Computes)
                {
                    return &const_cast<CSemanticProgram&>(Program).GetOrCreateReferenceType(
                        _DataDefinition._NegativeType,
                        DataDefinitionPositiveValueType);
                }
            }
        }
    }

    return DataDefinitionPositiveValueType;
}


//=======================================================================================
// CExprIdentifierTypeAlias Methods
//=======================================================================================

void CExprIdentifierTypeAlias::VisitImmediates(SAstVisitor& Visitor) const
{
    CExpressionBase::VisitImmediates(Visitor);
    Visitor.VisitImmediate("TypeAlias", _TypeAlias);
}

CExprIdentifierTypeAlias::CExprIdentifierTypeAlias(const CTypeAlias& TypeAlias, TSPtr<CExpressionBase>&& Context /* = nullptr */, TSPtr<CExpressionBase>&& Qualifier /* = nullptr */)
    : CExprIdentifierBase(Move(Context), Move(Qualifier))
    , _TypeAlias(TypeAlias)
{
}

const CTypeBase* CExprIdentifierTypeAlias::GetResultType(const CSemanticProgram& Program) const
{
    return _TypeAlias.GetTypeType();
}

//=======================================================================================
// CExprIdentifierTypeVariable Methods
//=======================================================================================

CExprIdentifierTypeVariable::CExprIdentifierTypeVariable(const CTypeVariable& TypeVariable, TSPtr<CExpressionBase>&& Context, TSPtr<CExpressionBase>&& Qualifier)
    : CExprIdentifierBase(Move(Context), Move(Qualifier))
    , _TypeVariable(TypeVariable)
{
    SetResultType(&TypeVariable.GetProgram().GetOrCreateTypeType(&TypeVariable, &TypeVariable));
}

void CExprIdentifierTypeVariable::VisitImmediates(SAstVisitor& Visitor) const
{
    CExpressionBase::VisitImmediates(Visitor);
    Visitor.VisitImmediate("TypeVariable", _TypeVariable);
}

//=======================================================================================
// CExprIdentifierFunction Methods
//=======================================================================================

CExprIdentifierFunction::CExprIdentifierFunction(
    const CFunction& Function,
    TArray<SInstantiatedTypeVariable> InstTypeVariables,
    const CTypeBase* ResultType,
    const CTypeBase* ConstructorNegativeReturnType,
    TSPtr<CExpressionBase>&& Context,
    TSPtr<CExpressionBase>&& Qualifier,
    bool bSuperQualified)
    : CExprIdentifierBase(Move(Context), Move(Qualifier))
    , _Function(Function)
    , _InstantiatedTypeVariables(Move(InstTypeVariables))
    , _ConstructorNegativeReturnType(ConstructorNegativeReturnType)
    , _bSuperQualified(bSuperQualified)
{
    if (ResultType)
    {
        SetResultType(ResultType);
    }
}


void CExprIdentifierFunction::VisitImmediates(SAstVisitor& Visitor) const
{
    CExpressionBase::VisitImmediates(Visitor);
    Visitor.VisitImmediate("Function", _Function);
    Visitor.VisitImmediate("bSuperQualified", _bSuperQualified);
}


//=======================================================================================
// CExprIdentifierOverloadedFunction Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprIdentifierOverloadedFunction::CExprIdentifierOverloadedFunction(
    TArray<const CFunction*>&& OverloadedFunctions,
    bool bConstructor,
    const CSymbol Symbol,
    const CTypeBase* OverloadedType,
    TSPtr<CExpressionBase>&& Context,
    TSPtr<CExpressionBase>&& Qualifier,
    const CTypeBase* Type)
    : CExprIdentifierBase(Move(Context), Move(Qualifier))
    , _FunctionOverloads(Move(OverloadedFunctions))
    , _bConstructor(bConstructor)
    , _Symbol(Symbol)
    , _TypeOverload(OverloadedType)
    , _bAllowUnrestrictedAccess(false)
{
    SetResultType(Type);
}

void CExprIdentifierOverloadedFunction::VisitImmediates(SAstVisitor& Visitor) const
{
    CExpressionBase::VisitImmediates(Visitor);
    Visitor.BeginArray("FunctionOverloads", _FunctionOverloads.Num());
    for (const CFunction* Function : _FunctionOverloads)
    {
        Visitor.VisitImmediate("", *Function);
    }
    Visitor.EndArray();
}


//=======================================================================================
// CExprInvocation Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CFunctionType* CExprInvocation::GetResolvedCalleeType() const
{
    ULANG_ASSERTF(!IsIrNode() || _ResolvedCalleeType, "GetResolvedCalleeType called on unanalyzed CExprInvocation");
    return _ResolvedCalleeType;
}

//---------------------------------------------------------------------------------------
const CExpressionBase* CExprInvocation::FindFirstAsyncSubExpr(const CSemanticProgram& Program) const
{
    const CExpressionBase* AsyncExpr = _Callee ? _Callee->FindFirstAsyncSubExpr(Program) : nullptr;
    if (AsyncExpr)
    {
        return AsyncExpr;
    }
    AsyncExpr = _Argument->FindFirstAsyncSubExpr(Program);

    if (AsyncExpr)
    {
        return AsyncExpr;
    }
    
    if (_ResolvedCalleeType && GetResolvedCalleeType()->GetEffects()[EEffect::suspends])
    {
        return this;
    }

    return nullptr;
}

//---------------------------------------------------------------------------------------
bool CExprInvocation::CanFail() const
{
    // The expression may fail if any of the receiver, the arguments, or the invocation itself may fail.
    if (_ResolvedCalleeType && GetResolvedCalleeType()->GetEffects()[EEffect::decides])
    {
        return true;
    }

    if (_Callee.IsValid() && _Callee->CanFail())
    {
        return true;
    }

    if (_Argument.IsValid() && _Argument->CanFail())
    {
        return true;
    }

    return false;
}

//---------------------------------------------------------------------------------------
bool CExprInvocation::operator==(const CExpressionBase& Other) const
{
    return BaseCompare(*this, Other)
        && IsSubExprEqual(_Callee, static_cast<const CExprInvocation&>(Other)._Callee)
        && IsSubExprEqual(_Argument, static_cast<const CExprInvocation&>(Other)._Argument);
}

const CExprIdentifierFunction* GetConstructorInvocationCallee(const CExprInvocation& Invocation)
{
    const TSPtr<CExpressionBase>& Callee = Invocation.GetCallee();
    if (Callee->GetNodeType() != EAstNodeType::Identifier_Function)
    {
        return nullptr;
    }
    const CExprIdentifierFunction& Identifier = static_cast<const CExprIdentifierFunction&>(*Callee);
    if (!Identifier._ConstructorNegativeReturnType)
    {
        return nullptr;
    }
    return &Identifier;
}

const CExprIdentifierFunction* GetConstructorInvocationCallee(const CExpressionBase& Expression)
{
    if (Expression.GetNodeType() != EAstNodeType::Invoke_Invocation)
    {
        return nullptr;
    }
    return GetConstructorInvocationCallee(static_cast<const CExprInvocation&>(Expression));
}

bool IsConstructorInvocation(const CExprInvocation& Invocation)
{
    return static_cast<bool>(GetConstructorInvocationCallee(Invocation));
}

bool IsConstructorInvocation(const CExpressionBase& Expression)
{
    return static_cast<bool>(GetConstructorInvocationCallee(Expression));
}

//=======================================================================================
// CExprTupleElement Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprTupleElement::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != EAstNodeType::Invoke_TupleElement)
    {
        return false;
    }

    const CExprTupleElement& OtherTupleElement = static_cast<const CExprTupleElement&>(Other);

    return (_ElemIdx == OtherTupleElement._ElemIdx)
        && IsSubExprEqual(_TupleExpr, OtherTupleElement._TupleExpr);
}


//=======================================================================================
// CExprAssignment Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprAssignment::operator==(const CExpressionBase& Other) const
{
    return Other.GetNodeType() == EAstNodeType::Assignment
        && IsSubExprEqual(_Lhs, static_cast<const CExprAssignment&>(Other)._Lhs)
        && IsSubExprEqual(_Rhs, static_cast<const CExprAssignment&>(Other)._Rhs);
}


//=======================================================================================
// CExprShortCircuitAnd Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprShortCircuitAnd::operator==(const CExpressionBase& Other) const
{
    return Other.GetNodeType() == EAstNodeType::Invoke_ShortCircuitAnd
        && IsSubExprEqual(Lhs(), static_cast<const CExprShortCircuitAnd&>(Other).Lhs())
        && IsSubExprEqual(Rhs(), static_cast<const CExprShortCircuitAnd&>(Other).Rhs());
}


//=======================================================================================
// CExprShortCircuitOr Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprShortCircuitOr::operator==(const CExpressionBase& Other) const
{
    return Other.GetNodeType() == EAstNodeType::Invoke_ShortCircuitOr
        && IsSubExprEqual(Lhs(), static_cast<const CExprShortCircuitOr&>(Other).Lhs())
        && IsSubExprEqual(Rhs(), static_cast<const CExprShortCircuitOr&>(Other).Rhs());
}


//=======================================================================================
// CExprLogicalNot Methods
//=======================================================================================

const CTypeBase* CExprLogicalNot::GetResultType(const CSemanticProgram& Program) const
{
    return &Program._logicType;
}

//---------------------------------------------------------------------------------------
bool CExprLogicalNot::operator==(const CExpressionBase& Other) const
{
    return Other.GetNodeType() == EAstNodeType::Invoke_LogicalNot
        && IsSubExprEqual(Operand(), static_cast<const CExprLogicalNot&>(Other).Operand());
}


//=======================================================================================
// CExprComparison Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CUTF8String CExprComparison::GetErrorDesc() const
{
    switch(_Op)
    {
    case Verse::Vst::BinaryOpCompare::op::eq:    return "comparison operator '='";
    case Verse::Vst::BinaryOpCompare::op::gt:    return "comparison operator '>'";
    case Verse::Vst::BinaryOpCompare::op::gteq:  return "comparison operator '>='";
    case Verse::Vst::BinaryOpCompare::op::lt:    return "comparison operator '<'";
    case Verse::Vst::BinaryOpCompare::op::lteq:  return "comparison operator '<='";
    case Verse::Vst::BinaryOpCompare::op::noteq: return "comparison operator '<>'";
    default:
        ULANG_UNREACHABLE();
    }
}


//---------------------------------------------------------------------------------------
bool CExprComparison::operator==(const CExpressionBase& Other) const
{
    return CExprInvocation::operator==(Other)
        && _Op == static_cast<const CExprComparison&>(Other)._Op;
}


//=======================================================================================
// CExprMakeOption Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprMakeOption::operator==(const CExpressionBase& Other) const
{
    return Other.GetNodeType() == EAstNodeType::Invoke_MakeOption
        && IsSubExprEqual(Operand(), static_cast<const CExprMakeOption&>(Other).Operand());
}


//=======================================================================================
// CExprMapTypeFormer Methods
//=======================================================================================

const CExpressionBase*  CExprMapTypeFormer::FindFirstAsyncSubExpr(const CSemanticProgram& Program) const
{
    const CExpressionBase* AsyncExpr = nullptr;
    for (const TSRef<CExpressionBase>& KeyTypeAst : _KeyTypeAsts)
    {
        AsyncExpr = KeyTypeAst->FindFirstAsyncSubExpr(Program);
        if (AsyncExpr) { return AsyncExpr; }
    }
    return _ValueTypeAst->FindFirstAsyncSubExpr(Program);
}


//=======================================================================================
// CExprSubtype Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CTypeType& CExprSubtype::GetSubtypeType() const
{
    ULANG_ASSERTF(_TypeType, "GetSubtypeType called on unanalyzed expression");
    return *_TypeType;
}

//=======================================================================================
// CExprTupleType Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprTupleType::operator==(const CExpressionBase& Other) const
{
    if (!BaseCompare(*this, Other))
    {
        return false;
    }

    const CExprTupleType* OtherTupleType = static_cast<const CExprTupleType*>(&Other);

    return (_TypeType && (_TypeType == OtherTupleType->_TypeType))
        || AreSubExprsEqual(_ElementTypeExprs, OtherTupleType->_ElementTypeExprs);
}


//=======================================================================================
// CExprMakeMap Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprMakeMap::CanFail() const
{
    // This can't just call CanFail on subexpressions, because the subexpressions will be
    // CExprFunctionLiteral, which doesn't propagate CanFail from its subexpressions.
    for (const CExpressionBase* SubExpr : _SubExprs)
    {
        ULANG_ASSERTF (SubExpr->GetNodeType() == EAstNodeType::Literal_Function, "Expected subexpressions to be function literals");
        const CExprFunctionLiteral* PairLiteral = static_cast<const CExprFunctionLiteral*>(SubExpr);
        if (PairLiteral->Range()->CanFail() || PairLiteral->Domain()->CanFail())
        {
            return true;
        }
    }

    return false;
}


//=======================================================================================
// CExprMakeRange Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprMakeRange::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != EAstNodeType::Invoke_MakeRange)
    {
        return false;
    }
    const CExprMakeRange& OtherCtor = static_cast<const CExprMakeRange&>(Other);

    if (_Report != OtherCtor._Report)
    {
        return false;
    }

    if (_Lhs != OtherCtor._Lhs)
    {
        return false;
    }

    if (_Rhs != OtherCtor._Rhs)
    {
        return false;
    }

    return true;
}


//=======================================================================================
// CExprInvokeType Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprInvokeType::CExprInvokeType(const CTypeBase* NegativeType, const CTypeBase* PositiveType, bool bIsFallible, TSPtr<CExpressionBase>&& TypeAst, TSRef<CExpressionBase>&& Argument)
: _NegativeType(NegativeType), _bIsFallible(bIsFallible), _TypeAst(Move(TypeAst)), _Argument(Move(Argument))
{
    // Invoking void returns false (i.e. the sole value of the type true), and all other types are identity functions.
    const CSemanticProgram& Program = PositiveType->GetProgram();
    if (PositiveType->GetNormalType().IsA<CVoidType>())
    {
        SetResultType(&Program._trueType);
    }
    else
    {
        SetResultType(PositiveType);
    }
}

//---------------------------------------------------------------------------------------
const CExpressionBase* CExprInvokeType::FindFirstAsyncSubExpr(const CSemanticProgram& Program) const
{
    if (const CExpressionBase* AsyncExpr = _TypeAst ? _TypeAst->FindFirstAsyncSubExpr(Program) : nullptr)
    {
        return AsyncExpr;
    }

    if (const CExpressionBase* AsyncExpr = _Argument ? _Argument->FindFirstAsyncSubExpr(Program) : nullptr)
    {
        return AsyncExpr;
    }

    return nullptr;
}

//---------------------------------------------------------------------------------------
bool CExprInvokeType::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != EAstNodeType::Invoke_Type)
    {
        return false;
    }
    const CExprInvokeType& OtherInvokeType = static_cast<const CExprInvokeType&>(Other);
    return _Report == OtherInvokeType._Report
        && _NegativeType == OtherInvokeType._NegativeType
        && _bIsFallible == OtherInvokeType._bIsFallible
        && _TypeAst == OtherInvokeType._TypeAst
        && _Argument == OtherInvokeType._Argument;
}


//=======================================================================================
// CExprPointerToReference Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprPointerToReference::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != EAstNodeType::Invoke_PointerToReference)
    {
        return false;
    }
    const CExprPointerToReference& OtherPointerToReference = static_cast<const CExprPointerToReference&>(Other);

    if (Operand() != OtherPointerToReference.Operand())
    {
        return false;
    }

    return true;
}

//---------------------------------------------------------------------------------------
const CTypeBase* CExprPointerToReference::GetResultType(const CSemanticProgram& Program) const
{
    const CTypeBase* Result = Program.GetDefaultUnknownType();

    if (Operand())
    {
        const CTypeBase* OperandResultType = Operand()->GetResultType(Program);

        if (const CPointerType* OperandPointerType = OperandResultType ? OperandResultType->GetNormalType().AsNullable<CPointerType>() : nullptr)
        {
            Result = &const_cast<CSemanticProgram&>(Program).GetOrCreateReferenceType(
                OperandPointerType->NegativeValueType(),
                OperandPointerType->PositiveValueType());
        }
    }

    return Result;
}


//=======================================================================================
// CExprSet Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprSet::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != EAstNodeType::Invoke_Set)
    {
        return false;
    }
    const CExprSet& OtherSet = static_cast<const CExprSet&>(Other);
    if (IsSubExprEqual(Operand(), OtherSet.Operand()))
    {
        return false;
    }
    return true;
}

//=======================================================================================
// CExprNewPointer Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprNewPointer::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != EAstNodeType::Invoke_NewPointer)
    {
        return false;
    }
    const CExprNewPointer& OtherNewPointer = static_cast<const CExprNewPointer&>(Other);

    if (_Report != OtherNewPointer._Report)
    {
        return false;
    }

    if (_Value != OtherNewPointer._Value)
    {
        return false;
    }

    return true;
}

//=======================================================================================
// CExprReferenceToValue Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprReferenceToValue::CExprReferenceToValue(TSPtr<CExpressionBase>&& Operand)
    : CExprUnaryOp(Move(Operand))
{}

//=======================================================================================
// CExprCodeBlock Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CTypeBase* CExprCodeBlock::GetResultType(const CSemanticProgram& Program) const
{
    return _SubExprs.IsFilled()? _SubExprs.Last()->GetResultType(Program) : &Program._trueType;
}

//=======================================================================================
// CExprLet Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CTypeBase* CExprLet::GetResultType(const CSemanticProgram& Program) const
{
    return _SubExprs.IsFilled()? _SubExprs.Last()->GetResultType(Program) : &Program._trueType;
}

//=======================================================================================
// CExprReturn Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CTypeBase* CExprReturn::GetResultType(const CSemanticProgram& Program) const
{
    return &Program._falseType;
}


//=======================================================================================
// CExprIf Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CExpressionBase* CExprIf::FindFirstAsyncSubExpr(const CSemanticProgram& Program) const
{
    const CExpressionBase* AsyncExpr;

    AsyncExpr = _Condition->FindFirstAsyncSubExpr(Program);
    if (AsyncExpr)
    {
        return AsyncExpr;
    }

    if (_ThenClause)
    {
        AsyncExpr = _ThenClause->FindFirstAsyncSubExpr(Program);
        if (AsyncExpr)
        {
            return AsyncExpr;
        }
    }

    if (_ElseClause)
    {
        AsyncExpr = _ElseClause->FindFirstAsyncSubExpr(Program);
        if (AsyncExpr)
        {
            return AsyncExpr;
        }
    }

    return nullptr;
}

//---------------------------------------------------------------------------------------
bool CExprIf::CanFail() const
{
    return (_ThenClause && _ThenClause->CanFail())
        || (_ElseClause && _ElseClause->CanFail());
}

//---------------------------------------------------------------------------------------
bool CExprIf::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != EAstNodeType::Flow_If)
    {
        return false;
    }

    const CExprIf& OtherIf = static_cast<const CExprIf&>(Other);
    return _Condition == OtherIf._Condition
        && _ThenClause == OtherIf._ThenClause
        && _ElseClause == OtherIf._ElseClause;
}

//=======================================================================================
// CExprIteration Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CExpressionBase* CExprIteration::FindFirstAsyncSubExpr(const CSemanticProgram& Program) const
{
    for (CExpressionBase* Filter : _Filters)
    {
        const CExpressionBase* AsyncExpr = Filter->FindFirstAsyncSubExpr(Program);
        if (AsyncExpr)
        {
            return AsyncExpr;
        }
    }

    return _Body.IsValid() ? _Body->FindFirstAsyncSubExpr(Program) : nullptr;
}

//---------------------------------------------------------------------------------------
bool CExprIteration::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != GetNodeType())
    {
        return false;
    }
    const CExprIteration& OtherIteration = static_cast<const CExprIteration&>(Other);

    if (_Filters.Num() != OtherIteration._Filters.Num())
    {
        return false;
    }
    for (int32_t FilterIndex = 0; FilterIndex < _Filters.Num(); ++FilterIndex)
    {
        if (!IsSubExprEqual(_Filters[FilterIndex], OtherIteration._Filters[FilterIndex]))
        {
            return false;
        }
    }

    if (!IsSubExprEqual(_Body, OtherIteration._Body) ||
        OtherIteration._AssociatedScope.IsValid() != _AssociatedScope.IsValid())
    {
        return false;
    }

    if (_AssociatedScope)
    {
        const TArray<TSRef<CDefinition>>& ItDefs = _AssociatedScope->GetDefinitions();
        const TArray<TSRef<CDefinition>>& OtherItDefs = OtherIteration._AssociatedScope->GetDefinitions();
        if (ItDefs.Num() != OtherItDefs.Num())
        {
            return false;
        }

        for (int32_t DefIndex = 0; DefIndex < ItDefs.Num(); ++DefIndex)
        {
            if (ItDefs[DefIndex] != OtherItDefs[DefIndex])
            {
                return false;
            }
        }
    }

    return true;
}

//=======================================================================================
// CIrArrayAdd Methods
//=======================================================================================

bool CIrArrayAdd::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != GetNodeType())
    {
        return false;
    }

    const CIrArrayAdd& OtherArrayAdd = static_cast<const CIrArrayAdd&>(Other);

    return *_Source == *OtherArrayAdd._Source;
}

//=======================================================================================
// CIrMapAdd Methods
//=======================================================================================

bool CIrMapAdd::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != GetNodeType())
    {
        return false;
    }
    const CIrMapAdd& OtherMapAdd = static_cast<const CIrMapAdd&>(Other);
    return
        *_Key == *OtherMapAdd._Key &&
        *_Value == *OtherMapAdd._Value;
}

const CExpressionBase* CIrMapAdd::FindFirstAsyncSubExpr(const CSemanticProgram& Program) const
{
    if (const CExpressionBase* Result = _Key->FindFirstAsyncSubExpr(Program))
    {
        return Result;
    }
    return _Value->FindFirstAsyncSubExpr(Program);
}

bool CIrMapAdd::CanFail() const
{
    return _Key->CanFail() || _Value->CanFail();
}

void CIrMapAdd::VisitChildren(SAstVisitor& Visitor) const
{
    Visitor.Visit("Key", _Key);
    Visitor.Visit("Value", _Value);
}

//=======================================================================================
// CIrArrayUnsafeCall Methods
//=======================================================================================

bool CIrArrayUnsafeCall::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != GetNodeType())
    {
        return false;
    }
    const CIrArrayUnsafeCall& OtherArrayUnsafeCall = static_cast<const CIrArrayUnsafeCall&>(Other);
    return
        *_Callee == *OtherArrayUnsafeCall._Callee &&
        *_Argument == *OtherArrayUnsafeCall._Argument;
}


//=======================================================================================
// CIrConvertToDynamic Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CIrConvertToDynamic::CIrConvertToDynamic(const CTypeBase* ResultType, TSRef<CExpressionBase>&& Operand)
    : CExprUnaryOp(Move(Operand), EVstMappingType::Ir)
{
    IrSetResultType(ResultType);
}

//=======================================================================================
// CIrConvertToDynamic Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CIrConvertFromDynamic::CIrConvertFromDynamic(const CTypeBase* ResultType, TSRef<CExpressionBase>&& Operand)
    : CExprUnaryOp(Move(Operand), EVstMappingType::Ir)
{
    IrSetResultType(ResultType);
}

//=======================================================================================
// CIrFor Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CExpressionBase* CIrFor::FindFirstAsyncSubExpr(const CSemanticProgram& Program) const
{
    const CExpressionBase* AsyncExpr = _Definition->FindFirstAsyncSubExpr(Program);
    if (AsyncExpr)
    {
        return AsyncExpr;
    }
    return _Body.IsValid() ? _Body->FindFirstAsyncSubExpr(Program) : nullptr;
}

//---------------------------------------------------------------------------------------
bool CIrFor::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != GetNodeType())
    {
        return false;
    }

    const CIrFor& OtherFor = static_cast<const CIrFor&>(Other);
    
    if (!IsSubExprEqual(*_Definition, *OtherFor._Definition) ||
        !IsSubExprEqual(_Body, OtherFor._Body) ||
        OtherFor._AssociatedScope.IsValid() != _AssociatedScope.IsValid())
    {
        return false;
    }

    if (_AssociatedScope)
    {
        const TArray<TSRef<CDefinition>>& ItDefs = _AssociatedScope->GetDefinitions();
        const TArray<TSRef<CDefinition>>& OtherItDefs = OtherFor._AssociatedScope->GetDefinitions();
        if (ItDefs.Num() != OtherItDefs.Num())
        {
            return false;
        }

        for (int32_t DefIndex = 0; DefIndex < ItDefs.Num(); ++DefIndex)
        {
            if (ItDefs[DefIndex] != OtherItDefs[DefIndex])
            {
                return false;
            }
        }
    }

    return true;
}

//=======================================================================================
// CIrForBody Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CExpressionBase* CIrForBody::FindFirstAsyncSubExpr(const CSemanticProgram& Program) const
{
    return _Body.IsValid() ? _Body->FindFirstAsyncSubExpr(Program) : nullptr;
}

//---------------------------------------------------------------------------------------
bool CIrForBody::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != GetNodeType())
    {
        return false;
    }

    const CIrForBody& OtherForBody = static_cast<const CIrForBody&>(Other);

    return IsSubExprEqual(_Body, OtherForBody._Body);
}

//=======================================================================================
// CExprArchetypeInstantiation Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprArchetypeInstantiation::CExprArchetypeInstantiation(TSRef<CExpressionBase>&& ClassAst, CExprMacroCall::CClause&& BodyAst, const CTypeBase* ResultType)
: CExpressionBase(ResultType)
, _ClassAst(Move(ClassAst))
, _BodyAst(Move(BodyAst))
{
    ULANG_ASSERTF(ResultType->GetNormalType().IsA<CClass>(), "Expected result type to be a class");
}

const CClass* CExprArchetypeInstantiation::GetClass(const CSemanticProgram& Program) const
{
    return &GetResultType(Program)->GetNormalType().AsChecked<CClass>();
}

bool CExprArchetypeInstantiation::operator==(const CExpressionBase& Other) const
{
    if (!BaseCompare(*this, Other))
    {
        return false;
    }
    const CExprArchetypeInstantiation& OtherInstantiation = static_cast<const CExprArchetypeInstantiation&>(Other);

    if (!IsSubExprEqual(_ClassAst.Get(), OtherInstantiation._ClassAst.Get()))
    {
        return false;
    }

    if (!AreSubExprsEqual(_Arguments, OtherInstantiation._Arguments))
    {
        return false;
    }

    return true;
}


//=======================================================================================
// CExprBreak Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const CTypeBase* CExprBreak::GetResultType(const CSemanticProgram& Program) const
{
    return &Program._falseType;
}


//=======================================================================================
// CExprSnippet Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
const uLang::CTypeBase* CExprSnippet::GetResultType(const CSemanticProgram& Program) const
{
    return &Program._trueType;
}


//=======================================================================================
// CExprModuleDefinition Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprModuleDefinition::CExprModuleDefinition(CModulePart& Module, TArray<TSRef<CExpressionBase>>&& Members)
    : CMemberDefinitions(Move(Members))
    , _Name(Module.GetModule()->AsNameStringView())
    , _SemanticModule(&Module)
{
    // This constructor is not used when creating IR
    ULANG_ASSERTF(Module.GetAstNode() == nullptr, "Expected reciprocal pairing with AST node");
    Module.SetAstNode(this);
}

//---------------------------------------------------------------------------------------
CExprModuleDefinition::~CExprModuleDefinition()
{
    if (_SemanticModule)
    {
        if (IsIrNode())
        {
            ULANG_ASSERTF(_SemanticModule->GetIrNode(true) == this, "Expected reciprocal pairing with IR node");
            _SemanticModule->SetIrNode(nullptr);
        }
        else
        {
            ULANG_ASSERTF(_SemanticModule->GetAstNode() == this, "Expected reciprocal pairing with Ast node");
            _SemanticModule->SetAstNode(nullptr);
        }
    }
}

//---------------------------------------------------------------------------------------
const CTypeBase* CExprModuleDefinition::GetResultType(const CSemanticProgram& Program) const
{
    return &Program._voidType;
}


//=======================================================================================
// CExprEnumDefinition Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprEnumDefinition::CExprEnumDefinition(CEnumeration& Enum, TArray<TSRef<CExpressionBase>>&& Members, EVstMappingType VstMappingType)
    : CExpressionBase(VstMappingType)
    , _Enum(Enum)
    , _Members(Move(Members))
{   
    if(IsIrNode())
    { 
        ULANG_ASSERTF(_Enum.GetIrNode(true) == nullptr, "Expected reciprocal pairing with IR node");
        _Enum.SetIrNode(this);
    }
    else
    {
        ULANG_ASSERTF(_Enum.GetAstNode() == nullptr, "Expected reciprocal pairing with AST node");
        _Enum.SetAstNode(this);
    }
}

//---------------------------------------------------------------------------------------
CExprEnumDefinition::~CExprEnumDefinition()
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_Enum.GetIrNode(true) == this, "Expected reciprocal pairing with IR node");
        _Enum.SetIrNode(nullptr);
    }
    else
    {
        ULANG_ASSERTF(_Enum.GetAstNode() == this, "Expected reciprocal pairing with AST node");
        _Enum.SetAstNode(nullptr);
    }
}

//---------------------------------------------------------------------------------------
const CTypeBase* CExprEnumDefinition::GetResultType(const CSemanticProgram& Program) const
{
    return &_Enum;
}

//---------------------------------------------------------------------------------------
void CExprEnumDefinition::VisitImmediates(SAstVisitor& Visitor) const
{
    CExpressionBase::VisitImmediates(Visitor);
    Visitor.VisitImmediate("Enum", _Enum);
}


//=======================================================================================
// CExprScopedAccessLevelDefinition Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprScopedAccessLevelDefinition::CExprScopedAccessLevelDefinition(TSRef<CScopedAccessLevelDefinition>& AccessLevelDefinition, EVstMappingType VstMappingType)
    : CExpressionBase(VstMappingType)
    , _AccessLevelDefinition(AccessLevelDefinition)
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_AccessLevelDefinition->GetIrNode(true) == nullptr, "Expected reciprocal pairing with IR node");
        _AccessLevelDefinition->SetIrNode(this);
    }
    else
    {
        ULANG_ASSERTF(_AccessLevelDefinition->GetAstNode() == nullptr, "Expected reciprocal pairing with AST node");
        _AccessLevelDefinition->SetAstNode(this);
    }
    SetResultType(_AccessLevelDefinition->GetTypeType());
}

//---------------------------------------------------------------------------------------
CExprScopedAccessLevelDefinition::~CExprScopedAccessLevelDefinition()
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_AccessLevelDefinition->GetIrNode(true) == this, "Expected reciprocal pairing with IR node");
        _AccessLevelDefinition->SetIrNode(nullptr);
    }
    else
    {
        ULANG_ASSERTF(_AccessLevelDefinition->GetAstNode() == this, "Expected reciprocal pairing with AST node");
        _AccessLevelDefinition->SetAstNode(nullptr);
    }
}

//---------------------------------------------------------------------------------------
void CExprScopedAccessLevelDefinition::VisitImmediates(SAstVisitor& Visitor) const
{
    CExpressionBase::VisitImmediates(Visitor);
    Visitor.VisitImmediate("AccessLevel", _AccessLevelDefinition->AsCode());
}

//=======================================================================================
// CExprInterfaceDefinition Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprInterfaceDefinition::CExprInterfaceDefinition(CInterface& Interface, TArray<TSRef<CExpressionBase>>&& SuperInterfaces, TArray<TSRef<CExpressionBase>>&& Members, EVstMappingType VstMappingType)
    : CExpressionBase(VstMappingType)
    , CMemberDefinitions(Move(Members))
    , _Interface(Interface)
    , _SuperInterfaces(Move(SuperInterfaces))
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_Interface.GetIrNode(true) == nullptr, "Expected reciprocal pairing with IR node");
        _Interface.SetIrNode(this);
    }
    else
    {
        ULANG_ASSERTF(_Interface.GetAstNode() == nullptr, "Expected reciprocal pairing with AST node");
        _Interface.SetAstNode(this);
    }
    SetResultType(&Interface.GetProgram().GetOrCreateTypeType(Interface._NegativeInterface, &Interface));
}

//---------------------------------------------------------------------------------------
CExprInterfaceDefinition::~CExprInterfaceDefinition()
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_Interface.GetIrNode(true) == this, "Expected reciprocal pairing with IR node");
        _Interface.SetIrNode(nullptr);
    }
    else
    {
        ULANG_ASSERTF(_Interface.GetAstNode() == this, "Expected reciprocal pairing with AST node");
        _Interface.SetAstNode(nullptr);
    }
}

//---------------------------------------------------------------------------------------
void CExprInterfaceDefinition::VisitImmediates(SAstVisitor& Visitor) const
{
    CExpressionBase::VisitImmediates(Visitor);
    Visitor.VisitImmediate("Interface", _Interface);
}


//=======================================================================================
// CExprClassDefinition Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprClassDefinition::CExprClassDefinition(CClass& Class, TArray<TSRef<CExpressionBase>>&& SuperTypes, TArray<TSRef<CExpressionBase>>&& Members, EVstMappingType VstMappingType)
    : CExpressionBase(VstMappingType)
    , CMemberDefinitions(Move(Members))
    , _Class(Class)
    , _SuperTypes(Move(SuperTypes))
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_Class._Definition->GetIrNode(true) == nullptr, "Expected reciprocal pairing with IR node");
        _Class._Definition->SetIrNode(this);
    }
    else
    {
        ULANG_ASSERTF(_Class._Definition->GetAstNode() == nullptr, "Expected reciprocal pairing with AST node");
        _Class._Definition->SetAstNode(this);
    }
    SetResultType(&Class.GetProgram().GetOrCreateTypeType(Class._NegativeClass, &Class));
}

//---------------------------------------------------------------------------------------
CExprClassDefinition::~CExprClassDefinition()
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_Class._Definition->GetIrNode(true) == this, "Expected reciprocal pairing with IR node");
        _Class._Definition->SetIrNode(nullptr);
    }
    else
    {
        ULANG_ASSERTF(_Class._Definition->GetAstNode() == this, "Expected reciprocal pairing with AST node");
        _Class._Definition->SetAstNode(nullptr);
    }
}

//---------------------------------------------------------------------------------------
void CExprClassDefinition::VisitImmediates(SAstVisitor& Visitor) const
{
    CExpressionBase::VisitImmediates(Visitor);
    Visitor.VisitImmediate("Class", *_Class.Definition());
}


//=======================================================================================
// CExprDataDefinition Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprDataDefinition::CExprDataDefinition(const TSRef<CDataDefinition>& DataMember, TSPtr<CExpressionBase>&& Element, TSPtr<CExpressionBase>&& ValueDomain, TSPtr<CExpressionBase>&& Value, EVstMappingType VstMappingType)
    : CExprDefinition(Move(Element), Move(ValueDomain), Move(Value), VstMappingType)
    , _DataMember(DataMember)
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_DataMember->GetIrNode(true) == nullptr, "Expected reciprocal pairing with IR node");
        _DataMember->SetIrNode(this);
    }
    else
    {
        ULANG_ASSERTF(_DataMember->GetAstNode() == nullptr, "Expected reciprocal pairing with AST node");
        _DataMember->SetAstNode(this);
    }
}

//---------------------------------------------------------------------------------------
CExprDataDefinition::~CExprDataDefinition()
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_DataMember->GetIrNode(true) == this, "Expected reciprocal pairing with IR node");
        _DataMember->SetIrNode(nullptr);
    }
    else
    {
        ULANG_ASSERTF(_DataMember->GetAstNode() == this, "Expected reciprocal pairing with AST node");
        _DataMember->SetAstNode(nullptr);
    }
}

//---------------------------------------------------------------------------------------
const CTypeBase* CExprDataDefinition::GetResultType(const CSemanticProgram& Program) const
{
    // If the result type is explicitly set, use it.  This is required for `var`
    // definition expressions, which are of pointer type but evaluate to the
    // contained value.
    if (const CTypeBase* ResultType = CExprDefinition::GetResultType(Program))
    {
        return ResultType;
    }
    // Otherwise, use the related data member type.
    if (_DataMember->GetType() == nullptr)
    {
        return Program.GetDefaultUnknownType();
    }
    else
    {
        return _DataMember->GetType();
    }
}


//=======================================================================================
// CExprIterationPairDefinition Methods
//=======================================================================================

CExprIterationPairDefinition::CExprIterationPairDefinition(
    TSRef<CDataDefinition>&& KeyDefinition,
    TSRef<CDataDefinition>&& ValueDefinition,
    TSPtr<CExpressionBase>&& Element,
    TSPtr<CExpressionBase>&& ValueDomain,
    TSPtr<CExpressionBase>&& Value,
    EVstMappingType VstMappingType /* = EVstMappingType::Ast */)
: CExprDefinition(Move(Element), Move(ValueDomain), Move(Value), VstMappingType)
, _KeyDefinition(Move(KeyDefinition))
, _ValueDefinition(Move(ValueDefinition))
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_KeyDefinition->GetIrNode(true) == nullptr, "Expected reciprocal pairing with IR node");
        _KeyDefinition->SetIrNode(this);

        ULANG_ASSERTF(_ValueDefinition->GetIrNode(true) == nullptr, "Expected reciprocal pairing with IR node");
        _ValueDefinition->SetIrNode(this);
    }
    else
    {
        ULANG_ASSERTF(_KeyDefinition->GetAstNode() == nullptr, "Expected reciprocal pairing with AST node");
        _KeyDefinition->SetAstNode(this);

        ULANG_ASSERTF(_ValueDefinition->GetAstNode() == nullptr, "Expected reciprocal pairing with AST node");
        _ValueDefinition->SetAstNode(this);
    }
}

CExprIterationPairDefinition::~CExprIterationPairDefinition()
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_KeyDefinition->GetIrNode(true) == this, "Expected reciprocal pairing with IR node");
        _KeyDefinition->SetIrNode(nullptr);

        ULANG_ASSERTF(_ValueDefinition->GetIrNode(true) == this, "Expected reciprocal pairing with IR node");
        _ValueDefinition->SetIrNode(nullptr);
    }
    else
    {
        ULANG_ASSERTF(_KeyDefinition->GetAstNode() == this, "Expected reciprocal pairing with AST node");
        _KeyDefinition->SetAstNode(nullptr);

        ULANG_ASSERTF(_ValueDefinition->GetAstNode() == this, "Expected reciprocal pairing with AST node");
        _ValueDefinition->SetAstNode(nullptr);
    }
}


//=======================================================================================
// CExprFunctionDefinition Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprFunctionDefinition::CExprFunctionDefinition(const TSRef<CFunction>& Function, TSPtr<CExpressionBase>&& Element, TSPtr<CExpressionBase>&& ValueDomain, TSPtr<CExpressionBase>&& Value, EVstMappingType VstMappingType)
    : CExprDefinition(Move(Element), Move(ValueDomain), Move(Value), VstMappingType)
    , _Function(Function)
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_Function->GetIrNode(true) == nullptr, "Expected reciprocal pairing with IR node");
        _Function->SetIrNode(this);
    }
    else
    {
        ULANG_ASSERTF(_Function->GetAstNode() == nullptr, "Expected reciprocal pairing with AST node");
        _Function->SetAstNode(this);
    }
}

//---------------------------------------------------------------------------------------
CExprFunctionDefinition::~CExprFunctionDefinition()
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_Function->GetIrNode(true) == this, "Expected reciprocal pairing with IR node");
        _Function->SetIrNode(nullptr);
    }
    else
    {
        ULANG_ASSERTF(_Function->GetAstNode() == this, "Expected reciprocal pairing with AST node");
        _Function->SetAstNode(nullptr);
    }
}

//---------------------------------------------------------------------------------------
const CTypeBase* CExprFunctionDefinition::GetResultType(const CSemanticProgram& Program) const
{
    return _Function->_Signature.GetFunctionType();
}


//=======================================================================================
// CExprTypeAliasDefinition Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
CExprTypeAliasDefinition::CExprTypeAliasDefinition(const TSRef<CTypeAlias>& TypeAlias, TSPtr<CExpressionBase>&& Element, TSPtr<CExpressionBase>&& ValueDomain, TSPtr<CExpressionBase>&& Value, EVstMappingType VstMappingType)
    : CExprDefinition(Move(Element), Move(ValueDomain), Move(Value), VstMappingType)
    , _TypeAlias(TypeAlias)
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_TypeAlias->GetIrNode(true) == nullptr, "Expected reciprocal pairing with IR node");
        _TypeAlias->SetIrNode(this);
    }
    else
    {
        ULANG_ASSERTF(_TypeAlias->GetAstNode() == nullptr, "Expected reciprocal pairing with AST node");
        _TypeAlias->SetAstNode(this);
    }
}

//---------------------------------------------------------------------------------------`
CExprTypeAliasDefinition::~CExprTypeAliasDefinition()
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_TypeAlias->GetIrNode(true) == this, "Expected reciprocal pairing with IR node");
        _TypeAlias->SetIrNode(nullptr);
    }
    else
    {
        ULANG_ASSERTF(_TypeAlias->GetAstNode() == this, "Expected reciprocal pairing with AST node");
        _TypeAlias->SetAstNode(nullptr);
    }
}


//=======================================================================================
// CExprUsing Methods
//=======================================================================================

const CTypeBase* CExprUsing::GetResultType(const CSemanticProgram& Program) const
{
    return &Program._voidType;
}

void CExprUsing::VisitImmediates(SAstVisitor& Visitor) const
{
    CExpressionBase::VisitImmediates(Visitor);
    if (_Module) 
    {
        Visitor.VisitImmediate("Module", *_Module);
    }
}


//=======================================================================================
// CExprImport Methods
//=======================================================================================

CExprImport::CExprImport(const TSRef<CModuleAlias>& ModuleAlias, TSRef<CExpressionBase>&& Path, EVstMappingType VstMappingType)
: CExpressionBase(VstMappingType)
, _ModuleAlias(ModuleAlias)
, _Path(Move(Path))
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_ModuleAlias->GetIrNode(true) == nullptr, "Expected reciprocal pairing with IR node");
        _ModuleAlias->SetIrNode(this);
    }
    else
    {
        ULANG_ASSERTF(_ModuleAlias->GetAstNode() == nullptr, "Expected reciprocal pairing with AST node");
        _ModuleAlias->SetAstNode(this);
    }
}

CExprImport::~CExprImport()
{
    if (IsIrNode())
    {
        ULANG_ASSERTF(_ModuleAlias->GetIrNode(true) == this, "Expected reciprocal pairing with IR node");
        _ModuleAlias->SetIrNode(nullptr);
    }
    else
    {
        ULANG_ASSERTF(_ModuleAlias->GetAstNode() == this, "Expected reciprocal pairing with AST node");
        _ModuleAlias->SetAstNode(nullptr);
    }
}

const CTypeBase* CExprImport::GetResultType(const CSemanticProgram& Program) const
{
    return Program._typeType;
}

void CExprImport::VisitImmediates(SAstVisitor& Visitor) const
{
    CExpressionBase::VisitImmediates(Visitor);
    if (_ModuleAlias)
    {
        Visitor.VisitImmediate("ModuleAlias", *_ModuleAlias);
    }
}


//=======================================================================================
// CExprVar Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
bool CExprVar::operator==(const CExpressionBase& Other) const
{
    if (Other.GetNodeType() != EAstNodeType::Definition_Var)
    {
        return false;
    }
    const CExprVar& OtherVar = static_cast<const CExprVar&>(Other);
    if (IsSubExprEqual(Operand(), OtherVar.Operand()))
    {
        return false;
    }
    return true;
}


//=======================================================================================
// CAstPackage Methods
//=======================================================================================

void CAstPackage::VisitImmediates(SAstVisitor& Visitor) const
{
    CAstNode::VisitImmediates(Visitor); 

    Visitor.VisitImmediate("Name", _Name);
    Visitor.VisitImmediate("VersePath", _VersePath);
    if (_RootModule)
    {
        Visitor.VisitImmediate("RootModule", _RootModule->GetModule());
    }
    Visitor.BeginArray("Dependencies", _Dependencies.Num());
    for (const CAstPackage* Dependency : _Dependencies)
    {
        Visitor.VisitImmediate("", Dependency->_Name);
    }
    Visitor.EndArray();

    Visitor.VisitImmediate("Scope", ToString(_VerseScope));
    Visitor.VisitImmediate("Role", ToString(_Role));
    Visitor.VisitImmediate("EffectiveVerseVersion", static_cast<int64_t>(_EffectiveVerseVersion));
    Visitor.VisitImmediate("UploadedAtFNVersion", static_cast<int64_t>(_UploadedAtFNVersion));

    Visitor.VisitImmediate("bAllowNative", _bAllowNative);
    Visitor.VisitImmediate("bTreatModulesAsImplicit", _bTreatModulesAsImplicit);
    Visitor.VisitImmediate("AllowExperimental", _bAllowExperimental);
}

bool CAstPackage::CanSeeDefinition(const CDefinition& Definition) const
{
    if (Definition.IsBuiltIn())
    {
        return true;
    }
    else if (const CModule* Module = Definition.AsNullable<CModule>())
    {
        for (const CModulePart* Part : Module->GetParts())
        {
            const CAstPackage* DefinitionPackage = Part->GetPackage();
            if (!DefinitionPackage || DefinitionPackage == this || _Dependencies.Contains(DefinitionPackage))
            {
                return true;
            }
        }
        return false;
    }
    else
    {
        const CAstPackage* DefinitionPackage = Definition._EnclosingScope.GetPackage();
        return !DefinitionPackage || DefinitionPackage == this || _Dependencies.Contains(DefinitionPackage);
    }
}

//=======================================================================================
// CAstCompilationUnit Methods
//=======================================================================================

EPackageRole CAstCompilationUnit::GetRole() const
{
    ULANG_ASSERT(!_Packages.IsEmpty());
    
    EPackageRole Role = _Packages[0]->_Role;

#if ULANG_DO_CHECK
    // Validate assumption that all packages have the same role
    for (int32_t Index = 1; Index < _Packages.Num(); ++Index)
    {
        ULANG_ASSERT(_Packages[Index]->_Role == Role);
    }
#endif

    return Role;
}

bool CAstCompilationUnit::IsAllowNative() const
{
    ULANG_ASSERT(_Packages.Num() == 1); // Semantic analyzer should enforce this
    return _Packages[0]->_bAllowNative;
}

//=======================================================================================
// CAstProject Methods
//=======================================================================================

const CAstPackage* CAstProject::FindPackageByName(const CUTF8String& PackageName) const
{
    for (const CAstCompilationUnit* CompilationUnit : _OrderedCompilationUnits)
    {
        for (const CAstPackage* Package : CompilationUnit->Packages())
        {
            if (Package->_Name == PackageName)
            {
                return Package;
            }
        }
    }

    return nullptr;
}

int32_t CAstProject::GetNumPackages() const
{
    int32_t NumPackages = 0;
    for (const CAstCompilationUnit* CompilationUnit : _OrderedCompilationUnits)
    {
        NumPackages += CompilationUnit->Packages().Num();
    }
    return NumPackages;
}

}  // namespace uLang
