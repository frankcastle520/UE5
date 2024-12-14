// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/SharedPointerArray.h"
#include "uLang/Common/Templates/Conditionals.h"
#include "uLang/Common/Text/Symbol.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Semantics/DataDefinition.h"
#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Semantics/Signature.h"
#include "uLang/SourceProject/PackageRole.h"
#include "uLang/Syntax/VstNode.h"

namespace uLang
{

// Forward declarations
class CEnumeration;
class CAstNode;
class CAstCompilationUnit;
class CExpressionBase;
class CExprCodeBlock;
class CSemanticProgram;
class CSnippet;
class CModule;
class CModulePart;
class CModuleAlias;
class CControlScope;
class CTypeAlias;
class CTypeVariable;
class CEnumerator;

// NOTE: (YiLiangSiew) Currently, Visual Verse relies on the numerical values of these enumerations. If you
// change this, be sure to update `BaseVisualVerseSettings.ini` as well.
#define VERSE_VISIT_AST_NODE_TYPES(v) \
    /* Helper expressions */ \
    v(Error_, CExprError)                /* An expression that could not be analyzed due to an error. Attempts to analyze should propagate the error. */ \
    v(Placeholder_, CExprPlaceholder)    /* Corresponds to a Placeholder Vst node. */ \
    v(External, CExprExternal)           /* An (unknown) external expression - should never reach the code generator */ \
    v(PathPlusSymbol, CExprPathPlusSymbol) /* Path of the current scope plus a symbol - /unrealengine.com/UnrealEngine@5 */ \
    \
    /* Literals */ \
    v(Literal_Logic, CExprLogic)              /* Logic literal - true/false */ \
    v(Literal_Number, CExprNumber)            /* Integer literal - 42, 0, -123, 123_456_789, 0x12fe, 0b101010 */ \
                                              /* or Float literal - 42.0, 0.0, -123.0, 123_456.0, 3.14159, .5, -.33, 4.2e1, -1e6, 7.5e-8 */ \
    v(Literal_Char, CExprChar)                /* Character literal - 'a', '\n' */ \
    v(Literal_String, CExprString)            /* String literal - "Hello, world!", "Line 1\nLine2" */ \
    v(Literal_Path, CExprPath)                /* Path literal - /unrealengine.com/UnrealEngine@5 */ \
    v(Literal_Enum, CExprEnumLiteral)         /* Enumerator - Color.Red, Size.XXL */ \
    v(Literal_Type, CExprType)                /* Type - type{<expr>} */ \
    v(Literal_Function, CExprFunctionLiteral) /* a=>b or function(a){b} */ \
    \
    /* Identifiers */ \
    v(Identifier_Unresolved, CExprIdentifierUnresolved)                 /* An existing identifier that is unresolved. It is produced by desugaring and consumed by analysis. */ \
    v(Identifier_Class, CExprIdentifierClass)                           /* Type identifier - e.g. my_type, int, color, string */ \
    v(Identifier_Module, CExprIdentifierModule)                         /* Module name */ \
    v(Identifier_ModuleAlias, CExprIdentifierModuleAlias)               /* Module alias name */ \
    v(Identifier_Enum, CExprEnumerationType)                            /* Enum name */ \
    v(Identifier_Interface, CExprInterfaceType)                         /* Interface name */ \
    v(Identifier_Data, CExprIdentifierData)                             /* Scoped data-definition (class member, local, etc.) */ \
    v(Identifier_TypeAlias, CExprIdentifierTypeAlias)                   /* Access to type alias */ \
    v(Identifier_TypeVariable, CExprIdentifierTypeVariable)             /* Access to a type variable */ \
    v(Identifier_Function, CExprIdentifierFunction)                     /* Access to functions */ \
    v(Identifier_OverloadedFunction, CExprIdentifierOverloadedFunction) /* An overloaded function identifier that hasn't been resolved to a specific overload. */ \
    v(Identifier_Self, CExprSelf)                                       /* Access to the instance the current function is being invoked on. */ \
    v(Identifier_Local, CExprLocal)                                     /* Access to the the current scope for the identifier that has this qualifier. */ \
    v(Identifier_BuiltInMacro, CExprIdentifierBuiltInMacro)             /* An intrinsic macro: option{}, logic{}, array{}, etc. */ \
    \
    /* Multi purpose syntax */ \
    v(Definition, CExprDefinition) /* represents syntactic forms elt:domain=value, elt:domain, elt=value */ \
    \
    /* Macro */ \
    v(MacroCall, CExprMacroCall) /* macro invocations should be resolved by the compiler; the code generator should never encounter this. */ \
    \
    /* Invocations */ \
    v(Invoke_Invocation, CExprInvocation)                 /* Routine call - expr1.call(expr2, expr3) */ \
    v(Invoke_UnaryArithmetic, CExprUnaryArithmetic)       /* negation */ \
    v(Invoke_BinaryArithmetic, CExprBinaryArithmetic)     /* add, sub, mul, div; two operands only */ \
    v(Invoke_ShortCircuitAnd, CExprShortCircuitAnd)       /* short-circuit evaluation of logic and */ \
    v(Invoke_ShortCircuitOr, CExprShortCircuitOr)         /* short-circuit evaluation of logic or */ \
    v(Invoke_LogicalNot, CExprLogicalNot)                 /* logical not operator */ \
    v(Invoke_Comparison, CExprComparison)                 /* comparison operators */ \
    v(Invoke_QueryValue, CExprQueryValue)                 /* Querying the value of a logic or option. */ \
    v(Invoke_MakeOption, CExprMakeOption)                 /* Making an option value. */ \
    v(Invoke_MakeArray, CExprMakeArray)                   /* Making an array value. */ \
    v(Invoke_MakeMap, CExprMakeMap)                       /* Making a map value. */ \
    v(Invoke_MakeTuple, CExprMakeTuple)                   /* Making a tuple value - (1, 2.0f, "three") */ \
    v(Invoke_TupleElement, CExprTupleElement)             /* Tuple element access `TupleExpr(Idx)` */ \
    v(Invoke_MakeRange, CExprMakeRange)                   /* Making a range value. */ \
    v(Invoke_Type, CExprInvokeType)                       /* Invoke a type as a function on a value - type(expr) or type[expr]. */ \
    v(Invoke_PointerToReference, CExprPointerToReference) /* Access the mutable reference behind the pointer. */ \
    v(Invoke_Set, CExprSet)                               /* Evaluate operand to an l-expression. */ \
    v(Invoke_NewPointer, CExprNewPointer)                 /* Create a new pointer from an initial value. */ \
    v(Invoke_ReferenceToValue, CExprReferenceToValue)     /* Evaluates the value of an expression yielding a reference type. */ \
    \
    v(Assignment, CExprAssignment) /* Assignment operation - expr1 = expr2, expr1 := expr2, expr1 += expr2, etc. */ \
    \
    /* TypeFormers */ \
    v(Invoke_ArrayFormer, CExprArrayTypeFormer)                         /* Invoke (at compile time) a formation of an array of another type */ \
    v(Invoke_GeneratorFormer, CExprGeneratorTypeFormer)                 /* Invoke (at compile time) a formation of an generator type. */ \
    v(Invoke_MapFormer, CExprMapTypeFormer)                             /* Invoke (at compile time) a formation of a map from a key and value type. */ \
    v(Invoke_OptionFormer, CExprOptionTypeFormer)                       /* Invoke (at compile time) a formation of an option of some primitive type */ \
    v(Invoke_Subtype, CExprSubtype)                                     /* Invoke (at compile time) a formation of a metaclass type. */ \
    v(Invoke_TupleType, CExprTupleType)                                 /* Get or create a tuple tuple based on `tuple(type1, type2, ...)` */ \
    v(Invoke_Arrow, CExprArrow)                                         /* Create a function type from a parameter and return type. */ \
    \
    v(Invoke_ArchetypeInstantiation, CExprArchetypeInstantiation) /* Initializer list style instantiation - Type{expr1, id=expr2, ...} */ \
    \
    /* Flow Control */ \
    v(Flow_CodeBlock, CExprCodeBlock)       /* Code block - block {expr1; expr2} */ \
    v(Flow_Let, CExprLet)                   /* let {definition1; definition2} */ \
    v(Flow_Defer, CExprDefer)               /* defer {expr1; expr2} */ \
    v(Flow_If, CExprIf)                     /* Conditional with failable tests- if (Test[]) {clause1}, if (Test[]) {clause1} else {else_clause} */ \
    v(Flow_Iteration, CExprIteration)       /* Bounded iteration over an iterable type - for(Num:Nums, ...) {DoStuff(Num, ...)} */ \
    v(Flow_Loop, CExprLoop)                 /* Simple loop - loop {DoStuff()} */ \
    v(Flow_Break, CExprBreak)               /* Control flow early exit - loop {if (IsEarlyExit[]) {break}; DoLoopStuff()} */ \
    v(Flow_Return, CExprReturn)             /* Return statement - return expr */ \
    v(Flow_ProfileBlock, CExprProfileBlock) /* profile block */\
    \
    v(Ir_For, CIrFor)                               /* Bounded iteration over an iterable type - for(Num:Nums) {DoStuff(Num)} */ \
    v(Ir_ForBody, CIrForBody)                       /* Only used inside CIrFor, and only around the body of the innermost CIrFor */ \
    v(Ir_ArrayAdd, CIrArrayAdd)                     /* Append an item to an array */ \
    v(Ir_MapAdd, CIrMapAdd)                         /* Add a key-value pair to a map */ \
    v(Ir_ArrayUnsafeCall, CIrArrayUnsafeCall)       /* Infallibly access an element of an array */ \
    v(Ir_ConvertToDynamic, CIrConvertToDynamic)     /* Converts a value to a dynamically typed value. */ \
    v(Ir_ConvertFromDynamic, CIrConvertFromDynamic) /* Converts a value from a dynamically typed value. */ \
    \
    /* Concurrency Primitives */ \
    v(Concurrent_Sync, CExprSync)                  /* sync {Coro1(); Coro2()} */ \
    v(Concurrent_Rush, CExprRush)                  /* rush {Coro1(); Coro2()} */ \
    v(Concurrent_Race, CExprRace)                  /* race {Coro1(); Coro2()} */ \
    v(Concurrent_SyncIterated, CExprSyncIterated)  /* sync(Item:Container) {Item.Coro1(); Coro2(Item)} */ \
    v(Concurrent_RushIterated, CExprRushIterated)  /* rush(Item:Container) {Item.Coro1(); Coro2(Item)} */ \
    v(Concurrent_RaceIterated, CExprRaceIterated)  /* race(Item:Container) {Item.Coro1(); Coro2(Item)} */ \
    v(Concurrent_Branch, CExprBranch)              /* branch {Coro1(); Coro2()} */ \
    v(Concurrent_Spawn, CExprSpawn)                /* spawn {Coro()} */ \
    \
    /* Definitions */ \
    v(Definition_Module, CExprModuleDefinition) \
    v(Definition_Enum, CExprEnumDefinition) \
    v(Definition_Interface, CExprInterfaceDefinition) \
    v(Definition_Class, CExprClassDefinition) \
    v(Definition_Data, CExprDataDefinition) \
    v(Definition_IterationPair, CExprIterationPairDefinition) \
    v(Definition_Function, CExprFunctionDefinition) \
    v(Definition_TypeAlias, CExprTypeAliasDefinition) \
    v(Definition_Using, CExprUsing) \
    v(Definition_Import, CExprImport) \
    v(Definition_Where, CExprWhere) \
    v(Definition_Var, CExprVar) \
    v(Definition_ScopedAccessLevel, CExprScopedAccessLevelDefinition) \
    v(Invoke_MakeNamed, CExprMakeNamed)        /* ?ParamName:=Value pair - or default value placeholder (value not set) */ \
    \
    /* Containing Context - may contain expressions though they aren't expressions themselves */ \
    v(Context_Project, CAstProject) \
    v(Context_CompilationUnit, CAstCompilationUnit) \
    v(Context_Package, CAstPackage) \
    v(Context_Snippet, CExprSnippet)

#define FORWARD_DECLARE(_, Class) class Class;
VERSE_VISIT_AST_NODE_TYPES(FORWARD_DECLARE)
#undef FORWARD_DECLARE

/**
 * This is used to differentiate between different types of AST nodes when it is only
 * known that an instance is of type CAstNode, but not the specific subclass.
 * It is returned by the method CAstNode::GetNodeType()
 **/
enum class EAstNodeType : uint8_t
{
#define VISIT_AST_NODE_TYPE(Name, Class) Name,
    VERSE_VISIT_AST_NODE_TYPES(VISIT_AST_NODE_TYPE)
#undef VISIT_AST_NODE_TYPE
};

struct SAstNodeTypeInfo
{
    const char* _EnumeratorName;
    const char* _CppClassName;
};

/** Returns the name of an AST node type. */
VERSECOMPILER_API SAstNodeTypeInfo GetAstNodeTypeInfo(uLang::EAstNodeType Type);

//---------------------------------------------------------------------------------------
// Indicates whether an expression should return immediately - such as functions, after a
// duration (including immediately) such as coroutines or either.
enum class EInvokeTime : uint8_t
{
    Immediate  = 1 << 0, // May only call an immediate expression (such as a function call) and any async expression (such as a coroutine call) should result in an error.
    Async      = 1 << 1, // May only call an async expression (such as a coroutine call) and any immediate expression (such as a function call) should result in an error. Only true within one of the pre-defined calling contexts (`sync{}`, `race{}`, etc.).
    Any_ = Immediate | Async // Calling either immediate or async expressions is allowed.
};

inline const char* InvokeTimeAsCString(EInvokeTime InvokeTime)
{
    switch(InvokeTime)
    {
    case EInvokeTime::Immediate: return "Immediate";
    case EInvokeTime::Async: return "Async";
    case EInvokeTime::Any_: return "Any_";
    default: ULANG_UNREACHABLE();
    }
}

/**
 * Abstract base for applying some operation / iterating through AST structures.
 * @see CAstNode::VisitChildren()
 **/
struct SAstVisitor
{
    /** Called when visiting an AST node **/
    virtual void VisitImmediate(const char* FieldName, CUTF8StringView Value) {}
    virtual void VisitImmediate(const char* FieldName, int64_t Value) {}
    virtual void VisitImmediate(const char* FieldName, double Value) {}
    virtual void VisitImmediate(const char* FieldName, bool Value) {}
    virtual void VisitImmediate(const char* FieldName, const CTypeBase* Type) {}
    virtual void VisitImmediate(const char* FieldName, const CDefinition& Definition) {}
    virtual void VisitImmediate(const char* FieldName, const Verse::Vst::Node& VstNode) {}

    void VisitImmediate(const char* FieldName, const char* CString) { VisitImmediate(FieldName, CUTF8StringView(CString)); }

    virtual void Visit(const char* FieldName, CAstNode& AstNode) = 0;
    virtual void BeginArray(const char* FieldName, intptr_t Num) {}
    virtual void VisitElement(CAstNode& AstNode) = 0;
    virtual void EndArray() {}

    template<typename NodeType, bool bAllowNull, typename AllocatorType, typename... AllocatorArgTypes>
    void Visit(const char* FieldName, const TSPtrG<NodeType, bAllowNull, AllocatorType, AllocatorArgTypes...>& NodePointer)
    {
        if (!bAllowNull || NodePointer.IsValid())
        {
            Visit(FieldName, *NodePointer);
        }
    }

    template<typename NodeType, bool bAllowNull, typename AllocatorType, typename... AllocatorArgTypes>
    void VisitElement(const TSPtrG<NodeType, bAllowNull, AllocatorType, AllocatorArgTypes...>& NodePointer)
    {
        if (!bAllowNull || NodePointer.IsValid())
        {
            VisitElement(*NodePointer);
        }
    }

    template<typename NodeType, bool bAllowNull, typename NodeAllocatorType, typename ElementAllocatorType, typename... ElementAllocatorArgTypes>
    void VisitArray(const char* FieldName, const TArrayG<TSPtrG<NodeType, bAllowNull, NodeAllocatorType>, ElementAllocatorType, ElementAllocatorArgTypes...>& Array)
    {
        BeginArray(FieldName, Array.Num());
        for (const TSPtrG<NodeType, bAllowNull, NodeAllocatorType>& Element : Array)
        {
            if (!bAllowNull || Element.IsValid())
            {
                VisitElement(*Element);
            }
        }
        EndArray();
    }
    
    template<typename NodeType, bool bAllowNull, typename ElementAllocatorType, typename... RawAllocatorArgTypes>
    void VisitArray(const char* FieldName, const TSPtrArrayG<NodeType, bAllowNull, ElementAllocatorType, RawAllocatorArgTypes...>& Array)
    {
        BeginArray(FieldName, Array.Num());
        for (NodeType* Node : Array)
        {
            if (!bAllowNull || Node)
            {
                VisitElement(*Node);
            }
        }
        EndArray();
    }
};

enum class EVstMappingType
{
    Ast, AstNonReciprocal, Ir
};

/**
 * Abstract base class for AST nodes.
 */
class VERSECOMPILER_API CAstNode : public CSharedMix
{
public:
    CAstNode(EVstMappingType VstMappingType = EVstMappingType::Ast) : _VstMappingType(VstMappingType) {}
    
    virtual ~CAstNode();

    virtual EAstNodeType GetNodeType() const = 0;
    virtual const CExpressionBase* AsExpression() const { return nullptr; }
    virtual CExpressionBase* AsExpression() { return nullptr; }
    virtual bool MayHaveAttributes() const { return true; }

    /**
     * Iterates over this AST node's immediate fields, calling Visitor.VisitImmediate for each immediate field.
     */
    virtual void VisitImmediates(SAstVisitor& Visitor) const
    {
        const char* VstMappingTypeString;
        switch(_VstMappingType)
        {
        case EVstMappingType::Ast: VstMappingTypeString = "Ast"; break;
        case EVstMappingType::AstNonReciprocal: VstMappingTypeString = "AstNonReciprocal"; break;
        case EVstMappingType::Ir: VstMappingTypeString = "Ir"; break;
        default: ULANG_UNREACHABLE();
        };
        Visitor.VisitImmediate("VstMappingType", VstMappingTypeString);
        if (_MappedVstNode)
        {
            Visitor.VisitImmediate("MappedVstNode", *_MappedVstNode);
        }
    }

    /**
     * Iterates over this AST node's direct children, calling Visitor.Visit for each child.
     **/
    virtual void VisitChildren(SAstVisitor& Visitor) const {}

    /**
     * Wrapper for VisitChildren that takes a lambda that is called for each child.
     * The signature for the lambda should be (const SAstVisitor& RecurseVisitor, CAstNode&)
     * You can use RecurseVisitor to recursively call VisitChildren with the same lambda.
     */
    template<typename FunctionType>
    ULANG_FORCEINLINE void VisitChildrenLambda(FunctionType&& Function) const;

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Text methods
    virtual CUTF8String GetErrorDesc() const = 0;

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Syntax <> Semantic Mapping
    const Verse::Vst::Node* GetMappedVstNode() const { return _MappedVstNode; }

    void SetNonReciprocalMappedVstNode(const Verse::Vst::Node* VstNode)
    {
        _VstMappingType = EVstMappingType::AstNonReciprocal;
        _MappedVstNode = VstNode; 
    }

    void SetIrMappedVstNode(const Verse::Vst::Node* VstNode)
    {
        _VstMappingType = EVstMappingType::Ir;
        _MappedVstNode = VstNode;
    }

    // True if this AstNode is used to represent an IrNode. Needed to disable some asserts and clean up code.
    // Will be removed when IrNodes have their own type.
    bool IsIrNode() const
    {
        return _VstMappingType == EVstMappingType::Ir;
    }

    bool IsVstMappingReciprocal() const
    {
        return _VstMappingType == EVstMappingType::Ast;
    }

protected:

    friend struct Verse::Vst::Node;
    // Syntax<>Semantic mapping
    mutable EVstMappingType _VstMappingType;
    mutable const Verse::Vst::Node* _MappedVstNode{nullptr};
};

/**
 * Abstract base class for AST expressions.
 * Created from abstract syntax tree (CExpressionBase and CSemanticProgram) generated by semantic analyzer.
 **/
class VERSECOMPILER_API CExpressionBase : public CAstNode, public CAttributable
{
public:

    explicit CExpressionBase(EVstMappingType VstMappingType = EVstMappingType::Ast) : CAstNode(VstMappingType) {}
    explicit CExpressionBase(const CTypeBase* InResultType)
    : _Report(SAnalysisResult{InResultType})
    {}

    virtual const CExpressionBase* AsExpression() const override { return this; }
    virtual CExpressionBase* AsExpression() override { return this; }
    virtual bool MayHaveAttributes() const override { return false; }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override
    {
        CAstNode::VisitImmediates(Visitor);
        if (_Report.IsSet())
        {
            Visitor.VisitImmediate("ResultType", _Report->ResultType);
        }
    }

    // Determines if expression is immediate (completes within the current update/frame) or 
    // async (completes within the current update/frame or later).
    //
    // @TODO: SOL-1423, DetermineInvokeTime() re-traverses the expression tree, which could add up time wise 
    //        (approaching on n^2) -- there should be a beter way to check this on the initial ProcessExpression()
    EInvokeTime DetermineInvokeTime(const CSemanticProgram& Program) const { return (FindFirstAsyncSubExpr(Program) == nullptr) ? EInvokeTime::Immediate : EInvokeTime::Async; }

    // Returns itself or the first async sub-expression or return nullptr if it and all its sub-expressions are immediate.
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const  { return nullptr; }

    // Returns whether the expression may fail.
    virtual bool CanFail() const { return false; }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Type methods
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const;
    void SetResultType(const CTypeBase* InResultType);
    void RefineResultType(const CTypeBase* RefinedResultType);
    
    bool IsAnalyzed() const { return _Report.IsSet(); }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Comparison
    static bool BaseCompare(const CExpressionBase& A, const CExpressionBase& B)
    {
        return
            A.GetNodeType() == B.GetNodeType() &&
            A._Report == B._Report;
    }
    virtual bool operator==(const CExpressionBase& Other) const = 0;
    bool operator!=(const CExpressionBase& Other) const { return !(*this == Other); }
    static bool IsSubExprEqual(const CExpressionBase* Lhs, const CExpressionBase* Rhs);
    static bool IsSubExprEqual(const CExpressionBase& Lhs, const CExpressionBase& Rhs);
    static bool IsSubExprEqual(const TSPtr<CExpressionBase>& Lhs, const TSPtr<CExpressionBase>& Rhs);
    static bool IsSubExprEqual(const TSRef<CExpressionBase>& Lhs, const TSRef<CExpressionBase>& Rhs);
    static bool AreSubExprsEqual(const TSPtrArray<CExpressionBase>& Lhs, const TSPtrArray<CExpressionBase>& Rhs);
    static bool AreSubExprsEqual(const TArray<TSPtr<CExpressionBase>>& Lhs, const TArray<TSPtr<CExpressionBase>>& Rhs);

    const CTypeBase* IrGetResultType() const { return _Report ? _Report->ResultType : nullptr; }
    void IrSetResultType(const CTypeBase* TypeBase)
    {
        if (TypeBase)
        {
            _Report = SAnalysisResult{ TypeBase };
        }
        else
        {
            _Report.Reset();
        }
    }
protected:

    // Analysis Results
    struct SAnalysisResult
    {
        bool operator==(const SAnalysisResult& Other) const { return ResultType == Other.ResultType; }
        /** The type to which this node evaluates. */
        const CTypeBase* ResultType;
    };

    TOptional<SAnalysisResult> _Report;

};  // CExpressionBase


/**
 * Base for expressions that have an array of subexpressions
 * Subclasses: CExprMakeArray, CExprMakeMap, CExprMakeTuple, CExprCodeBlock, CExprConcurrentBlockBase
 */
class VERSECOMPILER_API CExprCompoundBase : public CExpressionBase
{
public:
    // Methods

    explicit CExprCompoundBase(int32_t ReserveSubExprNum, EVstMappingType VstMappingType = EVstMappingType::Ast)
        : CExpressionBase(VstMappingType)
    {
        _SubExprs.Reserve(ReserveSubExprNum);
    }

    explicit CExprCompoundBase(TSPtrArray<CExpressionBase>&& SubExprs, EVstMappingType VstMappingType = EVstMappingType::Ast)
        : CExpressionBase(VstMappingType)
        , _SubExprs(Move(SubExprs))
    {}

    explicit CExprCompoundBase(TSPtr<CExpressionBase>&& SubExpr1, TSPtr<CExpressionBase>&& SubExpr2)
        : CExprCompoundBase(2)
    {
        AppendSubExpr(Move(SubExpr1));
        AppendSubExpr(Move(SubExpr2));
    }

    CExprCompoundBase() = default;

    bool IsEmpty() const                                               { return _SubExprs.IsEmpty(); }
    int32_t SubExprNum() const                                         { return _SubExprs.Num(); }
    const uLang::CExpressionBase*      GetLastSubExpr() const          { return _SubExprs.Last(); }
    const TSPtrArray<CExpressionBase>& GetSubExprs() const             { return _SubExprs; }
    TSPtrArray<CExpressionBase>&       GetSubExprs()                   { return _SubExprs; }
    TSPtrArray<CExpressionBase>&&      TakeSubExprs()                  { return Move(_SubExprs); }
    void AppendSubExpr(TSPtr<CExpressionBase> SubExpr)               { _SubExprs.Add(Move(SubExpr)); }
    void PrependSubExpr(TSPtr<CExpressionBase> SubExpr)              { _SubExprs.Insert(Move(SubExpr), 0); }
    void SetSubExprs(TSPtrArray<CExpressionBase> AnalyzedExprs)      { _SubExprs = Move(AnalyzedExprs); }
    void ReplaceSubExpr(TSPtr<CExpressionBase> SubExpr, int32_t Index)
    {
        ULANG_ASSERTF(Index >= 0 && Index < _SubExprs.Num(), "Replacing invalid subexpression index");
        _SubExprs.ReplaceAt(Move(SubExpr), Index);
    }

    virtual bool CanFail() const override;
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override;
    virtual void VisitChildren(SAstVisitor& Visitor) const override  { Visitor.VisitArray("SubExprs", _SubExprs); }
    virtual bool operator==(const CExpressionBase& Other) const override;

protected:

    // Internal data

    TSPtrArray<CExpressionBase> _SubExprs;
};


/**
 * Base class of binary operators.
 */
class VERSECOMPILER_API CExprBinaryOp : public CExpressionBase
{
private:
    // Operands
    TSPtr<CExpressionBase> _Lhs;
    TSPtr<CExpressionBase> _Rhs;

public:

    const TSPtr<CExpressionBase>& Lhs() const { return _Lhs; }
    const TSPtr<CExpressionBase>& Rhs() const { return _Rhs; }
    TSPtr<CExpressionBase>&& TakeRhs() { return Move(_Rhs); }
    void SetLhs(TSPtr<CExpressionBase>&& NewLhs) { _Lhs = Move(NewLhs); }
    void SetRhs(TSPtr<CExpressionBase>&& NewRhs) { _Rhs = Move(NewRhs); }

    CExprBinaryOp(TSPtr<CExpressionBase>&& Lhs, TSPtr<CExpressionBase>&& Rhs) : _Lhs(Move(Lhs)), _Rhs(Move(Rhs)) {}

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override { const CExpressionBase* AsyncExpr = _Lhs->FindFirstAsyncSubExpr(Program); return AsyncExpr ? AsyncExpr : _Rhs->FindFirstAsyncSubExpr(Program); }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Lhs", _Lhs); Visitor.Visit("Rhs", _Rhs); }
};



// Note - if these subclasses of CExpressionBase become more sophisticated they should
// probably be moved to their own files.


/**
 * Expression for external{} macro used in digests - should never reach the code generator
 */
class VERSECOMPILER_API CExprExternal : public CExpressionBase
{
public:
    // Methods

    explicit CExprExternal(const CSemanticProgram& Program);

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::External; }
    virtual CUTF8String  GetErrorDesc() const override { return "external{}"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::External; }
};


/**
 * Logic literal - true/false
 **/
class VERSECOMPILER_API CExprLogic : public CExpressionBase
{
public:

    bool _Value;

    explicit CExprLogic(const CSemanticProgram& Program, bool Value);

    virtual EAstNodeType GetNodeType() const override   { return EAstNodeType::Literal_Logic; }
    virtual CUTF8String  GetErrorDesc() const override  { return _Value ? "true" : "false"; }
    virtual bool operator==(const CExpressionBase& Other) const override  { return BaseCompare(*this, Other) && _Value == static_cast<const CExprLogic&>(Other)._Value; }

    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("Value", _Value); }
};


/**
 * Integer literal - 42, 0, -123, 123_456_789, 0x12fe, 0b101010
 */
class VERSECOMPILER_API CExprNumber : public CExpressionBase
{
private:
    union
    {
        Integer _IntValue;
        Float _FloatValue;
    };
    bool _bIsFloat : 1;

public:

    ULANG_FORCEINLINE explicit CExprNumber()
        : _IntValue(0)
        , _bIsFloat(false)
    {}
 
    explicit CExprNumber(CSemanticProgram&, Integer);

    explicit CExprNumber(CSemanticProgram&, Float);

    bool IsFloat() const { return _bIsFloat; }
    Integer GetIntValue() const { ULANG_ASSERTF(!_bIsFloat, "Float number being treated as integer."); return _IntValue; }
    void SetIntValue(CSemanticProgram&, Integer);
    Float GetFloatValue() const { ULANG_ASSERTF( _bIsFloat, "Int number being treated as float"); return _FloatValue; }
    void SetFloatValue(CSemanticProgram&, Float);

    virtual EAstNodeType GetNodeType() const override   { return EAstNodeType::Literal_Number; }
    virtual CUTF8String  GetErrorDesc() const override  { return _bIsFloat ? "float literal" : "integer literal"; }
    virtual bool operator==(const CExpressionBase& Other) const override {
        return BaseCompare(*this, Other) && this->_IntValue == static_cast<const CExprNumber&>(Other)._IntValue;
    }

    virtual void VisitImmediates(SAstVisitor& Visitor) const override
    {
        CExpressionBase::VisitImmediates(Visitor);
        if (_bIsFloat)
        {
            Visitor.VisitImmediate("FloatValue", _FloatValue);
        }
        else 
        {
            Visitor.VisitImmediate("IntValue", _IntValue);
        }
    }
};


/**
 * Character literal - 'H' '\n' '{0c00}' '{0u1f600}'
 **/
class VERSECOMPILER_API CExprChar : public CExpressionBase
{
public:
    enum class EType
    {
        UTF8CodeUnit,
        UnicodeCodePoint
    };

    uint32_t _CodePoint;
    EType _Type;

    explicit CExprChar(uint32_t CodePoint, EType Type)
        : _CodePoint(CodePoint)
        , _Type(Type)
    {
        if(_Type == EType::UTF8CodeUnit)
        {
            ULANG_ASSERTF(_CodePoint <= 0xFF, "utf8 code units must be <= 0xFF");
        }
    }

    virtual EAstNodeType GetNodeType() const override   { return EAstNodeType::Literal_Char; }
    virtual CUTF8String  GetErrorDesc() const override  { return "char literal"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Literal_Char && _CodePoint == static_cast<const CExprChar&>(Other)._CodePoint; }

    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("CodePoint", static_cast<int64_t>(_CodePoint)); }
};


/**
 * String literal - "Hello, world!", "Line 1\nLine2"
 **/
class VERSECOMPILER_API CExprString : public CExpressionBase
{
public:
    // Note that in the future this (or a related class) may include an array of substrings and sub-expressions for string interpolation.

    // Ready to use string with any escaped characters translated
    CUTF8String _String;

    explicit CExprString(CUTF8String String)
        : _String(Move(String)) {}

    virtual EAstNodeType GetNodeType() const override   { return EAstNodeType::Literal_String; }
    virtual CUTF8String  GetErrorDesc() const override  { return "string literal"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Literal_String && _String == static_cast<const CExprString&>(Other)._String; }

    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("String", _String); }
};

/**
 * Path literal - /unrealengine.com/UnrealEngine
 **/
class VERSECOMPILER_API CExprPath : public CExpressionBase
{
public:
    // Ready to use path with any escaped characters translated
    CUTF8String _Path;

    explicit CExprPath(CUTF8String Path)
        : _Path(Move(Path)) {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Literal_Path; }
    virtual CUTF8String  GetErrorDesc() const override { return "path literal"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Literal_Path && _Path == static_cast<const CExprPath&>(Other)._Path; }

    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("Path", _Path); }
};

/**
 * Expression that evaluates to the path of the current scope, plus a given symbol, semantic analysis replaces this node with a CExprString
 **/
class VERSECOMPILER_API CExprPathPlusSymbol : public CExpressionBase
{
public:
    const CSymbol _Symbol;

    explicit CExprPathPlusSymbol(const CSymbol& Symbol)
        : _Symbol(Symbol) {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::PathPlusSymbol; }
    virtual CUTF8String  GetErrorDesc() const override { return "path plus symbol"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::PathPlusSymbol && _Symbol == static_cast<const CExprPathPlusSymbol&>(Other)._Symbol; }
};

/**
 * The base class of identifiers expressions.
 */
class VERSECOMPILER_API CExprIdentifierBase : public CExpressionBase
{
public:

    CExprIdentifierBase(TSPtr<CExpressionBase>&& Context = nullptr, TSPtr<CExpressionBase>&& Qualifier = nullptr)
        : _Context(Move(Context)), _Qualifier(Move(Qualifier))
    {}

    const TSPtr<CExpressionBase>& Context() const { return _Context; }
    const TSPtr<CExpressionBase>& Qualifier() const { return _Qualifier; }
    
    TSPtr<CExpressionBase>&& TakeContext() { return Move(_Context); }
    TSPtr<CExpressionBase>&& TakeQualifier() { return Move(_Qualifier); }

    void SetContext(TSPtr<CExpressionBase> Context)
    {
        _Context = Move(Context);
    }

    void SetQualifier(TSPtr<CExpressionBase> Qualifier)
    {
        _Qualifier = Move(Qualifier);
    }

    // CAstNode interface.
    virtual bool MayHaveAttributes() const override { return true; }

    // CExpressionBase interface.
    virtual bool CanFail() const override { return _Context.IsValid() && _Context->CanFail(); }
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override
    {
         const CExpressionBase* AsyncExpr = nullptr;
         if (_Context) { AsyncExpr = _Context->FindFirstAsyncSubExpr(Program); }
         if (!AsyncExpr && _Qualifier) { AsyncExpr =  _Qualifier->FindFirstAsyncSubExpr(Program); }
         return AsyncExpr;
    }
    virtual bool operator==(const CExpressionBase& Other) const override
    {
        return CExpressionBase::BaseCompare(*this, Other)
            && _Context == static_cast<const CExprIdentifierBase&>(Other)._Context
            && _Qualifier == static_cast<const CExprIdentifierBase&>(Other)._Qualifier;
    }

    virtual void VisitChildren(SAstVisitor& Visitor) const override
    {
        Visitor.Visit("Context", _Context);
    }

private:
    TSPtr<CExpressionBase> _Context;
    TSPtr<CExpressionBase> _Qualifier;
};

/**
 * Enumerator - #GlobalEnum.enumeration, #.enumeration, MyType#Confirm.yes, #Confirm.yes,
 * #.yes, MyType@set_size#size.medium, #size.medium, #.medium, #position.upper, #.upper
 **/
class VERSECOMPILER_API CExprEnumLiteral : public CExprIdentifierBase
{
public:
    // Specific enumerator this represents - note that it also contains a pointer to its enumeration type.
    const CEnumerator* _Enumerator;

    ULANG_FORCEINLINE explicit CExprEnumLiteral(const CEnumerator* Enumerator, TSPtr<CExpressionBase>&& Context = nullptr, TSPtr<CExpressionBase>&& Qualifier = nullptr)
        : CExprIdentifierBase(Move(Context), Move(Qualifier))
        , _Enumerator(Enumerator)
    {}

    virtual EAstNodeType     GetNodeType() const override   { return EAstNodeType::Literal_Enum; }
    virtual CUTF8String      GetErrorDesc() const override  { return "enumerator"; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual bool operator==(const CExpressionBase& Other) const override { return CExprIdentifierBase::operator==(Other) && _Enumerator == static_cast<const CExprEnumLiteral&>(Other)._Enumerator; }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;
};

/**
 * Type expression - type{<expr>}
 */
class VERSECOMPILER_API CExprType : public CExpressionBase
{
public:
    
    const TSRef<CExpressionBase> _AbstractValue;

    CExprType(TSRef<CExpressionBase>&& AbstractValue, const CTypeType& TypeType)
        : _AbstractValue(Move(AbstractValue))
    {
        SetResultType(&TypeType);
    }

    const CTypeType* GetTypeType() const
    {
        return static_cast<const CTypeType*>(_Report->ResultType);
    }

    const CTypeBase* GetType() const
    {
        const CTypeType* TypeType = GetTypeType();
        return TypeType->PositiveType();
    }

    virtual EAstNodeType     GetNodeType() const override   { return EAstNodeType::Literal_Type; }
    virtual CUTF8String      GetErrorDesc() const override  { return "type"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return GetNodeType() == Other.GetNodeType() && IsSubExprEqual(_AbstractValue, static_cast<const CExprType&>(Other)._AbstractValue); }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("AbstractValue", _AbstractValue); }
};

/**
 * Function literal - a=>b or function(a){b}
 */
class VERSECOMPILER_API CExprFunctionLiteral : public CExpressionBase
{
public:

    CExprFunctionLiteral(TSRef<CExpressionBase>&& Domain, TSRef<CExpressionBase>&& Range)
    : _Domain(Move(Domain))
    , _Range(Move(Range))
    {}

    const TSRef<CExpressionBase>& Domain() const { return _Domain; }
    const TSRef<CExpressionBase>& Range () const { return _Range ; }

    void SetDomain(TSRef<CExpressionBase>&& NewDomain) { _Domain = Move(NewDomain); }
    void SetRange(TSRef<CExpressionBase>&& NewRange  ) { _Range  = Move(NewRange) ; }

    virtual EAstNodeType GetNodeType() const override  { return EAstNodeType::Literal_Function; }
    virtual CUTF8String  GetErrorDesc() const override { return "function literal"; }
    virtual bool operator==(const CExpressionBase& Other) const override
    {
        return GetNodeType() == Other.GetNodeType()
            && IsSubExprEqual(_Domain, static_cast<const CExprFunctionLiteral&>(Other)._Domain)
            && IsSubExprEqual(_Range, static_cast<const CExprFunctionLiteral&>(Other)._Range);
    }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Domain", _Domain); Visitor.Visit("Range", _Range); }

private:
    TSRef<CExpressionBase> _Domain;
    TSRef<CExpressionBase> _Range;
};

/**
 * Access to the instance the current function is being invoked on.
 **/
class VERSECOMPILER_API CExprSelf : public CExprIdentifierBase
{
public:
    CExprSelf(const CTypeBase* Type, TSPtr<CExpressionBase>&& Qualifier = nullptr)
    : CExprIdentifierBase(nullptr, Move(Qualifier))
    {
        SetResultType(Type);
    }

    virtual EAstNodeType GetNodeType() const override   { return EAstNodeType::Identifier_Self; }
    virtual CUTF8String  GetErrorDesc() const override  { return "'Self'"; }
};

/// Represents the `(local:) qualifier`.
class VERSECOMPILER_API CExprLocal : public CExprIdentifierBase
{
public:
    CExprLocal(const CScope& Scope) : CExprIdentifierBase(), _Scope(Scope)
    {}

    virtual EAstNodeType GetNodeType() const override
    {
        return EAstNodeType::Identifier_Local;
    }

    virtual CUTF8String GetErrorDesc() const override
    {
        return "'(local:)'";
    }

    const CScope& GetScope() const
    {
        return _Scope;
    }

private:
    const CScope& _Scope;
};

/**
 * Represents a name of a a compiler built-in macro; e.g. option, array.
 * These should always be resolved by analysis and never make their way into code gen.
 **/
class VERSECOMPILER_API CExprIdentifierBuiltInMacro : public CExprIdentifierBase
{
public:
    const CSymbol _Symbol;

    CExprIdentifierBuiltInMacro(const CSymbol Symbol, const CTypeBase* Type)
        : CExprIdentifierBase(nullptr, nullptr)
        , _Symbol(Symbol)
    {
        SetResultType(Type);
    }

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Identifier_BuiltInMacro; }
    virtual CUTF8String GetErrorDesc() const override { return _Symbol.AsStringView(); }
    virtual bool operator==(const CExpressionBase& Other) const override { return CExprIdentifierBase::operator==(Other) && _Symbol == static_cast<const CExprIdentifierBuiltInMacro&>(Other)._Symbol; }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("Symbol", _Symbol.AsStringView()); }
};

/**
 * An unresolved type identifier that is produced by desugaring, and consumed by analysis.
 **/
class VERSECOMPILER_API CExprIdentifierUnresolved : public CExprIdentifierBase
{
public:
    const CSymbol _Symbol;

    // Used for some internal compiler-generated code that is
    // allowed to lookup identifiers that are otherwise restricted
    // (private, internal, ...)
    bool _bAllowUnrestrictedAccess;
	
    const bool _bAllowReservedOperators;
   
    CExprIdentifierUnresolved(const CSymbol& Symbol, TSPtr<CExpressionBase>&& Context = nullptr, TSPtr<CExpressionBase>&& Qualifier = nullptr, bool bAllowReservedOperators = false)
        : CExprIdentifierBase( Move(Context), Move(Qualifier))
        , _Symbol(Symbol)
        , _bAllowUnrestrictedAccess(false)
        , _bAllowReservedOperators(bAllowReservedOperators)
    {}

    // Use with extreme caution! Setting this allows this identifier lookup to succeed where it 
    // would otherwise fail due to not having permission
    void GrantUnrestrictedAccess() { _bAllowUnrestrictedAccess = true; }

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Identifier_Unresolved; }
    virtual bool MayHaveAttributes() const override { return true; }
    virtual CUTF8String  GetErrorDesc() const override { return _Symbol.AsString(); }
    virtual bool operator==(const CExpressionBase& Other) const override { return CExprIdentifierBase::operator==(Other) && _Symbol == static_cast<const CExprIdentifierUnresolved&>(Other)._Symbol; }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("Symbol", _Symbol.AsStringView()); }
};


/**
 * Type identifier - MyType
 **/
class VERSECOMPILER_API CExprIdentifierClass : public CExprIdentifierBase
{
public:

    CExprIdentifierClass(const CTypeType* Type, TSPtr<CExpressionBase>&& Context = nullptr, TSPtr<CExpressionBase>&& Qualifier = nullptr);

    const CTypeType* GetTypeType(const CSemanticProgram& Program) const;
    const CClass* GetClass(const CSemanticProgram& Program) const;

    virtual EAstNodeType GetNodeType() const override   { return EAstNodeType::Identifier_Class; }
    virtual CUTF8String  GetErrorDesc() const override;
};

/**
 * Module identifier
 **/
class VERSECOMPILER_API CExprIdentifierModule : public CExprIdentifierBase
{
public:

    CExprIdentifierModule(const CModule* Module, TSPtr<CExpressionBase>&& Context = nullptr, TSPtr<CExpressionBase>&& Qualifier = nullptr);

    CModule const* GetModule(const CSemanticProgram& Program) const;

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Identifier_Module; }
    virtual CUTF8String  GetErrorDesc() const override { return "module identifier"; }
};

/**
 * Module alias identifier
 **/
class VERSECOMPILER_API CExprIdentifierModuleAlias : public CExprIdentifierBase
{
public:

    const CModuleAlias& _ModuleAlias;

    CExprIdentifierModuleAlias(const CModuleAlias& ModuleAlias, TSPtr<CExpressionBase>&& Context = nullptr, TSPtr<CExpressionBase>&& Qualifier = nullptr)
    : CExprIdentifierBase(Move(Context), Move(Qualifier))
    , _ModuleAlias(ModuleAlias)
    {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Identifier_ModuleAlias; }
    virtual CUTF8String  GetErrorDesc() const override { return "module alias identifier"; }
};

/**
 * Enum identifier
 * @jira SOL-1013 : Shouldn't this inherit from CExprIdentifierClass?
 **/
class VERSECOMPILER_API CExprEnumerationType : public CExprIdentifierBase
{
public:

    CExprEnumerationType(const CTypeType* TypeType, TSPtr<CExpressionBase>&& Context = nullptr, TSPtr<CExpressionBase>&& Qualifier = nullptr)
        : CExprIdentifierBase(Move(Context), Move(Qualifier))
    {
        SetResultType(TypeType);
    }

    const CTypeType* GetTypeType(const CSemanticProgram& Program) const;
    const CEnumeration* GetEnumeration(const CSemanticProgram& Program) const;

    virtual EAstNodeType GetNodeType() const override   { return EAstNodeType::Identifier_Enum; }
    virtual CUTF8String  GetErrorDesc() const override  { return "enum type identifier"; }
};

/**
 * Interface identifier
 * @jira SOL-1013 : Shouldn't this inherit from CExprIdentifierClass?
 **/
class VERSECOMPILER_API CExprInterfaceType : public CExprIdentifierBase
{
public:

    // Identified type

    CExprInterfaceType(const CTypeType* TypeType, TSPtr<CExpressionBase>&& Context = nullptr, TSPtr<CExpressionBase>&& Qualifier = nullptr)
        : CExprIdentifierBase(Move(Context), Move(Qualifier))
    {
        SetResultType(TypeType);
    }

    const CTypeType* GetTypeType(const CSemanticProgram& Program) const { return &GetResultType(Program)->GetNormalType().AsChecked<CTypeType>(); }
    const CInterface* GetInterface(const CSemanticProgram& Program) const;

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Identifier_Interface; }
    virtual CUTF8String GetErrorDesc() const override { return "interface type identifier"; }
};


/**
 * Local or class identifier - temp, arg, captured
 **/
class VERSECOMPILER_API CExprIdentifierData : public CExprIdentifierBase
{
public:

    // The variable this expression is referring to
    const CDataDefinition& _DataDefinition;

    CExprIdentifierData(const CSemanticProgram& Program, const CDataDefinition& DataDefinition, TSPtr<CExpressionBase>&& Context = nullptr, TSPtr<CExpressionBase>&& Qualifier = nullptr);

    virtual const CSymbol&   GetName() const                { return _DataDefinition.GetName(); }

    virtual EAstNodeType     GetNodeType() const override   { return EAstNodeType::Identifier_Data; }
    virtual CUTF8String      GetErrorDesc() const override  { return _DataDefinition.AsNameStringView(); }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual bool operator==(const CExpressionBase& Other) const override { return CExprIdentifierBase::operator==(Other) && &_DataDefinition == &static_cast<const CExprIdentifierData&>(Other)._DataDefinition; }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("DataDefinition", _DataDefinition); }
};

/**
 * Access to a type alias
 */
class VERSECOMPILER_API CExprIdentifierTypeAlias : public CExprIdentifierBase
{
public:

    // The type alias this expression is referring to
    const CTypeAlias& _TypeAlias;

    CExprIdentifierTypeAlias(const CTypeAlias& TypeAlias, TSPtr<CExpressionBase>&& Context = nullptr, TSPtr<CExpressionBase>&& Qualifier = nullptr);

    virtual EAstNodeType     GetNodeType() const override   { return EAstNodeType::Identifier_TypeAlias; }
    virtual CUTF8String      GetErrorDesc() const override  { return "type alias identifier"; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual bool operator==(const CExpressionBase& Other) const override { return CExprIdentifierBase::operator==(Other) && &_TypeAlias == &static_cast<const CExprIdentifierTypeAlias&>(Other)._TypeAlias; }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;
};

/**
 * Access to a type variable
 */
class VERSECOMPILER_API CExprIdentifierTypeVariable : public CExprIdentifierBase
{
public:

    // The type variable this expression is referring to
    const CTypeVariable& _TypeVariable;

    CExprIdentifierTypeVariable(const CTypeVariable& TypeVariable, TSPtr<CExpressionBase>&& Context = nullptr, TSPtr<CExpressionBase>&& Qualifier = nullptr);

    virtual EAstNodeType     GetNodeType() const override   { return EAstNodeType::Identifier_TypeVariable; }
    virtual CUTF8String      GetErrorDesc() const override  { return "type variable identifier"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return CExprIdentifierBase::operator==(Other) && &_TypeVariable == &static_cast<const CExprIdentifierTypeVariable&>(Other)._TypeVariable; }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;
};

/**
 * Access to instance function members
 **/
class VERSECOMPILER_API CExprIdentifierFunction : public CExprIdentifierBase
{
public:

    const CFunction& _Function;
    // `CFlowType`s created as part of instantiating `_Function`.
    TArray<SInstantiatedTypeVariable> _InstantiatedTypeVariables;
    const CTypeBase* _ConstructorNegativeReturnType;
    const bool _bSuperQualified;

    CExprIdentifierFunction(
        const CFunction& Function,
        const CTypeBase* ResultType,
        TSPtr<CExpressionBase>&& Context = nullptr,
        TSPtr<CExpressionBase>&& Qualifier = nullptr)
        : CExprIdentifierFunction(Function, {}, ResultType, nullptr, Move(Context), Move(Qualifier), false)
    {}

    CExprIdentifierFunction(
        const CFunction& Function,
        TArray<SInstantiatedTypeVariable> InstTypeVariables,
        const CTypeBase* ResultType,
        const CTypeBase* ConstructorNegativeReturnType,
        TSPtr<CExpressionBase>&& Context,
        TSPtr<CExpressionBase>&& Qualifier,
        bool bSuperQualified);

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Identifier_Function; }
    virtual CUTF8String GetErrorDesc() const override { return "function identifier"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return CExprIdentifierBase::operator==(Other) && &_Function == &static_cast<const CExprIdentifierFunction&>(Other)._Function; }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;
};

/**
 * An overloaded function identifier that hasn't been resolved to a specific overload.
 */
class VERSECOMPILER_API CExprIdentifierOverloadedFunction : public CExprIdentifierBase
{
public:

    const TArray<const CFunction*> _FunctionOverloads;
    bool _bConstructor;
    const CSymbol _Symbol;
    const CTypeBase* _TypeOverload;

    // Used for some internal compiler-generated code that is
    // allowed to lookup identifiers that are otherwise restricted
    // (private, internal, ...)
    bool _bAllowUnrestrictedAccess;

    CExprIdentifierOverloadedFunction(
        TArray<const CFunction*>&& OverloadedFunctions,
        bool bConstructor,
        const CSymbol Symbol,
        const CTypeBase* OverloadedType,
        TSPtr<CExpressionBase>&& Context,
        TSPtr<CExpressionBase>&& Qualifier,
        const CTypeBase* Type);

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Identifier_OverloadedFunction; }
    virtual CUTF8String GetErrorDesc() const override { return "overloaded function identifier"; }
    virtual bool operator==(const CExpressionBase& Other) const override 
    {
        return CExprIdentifierBase::operator==(Other) 
            && _FunctionOverloads == static_cast<const CExprIdentifierOverloadedFunction&>(Other)._FunctionOverloads 
            && _TypeOverload == static_cast<const CExprIdentifierOverloadedFunction&>(Other)._TypeOverload;
    }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;
};

/**
 * Represents all definitions (and assignments) supported by Verse.
 * They are:
 *      element:valueDomain=value   e.g. x:int=5     e.g. f(x:int):int=x*x
 *      element:valueDomain         e.g. x:int       e.g. f(x:int):int
 *      element=value               e.g. x=5         e.g. f(x:int)=x*x
 */
class VERSECOMPILER_API CExprDefinition : public CExpressionBase
{
public:
    CExprDefinition(TSPtr<CExpressionBase>&& Element, TSPtr<CExpressionBase>&& ValueDomain, TSPtr<CExpressionBase>&& Value, EVstMappingType VstMappingType = EVstMappingType::Ast)
        : CExpressionBase(VstMappingType)
    {
        if (Element) { SetElement(Move(Element.AsRef())); }
        if (ValueDomain) { SetValueDomain(Move(ValueDomain.AsRef())); }
        if (Value) { SetValue(Move(Value.AsRef())); }
    }

    explicit CExprDefinition(EVstMappingType VstMappingType = EVstMappingType::Ast) : CExpressionBase(VstMappingType)  {}

    const TSPtr<CExpressionBase>& Element() const { return _Element; }
    void SetElement(TSRef<CExpressionBase>&& Element) { _Element = Move(Element); }
    TSPtr<CExpressionBase> TakeElement() { return Move(_Element); }

    void SetName(CSymbol Name) { _Name = Name; }
    CSymbol GetName() const { return _Name; }
    bool IsNamed() const { return !_Name.IsNull(); }

    const TSPtr<CExpressionBase>& ValueDomain() const { return _ValueDomain; }
    void SetValueDomain(TSRef<CExpressionBase>&& ValueDomain) { _ValueDomain = Move(ValueDomain); }
    TSPtr<CExpressionBase> TakeValueDomain() { return Move(_ValueDomain); }

    const TSPtr<CExpressionBase>& Value() const { return _Value; }
    void SetValue(TSRef<CExpressionBase>&& Value) { _Value = Move(Value); }
    TSPtr<CExpressionBase> TakeValue() { return Move(_Value); }

    // CAstNode interface.
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Definition; }
    virtual CUTF8String GetErrorDesc() const override { return CUTF8String("definition"); }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Element", _Element); Visitor.Visit("ValueDomain", _ValueDomain); Visitor.Visit("Value", _Value); }
    virtual bool MayHaveAttributes() const override { return true; }

    // CExpressionBase interface.
    virtual bool operator==(const CExpressionBase& Other) const override;
    virtual bool CanFail() const override;
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override;

protected:
    TSPtr<CExpressionBase> _Element;
    TSPtr<CExpressionBase> _ValueDomain;
    TSPtr<CExpressionBase> _Value;
    CSymbol _Name; // If non-null, then usage requires being ?named and presence of `_Value` indicates that it has a default
};


enum class EMacroClauseTag : uint32_t
{
    None = 0x1 << 0,
    Of = 0x1 << 1,
    Do = 0x1 << 2
};

constexpr EMacroClauseTag operator| (EMacroClauseTag A, EMacroClauseTag B) { return static_cast<EMacroClauseTag>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B)); }
constexpr bool HasAnyTags(EMacroClauseTag A, EMacroClauseTag B) { return 0 != (static_cast<uint32_t>(A) & static_cast<uint32_t>(B)); }
constexpr bool HasAllTags(EMacroClauseTag A, EMacroClauseTag RequiredTags)
{
    return RequiredTags == static_cast<EMacroClauseTag>(static_cast<uint32_t>(A) & static_cast<uint32_t>(RequiredTags));
}
inline const char* MacroClauseTagAsCString(EMacroClauseTag Tag)
{
    switch(Tag)
    {
    case EMacroClauseTag::None: return "None";
    case EMacroClauseTag::Of: return "Of";
    case EMacroClauseTag::Do: return "Do";
    default: ULANG_UNREACHABLE();
    };
}
inline const char* MacroClauseFormAsCString(Verse::Vst::Clause::EForm Form)
{
    switch(Form)
    {
    case Verse::Vst::Clause::EForm::Synthetic: return "Synthetic";
    case Verse::Vst::Clause::EForm::NoSemicolonOrNewline: return "NoSemicolonOrNewline";
    case Verse::Vst::Clause::EForm::HasSemicolonOrNewline: return "HasSemicolonOrNewline";
    case Verse::Vst::Clause::EForm::IsAppendAttributeHolder: return "IsAppendAttributeHolder";
    case Verse::Vst::Clause::EForm::IsPrependAttributeHolder: return "IsPrependAttributeHolder";
    default: ULANG_UNREACHABLE();
    };
}

/** A macro call of the form m1{}, m2(){}, or more generally m(){}keyword_1{}keyword_2{}...keyword_N{} */
class VERSECOMPILER_API CExprMacroCall : public CExpressionBase
{

public:

    /** A macro is an identifier followed by any number of tagged clauses. This class represents a single clause. */
    class CClause
    {
    public:
        using EForm = Verse::Vst::Clause::EForm;

        CClause(EMacroClauseTag Tag, EForm Form, TArray<TSRef<CExpressionBase>>&& Exprs)
            : _Tag(Tag)
            , _Form(Form)
            , _Exprs(Move(Exprs))
        {}

        EMacroClauseTag Tag() const { return _Tag; }
        EForm Form() const { return _Form; }
        TArray<TSRef<CExpressionBase>> const & Exprs() const { return _Exprs; }
        TArray<TSRef<CExpressionBase>>       & Exprs()       { return _Exprs; }


    private:
        EMacroClauseTag _Tag;
        Verse::Vst::Clause::EForm _Form;
        TArray<TSRef<CExpressionBase>> _Exprs;
    };


public:
    CExprMacroCall( TSRef<CExpressionBase>&& Name, int32_t NumClauses = 0 )
        : _Name(Move(Name))
        , _Clauses()
    {
        if (NumClauses != 0)
        {
            _Clauses.Reserve(NumClauses);
        }
    }

    void AppendClause(CClause&& Clause)
    {
        _Clauses.Add(Move(Clause));
    }

    TSRef<CExpressionBase> const & Name() const { return _Name; }
    TSRef<CExpressionBase>       & Name()       { return _Name; }
    void SetName(TSRef<CExpressionBase>&& NewName) { _Name = Move(NewName); }

    TSRef<CExpressionBase>&& TakeName() { return Move(_Name); }

    TArray<CClause> const& Clauses() const { return _Clauses; }
    TArray<CClause>      & Clauses()       { return _Clauses; }

    TArray<CClause>&& TakeClauses() { return Move(_Clauses); }

private:
    // Inherited via CExpressionBase
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::MacroCall; }
    virtual CUTF8String GetErrorDesc() const override { return "macro invocation"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return BaseCompare(*this,Other); }

    virtual void VisitChildren(SAstVisitor& Visitor) const override
    {
        Visitor.Visit("Name", _Name);
        Visitor.BeginArray("Clauses", _Clauses.Num());
        for (const CClause& Clause : _Clauses)
        {
            Visitor.VisitArray("Exprs", Clause.Exprs());
        }
        Visitor.EndArray();
    }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override
    {
        CExpressionBase::VisitImmediates(Visitor);
        Visitor.BeginArray("Clauses", _Clauses.Num());
        for (const CClause& Clause : _Clauses)
        {
            Visitor.VisitImmediate("Tag", MacroClauseTagAsCString(Clause.Tag()));
            Visitor.VisitImmediate("Form", MacroClauseFormAsCString(Clause.Form()));
        }
        Visitor.EndArray();
    }

private:
    TSRef<CExpressionBase> _Name;
    TArray<CClause> _Clauses;
};


/**
 * Routine call - expr1.call(expr2, expr3)
 **/
class VERSECOMPILER_API CExprInvocation : public CExpressionBase
{
public:

    enum class EBracketingStyle
    {
        Undefined,
        Parentheses,
        SquareBrackets,
    };
    EBracketingStyle _CallsiteBracketStyle = EBracketingStyle::Undefined;

    CExprInvocation(EBracketingStyle CallsiteBracketStyle, TSRef<CExpressionBase>&& Callee, TSRef<CExpressionBase>&& Argument)
        : _CallsiteBracketStyle(CallsiteBracketStyle)
        , _Callee(Move(Callee))
        , _Argument(Move(Argument))
    {}

    CExprInvocation(EBracketingStyle CallsiteBracketStyle, const TSRef<CExpressionBase>& Callee, const TSRef<CExpressionBase>& Argument)
        : _CallsiteBracketStyle(CallsiteBracketStyle)
        , _Callee(Callee)
        , _Argument(Argument)
    {}

    CExprInvocation(EBracketingStyle CallsiteBracketStyle,
                    const TSRef<CExpressionBase>& Callee,
                    const TSRef<CExpressionBase>& Argument,
                    const CFunctionType* ResolvedCalleeType,
                    const CTypeBase* ResultType)
     : CExprInvocation(CallsiteBracketStyle, Callee, Argument)
    {
      SetResolvedCalleeType(ResolvedCalleeType);
      SetResultType(ResultType);
    }

    explicit CExprInvocation(TSRef<CExpressionBase>&& Argument)
        : _Argument(Move(Argument))
    {}

    CExprInvocation(CExprInvocation&& Rhs)
        : _Callee(Move(Rhs._Callee))
        , _Argument(Move(Rhs._Argument))
        , _ResolvedCalleeType(Rhs._ResolvedCalleeType)
    {}

    const TSPtr<CExpressionBase>& GetCallee() const { return _Callee; }
    TSPtr<CExpressionBase>&& TakeCallee()           { return Move(_Callee); }
    void SetCallee(TSPtr<CExpressionBase>&& Callee) { _Callee = Move(Callee); }

    const TSPtr<CExpressionBase>& GetArgument() const { return _Argument; }
    TSPtr<CExpressionBase>&& TakeArgument() { return Move(_Argument); }
    void SetArgument(TSPtr<CExpressionBase>&& Arguments) { _Argument = Move(Arguments); }
    
    const CFunctionType* GetResolvedCalleeType() const;
    void SetResolvedCalleeType(const CFunctionType* ResolvedCalleeType) { _ResolvedCalleeType = ResolvedCalleeType; }

    /// Get the function that this invocation calls                     
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override;

    virtual bool CanFail() const override;
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_Invocation; }
    virtual CUTF8String GetErrorDesc() const override { return "invocation"; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Callee", _Callee); Visitor.Visit("Argument", _Argument); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override
    {
        CExpressionBase::VisitImmediates(Visitor);
        const char* BracketStyleString;
        switch(_CallsiteBracketStyle)
        {
        case EBracketingStyle::Undefined: BracketStyleString = "Undefined"; break;
        case EBracketingStyle::Parentheses: BracketStyleString = "Parentheses"; break;
        case EBracketingStyle::SquareBrackets: BracketStyleString = "SquareBrackets"; break;
        default: ULANG_UNREACHABLE();
        };
        Visitor.VisitImmediate("CallsiteBracketStyle", BracketStyleString);

        if (_ResolvedCalleeType)
        {
            Visitor.VisitImmediate("ResolvedCalleeType", _ResolvedCalleeType);
        }
    }
    virtual bool operator==(const CExpressionBase& Other) const override;

private:
    // Callee subexpression - The function to call.
    TSPtr<CExpressionBase> _Callee;

    // Argument - possible a tuple of expressions, included named expressions
    TSPtr<CExpressionBase> _Argument;
    
    // More often than not, the function type could be inferred from _Callee, but in the case of generics,
    // you want to store the function type of the resolved generic.
    const CFunctionType* _ResolvedCalleeType{nullptr};
};

VERSECOMPILER_API const CExprIdentifierFunction* GetConstructorInvocationCallee(const CExprInvocation&);

VERSECOMPILER_API const CExprIdentifierFunction* GetConstructorInvocationCallee(const CExpressionBase&);

VERSECOMPILER_API bool IsConstructorInvocation(const CExprInvocation&);

VERSECOMPILER_API bool IsConstructorInvocation(const CExpressionBase&);

/**
 * Tuple element access `TupleExpr(Idx)`
 **/
class VERSECOMPILER_API CExprTupleElement : public CExpressionBase
{
public:

    // Expression that results in tuple to access element from
    TSPtr<CExpressionBase> _TupleExpr;

    // Index resolved from `_ElemIdxExpr`
    Integer _ElemIdx;

    // Expression used to determine index - currently must be an integer literal CExprNumber.
    // `_ElemIdx` is resolved form. This is stored just to track source information in VST nodes.
    TSPtr<CExpressionBase> _ElemIdxExpr;


    CExprTupleElement(CExprInvocation& Invocation) : _TupleExpr(Invocation.TakeCallee()), _ElemIdx(-1) { Invocation.GetMappedVstNode()->AddMapping(this); }
    CExprTupleElement(TSPtr<CExpressionBase> TupleExpr, Integer ElemIdx, const Verse::Vst::Node* MappedVstNode) : _TupleExpr(TupleExpr), _ElemIdx(ElemIdx) { _MappedVstNode = MappedVstNode; }

    virtual void VisitChildren(SAstVisitor& Visitor) const override           { Visitor.Visit("TupleExpr", _TupleExpr); Visitor.Visit("ElemIdxExpr", _ElemIdxExpr); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override         { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("ElemIdx", _ElemIdx); }
    virtual EAstNodeType   GetNodeType() const override                       { return EAstNodeType::Invoke_TupleElement; }
    virtual CUTF8String    GetErrorDesc() const override                      { return "tuple element access"; }
    virtual bool operator==(const CExpressionBase& Other) const override;
};


/**
 * Assignment -- expr1 = expr2, expr1 := expr2, expr1 += expr2, etc.
 */
class VERSECOMPILER_API CExprAssignment : public CExpressionBase
{
public:
    using EOp = Verse::Vst::Assignment::EOp;

    CExprAssignment(EOp Op, TSPtr<CExpressionBase>&& Lhs, TSPtr<CExpressionBase>&& Rhs)
        : _Op(Op), _Lhs(Move(Lhs)), _Rhs(Move(Rhs))
    {}

    EOp Op() const { return _Op; }
    const TSPtr<CExpressionBase>& Lhs() const { return _Lhs; }
    const TSPtr<CExpressionBase>& Rhs() const { return _Rhs; }

    TSPtr<CExpressionBase>&& TakeLhs() { return Move(_Lhs); }
    TSPtr<CExpressionBase>&& TakeRhs() { return Move(_Rhs); }

    void SetLhs(TSPtr<CExpressionBase>&& Lhs) { _Lhs = Move(Lhs); }
    void SetRhs(TSPtr<CExpressionBase>&& Rhs) { _Rhs = Move(Rhs); }

    //~ Begin CExpressionBase interface
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override { const CExpressionBase* AsyncExpr = _Lhs->FindFirstAsyncSubExpr(Program); return AsyncExpr ? AsyncExpr : _Rhs->FindFirstAsyncSubExpr(Program); }
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Assignment; }
    virtual CUTF8String GetErrorDesc() const override { return "assignment"; }
    
    virtual bool CanFail() const override { return _Lhs->CanFail() || _Rhs->CanFail(); }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Lhs", _Lhs); Visitor.Visit("Rhs", _Rhs); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("Op", Verse::Vst::AssignmentOpAsCString(_Op)); }
    virtual bool operator==(const CExpressionBase& Other) const override;
    //~ End CExpressionBase interface

private:
    const EOp _Op;
    TSPtr<CExpressionBase> _Lhs;
    TSPtr<CExpressionBase> _Rhs;
};


/**
 * Base class of unary operators.
 */
class VERSECOMPILER_API CExprUnaryOp : public CExpressionBase
{
public:

    explicit CExprUnaryOp(TSPtr<CExpressionBase>&& Operand, EVstMappingType VstMappingType = EVstMappingType::Ast)
        : CExpressionBase(VstMappingType)
    {
        SetOperand(Move(Operand));
    }

    const TSPtr<CExpressionBase>& Operand() const { return _Operand; }
    TSPtr<CExpressionBase>&& TakeOperand() { return Move(_Operand); }
    void SetOperand(TSPtr<CExpressionBase>&& Operand) { _Operand = Move(Operand); }

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override { return _Operand.IsValid() ? _Operand->FindFirstAsyncSubExpr(Program) : nullptr; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Operand", _Operand); }

private:
    
    TSPtr<CExpressionBase> _Operand;
};


class VERSECOMPILER_API CExprUnaryArithmetic : public CExprInvocation
{
public:
    enum class EOp
    {
        Negate
    };

    CExprUnaryArithmetic(EOp Op, TSRef<CExpressionBase>&& Rhs)
        : CExprInvocation(Move(Rhs))
        , _Op(Op)
    {
    }

    TSRef<CExpressionBase> Operand() const { return GetArgument().AsRef(); }
    void SetOperand(TSPtr<CExpressionBase>&& NewOperand) { SetArgument(Move(NewOperand)); }
    TSPtr<CExpressionBase>&& TakeOperand() { return TakeArgument(); }

    EOp Op() const { return _Op; }

private:
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_UnaryArithmetic; }
    virtual bool CanFail() const override { return false; }
    virtual CUTF8String GetErrorDesc() const override { return "unary negation"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return CExprInvocation::operator==(Other); }

    EOp _Op;
};


class VERSECOMPILER_API CExprBinaryArithmetic : public CExprInvocation
{
public:
    enum class EOp
    {
        Add,
        Sub,
        Mul,
        Div
    };

    CExprBinaryArithmetic(EOp Op, TSRef<CExpressionBase>&& Argument)
        : CExprInvocation(Move(Argument))
        , _Op(Op)
    {
    }

    EOp Op() const { return _Op; }

private:
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_BinaryArithmetic; }
    virtual CUTF8String GetErrorDesc() const override
    {
        switch (_Op)
        {
        case EOp::Add: return "binary addition";
        case EOp::Sub: return "binary subtraction";
        case EOp::Mul: return "binary multiplication";
        case EOp::Div: return "binary division";
        default: ULANG_UNREACHABLE();
        };
    }
    virtual bool operator==(const CExpressionBase& Other) const override { return CExprInvocation::operator==(Other) && _Op == static_cast<const CExprBinaryArithmetic&>(Other)._Op; }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override
    {
        CExpressionBase::VisitImmediates(Visitor);
        const char* OpString;
        switch(_Op)
        {
        case EOp::Add: OpString = "Add"; break;
        case EOp::Sub: OpString = "Sub"; break;
        case EOp::Mul: OpString = "Mul"; break;
        case EOp::Div: OpString = "Div"; break;
        default: ULANG_UNREACHABLE();
        };
        Visitor.VisitImmediate("Op", OpString);
    }

    EOp _Op;
};

/**
 * Short-circuit evaluation of a Boolean and
 **/
class VERSECOMPILER_API CExprShortCircuitAnd : public CExprBinaryOp
{
public:

    CExprShortCircuitAnd(TSPtr<CExpressionBase>&& Lhs, TSPtr<CExpressionBase>&& Rhs) : CExprBinaryOp(Move(Lhs), Move(Rhs)) {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_ShortCircuitAnd; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override { return Rhs()->GetResultType(Program); }
    virtual bool CanFail() const override { return true; }
    virtual CUTF8String GetErrorDesc() const override    { return "logical '&&'"; }
    virtual bool operator==(const CExpressionBase& Other) const override;
};


/**
 * Short-circuit evaluation of a Boolean or
 **/
class VERSECOMPILER_API CExprShortCircuitOr : public CExprBinaryOp
{
public:

    CExprShortCircuitOr(TSPtr<CExpressionBase>&& Lhs, TSPtr<CExpressionBase>&& Rhs, const CTypeBase* JoinType = nullptr)
        : CExprBinaryOp(Move(Lhs), Move(Rhs))
    {
        if (JoinType != nullptr)
        {
            SetResultType(JoinType);
        }
    }

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_ShortCircuitOr; }
    virtual bool CanFail() const override { return Rhs()->CanFail(); }
    virtual CUTF8String GetErrorDesc() const override    { return "logical '||'"; }
    virtual bool operator==(const CExpressionBase& Other) const override;

private:

};


/**
 * Logical not operator
 */
class VERSECOMPILER_API CExprLogicalNot : public CExprUnaryOp
{
public:

    CExprLogicalNot(TSPtr<CExpressionBase>&& Operand): CExprUnaryOp(Move(Operand)) {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_LogicalNot; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual bool CanFail() const override { return true; }
    virtual CUTF8String GetErrorDesc() const override    { return "logical 'not'"; }
    virtual bool operator==(const CExpressionBase& Other) const override;
};


/**
* Comparison operators.
*/
class VERSECOMPILER_API CExprComparison : public CExprInvocation
{
public:
    using EOp = Verse::Vst::BinaryOpCompare::op;

    CExprComparison(EOp Op, TSRef<CExpressionBase>&& Argument)
        : CExprInvocation(Move(Argument))
        , _Op(Op)
    {
        _CallsiteBracketStyle = CExprInvocation::EBracketingStyle::SquareBrackets;
    }

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_Comparison; }
    virtual CUTF8String GetErrorDesc() const override;
    virtual bool operator==(const CExpressionBase& Other) const override;
    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("Op", Verse::Vst::BinaryCompareOpAsCString(_Op)); }

    EOp Op() const { return _Op; }

private:
    EOp _Op;
};


/**
 * Query the value of a boolean or option value.
 **/
class VERSECOMPILER_API CExprQueryValue : public CExprInvocation
{
public:

    CExprQueryValue(TSRef<CExpressionBase>&& Operand)
    : CExprInvocation(Move(Operand))
    {
        _CallsiteBracketStyle = CExprInvocation::EBracketingStyle::SquareBrackets;
    }

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_QueryValue; }
    virtual CUTF8String GetErrorDesc() const override { return "postfix '?' operator"; }
};


/**
 * Box an option value
 */
class VERSECOMPILER_API CExprMakeOption : public CExprUnaryOp
{
public:

    CExprMakeOption(const CTypeBase* Type, TSPtr<CExpressionBase>&& Operand)
        : CExprUnaryOp(Move(Operand))
    {
        SetResultType(Type);
    }

    COptionType const* GetOptionType(const CSemanticProgram& Program) const
    {
        const CTypeBase* MyResultType = GetResultType(Program);
        return &(MyResultType->GetNormalType().AsChecked<COptionType>());
    }

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_MakeOption; }
    virtual bool CanFail() const override { return false; }
    virtual CUTF8String GetErrorDesc() const override { return "option value constructor"; }
    virtual bool operator==(const CExpressionBase& Other) const override;
};


/**
 * Create an array value
 * Can have zero or more subexpressions
 */
class VERSECOMPILER_API CExprMakeArray : public CExprCompoundBase
{
public:
    CExprMakeArray(int32_t ReserveSubExprNum) : CExprCompoundBase(ReserveSubExprNum) {}

    const CArrayType* GetArrayType(const CSemanticProgram& Program) const
    {
        return &GetResultType(Program)->GetNormalType().AsChecked<CArrayType>();
    }

    //~ Begin CExpressionBase interface
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_MakeArray; }
    virtual CUTF8String GetErrorDesc() const override { return "array value"; }
    //~ End CExpressionBase interface
};

/**
 * Create a map value
 * Can have zero or more subexpressions
 */
class VERSECOMPILER_API CExprMakeMap : public CExprCompoundBase
{
public:
    explicit CExprMakeMap(int32_t ReserveSubExprNum = 0) : CExprCompoundBase(ReserveSubExprNum) {}

    const CMapType* GetMapType(const CSemanticProgram& Program) const
    {
        return &GetResultType(Program)->GetNormalType().AsChecked<CMapType>();
    }

    //~ Begin CExpressionBase interface
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_MakeMap; }
    virtual CUTF8String GetErrorDesc() const override { return "map value"; }
    virtual bool CanFail() const override;
    //~ End CExpressionBase interface
};

/**
 * Create a tuple value
 */
class VERSECOMPILER_API CExprMakeTuple : public CExprCompoundBase
{
public:
    using CExprCompoundBase::CExprCompoundBase;

    const CTupleType* GetTupleType(const CSemanticProgram& Program) const
    {
        return &GetResultType(Program)->GetNormalType().AsChecked<CTupleType>();
    }

    //~ Begin CExpressionBase interface
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_MakeTuple; }
    virtual CUTF8String GetErrorDesc() const override { return "tuple value"; }
    //~ End CExpressionBase interface
};


/**
* Create a range value
*/
class VERSECOMPILER_API CExprMakeRange : public CExpressionBase
{
public:
    TSRef<CExpressionBase> _Lhs;
    TSRef<CExpressionBase> _Rhs;

    CExprMakeRange(TSRef<CExpressionBase>&& Lhs, TSRef<CExpressionBase>&& Rhs)
        : _Lhs(Move(Lhs))
        , _Rhs(Move(Rhs))
    {}

    void SetLhs(TSRef<CExpressionBase>&& Lhs) { _Lhs = Move(Lhs); }
    void SetRhs(TSRef<CExpressionBase>&& Rhs) { _Rhs = Move(Rhs); }

    //~ Begin CExpressionBase interface
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_MakeRange; }
    virtual bool CanFail() const override { return _Lhs->CanFail() || _Rhs->CanFail(); }
    virtual CUTF8String GetErrorDesc() const override { return "range constructor"; }
    virtual bool operator==(const CExpressionBase& Other) const override;
    //~ End CExpressionBase interface

    virtual void VisitChildren(SAstVisitor& Visitor) const override
    {
        Visitor.Visit("Lhs", _Lhs);
        Visitor.Visit("Rhs", _Rhs);
    }
};


/**
 * Invoke a type as a function on a value - type(expr) or type[expr].
 */
class VERSECOMPILER_API CExprInvokeType : public CExpressionBase
{
public:
    const CTypeBase* _NegativeType;
    const bool _bIsFallible;
    const TSPtr<CExpressionBase> _TypeAst;
    const TSRef<CExpressionBase> _Argument;
    
    CExprInvokeType(const CTypeBase* NegativeType, const CTypeBase* PositiveType, bool bIsFallible, TSPtr<CExpressionBase>&& TypeAst, TSRef<CExpressionBase>&& Argument);

    //~ Begin CExpressionBase interface
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override;
    virtual bool CanFail() const override { return _bIsFallible || _Argument->CanFail(); }
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_Type; }
    virtual CUTF8String GetErrorDesc() const override { return "type invocation"; }
    virtual bool operator==(const CExpressionBase& Other) const override;
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("TypeAst", _TypeAst); Visitor.Visit("Argument", _Argument); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override
    {
        CExpressionBase::VisitImmediates(Visitor);
        Visitor.VisitImmediate("NegativeType", _NegativeType);
        Visitor.VisitImmediate("bIsFallible", _bIsFallible);
    }
    //~ End CExpressionBase interface
};


/**
 * Read the value of a variable.
 */
class VERSECOMPILER_API CExprPointerToReference : public CExprUnaryOp
{
public:
    CExprPointerToReference(TSRef<CExpressionBase>&& Variable) : CExprUnaryOp(Move(Variable)) {}

    //~ Begin CExpressionBase interface
    virtual bool CanFail() const override { return Operand()->CanFail(); }
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_PointerToReference; }
    virtual CUTF8String GetErrorDesc() const override { return "pointer to reference"; }
    virtual bool operator==(const CExpressionBase& Other) const override;
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    //~ End CExpressionBase interface
};

/**
 * Evaluate operand to an l-expression.
 */
class VERSECOMPILER_API CExprSet : public CExprUnaryOp
{
    using CExprUnaryOp::CExprUnaryOp;

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_Set; }
    virtual bool CanFail() const override { return Operand()->CanFail(); }
    virtual CUTF8String GetErrorDesc() const override { return "set"; }
    virtual bool operator==(const CExpressionBase& Other) const override;
};

/**
 * Create a new pointer from an initial value.
 */
class VERSECOMPILER_API CExprNewPointer : public CExpressionBase
{
public:
    const TSRef<CExpressionBase> _Value;
    
    CExprNewPointer(const CPointerType* PointerType, TSRef<CExpressionBase>&& Value)
        : CExpressionBase(PointerType)
        , _Value(Move(Value))
    {}

    //~ Begin CExpressionBase interface
    virtual bool CanFail() const override { return _Value->CanFail(); }
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_NewPointer; }
    virtual CUTF8String GetErrorDesc() const override { return "pointer new"; }
    virtual bool operator==(const CExpressionBase& Other) const override;
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Value", _Value); }
    //~ End CExpressionBase interface
};

/**
 * Evaluates the value of an expression yielding a reference type.
 */
class VERSECOMPILER_API CExprReferenceToValue : public CExprUnaryOp
{
public:
    CExprReferenceToValue(TSPtr<CExpressionBase>&& Operand);

    //~ Begin CExpressionBase interface
    virtual bool CanFail() const override { return Operand()->CanFail(); }
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_ReferenceToValue; }
    virtual CUTF8String GetErrorDesc() const override { return "convert reference to value"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Invoke_ReferenceToValue && (*this) == static_cast<const CExprUnaryOp&>(Other); }
    //~ End CExpressionBase interface
};

/**
 * Code block - `{expr1; expr2}` or `do {expr1; expr2}`
 * Can have zero or more subexpressions
 **/
class VERSECOMPILER_API CExprCodeBlock : public CExprCompoundBase
{
public:
    // The scope containing locals for this block
    TSPtr<CControlScope> _AssociatedScope;

    // Methods

    explicit CExprCodeBlock(int32_t ReserveSubExprNum = 0) : CExprCompoundBase(ReserveSubExprNum) {}

    virtual EAstNodeType     GetNodeType() const override  { return EAstNodeType::Flow_CodeBlock; }
    virtual CUTF8String      GetErrorDesc() const override { return "code block"; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
};

class VERSECOMPILER_API CExprLet : public CExprCompoundBase
{
    using CExprCompoundBase::CExprCompoundBase;
    virtual EAstNodeType     GetNodeType() const override { return EAstNodeType::Flow_Let; }
    virtual CUTF8String      GetErrorDesc() const override { return "let"; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
};

/**
 * Return statement - return expr
 **/
class VERSECOMPILER_API CExprReturn : public CExpressionBase
{
public:

    CExprReturn() {}
    CExprReturn(TSPtr<CExpressionBase>&& Result, const CFunction* Function = nullptr)
    {
        SetResult(Move(Result));
        SetFunction(Function);
    }

    const TSPtr<CExpressionBase>& Result() const { return _Result; }
    void SetResult(TSPtr<CExpressionBase>&& Result) { _Result = Move(Result); }

    const CFunction* Function() const { return _Function; }
    void SetFunction(const CFunction* Function) { _Function = Function; }

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override { return _Result.IsValid() ? _Result->FindFirstAsyncSubExpr(Program) : nullptr; }
    virtual bool CanFail() const override { return _Result.IsValid() && _Result->CanFail(); }
    virtual EAstNodeType     GetNodeType() const override  { return EAstNodeType::Flow_Return; }
    virtual CUTF8String      GetErrorDesc() const override { return "return"; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual bool MayHaveAttributes() const override { return true; }

    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Result", _Result); }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Flow_Return && IsSubExprEqual(_Result, static_cast<const CExprReturn&>(Other)._Result); }

private:
    
    // Result expression
    TSPtr<CExpressionBase> _Result;

    // The function being returned from.
    const CFunction* _Function{nullptr};
};


/**
 * Conditional with failable tests- if (test[]) {clause1}, if (test[]) {clause1} else {else_clause}
 **/
class VERSECOMPILER_API CExprIf : public CExpressionBase
{
public:

    CExprIf(TSRef<CExprCodeBlock>&& Condition, TSPtr<CExpressionBase>&& ThenClause, TSPtr<CExpressionBase>&& ElseClause = nullptr)
        : _Condition(Move(Condition))
        , _ThenClause(Move(ThenClause))
        , _ElseClause(Move(ElseClause))
    {}
    
    const TSRef<CExprCodeBlock>& GetCondition() const { return _Condition; }
    void SetCondition(TSRef<CExprCodeBlock>&& Condition) { _Condition = Move(Condition); }

    const TSPtr<CExpressionBase>& GetThenClause() const { return _ThenClause; }
    void SetThenClause(TSPtr<CExpressionBase>&& ThenClause) { _ThenClause = Move(ThenClause); }

    const TSPtr<CExpressionBase>& GetElseClause() const { return _ElseClause; }
    void SetElseClause(TSPtr<CExpressionBase>&& ElseClause) { _ElseClause = Move(ElseClause); }

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override;
    virtual bool CanFail() const override;
    virtual EAstNodeType    GetNodeType() const override  { return EAstNodeType::Flow_If; }
    virtual CUTF8String     GetErrorDesc() const override { return "if"; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Condition", _Condition); Visitor.Visit("ThenClause", _ThenClause); Visitor.Visit("ElseClause", _ElseClause); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("bIsFilter", _bIsFilter); }
    virtual bool operator==(const CExpressionBase& Other) const override;

    // If can be used as a filter in a for(..) { }. Code generation need to know.
    bool _bIsFilter{ false }; 
private:
    
    TSRef<CExprCodeBlock> _Condition;
    TSPtr<CExpressionBase> _ThenClause;
    TSPtr<CExpressionBase> _ElseClause;
};


/**
 * Bounded iteration
 **/
class VERSECOMPILER_API CExprIteration : public CExpressionBase
// Note that CExprSyncIterated, CExprRushIterated and CExprRaceIterated are subclasses
{
public:
    CExprIteration() {}

    void SetBody(TSPtr<CExpressionBase>&& Body)     { _Body = Move(Body); }
    void AddFilter(TSRef<CExpressionBase>&& Filter) { _Filters.Add(Move(Filter)); }

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override;
    virtual bool CanFail() const override { return _Body->CanFail(); }
    virtual EAstNodeType   GetNodeType() const override  { return EAstNodeType::Flow_Iteration; }
    virtual CUTF8String    GetErrorDesc() const override { return "for"; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.VisitArray("Filters", _Filters); Visitor.Visit("Body", _Body); }
    virtual bool operator==(const CExpressionBase& Other) const override;

public:
    // The scope containing the variables used for iterating
    TSPtr<CControlScope> _AssociatedScope;

    // The "filters" that are used
    TArray<TSRef<CExpressionBase>> _Filters;

    // Expression to evaluate for every iteration that gets past the filters step
    TSPtr<CExpressionBase> _Body;
};


/**
 * Iteration based concurrency primitives - sync(item:collection){}, rush(item:collection){} or race(item:collection){}
 **/
class VERSECOMPILER_API CExprConcurrentIteratedBase : public CExprIteration
{
public:
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override { return this; }
};


/**
 * Iterated Sync (collection form) concurrency primitive - sync(item:collection) {_coro1() _coro2()}
 **/
class VERSECOMPILER_API CExprSyncIterated : public CExprConcurrentIteratedBase
{
public:
    virtual CUTF8String GetErrorDesc() const override { return "sync(:){}"; }
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Concurrent_SyncIterated; }
};


/**
 * Iterated Rush (collection form) concurrency primitive - rush(item:collection) {_coro1() _coro2()}
 **/
class VERSECOMPILER_API CExprRushIterated : public CExprConcurrentIteratedBase
{
public:
    virtual CUTF8String GetErrorDesc() const override { return "rush(:){}"; }
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Concurrent_RushIterated; }
};


/**
 * Iterated Race (collection form) concurrency primitive - race(item:collection) {_coro1() _coro2()}
 **/
class VERSECOMPILER_API CExprRaceIterated : public CExprConcurrentIteratedBase
{
public:
    virtual CUTF8String GetErrorDesc() const override { return "race(:){}"; }
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Concurrent_RaceIterated; }
};


/// Base class for all expressions that form a type out of input type(s)
class VERSECOMPILER_API CExprTypeFormer : public CExpressionBase
{
public:

    // Metatype of the actual type formed.
    const CTypeType* _TypeType{nullptr};

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override  { return nullptr; }
    virtual const CTypeBase*       GetResultType(const CSemanticProgram& Program) const override          { return _TypeType; }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("TypeType", _TypeType); }
};


class VERSECOMPILER_API CExprUnaryTypeFormer : public CExprTypeFormer
{
public:

    CExprUnaryTypeFormer(TSRef<CExpressionBase>&& InnerTypeAst): _InnerTypeAst(Move(InnerTypeAst)) {}

    const TSRef<CExpressionBase>& GetInnerTypeAst() const { return _InnerTypeAst; }
    void SetInnerTypeAst(TSRef<CExpressionBase>&& InnerTypeAst) { _InnerTypeAst = Move(InnerTypeAst); }
    
    // CExpressionBase interface.
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override { return _InnerTypeAst->FindFirstAsyncSubExpr(Program); }
    
    // CAstNode interface.
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("InnerTypeAst", _InnerTypeAst); }

private:

    TSRef<CExpressionBase> _InnerTypeAst;
};


class VERSECOMPILER_API CExprArrayTypeFormer : public CExprUnaryTypeFormer
{
public:

    CExprArrayTypeFormer(TSRef<CExpressionBase>&& InnerTypeAst): CExprUnaryTypeFormer(Move(InnerTypeAst)) {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_ArrayFormer; }
    virtual CUTF8String GetErrorDesc() const override { return "array type"; }
    bool operator==(const CExpressionBase& Other) const override { return false; }
    
    const CArrayType* GetArrayType() const
    {
        ULANG_ASSERTF(_TypeType, "GetArrayType called on unanalyzed expression");
        return &_TypeType->PositiveType()->GetNormalType().AsChecked<CArrayType>();
    }
};

class VERSECOMPILER_API CExprGeneratorTypeFormer : public CExprUnaryTypeFormer
{
public:

    /// Construct an expression that takes the `ValueType` and forms an Generator of `ValueType`.
    CExprGeneratorTypeFormer(TSRef<CExpressionBase>&& InnerTypeAst) : CExprUnaryTypeFormer(Move(InnerTypeAst)) {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_GeneratorFormer; }
    virtual CUTF8String GetErrorDesc() const override { return "generator(..)"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return false; }

    const CGeneratorType* GetGeneratorType() const
    {
        ULANG_ASSERTF(_TypeType, "GetGeneratorType called on unanalyzed expression");
        return &_TypeType->PositiveType()->GetNormalType().AsChecked<CGeneratorType>();
    }
};

class VERSECOMPILER_API CExprMapTypeFormer : public CExprTypeFormer
{
public:
    
    CExprMapTypeFormer(TArray<TSRef<CExpressionBase>>&& KeyTypeAsts, TSRef<CExpressionBase>&& ValueTypeAst)
    : CExprTypeFormer()
    , _KeyTypeAsts(KeyTypeAsts)
    , _ValueTypeAst(ValueTypeAst)
    {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_MapFormer; }
    virtual CUTF8String GetErrorDesc() const override { return "map type"; }
    bool operator==(const CExpressionBase& Other) const override { return false; }
    
    const TArray<TSRef<CExpressionBase>>& KeyTypeAsts() const { return _KeyTypeAsts; }
    void SetKeyTypeAst(TSRef<CExpressionBase>&& KeyTypeAst, int32_t Index) { _KeyTypeAsts[Index] = Move(KeyTypeAst); }

    const TSRef<CExpressionBase>& ValueTypeAst() const { return _ValueTypeAst; }
    void SetValueTypeAst(TSRef<CExpressionBase>&& ValueTypeAst) { _ValueTypeAst = Move(ValueTypeAst); }

    const CMapType* GetMapType() const
    {
        ULANG_ASSERTF(_TypeType, "GetMapType called on unanalyzed expression");
        return &_TypeType->PositiveType()->GetNormalType().AsChecked<CMapType>();
    }

    // CExpressionBase interface.
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override;
    
    // CAstNode interface.
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.VisitArray("KeyTypeAsts", _KeyTypeAsts); Visitor.Visit("ValueTypeAst", _ValueTypeAst); }

private:
    
    TArray<TSRef<CExpressionBase>> _KeyTypeAsts;
    TSRef<CExpressionBase> _ValueTypeAst;
};

class VERSECOMPILER_API CExprOptionTypeFormer : public CExprUnaryTypeFormer
{
public:

    CExprOptionTypeFormer(TSRef<CExpressionBase>&& InnerTypeAst): CExprUnaryTypeFormer(Move(InnerTypeAst)) {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_OptionFormer; }
    virtual CUTF8String GetErrorDesc() const override { return "option type"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return false; }

    const COptionType* GetOptionType() const
    {
        ULANG_ASSERTF(_TypeType, "GetOptionType called on unanalyzed expression");
        return &_TypeType->NegativeType()->GetNormalType().AsChecked<COptionType>();
    }
};


class VERSECOMPILER_API CExprSubtype : public CExprUnaryTypeFormer
{
public:

    /// Construct an expression that takes the `ValueType` and forms an option of `ValueType`.
    CExprSubtype(TSRef<CExpressionBase>&& InnerTypeAst): CExprUnaryTypeFormer(Move(InnerTypeAst)) {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_Subtype; }
    virtual CUTF8String GetErrorDesc() const override { return "subtype(..)"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return false; }
    
    const CTypeType& GetSubtypeType() const;
};

// Get or create a tuple tuple based on `tuple(type1, type2, ...)`
class VERSECOMPILER_API CExprTupleType : public CExprTypeFormer
{
public:

    CExprTupleType(int32_t ReserveTypeExprNum = 0)                           { _ElementTypeExprs.Reserve(ReserveTypeExprNum); }

    const TSPtrArray<CExpressionBase>& GetElementTypeExprs() const           { return _ElementTypeExprs; }
    TSPtrArray<CExpressionBase>&       GetElementTypeExprs()                 { return _ElementTypeExprs; }
    void ReplaceElementTypeExpr(TSPtr<CExpressionBase>&& TypeExpr, int32_t Index)
    {
        ULANG_ASSERTF(Index >= 0 && Index < _ElementTypeExprs.Num(), "Replacing invalid subexpression index");
        _ElementTypeExprs.ReplaceAt(Move(TypeExpr), Index);
    }

    virtual EAstNodeType   GetNodeType() const override                      { return EAstNodeType::Invoke_TupleType; }
    virtual CUTF8String    GetErrorDesc() const override                     { return "tuple(..)"; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override          { Visitor.VisitArray("ElemTypeExprs", _ElementTypeExprs); }
    virtual bool operator==(const CExpressionBase& Other) const override;
    
    const CTupleType* GetTupleType() const
    {
        ULANG_ASSERTF(_TypeType, "GetTupleType called on unanalyzed expression");
        return &_TypeType->PositiveType()->GetNormalType().AsChecked<CTupleType>();
    }

protected:

    TSPtrArray<CExpressionBase> _ElementTypeExprs;
};

/**
 * Create a function type from a parameter and return type.
 * Also used on the LHS of a definition as a pattern for iteration pairs.
 */
class VERSECOMPILER_API CExprArrow : public CExprTypeFormer
{
public:

    CExprArrow(TSRef<CExpressionBase>&& Domain, TSRef<CExpressionBase>&& Range)
        : _Domain(Move(Domain))
        , _Range(Move(Range))
    {}

    const TSRef<CExpressionBase>& Domain() const { return _Domain; }
    const TSRef<CExpressionBase>& Range() const { return _Range; }

    void SetDomain(TSRef<CExpressionBase>&& NewDomain) { _Domain = Move(NewDomain); }
    void SetRange(TSRef<CExpressionBase>&& NewRange) { _Range = Move(NewRange); }

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_Arrow; }
    virtual CUTF8String  GetErrorDesc() const override { return "function type"; }
    virtual bool operator==(const CExpressionBase& Other) const override
    {
        return GetNodeType() == Other.GetNodeType()
            && IsSubExprEqual(_Domain, static_cast<const CExprArrow&>(Other)._Domain)
            && IsSubExprEqual(_Range, static_cast<const CExprArrow&>(Other)._Range);
    }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Domain", _Domain); Visitor.Visit("Range", _Range); }

    const CFunctionType* GetFunctionType() const
    {
        ULANG_ASSERTF(_TypeType, "GetFunctionType called on unanalyzed expression");
        return &_TypeType->PositiveType()->GetNormalType().AsChecked<CFunctionType>();
    }

private:
    TSRef<CExpressionBase> _Domain;
    TSRef<CExpressionBase> _Range;
};

/**
 * Represents an initializer-list style construction for certain object types; 
 * i.e.: Type{expr1, id=expr2, ...}
 */
class VERSECOMPILER_API CExprArchetypeInstantiation : public CExpressionBase
{
public:
    CExprArchetypeInstantiation(TSRef<CExpressionBase>&& ClassAst, CExprMacroCall::CClause&& BodyAst, const CTypeBase* ResultType);

    const CClass* GetClass(const CSemanticProgram& Program) const;

    const TSRefArray<CExpressionBase>& Arguments() const { return _Arguments; }

    int32_t AppendArgument(TSRef<CExpressionBase>&& Argument)
    {
        return _Arguments.Add(Move(Argument));
    }

    virtual bool CanFail() const override 
    {
        for (const auto& Argument : _Arguments)
        {
            if (Argument->CanFail())
            {
                return true;
            }
        }
        return false;
    }

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Invoke_ArchetypeInstantiation; }

    virtual CUTF8String GetErrorDesc() const override { return "archetype constructor"; }

    virtual void VisitChildren(SAstVisitor& Visitor) const override
    {
        Visitor.Visit("ClassAst", _ClassAst);
        Visitor.VisitArray("BodyAstExprs", _BodyAst.Exprs());
        Visitor.BeginArray("Arguments", _Arguments.Num());
        for(const TSRef<CExpressionBase>& Argument : _Arguments)
        {
            Visitor.VisitElement(Argument);
        }
        Visitor.EndArray();
    }

    virtual void VisitImmediates(SAstVisitor& Visitor) const override
    {
        CExpressionBase::VisitImmediates(Visitor);
        Visitor.VisitImmediate("BodyAstTag", MacroClauseTagAsCString(_BodyAst.Tag()));
        Visitor.VisitImmediate("BodyAstForm", MacroClauseFormAsCString(_BodyAst.Form()));
    }

    bool operator==(const CExpressionBase& Other) const override;

    TSRef<CExpressionBase> _ClassAst;
    CExprMacroCall::CClause _BodyAst;

private:
    TSRefArray<CExpressionBase> _Arguments;
};

/**
 * Block based concurrency primitives - sync{}, rush{} or race{}
 * Can have two or more async subexpressions - 0 or 1 is a warning or error.  
 **/
class VERSECOMPILER_API CExprConcurrentBlockBase : public CExprCompoundBase
{
public:
    explicit CExprConcurrentBlockBase(int32_t ReserveSubExprNum = 0) : CExprCompoundBase(ReserveSubExprNum) {}

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override  { return this; }
};


/**
 * Sync concurrency primitive - sync {_coro1() _coro2()}
 * 2+ Async expressions run concurrently and next expression executed when *all* expressions
 * completed. Its overall result is the result of the last expression in the block to complete
 * (using most specific common compatible result type of all expressions or no result if there is
 * no common type). Expressions may only be async (such as coroutines).
 **/
class VERSECOMPILER_API CExprSync : public CExprConcurrentBlockBase
{
public:
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Concurrent_Sync; }
    virtual CUTF8String GetErrorDesc() const override { return "sync{}"; }
};


/**
 * Race concurrency primitive - race {_coro1() _coro2()}
 * 2+ Async expressions run concurrently and next expression executed when *fastest/first*
 * expression completed and all other expressions are aborted. Its overall result is result of
 * first expression in block to complete (using most specific common compatible result type of all
 * expressions or no result if there is no common type).
 **/
class VERSECOMPILER_API CExprRace : public CExprConcurrentBlockBase
{
public:
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Concurrent_Race; }
    virtual CUTF8String GetErrorDesc() const override { return "race{}"; }
};


/**
 * Rush concurrency primitive - rush {_coro1() _coro2()}
 * 2+ Async expressions run concurrently and next expression executed when *fastest/first*
 * expression completed and all other expressions continue independently until each is fully
 * completed. Its overall result is the result of the first expression in the block to complete
 * (using most specific common compatible result type of all expressions or no result if there is
 * no common type).
 **/
class VERSECOMPILER_API CExprRush : public CExprConcurrentBlockBase
{
public:
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Concurrent_Rush; }
    virtual CUTF8String GetErrorDesc() const override { return "rush{}"; }
};


/**
 * Expressions with a sub-block - branch, spawn, loop, defer
 **/
class VERSECOMPILER_API CExprSubBlockBase : public CExpressionBase
{
protected:
    // Sub-expression from block - either single expression or block of expressions
    TSPtr<CExpressionBase> _BlockExpr;

public:

    void SetExpr(TSPtr<CExpressionBase>&& Expr) { _BlockExpr = Move(Expr); }
    const TSPtr<CExpressionBase>& Expr() const { return _BlockExpr; }

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override { return nullptr; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("BlockExpr", _BlockExpr); }
    virtual bool operator==(const CExpressionBase& Other) const override                      { return (Other.GetNodeType() == GetNodeType()) && IsSubExprEqual(_BlockExpr, static_cast<const CExprSubBlockBase&>(Other)._BlockExpr); }
};


/**
 * Branch concurrency primitive - branch {Coro()}
 * One async expression started and next expression executed immediately while the started
 * expression continues independently until completion or the surrounding context (such as a
 * coroutine) completes in which case it aborts early. Result is a handle to the invoked coroutine / invoked
 * expression (also known as a future or promise) tracking the expression.
 **/
class VERSECOMPILER_API CExprBranch : public CExprSubBlockBase
{
public:
    virtual EAstNodeType GetNodeType() const override  { return EAstNodeType::Concurrent_Branch; }
    virtual CUTF8String  GetErrorDesc() const override { return "branch"; }
};


/**
 * Spawn concurrency primitive - spawn {Coro()}  [future: spawn {Expr1; Expr2}]
 * Async expression treated as a closure/lambda with next expression executed
 * immediately while the started async expression continues independently. Has no result.
 * spawn essentially makes any async expression an immediate expression. This also means that
 * it can allow async expressions / coroutines to be called within immediate functions.
 **/
class VERSECOMPILER_API CExprSpawn : public CExprSubBlockBase
{
public:
    virtual EAstNodeType     GetNodeType() const override  { return EAstNodeType::Concurrent_Spawn; }
    virtual CUTF8String      GetErrorDesc() const override { return "spawn"; }
};


/**
 * Loop flow/concurrency primitive/macro - loop {Expr1; Expr2}
 * Loops one or more expressions.
 * For safety/security it *must* have at least one async expression, `break`, or `return`
 * so it is less likely to have an infinite loop within a single update.
 **/
class VERSECOMPILER_API CExprLoop : public CExprSubBlockBase
{
public:
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override { return _BlockExpr->FindFirstAsyncSubExpr(Program); }
    virtual EAstNodeType           GetNodeType() const override           { return EAstNodeType::Flow_Loop; }
    virtual CUTF8String            GetErrorDesc() const override          { return "loop"; }

    virtual bool CanFail() const override {
        return  _BlockExpr->CanFail();
    }
};


/**
 * defer macro - defer {Expr1; Expr2}
 * Defers expressions until end of current scope. Only called if encountered in flow and called in reverse order encountered.
 **/
class VERSECOMPILER_API CExprDefer : public CExprSubBlockBase
{
public:
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override { return _BlockExpr->FindFirstAsyncSubExpr(Program); }
    virtual EAstNodeType           GetNodeType() const override           { return EAstNodeType::Flow_Defer; }
    virtual CUTF8String            GetErrorDesc() const override          { return "defer"; }
};

/**
 * Control flow early exit
 *
 *   loop:
 *       if (IsEarlyExit[]):
 *           break
 *       DoLoopStuff()
 **/
class VERSECOMPILER_API CExprBreak : public CExpressionBase
{
public:
    const CExpressionBase* _AssociatedControlFlow{nullptr};

    virtual EAstNodeType GetNodeType() const override           { return EAstNodeType::Flow_Break; }
    virtual CUTF8String  GetErrorDesc() const override          { return "break"; }
    virtual bool operator==(const CExpressionBase& Other) const override  { return Other.GetNodeType() == EAstNodeType::Flow_Break; }
    virtual bool MayHaveAttributes() const override { return true; }
    
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
};

/**
 * Represents members of a class/interface/module/snippet definition node
 */
class CMemberDefinitions
{
public:
    CMemberDefinitions() {}
    CMemberDefinitions(TArray<TSRef<CExpressionBase>>&& Members) : _Members(Move(Members)) {}

    const TArray<TSRef<CExpressionBase>>& Members() const { return _Members; }
    void SetMembers(TArray<TSRef<CExpressionBase>>&& Members) { _Members = Move(Members); }
    void AppendMember(TSRef<CExpressionBase>&& Member) { _Members.Add(Move(Member)); }
    void SetMember(TSRef<CExpressionBase>&& Member, int32_t Index) { _Members[Index] = Move(Member); }

    void VisitMembers(SAstVisitor& Visitor) const { Visitor.VisitArray("Members", _Members); }

private:
    TArray<TSRef<CExpressionBase>> _Members;
};

/**
 * Represents a snippet in the AST.
 */
class VERSECOMPILER_API CExprSnippet : public CExpressionBase, public CMemberDefinitions
{
public:
    CUTF8String _Path;
    CSnippet* _SemanticSnippet{ nullptr };

    CExprSnippet(const CUTF8StringView& Path) : _Path(Path) {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Context_Snippet; }
    virtual CUTF8String  GetErrorDesc() const override { return "snippet"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Context_Snippet && static_cast<const CExprSnippet&>(Other)._SemanticSnippet == _SemanticSnippet; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual void VisitChildren(SAstVisitor& Visitor) const override { VisitMembers(Visitor); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("Path", _Path); }
};

/**
 * Represents a module definition in the AST.
 */
class VERSECOMPILER_API CExprModuleDefinition : public CExpressionBase, public CMemberDefinitions
{
public:
    CUTF8String _Name;
    CModulePart* _SemanticModule{ nullptr }; // You can get the CModule from this as well
    bool _bLegacyPublic = false; // To emulate legacy behavior while vmodule files are allowed

    CExprModuleDefinition(const CUTF8StringView& Name, EVstMappingType VstMappingType = EVstMappingType::Ast)
        : CExpressionBase(VstMappingType)
        , _Name(Name)
    {}
    CExprModuleDefinition(CModulePart& Module, TArray<TSRef<CExpressionBase>>&& Members);
    ~CExprModuleDefinition();

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Definition_Module; }
    virtual CUTF8String  GetErrorDesc() const override { return "module definition"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Definition_Module && static_cast<const CExprModuleDefinition&>(Other)._SemanticModule == _SemanticModule; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual void VisitChildren(SAstVisitor& Visitor) const override { VisitMembers(Visitor); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("Name", _Name); }
};

/**
 * Represents an enum definition in the AST.
 */
class VERSECOMPILER_API CExprEnumDefinition : public CExpressionBase
{
public:
    CEnumeration& _Enum;
    
    const TArray<TSRef<CExpressionBase>> _Members;

    CExprEnumDefinition(CEnumeration& Enum, TArray<TSRef<CExpressionBase>>&& Members, EVstMappingType = EVstMappingType::Ast);
    ~CExprEnumDefinition();

    virtual EAstNodeType GetNodeType() const override                    { return EAstNodeType::Definition_Enum; }
    virtual CUTF8String  GetErrorDesc() const override                   { return "enum definition"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Definition_Enum && &static_cast<const CExprEnumDefinition&>(Other)._Enum == &_Enum; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.VisitArray("Members", _Members); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;
};

/**
 * Represents both named and anonymous scoped access level definitions in the AST.
 * e.g. foo_scope := scoped{A,B,C}  #named
 *      foo<scoped{A,B,C}> := 42 # anonymous
 */
class VERSECOMPILER_API CExprScopedAccessLevelDefinition : public CExpressionBase
{
public:
    TSRef<CScopedAccessLevelDefinition> _AccessLevelDefinition;
    TArray<TSRef<CExpressionBase>> _ScopeReferenceExprs;

    CExprScopedAccessLevelDefinition(TSRef<CScopedAccessLevelDefinition>& AccessLevelDefinition, EVstMappingType VstMappingType = EVstMappingType::Ast);
    ~CExprScopedAccessLevelDefinition();

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Definition_ScopedAccessLevel; }
    virtual CUTF8String  GetErrorDesc() const override { return "scoped access level"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Definition_ScopedAccessLevel && static_cast<const CExprScopedAccessLevelDefinition&>(Other)._AccessLevelDefinition == _AccessLevelDefinition; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.VisitArray("Scopes", _ScopeReferenceExprs); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;
};

/**
 * Represents a interface definition in the AST.
 */
class VERSECOMPILER_API CExprInterfaceDefinition : public CExpressionBase, public CMemberDefinitions
{
public:
    CInterface& _Interface;
    
    const TArray<TSRef<CExpressionBase>>& SuperInterfaces() const { return _SuperInterfaces; }
    void SetSuperInterfaces(TArray<TSRef<CExpressionBase>>&& SuperInterfaces) { _SuperInterfaces = SuperInterfaces; }
    void SetSuperInterface(TSRef<CExpressionBase>&& SuperInterface, int32_t Index) { _SuperInterfaces[Index] = Move(SuperInterface); }

    CExprInterfaceDefinition(CInterface& Interface, TArray<TSRef<CExpressionBase>>&& SuperInterfaces, TArray<TSRef<CExpressionBase>>&& Members, EVstMappingType = EVstMappingType::Ast);

    CExprInterfaceDefinition(CInterface& Interface, EVstMappingType VstMappingType = EVstMappingType::Ast)
        : CExprInterfaceDefinition(Interface, {}, {}, VstMappingType)
    {}

    ~CExprInterfaceDefinition();

    virtual EAstNodeType GetNodeType() const override                    { return EAstNodeType::Definition_Interface; }
    virtual CUTF8String  GetErrorDesc() const override                   { return "interface definition"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Definition_Interface && &static_cast<const CExprInterfaceDefinition&>(Other)._Interface == &_Interface; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { VisitMembers(Visitor); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;
    virtual bool MayHaveAttributes() const override { return true; }

private:

    TArray<TSRef<CExpressionBase>> _SuperInterfaces;
};

/**
 * Represents a class definition in the AST.
 */
class VERSECOMPILER_API CExprClassDefinition : public CExpressionBase, public CMemberDefinitions
{
public:
    CClass& _Class;
    
    CExprClassDefinition(CClass& Class, TArray<TSRef<CExpressionBase>>&& SuperTypes, TArray<TSRef<CExpressionBase>>&& Members, EVstMappingType = EVstMappingType::Ast);

    CExprClassDefinition(CClass& Class, EVstMappingType VstMappingType = EVstMappingType::Ast)
        : CExprClassDefinition(Class, {}, {}, VstMappingType)
    {
    }

    ~CExprClassDefinition();

    const TArray<TSRef<CExpressionBase>>& SuperTypes() const { return _SuperTypes; }
    void SetSuperTypes(TArray<TSRef<CExpressionBase>>&& SuperTypes) { _SuperTypes = Move(SuperTypes); }
    void SetSuperType(TSRef<CExpressionBase>&& SuperType, int32_t Index) { _SuperTypes[Index] = Move(SuperType); }

    virtual EAstNodeType GetNodeType() const override                    { return EAstNodeType::Definition_Class; }
    virtual CUTF8String  GetErrorDesc() const override                   { return "class definition"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Definition_Class && &static_cast<const CExprClassDefinition&>(Other)._Class == &_Class; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { VisitMembers(Visitor); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;

private:
    TArray<TSRef<CExpressionBase>> _SuperTypes;
};

/**
 * Represents a data definition in the AST.
 */
class VERSECOMPILER_API CExprDataDefinition : public CExprDefinition
{
public:
    const TSRef<CDataDefinition> _DataMember;

    CExprDataDefinition(const TSRef<CDataDefinition>& DataMember, TSPtr<CExpressionBase>&& Element, TSPtr<CExpressionBase>&& ValueDomain, TSPtr<CExpressionBase>&& Value, EVstMappingType = EVstMappingType::Ast);
    ~CExprDataDefinition();

    virtual EAstNodeType GetNodeType() const override                    { return EAstNodeType::Definition_Data; }
    virtual CUTF8String  GetErrorDesc() const override                   { return "data definition"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Definition_Data && &static_cast<const CExprDataDefinition&>(Other)._DataMember == &_DataMember; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual void VisitImmediates(SAstVisitor& Visitor) const override    { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("DataMember", *_DataMember); }
};

/**
 * Represents a map pair definition in the AST: (Key=>Value):Map
 */
class VERSECOMPILER_API CExprIterationPairDefinition : public CExprDefinition
{
public:
    const TSRef<CDataDefinition> _KeyDefinition;
    const TSRef<CDataDefinition> _ValueDefinition;

    CExprIterationPairDefinition(
        TSRef<CDataDefinition>&& KeyDefinition,
        TSRef<CDataDefinition>&& ValueDefinition,
        TSPtr<CExpressionBase>&& Element,
        TSPtr<CExpressionBase>&& ValueDomain,
        TSPtr<CExpressionBase>&& Value,
        EVstMappingType VstMappingType = EVstMappingType::Ast);
    ~CExprIterationPairDefinition();

    virtual EAstNodeType GetNodeType() const override                    { return EAstNodeType::Definition_IterationPair; }
    virtual CUTF8String  GetErrorDesc() const override                   { return "iteration pair definition"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Definition_IterationPair && static_cast<const CExprIterationPairDefinition&>(Other)._KeyDefinition == _KeyDefinition && static_cast<const CExprIterationPairDefinition&>(Other)._ValueDefinition == _ValueDefinition; }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override    { CExpressionBase::VisitImmediates(Visitor); Visitor.VisitImmediate("KeyDefinition", *_KeyDefinition); Visitor.VisitImmediate("ValueDefinition", *_ValueDefinition); }
};

/**
 * Add an item to an array
 * The array itself is not included in the node,
 * instead the result destination in the code generator is used.
 * In the future this will be explicit in the IR, but for now it's inside code generation.
 **/
class VERSECOMPILER_API CIrArrayAdd : public CExpressionBase
{
public:
    CIrArrayAdd(TSRef<CExpressionBase>&& Source)
        : CExpressionBase(EVstMappingType::Ir)
        , _Source(Move(Source))
        {}

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Ir_ArrayAdd; }
    virtual CUTF8String  GetErrorDesc() const override { return "array add"; }
    virtual bool operator==(const CExpressionBase& Other) const override;

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override {return _Source->FindFirstAsyncSubExpr(Program); }
    virtual bool CanFail() const override { return _Source->CanFail(); }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Source", _Source); }

    TSRef<CExpressionBase> _Source;
};

/// @see CIrArrayAdd
class VERSECOMPILER_API CIrMapAdd : public CExpressionBase
{
public:
    CIrMapAdd(TSRef<CExpressionBase>&& Key, TSRef<CExpressionBase>&& Value)
        : CExpressionBase(EVstMappingType::Ir)
        , _Key(Move(Key))
        , _Value(Move(Value))
    {
    }

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Ir_MapAdd; }
    virtual CUTF8String  GetErrorDesc() const override { return "map add"; }
    virtual bool operator==(const CExpressionBase&) const override;
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram&) const override;
    virtual bool CanFail() const override;
    virtual void VisitChildren(SAstVisitor& Visitor) const override;

    TSRef<CExpressionBase> _Key;
    TSRef<CExpressionBase> _Value;
};

class VERSECOMPILER_API CIrArrayUnsafeCall : public CExpressionBase
{
public:
    CIrArrayUnsafeCall(TSRef<CExpressionBase>&& Callee, TSRef<CExpressionBase>&& Arguments)
        : _Callee(Move(Callee))
        , _Argument(Move(Arguments))
    {
    }

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Ir_ArrayUnsafeCall; }

    virtual CUTF8String  GetErrorDesc() const override { return "array unsafe call"; }

    virtual bool operator==(const CExpressionBase&) const override;

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override
    {
        return _Callee->FindFirstAsyncSubExpr(Program);
    }

    virtual bool CanFail() const override
    {
        return
            _Callee->CanFail() ||
            _Argument->CanFail();
    }

    virtual void VisitChildren(SAstVisitor& Visitor) const override
    {
        Visitor.Visit("Callee", _Callee);
        Visitor.Visit("Argument", _Argument);
    }

    TSRef<CExpressionBase> _Callee;
    TSRef<CExpressionBase> _Argument;
};

/**
 * Converts a value to a dynamically typed value. Only present in the IR, not the AST.
 */
class VERSECOMPILER_API CIrConvertToDynamic : public CExprUnaryOp
{
public:
    CIrConvertToDynamic(const CTypeBase* ResultType, TSRef<CExpressionBase>&& Value);

    //~ Begin CExpressionBase interface
    virtual bool CanFail() const override { return Operand()->CanFail(); }
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Ir_ConvertToDynamic; }
    virtual CUTF8String GetErrorDesc() const override { return "convert value to dynamically typed value"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Ir_ConvertToDynamic && (*this) == static_cast<const CExprUnaryOp&>(Other); }
    //~ End CExpressionBase interface
};

/**
 * Converts a value to a dynamically typed value. Only present in the IR, not the AST.
 */
class VERSECOMPILER_API CIrConvertFromDynamic : public CExprUnaryOp
{
public:
    CIrConvertFromDynamic(const CTypeBase* ResultType, TSRef<CExpressionBase>&& Value);

    //~ Begin CExpressionBase interface
    virtual bool CanFail() const override { return Operand()->CanFail(); }
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Ir_ConvertFromDynamic; }
    virtual CUTF8String GetErrorDesc() const override { return "convert value from dynamically typed value"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Ir_ConvertFromDynamic && (*this) == static_cast<const CExprUnaryOp&>(Other); }
    //~ End CExpressionBase interface
};

/**
 * Bounded iteration
 **/
class VERSECOMPILER_API CIrFor : public CExpressionBase
{
public:
    bool _bOutermost{ true };
    bool _bGenerateResult{ true };
    bool _bCanFail{ false };

    const TSPtr<CDataDefinition> _KeyMember;
    const TSRef<CDataDefinition> _DataMember;
    const TSRef<CExprDefinition> _Definition;

    CIrFor(const TSRef<CDataDefinition>& DataMember, TSPtr<CExpressionBase>&& Element, TSPtr<CExpressionBase>&& ValueDomain, TSPtr<CExpressionBase>&& Value)
        : CExpressionBase(EVstMappingType::Ir)
        , _KeyMember()
        , _DataMember(DataMember)
        , _Definition(TSRef<CExprDefinition>::New(Move(Element), Move(ValueDomain), Move(Value), EVstMappingType::Ir)) {}

    CIrFor(const TSRef<CDataDefinition>& KeyMember, const TSRef<CDataDefinition>& DataMember, TSPtr<CExpressionBase>&& Element, TSPtr<CExpressionBase>&& ValueDomain, TSPtr<CExpressionBase>&& Value)
        : CExpressionBase(EVstMappingType::Ir)
        , _KeyMember(KeyMember)
        , _DataMember(DataMember)
        , _Definition(TSRef<CExprDefinition>::New(Move(Element), Move(ValueDomain), Move(Value), EVstMappingType::Ir)) {}


    void SetBody(TSPtr<CExpressionBase>&& Body) { _Body = Move(Body); }

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override;
    virtual bool CanFail() const override { return _bCanFail; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Definition", _Definition); Visitor.Visit("Body", _Body); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override
    {
        CExpressionBase::VisitImmediates(Visitor);
        Visitor.VisitImmediate("bGenerateResult", _bGenerateResult);
        if (_KeyMember)
        {
            Visitor.VisitImmediate("KeyMember", *_KeyMember);
        }
        Visitor.VisitImmediate("DataMember", *_DataMember);
    }

    virtual EAstNodeType   GetNodeType() const override { return EAstNodeType::Ir_For; }
    virtual CUTF8String    GetErrorDesc() const override { return "ir_for"; }
    virtual bool operator==(const CExpressionBase& Other) const override;

public:
    // The scope containing the variables used for iterating
    TSPtr<CControlScope> _AssociatedScope;

    // Expression to evaluate for every iteration that gets past the filters step
    TSPtr<CExpressionBase> _Body;
};

/**
 * Wraps the innermost body of CIrFor
 * It wraps the code not inside the failure context of CIrFor.
 **/
class VERSECOMPILER_API CIrForBody : public CExpressionBase
{
public:
    CIrForBody(TSPtr<CExpressionBase>&& Body)
        : CExpressionBase(EVstMappingType::Ir)
        , _Body(Body) {}

    void SetBody(TSPtr<CExpressionBase>&& Body) { _Body = Move(Body); }

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override;
    virtual bool CanFail() const override { return _Body->CanFail(); }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Body", _Body); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override {}

    virtual EAstNodeType   GetNodeType() const override { return EAstNodeType::Ir_ForBody; }
    virtual CUTF8String    GetErrorDesc() const override { return "ir_for_body"; }
    virtual bool operator==(const CExpressionBase& Other) const override;

public:
    // Expression to evaluate outside the failure contexts of the enclosing CIrFor
    TSPtr<CExpressionBase> _Body;
};

/**
 * Represents a function definition in the AST.
 */
class VERSECOMPILER_API CExprFunctionDefinition : public CExprDefinition
{
public:
    const TSRef<CFunction> _Function;

    CExprFunctionDefinition(const TSRef<CFunction>& Function, TSPtr<CExpressionBase>&& Element, TSPtr<CExpressionBase>&& ValueDomain, TSPtr<CExpressionBase>&& Value, EVstMappingType = EVstMappingType::Ast);
    ~CExprFunctionDefinition();

    virtual EAstNodeType GetNodeType() const override                    { return EAstNodeType::Definition_Function; }
    virtual CUTF8String  GetErrorDesc() const override                   { return "function definition"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Definition_Function && static_cast<const CExprFunctionDefinition&>(Other)._Function == _Function; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
};

/**
 * Represents a type alias definition in the AST.
 */
class VERSECOMPILER_API CExprTypeAliasDefinition : public CExprDefinition
{
public:
    const TSRef<CTypeAlias> _TypeAlias;

    CExprTypeAliasDefinition(const TSRef<CTypeAlias>& TypeAlias, TSPtr<CExpressionBase>&& Element, TSPtr<CExpressionBase>&& ValueDomain, TSPtr<CExpressionBase>&& Value, EVstMappingType = EVstMappingType::Ast);
    ~CExprTypeAliasDefinition();

    virtual EAstNodeType GetNodeType() const override                    { return EAstNodeType::Definition_TypeAlias; }
    virtual CUTF8String  GetErrorDesc() const override                   { return "type alias definition"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Definition_TypeAlias && static_cast<const CExprTypeAliasDefinition&>(Other)._TypeAlias == _TypeAlias; }
};

/**
 * Represents a using declaration in the AST.
 */
class VERSECOMPILER_API CExprUsing : public CExpressionBase
{
public:
    // Note that not all `using` refer to a module
    const CModule* _Module = nullptr;
    
    TSRef<CExpressionBase> _Context;

    CExprUsing(TSRef<CExpressionBase>&& Context) : _Context(Move(Context)) {}

    virtual EAstNodeType GetNodeType() const override                    { return EAstNodeType::Definition_Using; }
    virtual CUTF8String  GetErrorDesc() const override                   { return "using"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Definition_Using && static_cast<const CExprUsing&>(Other)._Module == _Module; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Context", _Context); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;
};

/**
 * Represents a profile block macro invocation in the AST.
 */
class VERSECOMPILER_API CExprProfileBlock : public CExprSubBlockBase
{
public:
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Flow_ProfileBlock; }
    virtual CUTF8String  GetErrorDesc() const override { return "profile"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Flow_ProfileBlock; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override { return _BlockExpr->GetResultType(Program); }
    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override { return _BlockExpr->FindFirstAsyncSubExpr(Program); }

    virtual void VisitImmediates(SAstVisitor& Visitor) const override
    {
        Visitor.Visit("UserTag", _UserTag);
        CExprSubBlockBase::VisitImmediates(Visitor);
    }

    virtual void VisitChildren(SAstVisitor& Visitor) const override
    {
        Visitor.Visit("UserTag", _UserTag);
        CExprSubBlockBase::VisitChildren(Visitor);
    }

    TSPtr<uLang::CExpressionBase> _UserTag;         // Must resolve to a string type
};

/**
 * Represents a import declaration in the AST.
 */
class VERSECOMPILER_API CExprImport : public CExpressionBase
{
public:
    const TSRef<CModuleAlias> _ModuleAlias;
    const TSRef<CExpressionBase> _Path;

    CExprImport(const TSRef<CModuleAlias>& ModuleAlias, TSRef<CExpressionBase>&& Path, EVstMappingType VstMappingType = EVstMappingType::Ast);
    ~CExprImport();

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Definition_Import; }
    virtual CUTF8String  GetErrorDesc() const override { return "import"; }
    virtual bool operator==(const CExpressionBase& Other) const override { return Other.GetNodeType() == EAstNodeType::Definition_Import; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.Visit("Path", _Path); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;
};

class VERSECOMPILER_API CExprWhere : public CExpressionBase
{
public:
    CExprWhere(TSRef<CExpressionBase>&& Lhs, TSPtrArray<CExpressionBase>&& Rhs)
        : _Lhs(Move(Lhs))
        , _Rhs(Move(Rhs))
    {
    }

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Definition_Where; }

    virtual CUTF8String GetErrorDesc() const override { return "where"; }

    virtual bool operator==(const CExpressionBase& Right) const override
    {
        if (!BaseCompare(*this, Right))
        {
            return false;
        }
        const CExprWhere& RightWhere = static_cast<const CExprWhere&>(Right);
        return
            IsSubExprEqual(_Lhs, RightWhere._Lhs) &&
            AreSubExprsEqual(_Rhs, RightWhere._Rhs);
    }

    virtual void VisitChildren(SAstVisitor& Visitor) const override
    {
        Visitor.Visit("Lhs", _Lhs);
        Visitor.VisitArray("Rhs", _Rhs);
    }

    const TSPtr<CExpressionBase>& Lhs() const
    {
        return _Lhs;
    }

    const TSPtrArray<CExpressionBase>& Rhs() const
    {
        return _Rhs;
    }

    TSPtrArray<CExpressionBase>& Rhs()
    {
        return _Rhs;
    }

    void SetLhs(TSPtr<CExpressionBase> NewLhs)
    {
        _Lhs = Move(NewLhs);
    }

    void SetRhs(TSPtrArray<CExpressionBase> NewRhs)
    {
        _Rhs = Move(NewRhs);
    }

private:
    TSPtr<CExpressionBase> _Lhs;
    TSPtrArray<CExpressionBase> _Rhs;
};

class VERSECOMPILER_API CExprVar : public CExprUnaryOp
{
    using CExprUnaryOp::CExprUnaryOp;

    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Definition_Var; }
    virtual bool CanFail() const override { return Operand()->CanFail(); }
    virtual CUTF8String GetErrorDesc() const override { return "var"; }
    virtual bool operator==(const CExpressionBase& Other) const override;
};

/**
 * Represents a named value / default value placeholder in the AST.
 */
class VERSECOMPILER_API CExprMakeNamed : public CExpressionBase
{
public:
    explicit CExprMakeNamed(CSymbol Name)
        : _Name(Name)
    {
    }

    CExprMakeNamed(CSymbol Name, TSPtr<CExpressionBase>&& Argument)
        : _Name(Name)
        , _Value(Move(Argument))
    {
    }

    const CSymbol& GetName() const                     { return _Name; }
    const TSPtr<CExpressionBase>& GetValue() const     { return _Value; }
    void SetValue(TSPtr<CExpressionBase> Value)        { _Value = Move(Value); }
    virtual EAstNodeType GetNodeType() const override  { return EAstNodeType::Invoke_MakeNamed; }
    virtual CUTF8String  GetErrorDesc() const override { return "named"; }

    virtual bool operator==(const CExpressionBase& Right) const override
    {
        if (!BaseCompare(*this, Right))
        {
            return false;
        }
        const CExprMakeNamed& RightNamedArgument = static_cast<const CExprMakeNamed&>(Right);
        return
            _Name == RightNamedArgument._Name &&
            IsSubExprEqual(_Value, RightNamedArgument._Value);
    }

    virtual void VisitChildren(SAstVisitor& Visitor) const override
    {
        CExpressionBase::VisitChildren(Visitor);
        Visitor.Visit("Value", _Value);
    }

    virtual void VisitImmediates(SAstVisitor& Visitor) const override
    {
        CExpressionBase::VisitImmediates(Visitor);
        Visitor.VisitImmediate("Name", _Name.AsStringView());
    }

    virtual const CExpressionBase* FindFirstAsyncSubExpr(const CSemanticProgram& Program) const override
    {
        return _Value? _Value->FindFirstAsyncSubExpr(Program) : nullptr;
    }

    virtual bool CanFail() const override
    {
        return _Value? _Value->CanFail() : false;
    }

private:
    CSymbol _Name;
    TSPtr<CExpressionBase> _Value;
};

/**
 * Represents a package in the AST.
 **/
class VERSECOMPILER_API CAstPackage : public CAstNode, public CMemberDefinitions
{
public:
    CUTF8String _Name;
    
    CUTF8String _VersePath; // Verse path of the root module of this package
    CModulePart* _RootModule = nullptr; // Root module representing this package's Verse path

    TArray<const CAstPackage*> _Dependencies; // As specified in package settings
    TArray<const CAstPackage*> _UsedDependencies; // Dependencies actually used

    EVerseScope _VerseScope; // Origin/visibility of Verse code in this package
    EPackageRole _Role; // The role this package plays
    uint32_t _EffectiveVerseVersion; // The effective language version the package targets.

    /// This allows us to determine when a package was uploaded for a given Fortnite release version.
    /// It is a HACK that conditionally enables/disables behaviour in the compiler in order to
    /// support previous mistakes allowed to slip through in previous Verse langauge releases  but
    /// now need to be supported for backwards compatability.
    /// When we can confirm that all Fortnite packages that are currently uploaded are beyond this
    /// version being used in all instances of the codebase, this can then be removed.
    uint32_t _UploadedAtFNVersion = VerseFN::UploadedAtFNVersion::Latest;

    // Track the number of persistent values found on a per-package basis instead of across all packages.
    int32_t _NumPersistentVars = 0;

    bool _bAllowNative; // If the native attribute is allowed
    bool _bTreatModulesAsImplicit; // If true, module macros in this package's source and digest will be treated as implicit

    bool _bAllowExperimental; // Whether to allow the use of experimental definitions in this package.

    CAstCompilationUnit* _CompilationUnit = nullptr; // Reverse pointer to our owner

    CAstPackage(const CUTF8String& Name,
                const CUTF8String& VersePath,
                const EVerseScope VerseScope,
                const EPackageRole Role,
                const uint32_t EffectiveVerseVersion,
                const uint32_t UploadedAtFNVersion,
                const bool bAllowNative,
                const bool bTreatDefinitionsAsImplicit,
                const bool bAllowExperimental)
        : _Name(Name)
        , _VersePath(VersePath)
        , _VerseScope(VerseScope)
        , _Role(Role)
        , _EffectiveVerseVersion(EffectiveVerseVersion)
        , _UploadedAtFNVersion(UploadedAtFNVersion)
        , _bAllowNative(bAllowNative)
        , _bTreatModulesAsImplicit(bTreatDefinitionsAsImplicit)
        , _bAllowExperimental(bAllowExperimental)
    {}

    // CAstNode interface.
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Context_Package; }
    virtual CUTF8String GetErrorDesc() const override { return "package"; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { VisitMembers(Visitor); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override;

    // Determine if the definition originates from this package or any of our dependencies
    bool CanSeeDefinition(const CDefinition& Definition) const;
};

/**
 * A group of packages that must be compiled as a unit (= a strongly connected component (SCC) in the dependency graph)
 **/
class VERSECOMPILER_API CAstCompilationUnit : public CAstNode
{
public:

    const TSRefArray<CAstPackage>& Packages() const { return _Packages; }
    void ReservePackages(int32_t Num) { _Packages.Reserve(Num); }
    void AppendPackage(TSRef<CAstPackage>&& Package) { _Packages.Add(Move(Package)); }

    EPackageRole GetRole() const;
    bool IsAllowNative() const;

    // CAstNode interface.
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Context_CompilationUnit; }
    virtual CUTF8String GetErrorDesc() const override { return "compilation unit"; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.VisitArray("Packages", _Packages); }

private:

    TSRefArray<CAstPackage> _Packages;
};

/**
 * Represents a project in the AST.
 **/
class VERSECOMPILER_API CAstProject : public CAstNode
{
public:
    
    CUTF8String _Name;

    CAstProject(const CUTF8StringView& Name) : _Name(Name) {}

    const TSRefArray<CAstCompilationUnit>& OrderedCompilationUnits() const { return _OrderedCompilationUnits; } // Guaranteed to be sorted in order of dependency
    void ReserveCompilationUnits(int32_t Num) { _OrderedCompilationUnits.Reserve(Num); }
    void AppendCompilationUnit(TSRef<CAstCompilationUnit>&& CompilationUnit) { _OrderedCompilationUnits.Add(Move(CompilationUnit)); }

    const CAstPackage* FindPackageByName(const CUTF8String& PackageName) const;
    int32_t GetNumPackages() const;
    
    // CAstNode interface.
    virtual EAstNodeType GetNodeType() const override { return EAstNodeType::Context_Project; }
    virtual CUTF8String GetErrorDesc() const override { return "project"; }
    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.VisitArray("CompilationUnits", _OrderedCompilationUnits); }
    virtual void VisitImmediates(SAstVisitor& Visitor) const override { CAstNode::VisitImmediates(Visitor); Visitor.VisitImmediate("Name", _Name); }

private:

    TSRefArray<CAstCompilationUnit> _OrderedCompilationUnits; // Guaranteed to be sorted in order of dependency
};

//=======================================================================================
// CAstNode Implementations
//=======================================================================================

template<typename FunctionType>
struct TAstFunctionVisitor : public SAstVisitor
{
    FunctionType _Function;
    TAstFunctionVisitor(FunctionType&& Function) : _Function(ForwardArg<FunctionType>(Function)) {}
    virtual void Visit(const char* FieldName, CAstNode& AstNode) override { _Function(*this, AstNode); }
    virtual void VisitElement(CAstNode& AstNode) override { _Function(*this, AstNode); }
};

template<typename FunctionType>
void CAstNode::VisitChildrenLambda(FunctionType&& Function) const
{
    TAstFunctionVisitor<FunctionType> FunctionVisitor(ForwardArg<FunctionType>(Function));
    VisitChildren(FunctionVisitor);
}

//=======================================================================================
// CExpressionBase Inline Methods
//=======================================================================================

//---------------------------------------------------------------------------------------
ULANG_FORCEINLINE bool CExpressionBase::IsSubExprEqual(const CExpressionBase* Lhs, const CExpressionBase* Rhs)
{
    return !Lhs == !Rhs && (!Lhs || *Lhs == *Rhs);
}

ULANG_FORCEINLINE bool CExpressionBase::IsSubExprEqual(const CExpressionBase& Lhs, const CExpressionBase& Rhs)
{
    return IsSubExprEqual(&Lhs, &Rhs);
}

//---------------------------------------------------------------------------------------
ULANG_FORCEINLINE bool CExpressionBase::IsSubExprEqual(const TSPtr<CExpressionBase>& Lhs, const TSPtr<CExpressionBase>& Rhs)
{
    return IsSubExprEqual(Lhs.Get(), Rhs.Get());
}

//---------------------------------------------------------------------------------------
ULANG_FORCEINLINE bool CExpressionBase::IsSubExprEqual(const TSRef<CExpressionBase>& Lhs, const TSRef<CExpressionBase>& Rhs)
{
    return IsSubExprEqual(Lhs.Get(), Rhs.Get());
}

//---------------------------------------------------------------------------------------
ULANG_FORCEINLINE bool CExpressionBase::AreSubExprsEqual(const TSPtrArray<CExpressionBase>& Lhs, const TSPtrArray<CExpressionBase>& Rhs)
{
    if (Lhs.Num() != Rhs.Num())
    {
        return false;
    }

    for (int32_t i = 0; i < Lhs.Num(); ++i)
    {
        if (!IsSubExprEqual(Lhs[i], Rhs[i]))
        {
            return false;
        }
    }

    return true;
}

//---------------------------------------------------------------------------------------
ULANG_FORCEINLINE bool CExpressionBase::AreSubExprsEqual(const TArray<TSPtr<CExpressionBase>>& Lhs, const TArray<TSPtr<CExpressionBase>>& Rhs)
{
    if (Lhs.Num() != Rhs.Num())
    {
        return false;
    }

    for (int32_t i = 0; i < Lhs.Num(); ++i)
    {
        if (!IsSubExprEqual(Lhs[i], Rhs[i]))
        {
            return false;
        }
    }

    return true;
}

//---------------------------------------------------------------------------------------
template <typename TIn, typename TOut>
struct TAsNullableTraitsOf;

template <typename TOut>
struct TAsNullableTraitsOf<TSPtr<CExpressionBase>, TOut>
{
    using TResult = TSPtr<TOut>;
    static TResult Cast(TSPtr<CExpressionBase> X) { return X.As<TOut>(); }
};

template <typename TOut>
struct TAsNullableTraitsOf<TSRef<CExpressionBase>, TOut>
{
    using TResult = TSPtr<TOut>;
    static TResult Cast(TSRef<CExpressionBase> X) { return X.As<TOut>(); }
};

template <typename TOut>
struct TAsNullableTraitsOf<CExpressionBase*, TOut>
{
    using TResult = TOut*;
    static TResult Cast(CExpressionBase* X) { return static_cast<TOut*>(X); }
};

template <typename TOut>
struct TAsNullableTraitsOf<const CExpressionBase*, TOut>
{
    using TResult = const TOut*;
    static TResult Cast(const CExpressionBase* X) { return static_cast<const TOut*>(X); }
};


template <typename TOut,
          typename TIn,
          typename Traits = TAsNullableTraitsOf<TIn, TOut>>
typename Traits::TResult AsNullable(TIn Expr)
{
#define VISIT_AST_NODE_TYPE(Name, Class)                                \
    if constexpr (std::is_same_v<TOut, Class>)                          \
    {                                                                   \
        if (Expr && Expr->GetNodeType() == EAstNodeType::Name)          \
        {                                                               \
            return Traits::Cast(Expr);                                  \
        }                                                               \
    }
    VERSE_VISIT_AST_NODE_TYPES(VISIT_AST_NODE_TYPE)
#undef VISIT_AST_NODE_TYPE
    return nullptr;
}

}  // namespace uLang
