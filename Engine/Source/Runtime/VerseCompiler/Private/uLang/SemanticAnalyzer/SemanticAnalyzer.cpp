// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/SemanticAnalyzer/SemanticAnalyzer.h"
#include "Desugarer.h"
#include "uLang/Common/Algo/AnyOf.h"
#include "uLang/Common/Algo/Cases.h"
#include "uLang/Common/Algo/Find.h"
#include "uLang/Common/Algo/FindIf.h"
#include "uLang/Common/Common.h"
#include "uLang/Common/Containers/Function.h"
#include "uLang/Common/Containers/Set.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/UniquePointer.h"
#include "uLang/Common/Misc/Arithmetic.h"
#include "uLang/Common/Misc/EnumUtils.h"
#include "uLang/Common/Misc/MathUtils.h"
#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/Parser/ReservedSymbols.h"
#include "uLang/Semantics/AccessLevel.h"
#include "uLang/Semantics/AccessibilityScope.h"
#include "uLang/Semantics/Attributable.h"
#include "uLang/Semantics/Effects.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/ModuleAlias.h"
#include "uLang/Semantics/ScopedAccessLevelType.h"
#include "uLang/Semantics/SemanticClass.h"
#include "uLang/Semantics/SemanticEnumeration.h"
#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/StructOrClass.h"
#include "uLang/Semantics/TypeAlias.h"
#include "uLang/Semantics/TypeScope.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Semantics/UnknownType.h"
#include "uLang/Semantics/VisitStamp.h"
#include "uLang/SourceProject/PackageRole.h"
#include "uLang/SourceProject/UploadedAtFNVersion.h"
#include "uLang/SourceProject/VerseVersion.h"
#include "uLang/Syntax/VstNode.h"
#include <cmath> // nexttoward
#include <errno.h> // errno
#include <stdlib.h> // itoa, ftoa, strtoll
#include <limits.h> // INT32_MIN, INT32_MAX
#include <limits> // std::numric_limits
#include <inttypes.h> // PRIdPTR

namespace
{
using namespace uLang;

template <typename T, typename U>
void AssignMin(T& Left, U&& Right)
{
    if (Left > Right)
    {
        Left = uLang::ForwardArg<U>(Right);
    }
}

bool ClassIsEnclosingScope(const CDefinition* Definition, const CClass& Class)
{
    return
        Definition->_EnclosingScope.GetKind() == CScope::EKind::Class &&
        Class.IsClass(static_cast<const CClass&>(Definition->_EnclosingScope));
}

bool EnclosingScopeIsNotControl(const CDefinition* Definition)
{
    return !Definition->_EnclosingScope.IsControlScope();
}

CAstPackage* GetPackage(const CDefinition& Definition)
{
    if (const CLogicalScope* Scope = Definition.DefinitionAsLogicalScopeNullable())
    {
        return Scope->GetPackage();
    }
    return Definition._EnclosingScope.GetPackage();
}

EPackageRole GetConstraintPackageRole(const CAstPackage* Package)
{
    return Package ? Package->_Role : EPackageRole::GeneralCompatConstraint;
}

EPackageRole GetConstraintPackageRole(const CDefinition& Definition)
{
    return GetConstraintPackageRole(GetPackage(Definition));
}

// Returns the ancestors of `Arg`, including `Arg`, but not including the
// root program or compat constraint root.
TArray<CScope*> Ancestors(CScope& Arg)
{
    TArray<CScope*> Result;
    // Ignore the program or compat constraint root. 
    for (auto Scope = &Arg, ParentScope = Scope->GetParentScope();
        ParentScope;
        Scope = ParentScope, ParentScope = ParentScope->GetParentScope())
    {
        Result.Add(Scope);
    }
    return Result;
}

// Returns the lowest common ancestor of `Left` and `Right`, searching by
// `GetScopeName`, or `nullptr` if there is no common ancestor.  Note the
// result comes from `Left`'s `Ancestors`.
CScope* LowestCommonAncestorByName(CScope& Left, CScope& Right)
{
    TArray<CScope*> Lefts = Ancestors(Left);
    TArray<CScope*> Rights = Ancestors(Right);
    CScope* PrevScope = nullptr;
    // Search starting at the root going down, returning the last equal
    // value found.  This is required (rather than the more traditional
    // search starting at the lowest point) because the search is by name,
    // and scopes may share names but be otherwise unrelated if the path to
    // them from the root is not the same.
    auto LeftFirst = Lefts.end();
    auto LeftLast = Lefts.Num() > Rights.Num() ? LeftFirst - Rights.Num() : Lefts.begin();
    auto RightFirst = Rights.end();
    while (LeftFirst != LeftLast)
    {
        CScope* LeftScope = *--LeftFirst;
        CScope* RightScope = *--RightFirst;
        if (LeftScope->GetScopeName() != RightScope->GetScopeName())
        {
            break;
        }
        PrevScope = LeftScope;
    }
    return PrevScope;
}

// Flags to record what kind of jumps occur in some code that skip past the succeeding code.
enum class ESkipFlags : uint8_t
{
    None       = 0,
    Break      = 1 << 0,
    Return     = 1 << 1,
    Suppressed = 1 << 2,

    NonSuppressed = Break | Return,
    All = NonSuppressed | Suppressed
};
ULANG_ENUM_BIT_FLAGS(ESkipFlags, inline);

// Flags to record what kind of jumps occur conditionally and unconditionally in some code that skip
// past the succeeding code.
struct SConditionalSkipFlags
{
    ESkipFlags _Unconditional{ESkipFlags::None};
    ESkipFlags _Conditional{ESkipFlags::None};

    SConditionalSkipFlags& operator|=(const SConditionalSkipFlags& Rhs)
    {
        _Unconditional |= Rhs._Unconditional;
        _Conditional |= Rhs._Conditional;
        return *this;
    }
    SConditionalSkipFlags& operator&=(const SConditionalSkipFlags& Rhs)
    {
        _Unconditional &= Rhs._Unconditional;
        _Conditional &= Rhs._Conditional;
        return *this;
    }
    SConditionalSkipFlags& operator&=(ESkipFlags Rhs)
    {
        _Unconditional &= Rhs;
        _Conditional &= Rhs;
        return *this;
    }
    SConditionalSkipFlags& operator|=(ESkipFlags Rhs)
    {
        _Unconditional |= Rhs;
        _Conditional |= Rhs;
        return *this;
    }
};

struct SReachabilityAnalysisVisitor : SAstVisitor
{
    SReachabilityAnalysisVisitor(const CSemanticProgram& Program, CDiagnostics& Diagnostics)
    : _Program(Program), _Diagnostics(Diagnostics)
    {}

    void Visit(CAstNode& Node);
    virtual void Visit(const char* /*FieldName*/, CAstNode& AstNode) override { Visit(AstNode); }
    virtual void VisitElement(CAstNode& AstNode) override { Visit(AstNode); }

    SConditionalSkipFlags GetDominatingSkips() const { return _DominatingSkips; }

private:
    
    const CSemanticProgram& _Program;
    CDiagnostics& _Diagnostics;
    const Verse::Vst::Node* _VstNode{nullptr};

    // Records the skipping jumps that dominate the current expression.
    // Dominate here refers to a dominator in the control flow graph: if the current expression is
    // only reachable by first executing another expression, we say that the current expression is
    // dominated by the other expression.
    SConditionalSkipFlags _DominatingSkips;

    SReachabilityAnalysisVisitor(SReachabilityAnalysisVisitor& Parent)
    : _Program(Parent._Program)
    , _Diagnostics(Parent._Diagnostics)
    , _VstNode(Parent._VstNode)
    , _DominatingSkips(Parent._DominatingSkips)
    {}
    
    void ProduceWarningIfUnreachable()
    {
        // If this code is dominated by an unconditional jump that skips it, and *not* an
        // unconditional warning suppression, emit a warning.
        if (!!(_DominatingSkips._Unconditional & ESkipFlags::NonSuppressed))
        {
            // Unless the code is also dominated by an unconditional warning suppression.
            if (!(_DominatingSkips._Unconditional & ESkipFlags::Suppressed))
            {
                _Diagnostics.AppendGlitch(SGlitchResult(EDiagnostic::WarnSemantic_UnreachableCode), SGlitchLocus(_VstNode));
            }

            // Once dead code is found suppress further warnings.
            _DominatingSkips._Unconditional |= ESkipFlags::Suppressed;
        }
    }
};

void SReachabilityAnalysisVisitor::Visit(CAstNode& AstNode)
{
    TGuardValue<const Verse::Vst::Node*> VstNodeGuard(_VstNode, AstNode.GetMappedVstNode() ? AstNode.GetMappedVstNode() : _VstNode);

    // Produce an unreachable warning if this expression is dominated by a jump that skips it.
    ProduceWarningIfUnreachable();

    switch (AstNode.GetNodeType())
    {
    case EAstNodeType::Error_:
    {
        // Don't recurse on the children of error nodes.
        break;
    }
    case EAstNodeType::Flow_If:
    {
        CExprIf& IfAst = static_cast<CExprIf&>(AstNode);

        Visit(*IfAst.GetCondition());
                    
        // Produce an unreachable warning once if there was a jump in the condition that skips the
        // rest of the if.
        ProduceWarningIfUnreachable();

        SConditionalSkipFlags ThenEarlyExit;
        if (IfAst.GetThenClause())
        {
            SReachabilityAnalysisVisitor ThenVisitor(*this);
            ThenVisitor.Visit(*IfAst.GetThenClause());
            ThenEarlyExit = ThenVisitor._DominatingSkips;
        }

        SConditionalSkipFlags ElseEarlyExit;
        if (IfAst.GetElseClause())
        {
            SReachabilityAnalysisVisitor ElseVisitor(*this);
            ElseVisitor.Visit(*IfAst.GetElseClause());
            ElseEarlyExit = ElseVisitor._DominatingSkips;
        }

        _DominatingSkips._Conditional   |= ThenEarlyExit._Conditional   | ElseEarlyExit._Conditional;
        _DominatingSkips._Unconditional |= ThenEarlyExit._Unconditional & ElseEarlyExit._Unconditional;

        break;
    }
    case EAstNodeType::Flow_Iteration:
    case EAstNodeType::Concurrent_SyncIterated:
    case EAstNodeType::Concurrent_RushIterated:
    case EAstNodeType::Concurrent_RaceIterated:
    {
        // A skipping jump inside for or the iterated concurrency primitives may not execute, so it
        // doesn't dominate expressions dominated by the parent expression.
        SReachabilityAnalysisVisitor ChildVisitor(*this);
        AstNode.VisitChildren(ChildVisitor);
        break;
    }
    case EAstNodeType::Flow_Defer:
    case EAstNodeType::Concurrent_Branch:
    case EAstNodeType::Concurrent_Spawn:
    {
        // defer, branch, and spawn should not have jumps that skip outside them.
        // However, there may be an erroneous skipping jump remaining that we produced an error for
        // earlier in analysis; don't propagate that skip outside this expression to prevent
        // spurious errors about unreachable code following it.
        SReachabilityAnalysisVisitor ChildVisitor(*this);
        ChildVisitor._DominatingSkips = SConditionalSkipFlags{ESkipFlags::None, ESkipFlags::None};
        AstNode.VisitChildren(ChildVisitor);
        break;
    }
    case EAstNodeType::Flow_Loop:
    {
        // A break in a loop doesn't dominate expressions dominated by the loop, but a return does.)
        SReachabilityAnalysisVisitor BodyVisitor(*this);
        BodyVisitor._DominatingSkips._Conditional = ESkipFlags::None;
        AstNode.VisitChildren(BodyVisitor);

        // Only allow loop if sub-expression(s) are async or have some conditional jump that skips
        // out of them.
        // @TODO: SOL-1423, DetermineInvokeTime() re-traverses the expression tree, which could add up time wise 
        //        (approaching on n^2) -- there should be a better way to check this on the initial ProcessExpression()
        CExprLoop& LoopAst = static_cast<CExprLoop&>(AstNode);
        if ((LoopAst.Expr()->DetermineInvokeTime(_Program) == EInvokeTime::Immediate)
            && BodyVisitor._DominatingSkips._Conditional == ESkipFlags::None)
        {
            _Diagnostics.AppendGlitch(SGlitchResult(EDiagnostic::ErrSemantic_InfiniteIteration), SGlitchLocus(_VstNode));
        }
        else if (BodyVisitor._DominatingSkips._Conditional == ESkipFlags::Return)
        {
            _DominatingSkips |= ESkipFlags::Return;
        }

        break;
    }
    case EAstNodeType::Concurrent_Sync:
    {
        // sync has independent subexpressions that dominate the subsequent expressions, meaning
        // that they will all be evaluated before the subsequent expressions are evaluated.
        CExprSync& SyncAst = static_cast<CExprSync&>(AstNode);
        SConditionalSkipFlags EarlyExits{ESkipFlags::None,ESkipFlags::None};
        for (CExpressionBase* SubexpressionAst : SyncAst.GetSubExprs())
        {
            SReachabilityAnalysisVisitor SubexpressionVisitor(*this);
            SubexpressionVisitor.Visit(*SubexpressionAst);
            EarlyExits |= SubexpressionVisitor._DominatingSkips;
        }
        _DominatingSkips |= EarlyExits;
        break;
    }
    case EAstNodeType::Concurrent_Rush:
    case EAstNodeType::Concurrent_Race:
    {
        // rush and race have independent subexpressions that don't dominate the subsequent
        // expressions, meaning that they might not be evaluated before the subsequent expressions
        // are evaluated.
        CExprConcurrentBlockBase& ConcurrentBlockAst = static_cast<CExprConcurrentBlockBase&>(AstNode);
        SConditionalSkipFlags AllSubexpressionSkips{ESkipFlags::All, ESkipFlags::All};
        SConditionalSkipFlags AnySubexpressionSkips{ESkipFlags::None, ESkipFlags::None};
        for (CExpressionBase* SubexpressionAst : ConcurrentBlockAst.GetSubExprs())
        {
            SReachabilityAnalysisVisitor SubexpressionVisitor(*this);
            SubexpressionVisitor.Visit(*SubexpressionAst);

            AllSubexpressionSkips &= SubexpressionVisitor._DominatingSkips;
            AnySubexpressionSkips |= SubexpressionVisitor._DominatingSkips;
        }

        // Propagate skips from some subexpressions as conditional skips.
        _DominatingSkips._Conditional |= AnySubexpressionSkips._Conditional;
        _DominatingSkips._Conditional |= AnySubexpressionSkips._Unconditional;

        // Propagate unconditional skips from *all* subexpressions as unconditional skips.
        _DominatingSkips._Unconditional |= AllSubexpressionSkips._Unconditional;

        break;
    }

    case EAstNodeType::Flow_CodeBlock:
    case EAstNodeType::Flow_Let:
    case EAstNodeType::Flow_ProfileBlock:
    {
        // Recurse on the code block's children. Don't produce an error that the code block is
        // unreachable if one of the children is a skipping jump.
        AstNode.VisitChildren(*this);
        break;
    }
    case EAstNodeType::Flow_Return:
    case EAstNodeType::Flow_Break:
    {
        // Recurse on the node's children.
        AstNode.VisitChildren(*this);

        // After visiting the children, check again if this parent node is unreachable.
        ProduceWarningIfUnreachable();

        // Record the return/break as dominating the subsequent expressions.
        if (AstNode.GetNodeType() == EAstNodeType::Flow_Break)
        {
            _DominatingSkips |= ESkipFlags::Break;
        }
        else if(AstNode.GetNodeType() == EAstNodeType::Flow_Return)
        {
            _DominatingSkips |= ESkipFlags::Return;
        }
                 
        // If the return/break has the ignore_unreachable attribute, suppress reachability errors
        // following it.
        if (static_cast<CExpressionBase&>(AstNode).HasAttributeClass(_Program._ignore_unreachable, _Program))
        {
            _DominatingSkips |= ESkipFlags::Suppressed;
        }
        break;
    }
    case EAstNodeType::Definition_Class:
    case EAstNodeType::Definition_Function:
    case EAstNodeType::Definition_Interface:
        // Don't recurse into nested class or function definitions.
        break;

    case EAstNodeType::Ir_For:
    case EAstNodeType::Ir_ForBody:
    case EAstNodeType::Ir_ArrayAdd:
    case EAstNodeType::Ir_MapAdd:
    case EAstNodeType::Ir_ArrayUnsafeCall:
    case EAstNodeType::Ir_ConvertToDynamic:
    case EAstNodeType::Ir_ConvertFromDynamic:
        ULANG_ERRORF("IR node in semantic analyzer.");
        break;

    case EAstNodeType::Context_Project:
    case EAstNodeType::Context_CompilationUnit:
    case EAstNodeType::Context_Package:
    case EAstNodeType::Context_Snippet:
        // We don't expect to find these nodes as subtrees of any AST we're analyzing the reachability of.

        ULANG_ERRORF("Unexpected node in reachability analysis: %s", AstNode.GetErrorDesc().AsCString());
        break;

    case EAstNodeType::Placeholder_:
    case EAstNodeType::External:
    case EAstNodeType::PathPlusSymbol:
    case EAstNodeType::Literal_Logic:
    case EAstNodeType::Literal_Number:
    case EAstNodeType::Literal_Char:
    case EAstNodeType::Literal_String:
    case EAstNodeType::Literal_Path:
    case EAstNodeType::Literal_Enum:
    case EAstNodeType::Literal_Type:
    case EAstNodeType::Literal_Function:
    case EAstNodeType::Definition:
    case EAstNodeType::MacroCall:
    case EAstNodeType::Identifier_Unresolved:
    case EAstNodeType::Identifier_Class:
    case EAstNodeType::Identifier_Module:
    case EAstNodeType::Identifier_ModuleAlias:
    case EAstNodeType::Identifier_Enum:
    case EAstNodeType::Identifier_Interface:
    case EAstNodeType::Identifier_Data:
    case EAstNodeType::Identifier_TypeAlias:
    case EAstNodeType::Identifier_TypeVariable:
    case EAstNodeType::Identifier_Function:
    case EAstNodeType::Identifier_OverloadedFunction:
    case EAstNodeType::Identifier_Self:
    case EAstNodeType::Identifier_BuiltInMacro:
    case EAstNodeType::Identifier_Local:
    case EAstNodeType::Invoke_Invocation:
    case EAstNodeType::Invoke_UnaryArithmetic:
    case EAstNodeType::Invoke_BinaryArithmetic:
    case EAstNodeType::Invoke_ShortCircuitAnd:
    case EAstNodeType::Invoke_ShortCircuitOr:
    case EAstNodeType::Invoke_LogicalNot:
    case EAstNodeType::Invoke_Comparison:
    case EAstNodeType::Invoke_QueryValue:
    case EAstNodeType::Invoke_MakeOption:
    case EAstNodeType::Invoke_MakeArray:
    case EAstNodeType::Invoke_MakeMap:
    case EAstNodeType::Invoke_MakeTuple:
    case EAstNodeType::Invoke_TupleElement:
    case EAstNodeType::Invoke_MakeRange:
    case EAstNodeType::Invoke_Type:
    case EAstNodeType::Invoke_PointerToReference:
    case EAstNodeType::Invoke_Set:
    case EAstNodeType::Invoke_NewPointer:
    case EAstNodeType::Invoke_ReferenceToValue:
    case EAstNodeType::Assignment:
    case EAstNodeType::Invoke_ArrayFormer:
    case EAstNodeType::Invoke_GeneratorFormer:
    case EAstNodeType::Invoke_MapFormer:
    case EAstNodeType::Invoke_OptionFormer:
    case EAstNodeType::Invoke_Subtype:
    case EAstNodeType::Invoke_TupleType:
    case EAstNodeType::Invoke_Arrow:
    case EAstNodeType::Invoke_ArchetypeInstantiation:
    case EAstNodeType::Invoke_MakeNamed:
    case EAstNodeType::Definition_Module:
    case EAstNodeType::Definition_Enum:
    case EAstNodeType::Definition_Data:
    case EAstNodeType::Definition_IterationPair:
    case EAstNodeType::Definition_TypeAlias:
    case EAstNodeType::Definition_Using:
    case EAstNodeType::Definition_Import:
    case EAstNodeType::Definition_Where:
    case EAstNodeType::Definition_Var:
    case EAstNodeType::Definition_ScopedAccessLevel:
    default:
    {
        // Recurse on the node's children.
        AstNode.VisitChildren(*this);

        // After visiting the children, check again if this parent node is unreachable.
        ProduceWarningIfUnreachable();

        break;
    }
    };
}
}

namespace uLang
{
namespace Vst = Verse::Vst;

/// Helper class that does the actual semantic analysis
class CSemanticAnalyzerImpl
{
public:

    //-------------------------------------------------------------------------------------------------
    CSemanticAnalyzerImpl(const TSRef<CSemanticProgram>& InProgram, const SBuildContext& InBuildContext)
        : _Program(InProgram)
        , _Diagnostics(InBuildContext._Diagnostics)
        , _NextRevision(InProgram->GetNextRevision())
        , _BuiltInPackageNames(InBuildContext._BuiltInPackageNames)
        , _OutPackageUsage(InBuildContext._PackageUsage)
        , _BuildParams(InBuildContext._Params)
        , _UnknownTypeName(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Unknown)))
        , _LogicLitSym_True(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::True)))
        , _LogicLitSym_False(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::False)))
        , _SelfName(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Self)))
        , _SuperName(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Super)))
        , _LocalName(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Local)))
        , _Symbol_subtype(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Subtype)))
        , _Symbol_tuple(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Tuple)))
        , _Symbol_break(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Break)))
        , _Symbol_import(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Import)))
        , _Symbol_generator(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Generator)))
        , _TaskName(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Task)))
        , _ForClauseScopeName(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::ForBackticks)))
        , _InnateMacros(InProgram)

        // If this changes, it also needs to be updated in VplIdeServer_Impl::AddFunctionSignatureOptions
        , _DeferredTaskAllocator(8192)
    {
        auto AddReserved = [this, &InProgram](const char* NameCStr)
            {
                _NamesReservedForFuture.Push(InProgram->GetSymbols()->AddChecked(NameCStr));
            };

// Disable PVS-Studio warning about identical comparison since it really doesn't help with anything here.
//-V:VISIT_RESERVED_SYMBOL:501
#define VISIT_RESERVED_SYMBOL(Name, Symbol, Reservation, VerseVersion, FNVersion) \
    if constexpr (Reservation == EIsReservedSymbolResult::ReservedFuture)         \
    {                                                                             \
        AddReserved(reinterpret_cast<const char*>(Symbol));                       \
    }
        VERSE_ENUMERATE_RESERVED_SYMBOLS(VISIT_RESERVED_SYMBOL)
#undef VISIT_RESERVED_SYMBOL
    }

    ~CSemanticAnalyzerImpl()
    {
        // Clean up the deferred tasks that never executed.
        for (SDeferredTaskList& TaskList : _DeferredTasks)
        {
            while (TaskList._Head)
            {
                SDeferredTask* FirstTask = TaskList._Head;
                TaskList._Head = FirstTask->NextTask;
                DeleteDeferredTask(FirstTask);
            };
        }
    }

    //-------------------------------------------------------------------------------------------------
    const TSRef<CSemanticProgram>& GetProgram() const { return _Program; }

    //-------------------------------------------------------------------------------------------------
    enum EDeferredPri
    {
        Deferred_Module = 0,       // Gather all types but defer their internals
        Deferred_Import,           // Process import statements
        Deferred_ModuleReferences, // Process references to modules

        Deferred_Type,                          // Process the internals of types
        Deferred_ValidateCycles,                // Make sure we don't have any cycles in classes or interfaces.
        Deferred_ClosedFunctionBodyExpressions, // Analyze closed-world function body expressions
        Deferred_ValidateType,                  // Done after LinkOverrides is called, ensure _OverriddenDefinition is valid

        Deferred_AttributeClassAttributes, // Process attributes on attribute classes
        Deferred_Attributes,               // Process attributes (like body code though should occur before bodies)
        Deferred_PropagateAttributes,      // Used to propagate attributes from parents to children like a function that returns a class and is native 
        Deferred_ValidateAttributes,

        Deferred_NonFunctionExpressions,      // Analyze expressions outside of functions (e.g. instance variable initializers)
        Deferred_OpenFunctionBodyExpressions, // Analyze open-world function body expressions

        Deferred_FinalValidation,         // Deferred tasks that only produce errors and can be deferred until all other analysis is done.

        Deferred__Num,
        Deferred__Invalid = Deferred__Num
    };


    //-------------------------------------------------------------------------------------------------
    // Whether the result of an expression is used or ignored.
    enum EResultContext
    {
        ResultIsUsed,
        ResultIsUsedAsType,
        ResultIsUsedAsAttribute,
        ResultIsUsedAsQualifier,
        ResultIsCalled,
        ResultIsCalledAsMacro,
        ResultIsDotted,
        ResultIsReturned,
        ResultIsIgnored,
        ResultIsIterated,
        ResultIsSpawned,
        ResultIsImported
    };

    //-------------------------------------------------------------------------------------------------
    // Whether a return is allowed in this context.
    enum EReturnContext : uint8_t
    {
        ReturnIsAllowed=0,
        ReturnIsDisallowedDueToNoFunction=1,
        ReturnIsDisallowedDueToFailureContext=2,
        ReturnIsDisallowedDueToSubexpressionOfAnotherReturn=4
    };
    ULANG_ENUM_BIT_FLAGS(EReturnContext, friend)

    //-------------------------------------------------------------------------------------------------
    // Whether the expression is in a context where references can be produced -
    // for example, assignment left-hand side.
    enum EReferenceableContext
    {
        NotInReferenceableContext,
        InReferenceableContext
    };

    //-------------------------------------------------------------------------------------------------
    struct SExprCtx
    {
        SExprCtx With(EReturnContext NewReturnCtx) const { SExprCtx Temp = *this; Temp.ReturnContext = NewReturnCtx; return Temp; }
        
        SExprCtx With(EReferenceableContext NewReferenceableCtx) const { SExprCtx Temp = *this; Temp.ReferenceableContext = NewReferenceableCtx; return Temp; }
        
        SExprCtx With(const CTypeBase* NewRequiredType) const
        {
            SExprCtx Temp = *this;
            Temp.RequiredType = NewRequiredType;
            return Temp;
        }

        SExprCtx WithResultIsUsedAsType() const
        {
            return With(ResultIsUsedAsType).With(EffectSets::Computes).With(nullptr).With(NotInReferenceableContext);
        }

        SExprCtx WithResultIsUsedAsQualifier() const
        {
            return With(ResultIsUsedAsQualifier).With(EffectSets::Computes).With(nullptr).With(NotInReferenceableContext);
        }

        SExprCtx WithResultIsIgnored() const
        {
            return With(ResultIsIgnored).With(nullptr).With(NotInReferenceableContext);
        }

        SExprCtx WithResultIsUsed(const CTypeBase* NewRequiredType) const
        {
            return With(ResultIsUsed).With(NewRequiredType).With(NotInReferenceableContext);
        }

        SExprCtx WithResultIsCalled() const
        {
            return With(ResultIsCalled).With(nullptr);
        }

        SExprCtx WithResultIsCalledAsMacro() const
        {
            return With(ResultIsCalledAsMacro).With(EffectSets::Computes).With(nullptr).With(NotInReferenceableContext);
        }

        SExprCtx WithResultIsDotted() const
        {
            return With(ResultIsDotted).With(nullptr);
        }

        SExprCtx WithResultIsImported(CPathType& PathType) const
        {
            return With(ResultIsImported).With(&PathType);
        }

        SExprCtx WithResultIsUsedAsAttribute(const CTypeBase* NewRequiredType) const
        {
            return With(ResultIsUsedAsAttribute).With(EffectSets::Computes).With(NewRequiredType).With(NotInReferenceableContext);
        }
        
        SExprCtx WithResultIsReturned(const CTypeBase* ReturnType) const
        {
            // If the return type is void or true (the unit type), allow nested returns since they
            // can't disagree with the outer return on the value being returned.
            // Otherwise, disallow nested return subexpressions that might disagree with the outer
            // partially evaluated return.
            EReturnContext NewReturnContext = ReturnIsDisallowedDueToSubexpressionOfAnotherReturn;
            if (ReturnType->GetNormalType().GetKind() == Cases<ETypeKind::Void, ETypeKind::True>)
            {
                NewReturnContext = ReturnContext;
            }

            return With(ResultIsReturned).With(ReturnType).With(NewReturnContext).With(NotInReferenceableContext);
        }

        SExprCtx WithResultIsIterated() const
        {
            return With(ResultIsIterated).With(nullptr).With(NotInReferenceableContext);
        }
        
        SExprCtx WithResultIsSpawned(const CTypeBase* NewRequiredType) const
        {
            return With(ResultIsSpawned).With(NewRequiredType).With(NotInReferenceableContext);
        }

        SExprCtx AllowReturnFromLeadingStatementsAsSubexpressionOfReturn() const
        {
            // Allow returns in statements that are subexpressions of another return, but precede
            // any evaluation steps that narrow the returned value.
            if ((ReturnContext & ReturnIsDisallowedDueToSubexpressionOfAnotherReturn)
                && ResultContext == ResultIsReturned)
            {
                return With(ReturnContext&~ReturnIsDisallowedDueToSubexpressionOfAnotherReturn);
            }
            else
            {
                return *this;
            }
        }

        SExprCtx With(SEffectSet NewAllowedEffects) const
        {
            SExprCtx Temp = *this;
            if (!Temp.AllowedEffects[EEffect::decides] && NewAllowedEffects[EEffect::decides])
            {
                Temp = Temp.With(ReturnContext | ReturnIsDisallowedDueToFailureContext);
            }
            Temp.AllowedEffects = NewAllowedEffects;
            return Temp;
        }
        
        SExprCtx WithDecides() const
        {
            return With(AllowedEffects.With(EEffect::decides).With(EEffect::no_rollback, false).With(EEffect::suspends, false));
        }

        SExprCtx AllowReservedUnderscoreFunctionIdentifier() const
        {
            SExprCtx Temp = *this;
            Temp.bAllowReservedUnderscoreFunctionIdentifier = true;
            return Temp;
        }

        SExprCtx DisallowReservedUnderscoreFunctionIdentifier() const
        {
            SExprCtx Temp = *this;
            Temp.bAllowReservedUnderscoreFunctionIdentifier = false;
            return Temp;
        }

        SExprCtx WithAllowNonInvokedReferenceToOverloadedFunction(bool X) const
        {
            SExprCtx Temp = *this;
            Temp.bAllowNonInvokedReferenceToOverloadedFunction = X;
            return Temp;
        }

        bool ResultIsUsedAsValue() const
        {
            return
                ResultContext != ResultIsUsedAsType &&
                ResultContext != ResultIsUsedAsQualifier &&
                ResultContext != ResultIsCalled &&
                ResultContext != ResultIsCalledAsMacro;
        }

        static SExprCtx Default() { return SExprCtx{}; }

        SEffectSet AllowedEffects{EffectSets::Computes};
        EResultContext ResultContext{ResultIsUsed};
        EReturnContext ReturnContext{ReturnIsDisallowedDueToNoFunction};
        EReferenceableContext ReferenceableContext{NotInReferenceableContext};
        const CTypeBase* RequiredType{nullptr}; // If set the this is the required type
        bool bAllowReservedUnderscoreFunctionIdentifier{false}; // this is temporary while '_' is a reserved identifier for future use
        bool bAllowExternalMacroCallInNonExternalRole{false};
        bool bAllowNonInvokedReferenceToOverloadedFunction{false};

    private:
        // we don't publicly provide this function because we have special handling for also resetting
        // whether you're in an assignment context based on the result context you're using
        const SExprCtx With(EResultContext NewResultCtx) const { SExprCtx Temp = *this; Temp.ResultContext = NewResultCtx; return Temp; }
    };

    enum EArchetypeInstantiationContext
    {
        ArchetypeInstantiationArgument,
        ConstructorInvocationCallee,
        NotInArchetypeInstantiationContext
    };

    //-------------------------------------------------------------------------------------------------
    // Used to pass information about a Definition node to a MacroCall in the Definition's Value subexpression.
    struct SMacroCallDefinitionContext
    {
        CSymbol _Name;
        const TSPtr<CExpressionBase> _Qualifier;
        TArray<SAttribute> _NameAttributes;
        TArray<SAttribute> _DefAttributes;
        bool _bIsParametric{false};

        SMacroCallDefinitionContext(CSymbol Name, const TSPtr<CExpressionBase>& Qualifier, TArray<SAttribute> NameAttributes, TArray<SAttribute> DefAttributes)
            : _Name(Name)
            , _Qualifier(Qualifier)
            , _NameAttributes(Move(NameAttributes))
            , _DefAttributes(Move(DefAttributes))
        {
        }

        SMacroCallDefinitionContext(CSymbol Name)
            : _Name(Name)
        {
        }
    };

    enum class EAnalysisContext : uint8_t
    {
        Default,
        CalleeAlreadyAnalyzed,
        FirstTupleElementAlreadyAnalyzed,
        IsInUsingExpression,
        ContextAlreadyAnalyzed,
    };
    ULANG_ENUM_BIT_FLAGS(EAnalysisContext, friend);

    enum class EReadWriteContext : uint8_t
    {
        Partial,
        Complete
    };

    struct SExprArgs
    {
        SExprArgs() {};
        EArchetypeInstantiationContext ArchetypeInstantiationContext = NotInArchetypeInstantiationContext;
        SMacroCallDefinitionContext* MacroCallDefinitionContext = nullptr;
        EAnalysisContext AnalysisContext = EAnalysisContext::Default;
        EReadWriteContext ReadWriteContext = EReadWriteContext::Complete;
    };

    enum class EDefinitionElementAnalysisResult
    {
        Failure,
        Definition,
    };

    //-------------------------------------------------------------------------------------------------
    // Describes the LHS of a definition expression. e.g. id, id:type, id^:type
    struct SDefinitionElementAnalysis
    {
        EDefinitionElementAnalysisResult AnalysisResult = EDefinitionElementAnalysisResult::Failure;
        CExprIdentifierUnresolved* IdentifierAst = nullptr;
        CExprVar* VarAst = nullptr;
        CExprInvocation* InvocationAst = nullptr;
        CSymbol IdentifierSymbol;
    };

    void DetectInaccessibleDependency(const CDefinition& Dependee, const CAstNode& AstNode, const Vst::Node* GlitchNode)
    {
        EAstNodeType NodeType = AstNode.GetNodeType();
        if (NodeType == EAstNodeType::Identifier_Class)
        {
            DetectInaccessibleDependency(
                Dependee,
                *static_cast<const CExprIdentifierClass&>(AstNode).GetClass(*_Program)->_Definition,
                GlitchNode);
        }
        else if (NodeType == EAstNodeType::Identifier_Module)
        {
            DetectInaccessibleDependency(
                Dependee,
                *static_cast<const CExprIdentifierModule&>(AstNode).GetModule(*_Program),
                GlitchNode);
        }
        else if (NodeType == EAstNodeType::Identifier_ModuleAlias)
        {
            DetectInaccessibleDependency(
                Dependee,
                static_cast<const CExprIdentifierModuleAlias&>(AstNode)._ModuleAlias,
                GlitchNode);
        }
        else if (NodeType == EAstNodeType::Identifier_Enum)
        {
            DetectInaccessibleDependency(
                Dependee,
                *static_cast<const CExprEnumerationType&>(AstNode).GetEnumeration(*_Program),
                GlitchNode);
        }
        else if (NodeType == EAstNodeType::Identifier_Interface)
        {
            DetectInaccessibleDependency(
                Dependee,
                *static_cast<const CExprInterfaceType&>(AstNode).GetInterface(*_Program),
                GlitchNode);
        }
        else if (NodeType == EAstNodeType::Identifier_Data)
        {
            DetectInaccessibleDependency(
                Dependee,
                static_cast<const CExprIdentifierData&>(AstNode)._DataDefinition,
                GlitchNode);
        }
        else if (NodeType == EAstNodeType::Identifier_TypeAlias)
        {
            DetectInaccessibleDependency(
                Dependee,
                static_cast<const CExprIdentifierTypeAlias&>(AstNode)._TypeAlias,
                GlitchNode);
        }
        else if (NodeType == EAstNodeType::Identifier_Function)
        {
            DetectInaccessibleDependency(
                Dependee,
                static_cast<const CExprIdentifierFunction&>(AstNode)._Function,
                GlitchNode);
        }
        else if (NodeType == EAstNodeType::Identifier_OverloadedFunction)
        {
            const CExprIdentifierOverloadedFunction& Identifier = static_cast<const CExprIdentifierOverloadedFunction&>(AstNode);
            for (const CFunction* FunctionOverload : Identifier._FunctionOverloads)
            {
                DetectInaccessibleDependency(
                    Dependee,
                    *FunctionOverload,
                    GlitchNode);
            }
            DetectInaccessibleTypeDependencies(
                Dependee,
                Identifier._TypeOverload,
                GlitchNode);
        }
        AstNode.VisitChildrenLambda([this, &Dependee, GlitchNode](const SAstVisitor&, const CAstNode& AstNode)
        {
            DetectInaccessibleDependency(Dependee, AstNode, GlitchNode);
        });
    }

    //-------------------------------------------------------------------------------------------------
    // Create glitch if we're trying to define something public/protected in terms of something that's not public/protected
    // Note: This will _not_ check if the dependency is accessible from the definition as that is already done elsewhere
    void DetectInaccessibleDependency(const CDefinition& Dependee, const CDefinition& Dependency, const Vst::Node* GlitchNode)
    {
        // Check function parameter default values.
        if (Dependency._EnclosingScope.GetKind() == CScope::EKind::Function)
        {
            const CDataDefinition* DataDefinition = Dependency.AsNullable<CDataDefinition>();
            if (!DataDefinition)
            {
                return;
            }
            const CExprDefinition* ExprDefinition = DataDefinition->GetAstNode();
            if (!ExprDefinition)
            {
                return;
            }
            const TSPtr<CExpressionBase>& Value = ExprDefinition->Value();
            if (!Value)
            {
                return;
            }
            DetectInaccessibleDependency(Dependee, *Value, GlitchNode);
            return;
        }

        // If this is a parametric class, check for accessibility of the parametric type definition instead.
        if (Dependency.IsA<CClassDefinition>())
        {
            if (Dependency._EnclosingScope.GetKind() == CScope::EKind::Function)
            {
                DetectInaccessibleDependency(Dependee, static_cast<const CFunction&>(Dependency._EnclosingScope), GlitchNode);
                return;
            }
        }

        // Produce an error if Dependency is less accessible than Dependee; i.e. if there may be
        // some scope where Dependee is accessible, but Dependency is not.
        SAccessibilityScope DependeeAccessibility = GetAccessibilityScope(Dependee);
        SAccessibilityScope DependencyAccessibility = GetAccessibilityScope(Dependency);
        if (DependeeAccessibility.IsMoreAccessibleThan(DependencyAccessibility))
        {
            AppendGlitch(
                *Dependee.GetAstNode(),
                EDiagnostic::ErrSemantic_Inaccessible,
                CUTF8String(
                    "Definition %s is accessible %s, but depends on %s, which is only accessible %s. "
                    "The definition should be no more accessible than its dependencies.",
                    GetQualifiedNameString(Dependee).AsCString(),
                    DependeeAccessibility.Describe().AsCString(),
                    GetQualifiedNameString(Dependency).AsCString(),
                    DependencyAccessibility.Describe().AsCString()));
            return;
        }

        if (DependeeAccessibility.IsVisibleInDigest(SDigestScope{}))
        {
            // If the dependee is exported to the digest in a PublicAPI package, don't allow a dependency in an InternalAPI package.
            const CAstPackage* DependeePackage = _Context._Scope->GetPackage();
            const CAstPackage* DependencyPackage = Dependency._EnclosingScope.GetPackage();
            if (DependeePackage->_VerseScope == EVerseScope::PublicAPI && DependencyPackage->_VerseScope == EVerseScope::InternalAPI)
            {
                AppendGlitch(
                    GlitchNode,
                    EDiagnostic::ErrSemantic_Inaccessible,
                        uLang::CUTF8String(
                            "Definition %s will be in the digest for package '%s' that has a VerseScope of PublicAPI, "
                            "but is dependent on %s, which is in the package '%s' that has a VerseScope of InternalAPI. "
                            "This will result in digest compile errors if InternalAPI digests are not available.",
                            GetQualifiedNameString(Dependee).AsCString(),
                            DependeePackage->_Name.AsCString(),
                            GetQualifiedNameString(Dependency).AsCString(),
                            DependencyPackage->_Name.AsCString()));
                return;
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    // Apply DetectInaccessibleDependency to a type
    void DetectInaccessibleTypeDependencies(const CDefinition& Definition, const CTypeBase* DefinitionType, const Vst::Node* VstNode)
    {
        if (DefinitionType && Definition.DerivedAccessLevel()._Kind == Cases<SAccessLevel::EKind::Public, SAccessLevel::EKind::Protected>)
        {
            SemanticTypeUtils::VisitAllDefinitions(DefinitionType, [this, &Definition, VstNode](const CDefinition& Dependency, const CSymbol& /*DependencyName*/)
            {
                DetectInaccessibleDependency(Definition, Dependency, VstNode);
            });
        }
    }

    //-------------------------------------------------------------------------------------------------
    // Create glitch if this function overrides without an override attribute, or has an override attribute without overriding.
    // Returns false and appends an error if incorrect usage of override attribute, true otherwise. 
    bool DetectIncorrectOverrideAttribute(const CFunction& Function)
    {
        const CSemanticProgram& Program = *_Program;
        bool bHasOverride = Function.HasAttributeClass(Program._overrideClass, Program);
        if (bHasOverride != static_cast<bool>(Function.GetOverriddenDefinition()))
        {
            // There are in fact three cases here, not only two. 
            // The missing one is the error messages for override and qualified name, where the qualification is incorrect.
            // This will result in the technically correct but confusing error: This function does not override a function but has an <override> attribute
            AppendGlitch(
                *Function.GetAstNode(),
                EDiagnostic::ErrSemantic_IncorrectOverride,
                CUTF8String(
                    Function.GetOverriddenDefinition()
                    ? "This function overrides a superclass function but has no <override> attribute"
                    : "This function does not override a function but has an <override> attribute"));
            return false;
        }

        // Make also sure that the overridden function is visible to the function that overrides it
        if (Function.GetOverriddenDefinition())
        {
            DeferredRequireAccessible(
                Function.GetAstNode()->GetMappedVstNode(),
                *Function.GetParentScope(),
                *Function.GetOverriddenDefinition());
        }

        // if the function has a qualifier attached, then the qualifier determines if it should have override or not.
        if (Function._Qualifier._Type == SQualifier::EType::NominalType)
        {
            // If qualifier is the same as scope then this is a new function, no override
            if (ULANG_ENSUREF(Function._Qualifier._Definition, "The qualifier was not set correctly during semantic analysis!")
                && Function._Qualifier._Definition == Function._EnclosingScope.ScopeAsType())
            {
                if (bHasOverride)
                {
                    AppendGlitch(
                        *Function.GetAstNode(),
                        EDiagnostic::ErrSemantic_IncorrectOverride,
                        CUTF8String("This function is explicitly new in this scope, it can't override anything"));
                    return false;
                }
            }
            // If qualifier is different from scope then this is an override, but <override> is not required in that case.
        }

        // if the function is a getter or a setter of some class field, it's not allowed to be overridden
        if (bHasOverride && Function.GetOverriddenDefinition()->_bIsAccessorOfSomeClassVar)
        {
            AppendGlitch(*Function.GetAstNode(),
                         EDiagnostic::ErrSemantic_IncorrectOverride,
                         CUTF8String("This function is used as an accessor of a class var. It cannot be overridden."));
            return false;
        }

        return true;
    }

    void GetScopedModulesFromAttribute(const CExpressionBase* scopedAttribute, TArray<const CScope*>& outScopedModules)
    {
        if (const CTypeBase* ResultType = scopedAttribute->GetResultType(*_Program))
        {
            if (const CTypeType* typeType = ResultType->GetNormalType().AsNullable<CTypeType>())
            {
                if (const CClass* classType = typeType->PositiveType()->GetNormalType().AsNullable<CClass>())
                {
                    if (classType->IsSubclassOf(*_Program->_scopedClass))
                    {
                        const CScopedAccessLevelDefinition& accessLevelDefinition = classType->AsChecked<const CScopedAccessLevelDefinition>();
                        outScopedModules = accessLevelDefinition._Scopes;
                    }
                }
            }
        }
    }

    bool HasAccessLevelAttribute(const CAttributable& AttributableObj)
    {
        return AttributableObj.HasAttributeClass(_Program->_publicClass, *_Program) ||
            AttributableObj.HasAttributeClass(_Program->_protectedClass, *_Program) ||
            AttributableObj.HasAttributeClass(_Program->_privateClass, *_Program) ||
            AttributableObj.HasAttributeClass(_Program->_internalClass, *_Program) ||
            AttributableObj.HasAttributeClass(_Program->_epicInternalClass, *_Program) ||
            AttributableObj.HasAttributeClass(_Program->_scopedClass, *_Program);
    }
    
    //-------------------------------------------------------------------------------------------------
    // Determine access level based on attributes.
    // [Currently used internally with CAttributable classes though could be cached for speed]
    TOptional<SAccessLevel> GetAccessLevelFromAttributes(const Vst::Node& ErrorNode, const CAttributable& AttributableObj)
    {
        const int32_t PublicCount = AttributableObj.GetAttributeClassCount(_Program->_publicClass, *_Program);
        const int32_t ProtectedCount = AttributableObj.GetAttributeClassCount(_Program->_protectedClass, *_Program);
        const int32_t PrivateCount = AttributableObj.GetAttributeClassCount(_Program->_privateClass, *_Program);
        const int32_t InternalCount = AttributableObj.GetAttributeClassCount(_Program->_internalClass, *_Program);
        const int32_t EpicInternalCount = AttributableObj.GetAttributeClassCount(_Program->_epicInternalClass, *_Program);

        const TArray<CExpressionBase*> ScopedAttributes = AttributableObj.FindAttributeExprs(_Program->_scopedClass, *_Program);
        const int32_t ScopedCount = ScopedAttributes.Num();
        
        auto GetLevelsString = [&]()
        {
            CUTF8StringBuilder LevelsStr(48u);
            if (PublicCount) { LevelsStr.Append(SAccessLevel::KindAsCString(SAccessLevel::EKind::Public)); }
            if (ProtectedCount) { LevelsStr.Append(SAccessLevel::KindAsCString(SAccessLevel::EKind::Protected)); }
            if (PrivateCount) { LevelsStr.Append(SAccessLevel::KindAsCString(SAccessLevel::EKind::Private)); }
            if (InternalCount) { LevelsStr.Append(SAccessLevel::KindAsCString(SAccessLevel::EKind::Internal)); }
            if (EpicInternalCount) { LevelsStr.Append(SAccessLevel::KindAsCString(SAccessLevel::EKind::EpicInternal)); }
            if (ScopedCount) { LevelsStr.Append(SAccessLevel::KindAsCString(SAccessLevel::EKind::Scoped)); }
            return LevelsStr.MoveToString();
        };

        // Produce an error if more than one access level attribute was specified.
        const int NumAccessLevelAttributes = PublicCount + PrivateCount + ProtectedCount + InternalCount + EpicInternalCount + ScopedCount;

        if (NumAccessLevelAttributes > 1)
        {
            const int NumAccessLevelTypes =
                (PublicCount       ? 1 : 0) +
                (PrivateCount      ? 1 : 0) +
                (ProtectedCount    ? 1 : 0) +
                (InternalCount     ? 1 : 0) +
                (EpicInternalCount ? 1 : 0) +
                (ScopedCount       ? 1 : 0);

            if (NumAccessLevelTypes > 1)
            {
                // error - can't specify something as both public and private
                AppendGlitch(
                    &ErrorNode,
                    EDiagnostic::ErrSemantic_AccessLevelConflict,
                    CUTF8String("Conflicting access levels:%s. Only one access level may be used or omit for default access.", GetLevelsString().AsCString()));
            }
            else
            {
                // error - some access level attribute is used more than once. eg. Double-public
                AppendGlitch(
                    &ErrorNode,
                    EDiagnostic::ErrSemantic_DuplicateAccessLevel,
                    CUTF8String("Duplicate access levels:%s. Only one access level may be used or omit for default access.", GetLevelsString().AsCString()));
            }
        }
        else if ((ProtectedCount || PrivateCount) && _Context._Scope->GetKind() != CScope::EKind::Class)
        {
            // Allow `protected`/`private` only inside classes
            AppendGlitch(
                &ErrorNode,
                EDiagnostic::ErrSemantic_InvalidAccessLevel,
                CUTF8String("Access levels protected and private are only allowed inside classes."));
            return TOptional<SAccessLevel>{SAccessLevel::EKind::Public};
        }

        if (NumAccessLevelAttributes <= 1 && !PublicCount && _Context._Scope->GetKind() == CScope::EKind::Class && static_cast<const CClass*>(_Context._Scope)->IsStruct())
        {
            if (_Context._Package->_EffectiveVerseVersion < Verse::Version::StructFieldsMustBePublic)
            {
                // For old versions, warn about the non-public accessibility and keep going.
                AppendGlitch(&ErrorNode, EDiagnostic::WarnSemantic_DeprecatedNonPublicStructField);
            }
            else if (NumAccessLevelAttributes == 1)
            {
                AppendGlitch(
                    &ErrorNode,
                    EDiagnostic::ErrSemantic_InvalidAccessLevel,
                    CUTF8String("Access level %s is not allowed in structs.", GetLevelsString().AsCString()));
                return TOptional<SAccessLevel>{SAccessLevel::EKind::Public};
            }
        }


        if      (PublicCount      ) { return TOptional<SAccessLevel>{SAccessLevel::EKind::Public}; }
        else if (ProtectedCount   ) { return TOptional<SAccessLevel>{SAccessLevel::EKind::Protected}; }
        else if (InternalCount    ) { return TOptional<SAccessLevel>{SAccessLevel::EKind::Internal}; }
        else if (PrivateCount     ) { return TOptional<SAccessLevel>{SAccessLevel::EKind::Private}; }
        else if (EpicInternalCount) { return TOptional<SAccessLevel>{SAccessLevel::EKind::EpicInternal}; }
        else if (ScopedCount      ) 
        {
            SAccessLevel Access(SAccessLevel::EKind::Scoped);
            GetScopedModulesFromAttribute(ScopedAttributes[0], Access._Scopes);
            return TOptional<SAccessLevel>{Access};
        }
        else                    
        {
            return {};
        }
    }

    //-------------------------------------------------------------------------------------------------
    // Creates an error if the referencing scope's package doesn't explicitly declare a dependency on the definition's package.
    void RequirePackageDependencyIsDeclared(const Vst::Node* ReferencingVstNode, const CScope& ReferencingScope, const CDefinition& Definition)
    {
        // An explicit dependency is not required for built-in definitions.
        if (Definition.IsBuiltIn())
        {
            return;
        }

        CAstPackage* ReferencingPackage = ReferencingScope.GetPackage();

        bool bFoundPackageDependency = false;
        const CAstPackage* FoundPackage = nullptr;
        TArrayG<const CAstPackage*, TInlineElementAllocator<1>> DefiningPackages;
        if (const CModule* Module = Definition.AsNullable<CModule>())
        {
            // Modules might have multiple parts defined in different packages, so check each part separately.
            if (!Module->HasParts())
            {
                bFoundPackageDependency = true;
            }
            else
            {
                for (const CModulePart* ModulePart : Module->GetParts())
                {
                    const CAstPackage* DefiningPackage = ModulePart->GetPackage();
                    if (!DefiningPackage || ReferencingPackage == DefiningPackage || ReferencingPackage->_Dependencies.Contains(DefiningPackage))
                    {
                        bFoundPackageDependency = true;
                        FoundPackage = DefiningPackage;
                        break;
                    }
                    else
                    {
                        DefiningPackages.Add(DefiningPackage);
                    }
                }
            }
        }
        else
        {
            const CAstPackage* DefiningPackage = Definition._EnclosingScope.GetPackage();
            if (!DefiningPackage || ReferencingPackage == DefiningPackage || ReferencingPackage->_Dependencies.Contains(DefiningPackage))
            {
                bFoundPackageDependency = true;
                FoundPackage = DefiningPackage;
            }
            else
            {
                DefiningPackages.Add(DefiningPackage);
            }
        }

        // Maintain package usage statistics if so desired
        if (bFoundPackageDependency && FoundPackage && FoundPackage != ReferencingPackage && _OutPackageUsage)
        {
            ReferencingPackage->_UsedDependencies.AddUnique(FoundPackage);
        }

        // Validate that the member came from a package that was explicitly declared a dependency
        if (!bFoundPackageDependency)
        {
            ULANG_ASSERTF(DefiningPackages.Num(), "Expected at least one defining package to be found");

            CUTF8StringBuilder MessageBuilder;
            MessageBuilder.AppendFormat(
                "`%s` is not defined in the current package (`%s`), and the current package is not explicitly dependent on a package that defines it. "
                "To fix this, consider modifying the dependencies of the .uplugin, .Build.cs or .vpackage file belonging to `%s` to include one of these packages:",
                GetQualifiedNameString(Definition).AsCString(),
                *ReferencingPackage->_Name,
                *ReferencingPackage->_Name);

            for (const CAstPackage* Package : DefiningPackages)
            {
                MessageBuilder.Append("\n    ");
                MessageBuilder.Append(Package->_Name);
            }

            AppendGlitch(
                ReferencingVstNode,
                EDiagnostic::ErrSemantic_DefinitionNotFromDependentPackage,
                MessageBuilder.MoveToString());
        }
    }

    //-------------------------------------------------------------------------------------------------
    // Creates an error if the definition isn't accessible from the referencing scope.
    bool RequireConstructorAccessible(const Vst::Node* ReferencingVstNode, const CScope& ReferencingScope, const CInterface& Interface)
    {
        return RequireConstructorAccessible(ReferencingVstNode, ReferencingScope, Interface, "interface");
    }
    bool RequireConstructorAccessible(const Vst::Node* ReferencingVstNode, const CScope& ReferencingScope, const CClassDefinition& Class)
    {
        return RequireConstructorAccessible(ReferencingVstNode, ReferencingScope, Class, "class");
    }
    template <typename DefinitionType>
    bool RequireConstructorAccessible(const Vst::Node* ReferencingVstNode, const CScope& ReferencingScope, const DefinitionType& ClassOrInterface, const char* Kind)
    {
        ULANG_ASSERTF(_CurrentTaskPhase >= Deferred_ValidateAttributes, "Should not reach here until attributes have been analyzed.");
        if (!ReferencingScope.CanAccess(ClassOrInterface, ClassOrInterface.DerivedConstructorAccessLevel()))
        {
            AppendGlitch(
                ReferencingVstNode,
                EDiagnostic::ErrSemantic_Inaccessible,
                CUTF8String(
                    "Invalid access of %s %s constructor `%s` from %s `%s`.",
                    ClassOrInterface.DerivedConstructorAccessLevel().AsCode().AsCString(),
                    Kind,
                    GetQualifiedNameString(ClassOrInterface).AsCString(),
                    CScope::KindToCString(ReferencingScope.GetKind()),
                    ReferencingScope.GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString()));
            return false;
        }
        return true;
    }

    bool RequireAccessible(
        const Vst::Node* ReferencingVstNode,
        const CScope& ReferencingScope,
        const CDefinition& Definition)
    {
        RequirePackageDependencyIsDeclared(ReferencingVstNode, ReferencingScope, Definition);
        ULANG_ASSERTF(_CurrentTaskPhase >= Deferred_ValidateAttributes, "Should not reach here until attributes have been analyzed.");
        if (!Definition.IsAccessibleFrom(ReferencingScope))
        {
            AppendGlitch(
                ReferencingVstNode,
                EDiagnostic::ErrSemantic_Inaccessible,
                CUTF8String(
                    "Invalid access of %s %s `%s` from %s `%s`.",
                    Definition.DerivedAccessLevel().AsCode().AsCString(),
                    DefinitionKindAsCString(Definition.GetKind()),
                    GetQualifiedNameString(Definition).AsCString(),
                    CScope::KindToCString(ReferencingScope.GetLogicalScope().GetKind()),
                    ReferencingScope.GetLogicalScope().GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString()));
            return false;
        }
        return true;
    }

    void DeferredRequireAccessible(const Vst::Node* ReferencingVstNode, const CScope& ReferencingScope, const CDefinition& Definition)
    {
        // Defer the attribute validation until the definition's attributes have been analyzed.
        EnqueueDeferredTask(Deferred_ValidateAttributes, [this, ReferencingVstNode, &ReferencingScope, &Definition]()
        {
            RequireAccessible(ReferencingVstNode, ReferencingScope, Definition);
        });
    }

    void DeferredRequireOverridableByArchetype(const Vst::Node* OverridingVstNode, const CDefinition& Definition)
    {
        EnqueueDeferredTask(Deferred_ValidateAttributes, [this, OverridingVstNode, &Definition]
        {
            if (Definition.IsFinal()
                && VerseFN::UploadedAtFNVersion::EnableFinalSpecifierFixes(_Context._Package->_UploadedAtFNVersion))
            {
                AppendGlitch(OverridingVstNode,
                    EDiagnostic::ErrSemantic_CannotOverrideFinalMember,
                    CUTF8String("Cannot override final field '%s'.", Definition.AsNameCString()));
            }
        });
    }

    void DeferredRequireOverrideDoesntChangeAccessLevel(TSRef<CExpressionBase> Where, const CDefinition& Definition)
    {
        EnqueueDeferredTask(Deferred_ValidateAttributes, [this, &Definition, Where = Move(Where)]
        {
            const CDefinition* ParentDefinition = Definition.GetOverriddenDefinition();
            if (!ParentDefinition)
            {
                return;
            }

            if (!ParentDefinition->IsAccessibleFrom(Definition._EnclosingScope))
            {
                AppendGlitch(*Where, 
                    EDiagnostic::ErrSemantic_Inaccessible,
                    CUTF8String("definition %s cannot override an inaccessible parent definition", Definition.AsNameCString()));
            } 
            // We do "else if" here because for private fields the accessibility
            // check can lead to weird error messages compounding.
            else if (Definition.SelfAccessLevel().IsSet())
            {
                AppendGlitch(*Where, 
                    EDiagnostic::ErrSemantic_OverrideCantChangeAccessLevel,
                    CUTF8String("Overridden definition %s cannot specify an accessibility level because it inherits accessibility from its parent definition", Definition.AsNameCString()));
            }
        });
    }
    
    //-------------------------------------------------------------------------------------------------
    void LinkFunctionOverride(CFunction* Function)
    {
        ULANG_ASSERTF(!Function->GetOverriddenDefinition(), "CFunction::_OverriddenDefinition shouldn't be initialized yet.");

        const CFunctionType* FunctionType = Function->_Signature.GetFunctionType();

        // Find any inherited definitions this function might be overriding.
        TOptional<const CNominalType*> MaybeContextType = Function->GetMaybeContextType();
        ULANG_ASSERTF(MaybeContextType, "Expected a member function");
        const CNominalType* ContextType = *MaybeContextType;

        // If qualifier and it's same as local scope, then this is a new function that doesn't override anything
        if (Function->_Qualifier._Definition == ContextType)
        {
            return;
        }

        SmallDefinitionArray OverriddenDefinitions = ContextType->FindInstanceMember(Function->GetName(), EMemberOrigin::Inherited, Function->_Qualifier, Function->GetPackage());

        // If there are multiple inherited definitions with the same name, verify that this function
        // either overrides exactly one of them, or has a distinct domain from the existing overloads.
        TArray<CDefinition*> OverriddenNonFunctionDefinitions;
        TArrayG<const CFunction*, TInlineElementAllocator<4>> OverriddenFunctionCandidates;
        TArrayG<const CFunction*, TInlineElementAllocator<4>> IndistinctDomainFunctions;
        for (CDefinition* OverriddenDefinition : OverriddenDefinitions)
        {
            CFunction* OverriddenFunctionCandidate = OverriddenDefinition->AsNullable<CFunction>();
            if (!OverriddenFunctionCandidate)
            {
                OverriddenNonFunctionDefinitions.Add(OverriddenDefinition);
            }
            else
            {
                // If this function's type is a subtype of the overridden function's type, then it's
                // a valid override.
                const CFunctionType* InstFunctionType = Instantiate(*Function)._Type;
                const CFunctionType* OverriddenFunctionCandidateType = Instantiate(*OverriddenFunctionCandidate)._Type;
                if (IsSubtype(InstFunctionType, OverriddenFunctionCandidateType))
                {
                    OverriddenFunctionCandidates.Add(OverriddenFunctionCandidate);
                }
                else if (!SemanticTypeUtils::AreDomainsDistinct(&OverriddenFunctionCandidateType->GetParamsType(), &FunctionType->GetParamsType()))
                {
                    IndistinctDomainFunctions.Add(OverriddenFunctionCandidate);
                }
            }
        }

        if (OverriddenNonFunctionDefinitions.Num())
        {
            // Produce an error if this function shadows some non-function definition.
            AppendGlitch(
                *Function->GetAstNode(),
                EDiagnostic::ErrSemantic_OverrideSignatureMismatch,
                CUTF8String(
                    "This function overrides non-function definition%s %s.",
                    OverriddenNonFunctionDefinitions.Num() == 1 ? "" : "s",
                    FormatDefinitionList(OverriddenNonFunctionDefinitions, "and ").AsCString()));
        }
        else if (OverriddenFunctionCandidates.Num() > 0 && Function->GetParentScope()->GetKind() != CScope::EKind::Class)
        {
            // Only functions in classes can override.
            AppendGlitch(
                *Function->GetAstNode(),
                EDiagnostic::ErrSemantic_IncorrectOverride,
                CUTF8String(
                    "This isn't a class function but tries to override:%s",
                    FormatOverloadList(OverriddenFunctionCandidates).AsCString()));
        }
        else if (IndistinctDomainFunctions.Num())
        {
            // Produce an error if this function's domain isn't distinct from some inherited overload.
            AppendGlitch(
                *Function->GetAstNode(),
                EDiagnostic::ErrSemantic_AmbiguousDefinition,
                CUTF8String(
                    "This function must have a distinct domain from these other functions with the same name:%s",
                    FormatOverloadList(IndistinctDomainFunctions).AsCString()));
        }
        else if (OverriddenFunctionCandidates.Num() > 1)
        {
            // Produce an error if it's ambiguous which inherited overload this function overrides.
            AppendGlitch(
                *Function->GetAstNode(),
                EDiagnostic::ErrSemantic_AmbiguousOverride,
                CUTF8String(
                    "This function override is ambiguous. Could be any of:%s",
                    FormatOverloadList(OverriddenFunctionCandidates).AsCString()));
        }
        else if (OverriddenFunctionCandidates.Num() == 1)
        {
            // Link the function to the function it overrides.
            Function->SetOverriddenDefinition(*OverriddenFunctionCandidates[0]);

            // If qualifier then it is either the same as the enclosing scope (this is a new function)
            // or the defining scope, not one that only overrides.
            if (Function->_Qualifier._Definition)
            {
                const CDefinition* BaseDefinition = &Function->GetBaseOverriddenDefinition();
                if (Function->_Qualifier._Definition != BaseDefinition->_EnclosingScope.ScopeAsType())
                {
                    AppendGlitch(
                        *Function->GetAstNode(),
                        EDiagnostic::ErrSemantic_InvalidQualifier,
                        CUTF8String(
                            "This qualifier must be the defining class '%s'",
                            BaseDefinition->_EnclosingScope.GetScopeName().AsCString()));
                }
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void LinkDataDefinitionOverride(const CClass* Class, CDataDefinition* DataDefinition)
    {
        // Find any inherited definitions this definition might be overriding.
        ULANG_ASSERTF(!DataDefinition->GetOverriddenDefinition(), "CDataDefinition::_OverriddenDefinition shouldn't be initialized yet.");

        const CTypeBase* DefinitionType = DataDefinition->GetType();

        // if the definition doesn't have a type, it can't be an override
        if (DefinitionType != nullptr)
        {
            SQualifier DataDefinitionQualifier = SQualifier::Unknown(); // TODO: should pass in DataDefinition->_Qualifier.
            SmallDefinitionArray OverriddenDefinitions = Class->FindInstanceMember(DataDefinition->GetName(), EMemberOrigin::Inherited, DataDefinitionQualifier, Class->GetPackage());

            // If there are multiple inherited definitions with the same name, verify that this definition
            // overrides exactly one of them, and is a compatible subtype with the base definition.
            TArrayG<const CDefinition*, TInlineElementAllocator<4>>     OverriddenNonDataDefinitions;
            TArrayG<const CDataDefinition*, TInlineElementAllocator<4>> OverriddenCandidates;
            TArrayG<const CDataDefinition*, TInlineElementAllocator<4>> IncorrectDomainCandidates;
            for (CDefinition* OverriddenDefinition : OverriddenDefinitions)
            {
                CDataDefinition* OverriddenCandidate = OverriddenDefinition->AsNullable<CDataDefinition>(); 
                if (!OverriddenCandidate)
                {
                    OverriddenNonDataDefinitions.Add(OverriddenDefinition);
                }
                else
                {
                    // If this member's type is a subtype of the overridden member's type, then it's
                    // a valid override.
                    const CTypeBase* OverriddenCandidateType = OverriddenCandidate->GetType();

                    if ((OverriddenCandidateType != nullptr) && IsSubtype(DefinitionType, OverriddenCandidateType))
                    {
                        OverriddenCandidates.Add(OverriddenCandidate);
                    }
                    else
                    {
                        IncorrectDomainCandidates.Add(OverriddenCandidate);
                    }
                }
            }

            if (OverriddenNonDataDefinitions.Num())
            {
                // Produce an error if this data definition shadows some non-data definition.
                AppendGlitch(
                    *DataDefinition->GetAstNode(),
                    EDiagnostic::ErrSemantic_OverrideSignatureMismatch,
                    CUTF8String(
                        "This data definition overrides non-data definition%s: %s",
                        OverriddenNonDataDefinitions.Num() == 1 ? "" : "s",
                        FormatDefinitionList(OverriddenNonDataDefinitions, "and ").AsCString()));
            }
            else if (IncorrectDomainCandidates.Num())
            {
                // Produce an error if this data member's domain isn't a subtype of what it tried to override.
                AppendGlitch(
                    *DataDefinition->GetAstNode(),
                    EDiagnostic::ErrSemantic_OverrideSignatureMismatch,
                    CUTF8String(
                        "This overriding data definition must be a subtype of the definition it tried to override: %s",
                        FormatDefinitionList(IncorrectDomainCandidates).AsCString()));
            }
            else if (OverriddenCandidates.Num() > 1)
            {
                // Produce an error if we somehow found multiple base definitions.
                AppendGlitch(
                    *DataDefinition->GetAstNode(),
                    EDiagnostic::ErrSemantic_AmbiguousOverride,
                    CUTF8String(
                        "This data member override is ambiguous. Could be any of:%s",
                        FormatDefinitionList(OverriddenCandidates).AsCString()));
            }
            else if (OverriddenCandidates.Num() == 1)
            {
                // Link to the definition it overrides.
                DataDefinition->SetOverriddenDefinition(*OverriddenCandidates[0]);
            }
        }
    }
    

    //-------------------------------------------------------------------------------------------------
    // Keeps track of which classes and interfaces have had their overrides linked already.
    struct SLinkOverridesState
    {
        TArray<const CClass*> _VisitedClasses;
        TArray<const CInterface*> _VisitedInterfaces;
    };
    
    //-------------------------------------------------------------------------------------------------
    void LinkClassOverrides(SLinkOverridesState& State, const CClass* Class)
    {
        if (!State._VisitedClasses.Contains(Class))
        {
            State._VisitedClasses.Add(Class);
            // Link all inherited functions before this class's functions.
            if (Class->_Superclass)
            {
                LinkClassOverrides(State, Class->_Superclass->_GeneralizedClass);
            }
            for (const CInterface* SuperInterface : Class->_SuperInterfaces)
            {
                LinkInterfaceOverrides(State, SuperInterface->_GeneralizedInterface);
            }
            for (CFunction* Function : Class->GetDefinitionsOfKind<CFunction>())
            {
                LinkFunctionOverride(Function);
            }
            for (CDataDefinition* DataDefinition : Class->GetDefinitionsOfKind<CDataDefinition>())
            {
                LinkDataDefinitionOverride(Class, DataDefinition);
            }
            for (const CClass* InstClass : Class->_InstantiatedClasses)
            {
                LinkClassOverrides(State, InstClass);
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void LinkInterfaceOverrides(SLinkOverridesState& State, const CInterface* Interface)
    {
        if (!State._VisitedInterfaces.Contains(Interface))
        {
            State._VisitedInterfaces.Add(Interface);
            // Link all inherited functions before this interface's functions.
            for (const CInterface* SuperInterface : Interface->_SuperInterfaces)
            {
                LinkInterfaceOverrides(State, SuperInterface->_GeneralizedInterface);
            }
            for (CFunction* Function : Interface->GetDefinitionsOfKind<CFunction>())
            {
                LinkFunctionOverride(Function);
            }
            for (const CInterface* InstInterface : Interface->_InstantiatedInterfaces)
            {
                LinkInterfaceOverrides(State, InstInterface);
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void LinkOverrides(SLinkOverridesState& State, const CLogicalScope& RootScope)
    {
        RootScope.IterateRecurseLogicalScopes([this, &State](const CLogicalScope& LogicalScope) -> EVisitResult
        {
            if (LogicalScope.GetKind() == CScope::EKind::Class)
            {
                LinkClassOverrides(State, static_cast<const CClass*>(&LogicalScope));
            }
            else if (LogicalScope.GetKind() == CScope::EKind::Interface)
            {
                LinkInterfaceOverrides(State, static_cast<const CInterface*>(&LogicalScope));
            }
            return EVisitResult::Continue;
        });
    }

    void LinkOverrides()
    {
        SLinkOverridesState State;
        LinkOverrides(State, *_Program);
        LinkOverrides(State, *_Program->_GeneralCompatConstraintRoot);
        LinkOverrides(State, *_Program->_PersistenceCompatConstraintRoot);
        LinkOverrides(State, *_Program->_PersistenceSoftCompatConstraintRoot);
    }
    
    enum class ETypeCompatibility
    {
        PublicNonFinalInstanceFunction,
        InstanceData,
        Other
    };

    ETypeCompatibility GetTypeCompatibility(const CFunction& Function)
    {
        return Function.IsInstanceMember() && !Function.IsFinal() ?
            ETypeCompatibility::PublicNonFinalInstanceFunction :
            ETypeCompatibility::Other;
    }

    static ETypeCompatibility GetTypeCompatibility(const CDataDefinition& DataDefinition)
    {
        return DataDefinition.IsInstanceMember()?
            ETypeCompatibility::InstanceData :
            ETypeCompatibility::Other;
    }

    //-------------------------------------------------------------------------------------------------
    bool IsCompatibleType(
        const CTypeBase* DefinitionType,
        const CTypeBase* CompatConstraintDefinitionType,
        ETypeCompatibility Compatibility)
    {
        // Instantiate flow types for any type variables used in the types.
        if (const CFunctionType* DefinitionFunctionType = DefinitionType->GetNormalType().AsNullable<CFunctionType>())
        {
            DefinitionType = SemanticTypeUtils::Instantiate(DefinitionFunctionType);
        }

        if (const CFunctionType* CompatConstraintDefinitionFunctionType = CompatConstraintDefinitionType->GetNormalType().AsNullable<CFunctionType>())
        {
            CompatConstraintDefinitionType = SemanticTypeUtils::Instantiate(CompatConstraintDefinitionFunctionType);
        }

        // Remap any nominal types in the compatibility constraint type from the compatibility constraint version to the source version.
        CompatConstraintDefinitionType = RemapTypeFromCompatConstraintRoot(CompatConstraintDefinitionType);

        // If the definition is a non-final instance member, require its type to be equivalent to the compatibility constraint version.
        // Otherwise, only require it to be a subtype.
        switch (Compatibility)
        {
        case ETypeCompatibility::PublicNonFinalInstanceFunction:
        case ETypeCompatibility::InstanceData:
            return SemanticTypeUtils::IsEquivalent(DefinitionType, CompatConstraintDefinitionType);
        case ETypeCompatibility::Other:
            return SemanticTypeUtils::IsSubtype(DefinitionType, CompatConstraintDefinitionType);
        default:
            ULANG_UNREACHABLE();
        }
    }


    //-------------------------------------------------------------------------------------------------
    bool IsCompatibleOrUnknownType(
        const CTypeBase* DefinitionType,
        const CTypeBase* CompatConstraintDefinitionType,
        ETypeCompatibility Compatibility)
    {
        return SemanticTypeUtils::IsUnknownType(DefinitionType)
            || SemanticTypeUtils::IsUnknownType(CompatConstraintDefinitionType)
            || IsCompatibleType(DefinitionType, CompatConstraintDefinitionType, Compatibility);
    }

    static EDiagnostic GetCompatRequirementAmbiguousDiagnostic(EPackageRole PackageRole)
    {
        return PackageRole == EPackageRole::PersistenceSoftCompatConstraint ?
            EDiagnostic::WarnSemantic_CompatibilityRequirementAmbiguous :
            EDiagnostic::ErrSemantic_CompatibilityRequirementAmbiguous;
    }

    static EDiagnostic GetCompatRequirementMissingDiagnostic(EPackageRole PackageRole)
    {
        return PackageRole == EPackageRole::PersistenceSoftCompatConstraint ?
            EDiagnostic::WarnSemantic_CompatibilityRequirementMissing :
            EDiagnostic::ErrSemantic_CompatibilityRequirementMissing;
    }

    static EDiagnostic GetCompatRequirementTypeDiagnostic(EPackageRole PackageRole)
    {
        return PackageRole == EPackageRole::PersistenceSoftCompatConstraint ?
            EDiagnostic::WarnSemantic_CompatibilityRequirementType :
            EDiagnostic::ErrSemantic_CompatibilityRequirementType;
    }

    static EDiagnostic GetCompatRequirementValueDiagnostic(EPackageRole PackageRole)
    {
        return PackageRole == EPackageRole::PersistenceSoftCompatConstraint ?
            EDiagnostic::WarnSemantic_CompatibilityRequirementValue :
            EDiagnostic::ErrSemantic_CompatibilityRequirementValue;
    }

    //-------------------------------------------------------------------------------------------------
    void RequireCompatibleType(
        const CDefinition& Definition,
        const CTypeBase* DefinitionType,
        const CDefinition& CompatConstraintDefinition,
        const CTypeBase* CompatConstraintDefinitionType,
        ETypeCompatibility Compatibility)
    {
        if (!IsCompatibleOrUnknownType(DefinitionType, CompatConstraintDefinitionType, Compatibility))
        {
            AppendGlitch(
                *Definition.GetAstNode(),
                GetCompatRequirementTypeDiagnostic(GetConstraintPackageRole(CompatConstraintDefinition)),
                CUTF8String("The type of this definition (%s) is not compatible with the type of the published definition (%s).",
                    DefinitionType->GetNormalType().AsCode().AsCString(),
                    CompatConstraintDefinitionType->GetNormalType().AsCode().AsCString()));
        }
    }

    void AnalyzePersistableCompatConstraints(
        const CClassDefinition& Class,
        const CClassDefinition& CompatConstraintClass,
        EPackageRole CompatConstraintRole)
    {
        if (!CompatConstraintClass.IsPersistable())
        {
            return;
        }
        TSet<const CDefinition*> ConstrainedDefinitions;
        for (const CDefinition* CompatConstraintDefinition : CompatConstraintClass.GetDefinitionsOfKind<CDataDefinition>())
        {
            // `LinkScopeCompatConstraints` errors if the definition cannot be resolved.
            if (const CDefinition* Definition = CompatConstraintDefinition->GetConstrainedDefinition())
            {
                ConstrainedDefinitions.Insert(uLang::Move(Definition));
            }
        }
        for (const CDataDefinition* Definition : Class.GetDefinitionsOfKind<CDataDefinition>())
        {
            if (!ConstrainedDefinitions.Contains(Definition))
            {
                if (CompatConstraintClass.IsStruct())
                {
                    AppendGlitch(
                        *Definition->GetAstNode(),
                        GetCompatRequirementValueDiagnostic(CompatConstraintRole),
                        CUTF8String("Data definitions may not be added to persistable structs.  Data `%s` was added to `%s`.",
                            Definition->AsNameCString(),
                            Class.AsCode().AsCString()
                            ));
                }
                else if (!Definition->HasInitializer())
                {
                    AppendGlitch(
                        *Definition->GetAstNode(),
                        GetCompatRequirementValueDiagnostic(CompatConstraintRole),
                        CUTF8String("Data definitions may not be added to persistable classes if they lack an initializer.  Data `%s` was added to `%s`.",
                            Definition->AsNameCString(),
                            Class.AsCode().AsCString()
                            ));
                }
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeCompatConstraints(const CClassDefinition& Class, const CClassDefinition& CompatConstraintClass)
    {
        AnalyzeCompatConstraintScope(CompatConstraintClass);

        EPackageRole CompatConstraintRole = GetConstraintPackageRole(CompatConstraintClass);

        // Changing a final class with no inheritance to a struct is allowed, but no other struct<->class changes.
        if (Class.IsStruct() && !CompatConstraintClass.IsStruct())
        {
            const bool bConstraintClassIsFinal = CompatConstraintClass._EffectAttributable.HasAttributeClass(_Program->_finalClass, *_Program);
            const bool bConstraintClassHasInheritance = CompatConstraintClass._Superclass || CompatConstraintClass._SuperInterfaces.Num();
            if (!bConstraintClassIsFinal || bConstraintClassHasInheritance)
            {
                AppendGlitch(
                    *Class.GetAstNode(),
                    GetCompatRequirementValueDiagnostic(CompatConstraintRole),
                    CUTF8String(
                        "This definition is a struct, but the published definition is a %s class with %s. Structs are only backward compatible with final classes with no inheritance.",
                        bConstraintClassIsFinal ? "final" : "non-final",
                        bConstraintClassHasInheritance ? "inheritance" : "no inheritance"
                        ));
            }
        }
        else if (!Class.IsStruct() && CompatConstraintClass.IsStruct())
        {
            AppendGlitch(
                *Class.GetAstNode(),
                GetCompatRequirementValueDiagnostic(CompatConstraintRole),
                "This definition is a class, but the published definition is a struct. Classes are not backward compatible with structs.");
        }

        AnalyzePersistableCompatConstraints(Class, CompatConstraintClass, CompatConstraintRole);

        if (CompatConstraintRole == EPackageRole::GeneralCompatConstraint)
        {
            // The class's constructor must be at least as accessible as the compatibility constraint
            // class's constructor.
            const SAccessibilityScope AccessibilityScope = GetAccessibilityScope(Class, Class.DerivedConstructorAccessLevel());
            const SAccessibilityScope CompatConstraintAccessibilityScope = RemapAccessibilityFromCompatConstraintRoot(
                GetAccessibilityScope(CompatConstraintClass, CompatConstraintClass.DerivedConstructorAccessLevel()));
            if (CompatConstraintAccessibilityScope.IsMoreAccessibleThan(AccessibilityScope))
            {
                AppendGlitch(
                    *Class.GetAstNode(),
                    EDiagnostic::ErrSemantic_CompatibilityRequirementAccess,
                    CUTF8String("This class's constructor is less accessible (%s) than the accessibility of the published class's constructor (%s).",
                        AccessibilityScope.Describe().AsCString(),
                        CompatConstraintAccessibilityScope.Describe().AsCString()
                        ));
            }

            // Changing a class from being final to non-final is ok, but not vice-versa.
            if (!Class.IsStruct()
                && !CompatConstraintClass.IsStruct()
                && Class._EffectAttributable.HasAttributeClass(_Program->_finalClass, *_Program)
                && !CompatConstraintClass._EffectAttributable.HasAttributeClass(_Program->_finalClass, *_Program))
            {
                AppendGlitch(
                    *Class.GetAstNode(),
                    EDiagnostic::ErrSemantic_CompatibilityRequirementValue,
                    "This definition is a final class, but the published definition is a non-final class. Final classes are not backward compatible with non-final classes.");
            }

            // Changing a class from being abstract to non-abstract is ok, but not vice-versa.
            if (!Class.IsStruct()
                && !CompatConstraintClass.IsStruct()
                && Class._EffectAttributable.HasAttributeClass(_Program->_abstractClass, *_Program)
                && !CompatConstraintClass._EffectAttributable.HasAttributeClass(_Program->_abstractClass, *_Program))
            {
                AppendGlitch(
                    *Class.GetAstNode(),
                    EDiagnostic::ErrSemantic_CompatibilityRequirementValue,
                    "This definition is an abstract class, but the published definition is a non-abstract class. Abstract classes are not backward compatible with non-abstract classes.");
            }

            // Making a non-unique class unique is ok, but not vice-versa.
            if (!Class._EffectAttributable.HasAttributeClass(_Program->_uniqueClass, *_Program)
                && CompatConstraintClass._EffectAttributable.HasAttributeClass(_Program->_uniqueClass, *_Program))
            {
                AppendGlitch(
                    *Class.GetAstNode(),
                    EDiagnostic::ErrSemantic_CompatibilityRequirementValue,
                    "This definition is a non-unique class, but the published definition is a unique class. Non-unique classes are not backward compatible with unique classes.");
            }

            // Final classes can be changed from non-concrete to concrete, but no other concreteness changes are allowed.
            const bool bClassIsConcrete = Class.IsConcrete();
            const bool bConstraintClassIsConcrete = CompatConstraintClass.IsConcrete();
            if (!bClassIsConcrete && bConstraintClassIsConcrete)
            {
                AppendGlitch(
                    *Class.GetAstNode(),
                    EDiagnostic::ErrSemantic_CompatibilityRequirementValue,
                    "This definition is a non-concrete class, but the published definition is a concrete class. Non-concrete classes are not backward compatible with concrete classes.");
            }
            else if (bClassIsConcrete && !bConstraintClassIsConcrete
                && !CompatConstraintClass._EffectAttributable.HasAttributeClass(_Program->_finalClass, *_Program))
            {
                AppendGlitch(
                    *Class.GetAstNode(),
                    EDiagnostic::ErrSemantic_CompatibilityRequirementValue,
                    "This definition is a concrete class, and the published definition is a non-concrete non-final class. Concrete classes are not backward compatible with non-concrete classes unless they are final.");
            }

            // Adding inheritance from a class or interface to a class is ok, but not removing or changing inheritance.
            if (CompatConstraintClass._Superclass
                && !IsCompatibleOrUnknownType(&Class, CompatConstraintClass._Superclass, ETypeCompatibility::Other))
            {
                AppendGlitch(
                    *Class.GetAstNode(),
                    EDiagnostic::ErrSemantic_CompatibilityRequirementType,
                    CUTF8String("This class is not a subtype of the published super class %s.",
                        CompatConstraintClass._Superclass->AsCode().AsCString()));
            }
            for (const CInterface* CompatConstraintSuperInterface : CompatConstraintClass._SuperInterfaces)
            {
                if (!IsCompatibleOrUnknownType(&Class, CompatConstraintSuperInterface, ETypeCompatibility::Other))
                {
                    AppendGlitch(
                        *Class.GetAstNode(),
                        EDiagnostic::ErrSemantic_CompatibilityRequirementType,
                        CUTF8String("This class is not a subtype of the published super interface %s.",
                            CompatConstraintSuperInterface->AsCode().AsCString()));
                }
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeCompatConstraints(const CDataDefinition& DataDefinition, const CDataDefinition& CompatConstraintDataDefinition)
    {
        // Require the type to be a subtype of the compatibility constraint version's type.
        RequireCompatibleType(
            DataDefinition,
            DataDefinition.GetType(),
            CompatConstraintDataDefinition,
            CompatConstraintDataDefinition.GetType(),
            GetTypeCompatibility(CompatConstraintDataDefinition));

        // If the data definition is an instance member and the compatibility constraint version has
        // an initializer, the current version of the data definition must also have an initializer.
        if (DataDefinition.IsInstanceMember()
            && CompatConstraintDataDefinition.HasInitializer()
            && !DataDefinition.HasInitializer())
        {
            AppendGlitch(
                *DataDefinition.GetAstNode(),
                EDiagnostic::ErrSemantic_CompatibilityRequirementInitialize,
                "This definition doesn't have a default value, but the published definition does. Removing the default value of an instance member is a compatibility breaking change.");
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeCompatConstraints(const CEnumeration& Enumeration, const CEnumeration& CompatConstraintEnumeration)
    {
        AnalyzeCompatConstraintScope(CompatConstraintEnumeration);

        // For the moment, don't allow adding enumerators to enumerations, since that can break exhaustive case expressions.
        for (const TSRef<CEnumerator>& Enumerator : Enumeration.GetDefinitionsOfKind<CEnumerator>())
        {
            if (!CompatConstraintEnumeration.FindDefinitions(Enumerator->GetName()).Num())
            {
                AppendGlitch(
                    *Enumerator->GetAstNode(),
                    EDiagnostic::ErrSemantic_CompatibilityRequirementType,
                    "This enumerator is not present in the published definition of the enumeration. Adding enumerators is not allowed as it can break exhaustive case expressions.");
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeCompatConstraints(const CFunction& Function, const CFunction& CompatConstraintFunction)
    {
        // Require the type to be compatible with the constraint version's type.
        RequireCompatibleType(
            Function,
            Function._Signature.GetFunctionType(),
            CompatConstraintFunction,
            CompatConstraintFunction._Signature.GetFunctionType(),
            GetTypeCompatibility(Function));

        // Don't allow changing a function to or from a constructor.
        const bool bIsConstructor = Function.HasAttributeClass(_Program->_constructorClass, *_Program);
        const bool bCompatConstraintIsConstructor = CompatConstraintFunction.HasAttributeClass(_Program->_constructorClass, *_Program);
        if (bIsConstructor != bCompatConstraintIsConstructor)
        {
            AppendGlitch(
                *Function.GetAstNode(),
                EDiagnostic::ErrSemantic_CompatibilityRequirementType,
                CUTF8String(
                    "This function is a %s function, but its published definition is a %s function. "
                    "Changing between constructor and non-constructor function is a compatibility breaking change.",
                    bIsConstructor ? "constructor" : "non-constructor",
                    bCompatConstraintIsConstructor ? "constructor" : "non-constructor"));
        }

        // Analyze the function's subdefinition compatibility constraints. This will just ignore
        // things like locals that don't have a constrained definition link, and only analyze e.g.
        // parametric classes that do.
        AnalyzeCompatConstraintScope(CompatConstraintFunction);
    }
    
    //-------------------------------------------------------------------------------------------------
    void AnalyzeCompatConstraints(const CInterface& Interface, const CInterface& CompatConstraintInterface)
    {
        AnalyzeCompatConstraintScope(CompatConstraintInterface);

        // Making a non-unique interface unique is ok, but not vice-versa.
        // TODO: we don't support unique interfaces yet.
        /*
        if (!Interface._EffectAttributable.HasAttributeClass(_Program->_uniqueClass, *_Program)
            && CompatConstraintInterface._EffectAttributable.HasAttributeClass(_Program->_uniqueClass, *_Program))
        {
            AppendGlitch(
                *Interface.GetAstNode(),
                EDiagnostic::ErrSemantic_CompatRequirementValue,
                "This definition is a non-unique interface, but the compatibility constraint definition is a unique interface. Non-unique interfaces are not backward compatible with unique interfaces.");
        }
        */

        // The interface's constructor must be at least as accessible as the compatibility constraint interface's constructor.
        const SAccessibilityScope AccessibilityScope = GetAccessibilityScope(Interface, Interface.DerivedConstructorAccessLevel());
        const SAccessibilityScope CompatConstraintAccessibilityScope = GetRemappedAccessibilityScope(
            CompatConstraintInterface,
            CompatConstraintInterface.DerivedConstructorAccessLevel());
        if (CompatConstraintAccessibilityScope.IsMoreAccessibleThan(AccessibilityScope))
        {
            AppendGlitch(
                *Interface.GetAstNode(),
                EDiagnostic::ErrSemantic_CompatibilityRequirementAccess,
                CUTF8String("This interface's constructor is less accessible (%s) than the published interface's constructor (%s).",
                    AccessibilityScope.Describe().AsCString(),
                    CompatConstraintAccessibilityScope.Describe().AsCString()
                ));
        }

        // Adding or removing any superinterface is a compatibility breaking change.
        for (const CInterface* CompatConstraintSuperInterface : CompatConstraintInterface._SuperInterfaces)
        {
            if (!IsCompatibleOrUnknownType(&Interface, CompatConstraintSuperInterface, ETypeCompatibility::Other))
            {
                AppendGlitch(
                    *Interface.GetAstNode(),
                    EDiagnostic::ErrSemantic_CompatibilityRequirementType,
                    CUTF8String("This interface is not a subtype of the published super interface %s.",
                        CompatConstraintSuperInterface->AsCode().AsCString()));
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeCompatConstraints(const CModule& /*Module*/, const CModule& CompatConstraintModule)
    {
        AnalyzeCompatConstraintScope(CompatConstraintModule);
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeCompatConstraints(const CModuleAlias& ModuleAlias, const CModuleAlias& CompatConstraintModuleAlias)
    {
        const CLogicalScope* CompatConstraintModule = RemapScopeFromCompatConstraintRoot(CompatConstraintModuleAlias.Module());
        ULANG_ASSERTF(CompatConstraintModule->GetKind() == CScope::EKind::Module, "Expected remapping to return a scope of the same kind");
        if (ModuleAlias.Module() != CompatConstraintModule)
        {
            AppendGlitch(
                *ModuleAlias.GetAstNode(),
                EDiagnostic::ErrSemantic_CompatibilityRequirementValue,
                CUTF8String(
                    "The value of this closed world definition (%s) is incompatible with the value of the published definition (%s).",
                    GetQualifiedNameString(*ModuleAlias.Module()).AsCString(),
                    GetQualifiedNameString(*static_cast<const CModule*>(CompatConstraintModule)).AsCString()
                    ));
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeCompatConstraints(const CTypeAlias& TypeAlias, const CTypeAlias& CompatConstraintTypeAlias)
    {
        const CTypeBase* CompatConstraintType = RemapTypeFromCompatConstraintRoot(CompatConstraintTypeAlias.GetType());
        if (!SemanticTypeUtils::IsSubtype(TypeAlias.GetType(), CompatConstraintType) || !SemanticTypeUtils::IsSubtype(CompatConstraintType, TypeAlias.GetType()))
        {
            AppendGlitch(
                *TypeAlias.GetAstNode(),
                EDiagnostic::ErrSemantic_CompatibilityRequirementValue,
                CUTF8String(
                    "The value of this closed world definition (%s) is incompatible with the value of the published definition (%s).",
                    TypeAlias.GetType()->AsCode().AsCString(),
                    CompatConstraintType->AsCode().AsCString()
                    ));
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeDefinitionCompatConstraints(
        const CDefinition& Definition,
        const CDefinition& CompatConstraintDefinition,
        const SAccessibilityScope& CompatConstraintAccessibilityScope)
    {
        EPackageRole CompatConstraintRole = GetConstraintPackageRole(CompatConstraintDefinition);
        if (CompatConstraintRole == EPackageRole::GeneralCompatConstraint)
        {
            const SAccessibilityScope AccessibilityScope = GetAccessibilityScope(Definition);
            if (CompatConstraintAccessibilityScope.IsMoreAccessibleThan(AccessibilityScope))
            {
                AppendGlitch(
                    *Definition.GetAstNode(),
                    EDiagnostic::ErrSemantic_CompatibilityRequirementAccess,
                    CUTF8String("This definition is less accessible (%s) than the published definition's accessibility (%s).",
                        AccessibilityScope.Describe().AsCString(),
                        CompatConstraintAccessibilityScope.Describe().AsCString()
                        ));
            }

            // Changing a final instance field to be non-final is ok, but not vice-versa.
            if (Definition.IsInstanceMember()
                && Definition.IsFinal()
                && !CompatConstraintDefinition.IsFinal())
            {
                AppendGlitch(
                    *Definition.GetAstNode(),
                    EDiagnostic::ErrSemantic_CompatibilityRequirementFinal,
                    CUTF8String("This field is final, but the published field is non-final. Changing a non-final field to be final is not backward compatible.",
                        AccessibilityScope.Describe().AsCString(),
                        CompatConstraintAccessibilityScope.Describe().AsCString()
                        ));
            }
        }

        const CDefinition::EKind Kind = Definition.GetKind();
        if (Kind != CompatConstraintDefinition.GetKind())
        {
            AppendGlitch(
                *Definition.GetAstNode(),
                GetCompatRequirementTypeDiagnostic(CompatConstraintRole),
                CUTF8String("The type of this definition (%s) is not compatible with the type of the published definition (%s).",
                    DefinitionKindAsCString(Kind),
                    DefinitionKindAsCString(CompatConstraintDefinition.GetKind())
                    ));
            return;
        }
        switch (Kind)
        {
        case CDefinition::EKind::Class:          AnalyzeCompatConstraints(Definition.AsChecked<CClassDefinition>(), CompatConstraintDefinition.AsChecked<CClassDefinition>()); break;
        case CDefinition::EKind::Data:           AnalyzeCompatConstraints(Definition.AsChecked<CDataDefinition >(), CompatConstraintDefinition.AsChecked<CDataDefinition >()); break;
        case CDefinition::EKind::Enumeration:    AnalyzeCompatConstraints(Definition.AsChecked<CEnumeration    >(), CompatConstraintDefinition.AsChecked<CEnumeration    >()); break;
        case CDefinition::EKind::Function:       AnalyzeCompatConstraints(Definition.AsChecked<CFunction       >(), CompatConstraintDefinition.AsChecked<CFunction       >()); break;
        case CDefinition::EKind::Interface:      AnalyzeCompatConstraints(Definition.AsChecked<CInterface      >(), CompatConstraintDefinition.AsChecked<CInterface      >()); break;
        case CDefinition::EKind::Module:         AnalyzeCompatConstraints(Definition.AsChecked<CModule         >(), CompatConstraintDefinition.AsChecked<CModule         >()); break;
        case CDefinition::EKind::ModuleAlias:    AnalyzeCompatConstraints(Definition.AsChecked<CModuleAlias    >(), CompatConstraintDefinition.AsChecked<CModuleAlias    >()); break;
        case CDefinition::EKind::TypeAlias:      AnalyzeCompatConstraints(Definition.AsChecked<CTypeAlias      >(), CompatConstraintDefinition.AsChecked<CTypeAlias      >()); break;
        case CDefinition::EKind::TypeVariable: ULANG_ERRORF("Encountered type variable %s", GetQualifiedNameString(Definition).AsCString()); break;
        case CDefinition::EKind::Enumerator: break;
        default: ULANG_UNREACHABLE();
        };
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeCompatConstraintScope(const CLogicalScope& CompatConstraintScope)
    {
        for (const CDefinition* CompatConstraintDefinition : CompatConstraintScope.GetDefinitions())
        {
            if (const CDefinition* Definition = CompatConstraintDefinition->GetConstrainedDefinition())
            {
                if (CompatConstraintDefinition->IsPersistenceCompatConstraint())
                {
                    SAccessibilityScope CompatConstraintAccessibilityScope = GetRemappedAccessibilityScope(*CompatConstraintDefinition);
                    AnalyzeDefinitionCompatConstraints(
                        *Definition,
                        *CompatConstraintDefinition,
                        CompatConstraintAccessibilityScope);
                }
                else if (GetConstraintPackageRole(*CompatConstraintDefinition) == EPackageRole::GeneralCompatConstraint)
                {
                    SAccessibilityScope CompatConstraintAccessibilityScope = GetRemappedAccessibilityScope(*CompatConstraintDefinition);
                    if (CompatConstraintAccessibilityScope.IsVisibleInDigest(SDigestScope{.bEpicInternal=true}))
                    {
                        AnalyzeDefinitionCompatConstraints(
                            *Definition,
                            *CompatConstraintDefinition,
                            CompatConstraintAccessibilityScope);
                    }
                }
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeCompatConstraints()
    {
        CLogicalScope* GeneralCompatConstraintRootScope = nullptr;
        CLogicalScope* PersistenceCompatConstraintRootScope = nullptr;
        CLogicalScope* PersistenceSoftCompatConstraintRootScope = nullptr;
        for (const CAstCompilationUnit* CompilationUnit : _Program->_AstProject->OrderedCompilationUnits())
        {
            for (const CAstPackage* Package : CompilationUnit->Packages())
            {
                if (Package->_Role == EPackageRole::GeneralCompatConstraint)
                {
                    // `RootModule` may be `nullptr` if an error occurred while analyzing the package Verse path.
                    if (CModulePart* RootModule = Package->_RootModule)
                    {
                        GeneralCompatConstraintRootScope = RootModule->GetModule();
                    }
                }
                else if (Package->_Role == EPackageRole::PersistenceCompatConstraint)
                {
                    // `RootModule` may be `nullptr` if an error occurred while analyzing the package Verse path.
                    if (CModulePart* RootModule = Package->_RootModule)
                    {
                        PersistenceCompatConstraintRootScope = RootModule->GetModule();
                    }
                }
                else if (Package->_Role == EPackageRole::PersistenceSoftCompatConstraint)
                {
                    // `RootModule` may be `nullptr` if an error occurred while analyzing the package Verse path.
                    if (CModulePart* RootModule = Package->_RootModule)
                    {
                        PersistenceSoftCompatConstraintRootScope = RootModule->GetModule();
                    }
                }
            }
        }
        if (GeneralCompatConstraintRootScope)
        {
            AnalyzeCompatConstraintScope(*GeneralCompatConstraintRootScope);
        }
        if (PersistenceCompatConstraintRootScope)
        {
            AnalyzeCompatConstraintScope(*PersistenceCompatConstraintRootScope);
        }
        if (PersistenceSoftCompatConstraintRootScope)
        {
            AnalyzeCompatConstraintScope(*PersistenceSoftCompatConstraintRootScope);
        }
    }
    
    //-------------------------------------------------------------------------------------------------
    const CTypeBase* RemappedTypeDefinitionAsType(const CTypeBase& Type, const CDefinition* RemappedDefinition)
    {
        if (!RemappedDefinition)
        {
            return &Type;
        }

        switch (RemappedDefinition->GetKind())
        {
        case CDefinition::EKind::Class: return &RemappedDefinition->AsChecked<CClassDefinition>();
        case CDefinition::EKind::Enumeration: return &RemappedDefinition->AsChecked<CEnumeration>();
        case CDefinition::EKind::Interface: return &RemappedDefinition->AsChecked<CInterface>();
        case CDefinition::EKind::Module: return &RemappedDefinition->AsChecked<CModule>();
        case CDefinition::EKind::ModuleAlias: return RemappedDefinition->AsChecked<CModuleAlias>().Module();
        case CDefinition::EKind::TypeAlias: return RemappedDefinition->AsChecked<CTypeAlias>().GetType();
        case CDefinition::EKind::TypeVariable: return &RemappedDefinition->AsChecked<CTypeVariable>();

        case CDefinition::EKind::Data:
        case CDefinition::EKind::Enumerator:
        case CDefinition::EKind::Function:
        default:
            return &Type;
        };
    }

    //-------------------------------------------------------------------------------------------------
    struct SFlowTypeMapping
    {
        const CFlowType* CompatConstraint;
        const CFlowType* RemappedCompatConstraint;
    };

    const CMapType& RemapMapTypeFromCompatConstraintRoot(const CMapType& MapType, TArray<SFlowTypeMapping>& RemappedFlowTypes)
    {
        const CTypeBase* RemappedKeyType = RemapTypeFromCompatConstraintRoot(MapType.GetKeyType(), RemappedFlowTypes);
        const CTypeBase* RemappedValueType = RemapTypeFromCompatConstraintRoot(MapType.GetValueType(), RemappedFlowTypes);
        if (RemappedKeyType == MapType.GetKeyType() && RemappedValueType == MapType.GetValueType())
        {
            return MapType;
        }
        return _Program->GetOrCreateMapType(*RemappedKeyType, *RemappedValueType, MapType.IsWeak());
    }

    const CTypeBase* RemapTypeFromCompatConstraintRoot(const CTypeBase* Type, TArray<SFlowTypeMapping>& RemappedFlowTypes)
    {
        if (const CFlowType* FlowType = Type->AsFlowType())
        {
            if (const SFlowTypeMapping* FlowTypeMapping = RemappedFlowTypes.FindByPredicate(
                [&](const SFlowTypeMapping& Candidate) {return Candidate.CompatConstraint == FlowType; }))
            {
                return FlowTypeMapping->RemappedCompatConstraint;
            }
            else
            {
                CFlowType& RemappedFlowType = _Program->CreateFlowType(FlowType->Polarity(), nullptr);
                RemappedFlowTypes.Add(SFlowTypeMapping{ FlowType, &RemappedFlowType });
                RemappedFlowType.SetChild(RemapTypeFromCompatConstraintRoot(FlowType->GetChild(), RemappedFlowTypes));
                for (const CFlowType* FlowEdgeType : FlowType->FlowEdges())
                {
                    const CTypeBase* RemappedFlowEdgeType = RemapTypeFromCompatConstraintRoot(FlowEdgeType, RemappedFlowTypes);
                    const CFlowType* RemappedFlowEdgeFlowType = RemappedFlowEdgeType->AsFlowType();
                    ULANG_ASSERT(RemappedFlowEdgeFlowType);
                    RemappedFlowType.AddFlowEdge(RemappedFlowEdgeFlowType);
                }
                return &RemappedFlowType;
            }
        }

        const CNormalType& NormalType = Type->GetNormalType();
        switch (NormalType.GetKind())
        {
        // Global types
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
            return Type;

        // Intrinsic parametric types
        case ETypeKind::Array:
        {
            const CArrayType& ArrayType = NormalType.AsChecked<CArrayType>();
            const CTypeBase* RemappedElementType = RemapTypeFromCompatConstraintRoot(ArrayType.GetElementType(), RemappedFlowTypes);
            return RemappedElementType == ArrayType.GetElementType()
                ? &ArrayType
                : &_Program->GetOrCreateArrayType(RemappedElementType);
        }
        case ETypeKind::Generator:
        {
            const CGeneratorType& GeneratorType = NormalType.AsChecked<CGeneratorType>();
            const CTypeBase* RemappedElementType = RemapTypeFromCompatConstraintRoot(GeneratorType.GetElementType(), RemappedFlowTypes);
            return RemappedElementType == GeneratorType.GetElementType()
                ? &GeneratorType
                : &_Program->GetOrCreateGeneratorType(RemappedElementType);
        }
        case ETypeKind::Map:
            return &RemapMapTypeFromCompatConstraintRoot(NormalType.AsChecked<CMapType>(), RemappedFlowTypes);
        case ETypeKind::Pointer:
        {
            const CPointerType& PointerType = NormalType.AsChecked<CPointerType>();
            const CTypeBase* RemappedNegativeValueType = RemapTypeFromCompatConstraintRoot(PointerType.NegativeValueType(), RemappedFlowTypes);
            const CTypeBase* RemappedPositiveValueType = RemapTypeFromCompatConstraintRoot(PointerType.PositiveValueType(), RemappedFlowTypes);
            return RemappedNegativeValueType == PointerType.NegativeValueType() && RemappedPositiveValueType == PointerType.PositiveValueType()
                ? &PointerType
                : &_Program->GetOrCreatePointerType(RemappedNegativeValueType, RemappedPositiveValueType);
        }
        case ETypeKind::Reference:
        {
            const CReferenceType& ReferenceType = NormalType.AsChecked<CReferenceType>();
            const CTypeBase* RemappedNegativeValueType = RemapTypeFromCompatConstraintRoot(ReferenceType.NegativeValueType(), RemappedFlowTypes);
            const CTypeBase* RemappedPositiveValueType = RemapTypeFromCompatConstraintRoot(ReferenceType.PositiveValueType(), RemappedFlowTypes);
            return RemappedNegativeValueType == ReferenceType.NegativeValueType() && RemappedPositiveValueType == ReferenceType.PositiveValueType()
                ? &ReferenceType
                : &_Program->GetOrCreateReferenceType(RemappedNegativeValueType, RemappedPositiveValueType);
        }
        case ETypeKind::Option:
        {
            const COptionType& OptionType = NormalType.AsChecked<COptionType>();
            const CTypeBase* RemappedValueType = RemapTypeFromCompatConstraintRoot(OptionType.GetValueType(), RemappedFlowTypes);
            return RemappedValueType == OptionType.GetValueType()
                ? &OptionType
                : &_Program->GetOrCreateOptionType(RemappedValueType);
        }
        case ETypeKind::Type:
        {
            const CTypeType& TypeType = NormalType.AsChecked<CTypeType>();
            const CTypeBase* RemappedNegativeType = RemapTypeFromCompatConstraintRoot(TypeType.NegativeType(), RemappedFlowTypes);
            const CTypeBase* RemappedPositiveType = RemapTypeFromCompatConstraintRoot(TypeType.PositiveType(), RemappedFlowTypes);
            return RemappedNegativeType == TypeType.NegativeType() && RemappedPositiveType == TypeType.PositiveType()
                ? &TypeType
                : &_Program->GetOrCreateTypeType(RemappedNegativeType, RemappedPositiveType);
        }
        case ETypeKind::Tuple:
        {
            const CTupleType& TupleType = NormalType.AsChecked<CTupleType>();
            CTupleType::ElementArray RemappedElementTypes;
            bool bIdentityRemapping = true;
            for (const CTypeBase* ElementType : TupleType.GetElements())
            {
                const CTypeBase* RemappedElementType = RemapTypeFromCompatConstraintRoot(ElementType, RemappedFlowTypes);
                if (RemappedElementType != ElementType)
                {
                    bIdentityRemapping = false;
                }
                RemappedElementTypes.Add(RemappedElementType);
            }
            return bIdentityRemapping
                ? &TupleType
                : &_Program->GetOrCreateTupleType(Move(RemappedElementTypes), TupleType.GetFirstNamedIndex());
        }
        case ETypeKind::Function:
        {
            const CFunctionType& FunctionType = NormalType.AsChecked<CFunctionType>();
            const CTypeBase* RemappedParamsType = RemapTypeFromCompatConstraintRoot(&FunctionType.GetParamsType(), RemappedFlowTypes);
            const CTypeBase* RemappedReturnType = RemapTypeFromCompatConstraintRoot(&FunctionType.GetReturnType(), RemappedFlowTypes);
            return RemappedParamsType == &FunctionType.GetParamsType() && RemappedReturnType == &FunctionType.GetReturnType()
                ? &FunctionType
                : &_Program->GetOrCreateFunctionType(
                    *RemappedParamsType,
                    *RemappedReturnType,
                    FunctionType.GetEffects(),
                    FunctionType.GetTypeVariables(),
                    FunctionType.ImplicitlySpecialized());
        }
        case ETypeKind::Named:
        {
            const CNamedType& NamedType = NormalType.AsChecked<CNamedType>();
            const CTypeBase* RemappedValueType = RemapTypeFromCompatConstraintRoot(NamedType.GetValueType(), RemappedFlowTypes);
            return RemappedValueType == NamedType.GetValueType()
                ? &NamedType
                : &_Program->GetOrCreateNamedType(NamedType.GetName(), RemappedValueType, NamedType.HasValue());
        }

        // Nominal types
        case ETypeKind::Variable:
        {
            const CTypeVariable& TypeVariable = NormalType.AsChecked<CTypeVariable>();
            const CDefinition* RemappedTypeVariable = TypeVariable.GetConstrainedDefinition();
            return RemappedTypeDefinitionAsType(TypeVariable, RemappedTypeVariable);
        }
        case ETypeKind::Class:
        {
            const CClass& Class = NormalType.AsChecked<CClass>();
            const CClassDefinition& ClassDefinition = *Class._Definition;
            const CDefinition* RemappedClassDefinition = ClassDefinition.GetConstrainedDefinition();
            return RemappedTypeDefinitionAsType(ClassDefinition, RemappedClassDefinition);
        }
        case ETypeKind::Module:
        {
            const CModule& Module = NormalType.AsChecked<CModule>();
            const CDefinition* RemappedModuleDefinition = Module.GetConstrainedDefinition();
            return RemappedTypeDefinitionAsType(Module, RemappedModuleDefinition);
        }
        case ETypeKind::Enumeration:
        {
            const CEnumeration& Enumeration = NormalType.AsChecked<CEnumeration>();
            const CDefinition* RemappedEnumerationDefinition = Enumeration.GetConstrainedDefinition();
            return RemappedTypeDefinitionAsType(Enumeration, RemappedEnumerationDefinition);
        }
        case ETypeKind::Interface:
        {
            const CInterface& Interface = NormalType.AsChecked<CInterface>();
            const CDefinition* RemappedInterfaceDefinition = Interface.GetConstrainedDefinition();
            return RemappedTypeDefinitionAsType(Interface, RemappedInterfaceDefinition);
        }
        default:
             ULANG_UNREACHABLE();
        };
    }

    //-------------------------------------------------------------------------------------------------
    const CTypeBase* RemapTypeFromCompatConstraintRoot(const CTypeBase* Type)
    {
        TArray<SFlowTypeMapping> RemappedFlowTypes;
        return RemapTypeFromCompatConstraintRoot(Type, RemappedFlowTypes);
    }

    //-------------------------------------------------------------------------------------------------
    const CLogicalScope* RemapScopeFromCompatConstraintRoot(const CLogicalScope* LogicalScope)
    {
        if (IsRootScope(*LogicalScope))
        {
            return _Program.Get();
        }

        const CDefinition* Definition = LogicalScope->ScopeAsDefinition();
        const CDefinition* ConstrainedDefinition = Definition->GetConstrainedDefinition();
        if (!ConstrainedDefinition)
        {
            ConstrainedDefinition = Definition;
        }
        
        const CLogicalScope* Result = ConstrainedDefinition->DefinitionAsLogicalScopeNullable();
        ULANG_ASSERT(Result);
        return Result;
    }

    //-------------------------------------------------------------------------------------------------
    SAccessibilityScope RemapAccessibilityFromCompatConstraintRoot(SAccessibilityScope&& AccessibilityScope)
    {
        SAccessibilityScope Result = Move(AccessibilityScope);
        switch (AccessibilityScope._Kind)
        {
        case SAccessibilityScope::EKind::Scope:
        {
            for (const CScope*& Scope : Result._Scopes)
            {
                Scope = RemapScopeFromCompatConstraintRoot(&Scope->GetLogicalScope());
                ULANG_ASSERT(Scope);
            }
            break;
        }
        case SAccessibilityScope::EKind::Universal:
        case SAccessibilityScope::EKind::EpicInternal:
            break;
        default:
            ULANG_UNREACHABLE();
        }
        return Result;
    }
    
    SAccessibilityScope GetRemappedAccessibilityScope(const CDefinition& Definition, const SAccessLevel& InitialAccessLevel = SAccessLevel::EKind::Public)
    {
        return RemapAccessibilityFromCompatConstraintRoot(GetAccessibilityScope(Definition, InitialAccessLevel));
    }

    //-------------------------------------------------------------------------------------------------
    void LinkFunctionCompatConstraints(CFunction& ConstrainedFunction, CFunction& CompatConstraintFunction, VisitStampType VisitStamp)
    {
        // Parametric classes/interfaces will exist as definitions in the function's scope, and need
        // to be linked, but variable definitions should not be.
        for (const TSRef<CDefinition>& CompatConstraintDefinition : CompatConstraintFunction.GetDefinitions())
        {
            if (CompatConstraintDefinition->GetKind() != Cases<CDefinition::EKind::Data, CDefinition::EKind::TypeVariable, CDefinition::EKind::Function>)
            {
                LinkDefinitionCompatConstraint(ConstrainedFunction, CompatConstraintDefinition, VisitStamp);
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    const CAstNode& GetErrorAstNodeForModuleParts(const CModule& Module)
    {
        for (const CModulePart* ModulePart : Module.GetParts())
        {
            if (ModulePart->GetAstNode())
            {
                return *ModulePart->GetAstNode();
            }
        }
        for (const CModulePart* ModulePart : Module.GetParts())
        {
            if (ModulePart->GetAstPackage())
            {
                return *ModulePart->GetAstPackage();
            }
        }

        ULANG_ERRORF(
            "Couldn't find an AST node for module parts of %s to use to report an error",
            GetQualifiedNameString(Module).AsCString());
        ULANG_UNREACHABLE();
    }

    //-------------------------------------------------------------------------------------------------
    const CAstNode& GetErrorAstNodeForDefinition(const CDefinition& Definition)
    {
        if (Definition.GetAstNode())
        {
            return *Definition.GetAstNode();
        }

        if (const CModule* Module = Definition.AsNullable<CModule>())
        {
            return GetErrorAstNodeForModuleParts(*Module);
        }

        ULANG_ERRORF(
            "Couldn't find an AST node for definition %s to use to report an error", 
            GetQualifiedNameString(Definition).AsCString());
        ULANG_UNREACHABLE();
    }

    //-------------------------------------------------------------------------------------------------
    const CAstNode& GetErrorAstNodeForScope(const CScope& Scope)
    {
        if (const CDefinition* Definition = Scope.ScopeAsDefinition())
        {
            return GetErrorAstNodeForDefinition(*Definition);
        }

        if (const CModule* Module = Scope.GetModule())
        {
            return GetErrorAstNodeForModuleParts(*Module);
        }

        ULANG_ERRORF(
            "Couldn't find an AST node for scope %s to use to report an error",
            Scope.GetScopeName().AsCString());
        ULANG_UNREACHABLE();
    }

    //-------------------------------------------------------------------------------------------------
    void LinkDefinitionCompatConstraint(
        CLogicalScope& ConstrainedDefinitionScope,
        const TSRef<CDefinition>& CompatConstraintDefinition,
        VisitStampType VisitStamp)
    {
        if (CompatConstraintDefinition->GetConstrainedDefinition())
        {
            return;
        }

        const CFunction* CompatConstraintFunction = CompatConstraintDefinition->AsNullable<CFunction>();
        ULANG_ASSERT(!CompatConstraintFunction || _CurrentTaskPhase >= Deferred_Type);

        SmallDefinitionArray ConstrainedDefinitions = ConstrainedDefinitionScope.FindDefinitions(
            CompatConstraintDefinition->GetName(),
            EMemberOrigin::InheritedOrOriginal,
            CompatConstraintDefinition->_Qualifier);

        // If there are multiple constrained definitions with the same name, try to resolve which is
        // constrained by looking at function domains.
        if (ConstrainedDefinitions.Num() > 1)
        {
            SmallDefinitionArray ConstrainedDefinitionCandidates;
            for (CDefinition* ConstrainedDefinition : ConstrainedDefinitions)
            {
                if (CompatConstraintFunction)
                {
                    if (CFunction* ConstrainedFunctionCandidate = ConstrainedDefinition->AsNullable<CFunction>())
                    {                
                        // If this function's type is a subtype of the overridden function's type, then it's
                        // a valid override.
                        const CFunctionType* CompatConstraintInstFunctionType = Instantiate(*CompatConstraintFunction)._Type;
                        const CFunctionType* ConstrainedFunctionCandidateType = Instantiate(*ConstrainedFunctionCandidate)._Type;
                        const CTypeBase* RemappedCompatConstraintInstFunctionType = RemapTypeFromCompatConstraintRoot(CompatConstraintInstFunctionType);
                        if (IsSubtype(ConstrainedFunctionCandidateType, RemappedCompatConstraintInstFunctionType))
                        {
                            ConstrainedDefinitionCandidates.Add(ConstrainedFunctionCandidate);
                        }
                    }
                    else
                    {
                        ConstrainedDefinitionCandidates.Add(ConstrainedDefinition);
                    }
                }
                else
                {
                    ConstrainedDefinitionCandidates.Add(ConstrainedDefinition);
                }
            }
            ConstrainedDefinitions = Move(ConstrainedDefinitionCandidates);
        }

        if (ConstrainedDefinitions.Num() == 1)
        {
            CDefinition& Definition = *ConstrainedDefinitions[0];
            CompatConstraintDefinition->SetConstrainedDefinition(Definition);

            const CDefinition::EKind Kind = CompatConstraintDefinition->GetKind();
            if (Definition.GetKind() == Kind)
            {
                switch (Definition.GetKind())
                {
                case CDefinition::EKind::Class:       LinkScopeCompatConstraints   (Definition.AsChecked<CClassDefinition>(), CompatConstraintDefinition->AsChecked<CClassDefinition>(), VisitStamp); break;
                case CDefinition::EKind::Enumeration: LinkScopeCompatConstraints   (Definition.AsChecked<CEnumeration    >(), CompatConstraintDefinition->AsChecked<CEnumeration    >(), VisitStamp); break;
                case CDefinition::EKind::Interface:   LinkScopeCompatConstraints   (Definition.AsChecked<CInterface      >(), CompatConstraintDefinition->AsChecked<CInterface      >(), VisitStamp); break;
                case CDefinition::EKind::Module:      LinkScopeCompatConstraints   (Definition.AsChecked<CModule         >(), CompatConstraintDefinition->AsChecked<CModule         >(), VisitStamp); break;
                case CDefinition::EKind::Function:    LinkFunctionCompatConstraints(Definition.AsChecked<CFunction       >(), CompatConstraintDefinition->AsChecked<CFunction       >(), VisitStamp); break;

                case CDefinition::EKind::Data:
                case CDefinition::EKind::ModuleAlias:
                case CDefinition::EKind::TypeAlias:
                case CDefinition::EKind::TypeVariable:
                case CDefinition::EKind::Enumerator: break;
                default: ULANG_UNREACHABLE();
                };
            }
        }
        else if (ConstrainedDefinitions.Num() == 0)
        {
            EnqueueDeferredTask(Deferred_ValidateAttributes, [this, &ConstrainedDefinitionScope, CompatConstraintDefinition]
            {
                if (CompatConstraintDefinition->IsPersistenceCompatConstraint() ||
                    GetConstraintPackageRole(*CompatConstraintDefinition) == EPackageRole::GeneralCompatConstraint)
                {
                    AppendGlitch(
                        GetErrorAstNodeForScope(ConstrainedDefinitionScope),
                        GetCompatRequirementMissingDiagnostic(GetConstraintPackageRole(*CompatConstraintDefinition)),
                        CUTF8String(
                            "Missing definition in source package that corresponds to published definition %s.",
                            GetQualifiedNameString(*CompatConstraintDefinition).AsCString()));
                }
            });
        }
        else
        {
            CUTF8String FormattedDefinitionList = FormatDefinitionList(ConstrainedDefinitions);
            EnqueueDeferredTask(Deferred_ValidateAttributes, [this, &ConstrainedDefinitionScope, CompatConstraintDefinition, FormattedDefinitionList]
            {
                if (CompatConstraintDefinition->IsPersistenceCompatConstraint() ||
                    GetConstraintPackageRole(*CompatConstraintDefinition) == EPackageRole::GeneralCompatConstraint)
                {
                    // Produce an error if it's ambiguous which overload this definition constrains.
                    AppendGlitch(
                        GetErrorAstNodeForScope(ConstrainedDefinitionScope),
                        GetCompatRequirementAmbiguousDiagnostic(GetConstraintPackageRole(*CompatConstraintDefinition)),
                        CUTF8String(
                            "Published definition corresponds to multiple possible source definitions:%s",
                            FormattedDefinitionList.AsCString()));
                }
            });
        }
    }

    //-------------------------------------------------------------------------------------------------
    void LinkScopeCompatConstraints(CLogicalScope& Scope, CLogicalScope& CompatConstraintScope, VisitStampType VisitStamp)
    {
        if (!CompatConstraintScope.TryMarkVisited(VisitStamp))
        {
            return;
        }
        for (const TSRef<CDefinition>& CompatConstraintDefinition : CompatConstraintScope.GetDefinitions())
        {
            // Types need to be linked before Deferred_Type as type identifiers may be resolved then, but
            // functions can't be linked until Deferred_ValidateType as they use type information to resolve
            // which overload to link.
            if (CompatConstraintDefinition->IsA<CFunction>())
            {
                EnqueueDeferredTask(Deferred_ValidateType, [this, &Scope, CompatConstraintDefinition]
                {
                    VisitStampType VisitStamp = CScope::GenerateNewVisitStamp();
                    LinkDefinitionCompatConstraint(Scope, CompatConstraintDefinition, VisitStamp);
                });
            }
            else
            {
                LinkDefinitionCompatConstraint(Scope, CompatConstraintDefinition, VisitStamp);
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void LinkCompatConstraints()
    {
        CModule* SourceRootModule = nullptr;
        CModule* GeneralCompatConstraintRootModule = nullptr;
        CModule* PersistenceCompatConstraintRootModule = nullptr;
        CModule* PersistenceSoftCompatConstraintRootModule = nullptr;
        // Note, multiple constraints are possible constraining any particular
        // package, though these constraint packages arise only in in-editor
        // tests and are empty.  The only possibility in UEFN or when cooking is
        // a single source and single constraint (per constraint package role).
        for (const CAstCompilationUnit* CompilationUnit : _Program->_AstProject->OrderedCompilationUnits())
        {
            for (const CAstPackage* Package : CompilationUnit->Packages())
            {
                if (Package->_Role == EPackageRole::Source)
                {
                    if (Package->_VerseScope == Cases<EVerseScope::InternalUser, EVerseScope::PublicUser>)
                    {
                        // `RootModule` may be `nullptr` if an error occurred while analyzing the package Verse path.
                        if (CModulePart* RootModule = Package->_RootModule)
                        {
                            // Assumes only a single user package when compat constraint packages exist.
                            SourceRootModule = RootModule->GetModule();
                        }
                    }
                }
                else if (Package->_Role == EPackageRole::GeneralCompatConstraint)
                {
                    // `RootModule` may be `nullptr` if an error occurred while analyzing the package Verse path.
                    if (CModulePart* RootModule = Package->_RootModule)
                    {
                        GeneralCompatConstraintRootModule = RootModule->GetModule();
                    }
                }
                else if (Package->_Role == EPackageRole::PersistenceCompatConstraint)
                {
                    // `RootModule` may be `nullptr` if an error occurred while analyzing the package Verse path.
                    if (CModulePart* RootModule = Package->_RootModule)
                    {
                        PersistenceCompatConstraintRootModule = RootModule->GetModule();
                    }
                }
                else if (Package->_Role == EPackageRole::PersistenceSoftCompatConstraint)
                {
                    // `RootModule` may be `nullptr` if an error occurred while analyzing the package Verse path.
                    if (CModulePart* RootModule = Package->_RootModule)
                    {
                        PersistenceSoftCompatConstraintRootModule = RootModule->GetModule();
                    }
                }
            }
        }
        if (SourceRootModule)
        {
            struct SCompatConstraint
            {
                CCompatConstraintRoot* _Root;
                CModule* _RootModule;
            };
            for (auto [Root, RootModule] : {
                SCompatConstraint{_Program->_GeneralCompatConstraintRoot.Get(), GeneralCompatConstraintRootModule},
                SCompatConstraint{_Program->_PersistenceCompatConstraintRoot.Get(), PersistenceCompatConstraintRootModule},
                SCompatConstraint{_Program->_PersistenceSoftCompatConstraintRoot.Get(), PersistenceSoftCompatConstraintRootModule}})
            {
                if (!RootModule)
                {
                    continue;
                }
                VisitStampType VisitStamp = CScope::GenerateNewVisitStamp();
                // Ignore scopes between the package root modules and the common ancestor.
                CScope* CommonRootModule = LowestCommonAncestorByName(*RootModule, *SourceRootModule);
                if (CommonRootModule != RootModule)
                {
                    for (CScope* Scope = RootModule->GetParentScope(); Scope != CommonRootModule; Scope = Scope->GetParentScope())
                    {
                        CLogicalScope* LogicalScope = Scope->AsLogicalScopeNullable();
                        if (!LogicalScope)
                        {
                            continue;
                        }
                        (void)LogicalScope->TryMarkVisited(VisitStamp);
                    }
                }
                // Link starting at the package root modules.  This allows for
                // the packages to be named unequal values and have unequal
                // paths from the program or compat constraint root (required
                // due to the possiblity of changing Verse path and UEFN and
                // cook Verse paths not being equal).
                RootModule->SetConstrainedDefinition(*SourceRootModule);
                LinkScopeCompatConstraints(*SourceRootModule, *RootModule, VisitStamp);
                // Link starting at the program roots to handle references to
                // symbols outside the package root modules.  This is required
                // to properly handle package dependencies - i.e., pointers to
                // anything from the root module of the package out of the
                // package.
                LinkScopeCompatConstraints(*_Program, *Root, VisitStamp);
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeProject(CAstProject& AstProject)
    {
        // Analyze all the project's compilation units.
        for (const TSRef<CAstCompilationUnit>& CompilationUnit : AstProject.OrderedCompilationUnits())
        {
            AnalyzeCompilationUnit(*CompilationUnit, AstProject);
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeCompilationUnit(CAstCompilationUnit& CompilationUnit, CAstProject& AstProject)
    {
        // Analyze all the compilation unit's packages.
        for (CAstPackage* Package : CompilationUnit.Packages())
        {
            AnalyzePackage(*Package);

            // Make all packages implicitly dependent on the built-in packages
            if (!_BuiltInPackageNames.Contains(Package->_Name))
            {
                for (const CUTF8String& BuiltInPackageName : _BuiltInPackageNames)
                {
                    const CAstPackage* BuiltInPackage = AstProject.FindPackageByName(BuiltInPackageName);
                    if (BuiltInPackage)
                    {
                        Package->_Dependencies.AddUnique(BuiltInPackage);
                    }
                }
            }
        }

        // Make sure all packages have the same role
        ULANG_ASSERT(!CompilationUnit.Packages().IsEmpty()); // The toolchain enforces this so it must always hold
        EPackageRole Role = CompilationUnit.Packages()[0]->_Role;
        for (int32_t Index = 1; Index < CompilationUnit.Packages().Num(); ++Index)
        {
            if (CompilationUnit.Packages()[Index]->_Role != Role)
            {
                AppendGlitch(
                    *CompilationUnit.Packages()[0],
                    EDiagnostic::ErrSemantic_PackageRoleMismatch,
                    CUTF8String(
                        "The packages `%s` and `%s` mutually depend on each other but have different package roles (%s and %s).",
                        *CompilationUnit.Packages()[0]->_Name,
                        *CompilationUnit.Packages()[Index]->_Name,
                        ToString(CompilationUnit.Packages()[0]->_Role),
                        ToString(CompilationUnit.Packages()[Index]->_Role)));
            }

            if (CompilationUnit.Packages().Num() >= 2 && (CompilationUnit.Packages()[0]->_bAllowNative || CompilationUnit.Packages()[Index]->_bAllowNative))
            {
                AppendGlitch(
                    *CompilationUnit.Packages()[0],
                    EDiagnostic::ErrSemantic_NativePackageDependencyCycle,
                    CUTF8String(
                        "VNI packages must not participate in dependency cycles (the packages `%s` and `%s` mutually depend on each other).",
                        *CompilationUnit.Packages()[0]->_Name,
                        *CompilationUnit.Packages()[Index]->_Name));
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzePackage(CAstPackage& AstPackage)
    {
        TGuardValue<CAstPackage*> PackageContextGuard(_Context._Package, &AstPackage);

        // Analyze the package's root path.
        const SPathAnalysis PathAnalysis = AnalyzePath(AstPackage._VersePath, AstPackage);
        if (PathAnalysis._Disposition != SPathAnalysis::EDisposition::Valid)
        {
            return;
        }

        // Find or create this package's root module
        AstPackage._RootModule = FindOrCreateModuleByPath(PathAnalysis, AstPackage);
        if (!AstPackage._RootModule)
        {
            return;
        }

        // Reserve space in used packages array
        if (_OutPackageUsage)
        {
            AstPackage._UsedDependencies.Reserve(AstPackage._Dependencies.Num());
        }

        // Analyze all the package's members.
        for (const TSRef<CExpressionBase>& Member : AstPackage.Members())
        {
            if (Member->GetNodeType() == EAstNodeType::Definition_Module)
            {
                CExprModuleDefinition& AstModule = static_cast<CExprModuleDefinition&>(*Member);
                AnalyzeFileModule(AstModule, AstPackage, AstPackage._RootModule);
            }
            else if (Member->GetNodeType() == EAstNodeType::Context_Snippet)
            {
                AnalyzeSnippet(static_cast<CExprSnippet&>(*Member), AstPackage, AstPackage._RootModule);
            }
            else
            {
                ULANG_ERRORF("Toolchain must ensure that a package only ever contains modules or snippets.");
            }
        }

        if (AstPackage._VerseScope == EVerseScope::PublicUser)
        {
            // After everything has been analyzed, validate that packages with User scope don't contain
            // any non-module definitions with an Epic-internal path. Note that this is a sanity check
            // to prevent Epic authors from erroneously marking their modules as "User" rather than a
            // security check on packages that actually come from users.
            EnqueueDeferredTask(Deferred_FinalValidation, [this, &AstPackage]
            {
                AstPackage._RootModule->GetModule()->IterateRecurseLogicalScopes([&](const CLogicalScope& LogicalScope)
                {
                    if (LogicalScope.GetKind() != CScope::EKind::Module
                        && LogicalScope.GetPackage() == &AstPackage
                        && LogicalScope.ScopeAsDefinition()
                        && LogicalScope.ScopeAsDefinition()->GetAstNode()
                        && LogicalScope.IsAuthoredByEpic())
                    {
                        AppendGlitch(
                            *LogicalScope.ScopeAsDefinition()->GetAstNode(),
                            EDiagnostic::ErrSemantic_UserPackageNotAllowedWithEpicPath,
                            CUTF8String(
                                "This package has a VerseScope of User, and so is not allowed to "
                                "contain the definition with the Epic-internal path %s.",
                                LogicalScope.GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString()));
                        return EVisitResult::Stop;
                    }

                    return EVisitResult::Continue;
                });
            });
        }
    }

    //-------------------------------------------------------------------------------------------------
    // Process a file module
    void AnalyzeFileModule(CExprModuleDefinition& AstModule, CAstPackage& AstPackage, CScope* ParentScope)
    {
        // In the metaverse, modules can be defined/referenced in several packages,
        // so we always expect them to potentially already exist
        CLogicalScope& LogicalScope = ParentScope->GetLogicalScope();
        CSymbol ModuleName = VerifyAddSymbol(AstModule, AstModule._Name);
        CModule* Module = LogicalScope.FindFirstDefinitionOfKind<CModule>(ModuleName, EMemberOrigin::Original);
        if (Module)
        {
            // Check for duplicate definition
            if (Module->HasParts() && AstPackage._VerseScope == EVerseScope::PublicUser)
            {
                AppendGlitch(AstModule, EDiagnostic::ErrSemantic_AmbiguousDefinition, CUTF8String("Duplicate explicit module definition (module has been previously defined as `%s`).", GetQualifiedNameString(*Module).AsCString()));
                return;
            }

            if (!Module->GetAstNode())
            {
                Module->SetAstNode(&AstModule);
            }
        }
        else
        {
            // Create a new semantic module corresponding to this AST node.
            Module = &ParentScope->CreateModule(VerifyAddSymbol(AstModule, AstModule._Name));
            Module->SetAstNode(&AstModule);
            Module->SetAstPackage(&AstPackage);
            // Emulate legacy behavior of vmodule files
            if (AstModule._bLegacyPublic)
            {
                Module->SetAccessLevel({SAccessLevel::EKind::Public});
            }
        }
        // Point semantic module to current module definition
        CModulePart* Part = &Module->CreatePart(ParentScope, false);
        Part->SetAstNode(&AstModule);
        Part->SetAstPackage(&AstPackage);
        AstModule._SemanticModule = Part;

        RequireUnambiguousDefinition(*Module, "file module");

        // Note: No attributes are processed or validated here since this an implicit module definition

        TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, AstModule._SemanticModule);

        // Analyze all the module's members.
        for (int32_t MemberIndex = 0; MemberIndex < AstModule.Members().Num(); ++MemberIndex)
        {
            const TSRef<CExpressionBase>& Member = AstModule.Members()[MemberIndex];
            if (Member->GetNodeType() == EAstNodeType::Definition_Module)
            {
                AnalyzeFileModule(static_cast<CExprModuleDefinition&>(*Member), AstPackage, AstModule._SemanticModule);
            }
            else if (Member->GetNodeType() == EAstNodeType::Context_Snippet)
            {
                AnalyzeSnippet(static_cast<CExprSnippet&>(*Member), AstPackage, AstModule._SemanticModule);
            }
            else
            {
                if (TSPtr<CExpressionBase> NewMember = AnalyzeDefinitionExpression(Member, SExprCtx::Default()))
                {
                    AstModule.SetMember(Move(NewMember.AsRef()), MemberIndex);
                }
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeSnippet(CExprSnippet& AstSnippet, CAstPackage& /*AstPackage*/, CScope* ParentScope)
    {
        // Create the semantic snippet corresponding to this AST node.
        TOptional<CSymbol> OptionalSnippet = _Program->GetSymbols()->Add(AstSnippet._Path);
        if (!OptionalSnippet.IsSet())
        {
            AppendGlitch(AstSnippet, EDiagnostic::ErrSemantic_TooLongIdentifier);
            CUTF8String TruncatedPath = AstSnippet._Path;
            TruncatedPath.Resize(CSymbolTable::MaxSymbolLength - 1);
            OptionalSnippet = _Program->GetSymbols()->AddChecked(TruncatedPath);
        }
        AstSnippet._SemanticSnippet = &_Program->GetOrCreateSnippet(OptionalSnippet.GetValue(), ParentScope);

        // Process top-level scope context for this module - the "global" definitions
        // Defer analysis until after remainder of modules have been added
        EnqueueDeferredTask(Deferred_Module, [this, &AstSnippet]()
        {
            AnalyzeMemberDefinitions(AstSnippet._SemanticSnippet, AstSnippet, SExprCtx::Default().With(EffectSets::ModuleDefault));
        });

        // Only consider top-level definitions defined in source packages as part of the statistics, since we don't really want
        // to compare against things that aren't directly user-written.
        // Also don't bother counting `epic_internal` definitions as part of the statistics since it's definitely not
        // from user code either.
        if (AstSnippet._SemanticSnippet && !AstSnippet._SemanticSnippet->IsAuthoredByEpic())
        {
            if (const CAstPackage* Package = AstSnippet._SemanticSnippet->GetPackage(); Package && Package->_VerseScope == EVerseScope::PublicUser)
            {
                _Diagnostics->AppendTopLevelDefinition(AstSnippet.Members().Num());
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    bool DesugarVstTopLevel(const Vst::Project& VstProject)
    {
        // Desugar the VST project to an AST project, and return whether any glitches were produced.
        const int32_t OriginalNumGlitches = _Diagnostics->GetGlitchNum();
        _Program->_AstProject = DesugarVstToAst(VstProject, *_Program->GetSymbols(), *_Diagnostics);
        return OriginalNumGlitches == _Diagnostics->GetGlitchNum();
    }

    //-------------------------------------------------------------------------------------------------
    bool AnalyzeAstTopLevel()
    {
        // Analyze the AST project, and return whether any glitches were produced.
        const int32_t OriginalNumGlitches = _Diagnostics->GetGlitchNum();
        AnalyzeProject(*_Program->_AstProject);
        return OriginalNumGlitches == _Diagnostics->GetGlitchNum();
    }

    //-------------------------------------------------------------------------------------------------
    bool ProcessStage(const EDeferredPri MaxPri)
    {
        const int32_t OriginalNumGlitches = _Diagnostics->GetGlitchNum();

        // Process all tasks with priorities up to MaxPri in ascending priority order.
        const EDeferredPri MinPri = Deferred_Module;
        EDeferredPri Pri = MinPri;
        while(Pri <= MaxPri)
        {
            TGuardValue<EDeferredPri> CurrentPriGuard(_CurrentTaskPhase, Pri);
            if (SDeferredTask* Task = _DeferredTasks[Pri]._Head)
            {
                _DeferredTasks[Pri]._Head = Task->NextTask; // Set it before invoking task as task can modify it
                if (!Task->NextTask)
                {
                    _DeferredTasks[Pri]._Tail = nullptr;
                }
                TGuardValue<SContext> ContextGuard(_Context, Task->_Context);
                Task->Run();
                DeleteDeferredTask(Task);
            }
            else
            {
                Pri = EDeferredPri(size_t(Pri) +  1);
            }
        };

        // If equal, means that no new glitches have been generated
        return OriginalNumGlitches == _Diagnostics->GetGlitchNum();
    }

    //-------------------------------------------------------------------------------------------------
    // Gather AST package usage statistics if requested
    void ProcessPackageUsage()
    {
        if (_OutPackageUsage && _Program->_AstProject)
        {
            _OutPackageUsage->_Packages.Reset();
            _OutPackageUsage->_Packages.Reserve(_Program->_AstProject->GetNumPackages());
            for (const CAstCompilationUnit* CompilationUnit : _Program->_AstProject->OrderedCompilationUnits())
            {
                for (const CAstPackage* Package : CompilationUnit->Packages())
                {
                    SPackageUsageEntry& Entry = _OutPackageUsage->_Packages.Emplace_GetRef();
                    Entry._PackageName = Package->_Name;
                    Entry._UsedDependencies.Reserve(Package->_UsedDependencies.Num());
                    for (const CAstPackage* UsedDependency : Package->_UsedDependencies)
                    {
                        Entry._UsedDependencies.Add(UsedDependency->_Name);
                    }
                }
            }
        }
    }

private:

    EDeferredPri _CurrentTaskPhase = Deferred__Invalid;

    //-------------------------------------------------------------------------------------------------
    /// Common Macro Forms
    enum class ESimpleMacroForm : uint8_t
    {
        ///  m0 - has no clauses; only possible via reserved keywords like 'break' and 'return'
        m0 = 0x1 << 0,
        ///  m1 - macro calls of the form 'MacroName{}'
        m1 = 0x1 << 1,
        ///  m2 - of the form 'MacroName(){}'
        m2 = 0x1 << 2,
        ///  both m1 and m2 forms are supported; e.g. 'class(Object){..}' and 'class{..}'
        m1m2 = m1 | m2
    };

    //-------------------------------------------------------------------------------------------------
    template<ESimpleMacroForm A, ESimpleMacroForm B>
    bool IsFormAllowed()
    {
        return (static_cast<uint8_t>(A) & static_cast<uint8_t>(B)) != 0;
    }

    //-------------------------------------------------------------------------------------------------
    template<ESimpleMacroForm AllowedForms, EMacroClauseTag AllowedTags=EMacroClauseTag::None>
    bool ValidateMacroForm(CExprMacroCall& MacroCallAst)
    {
        int32_t ClauseNum = MacroCallAst.Clauses().Num();

        // Check that the right number of clauses is present
        if (AllowedForms == ESimpleMacroForm::m0)
        {
            // For these 0-clause special macros (break, return, yield, and continue), the parser
            // will produce a single empty macro clause.
            if (ClauseNum != 1)
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_TooManyMacroClauses);
                return false;
            }
            else if (MacroCallAst.Clauses()[0].Tag() != EMacroClauseTag::None)
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSyntax_UnexpectedClauseTag, CUTF8String("Unexpected clause tag for macro."));
                return false;
            }
            else if (MacroCallAst.Clauses()[0].Exprs().Num())
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_TooManyMacroClauses);
                return false;
            }
        }
        else if (ClauseNum > 2)
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_TooManyMacroClauses);
            return false;
        }
        else if (ClauseNum == 2 && !IsFormAllowed<AllowedForms,ESimpleMacroForm::m2>())
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_TooManyMacroClauses);
            return false;
        }
        else if (ClauseNum == 1 && !IsFormAllowed<AllowedForms, ESimpleMacroForm::m1>())
        {
            AppendGlitch(
                MacroCallAst,
                IsFormAllowed<AllowedForms,
                ESimpleMacroForm::m0>()
                    ? EDiagnostic::ErrSemantic_TooManyMacroClauses
                    : EDiagnostic::ErrSemantic_NotEnoughMacroClauses);
            return false;
        }

        // Check that clause tags are valid for simple forms (of, none)
        if (ClauseNum == 0)
        {
            return true;
        }

        EMacroClauseTag Tag0 = MacroCallAst.Clauses()[0].Tag();

        if (ClauseNum == 1 && HasAnyTags(Tag0, AllowedTags))
        {
            return true;
        }
        else if (
            ClauseNum == 2
            && ((Tag0 == EMacroClauseTag::None) || (Tag0 == EMacroClauseTag::Of))
            && HasAnyTags(MacroCallAst.Clauses()[1].Tag(), AllowedTags))
        {
            return true;
        }

        AppendGlitch(MacroCallAst, EDiagnostic::ErrSyntax_UnexpectedClauseTag, CUTF8String("Unexpected clause tag for macro."));
        return false;
    }

    //-------------------------------------------------------------------------------------------------
    // Check that a member/parameter of a native class, struct or function signature is native if it is a struct
    // Note: This must run during the Deferred_ValidateAttributes phase or later
    enum class EValidateTypeIsNativeContext { Parameter, Member };
    void ValidateTypeIsNative(const CTypeBase* Type, EValidateTypeIsNativeContext Context, const CExpressionBase& DefineeAst)
    {
        ValidateTypeIsNative(Type->GetNormalType(), Context, DefineeAst);
    }
    void ValidateTypeIsNative(const CNormalType& Type, EValidateTypeIsNativeContext Context, const CExpressionBase& DefineeAst)
    {
        if (const CClass* DataClass = Type.AsNullable<CClass>())
        {
            if (!DataClass->IsNative())
            {
                if (Context == EValidateTypeIsNativeContext::Parameter)
                {
                    AppendGlitch(DefineeAst, EDiagnostic::ErrSemantic_NonNativeStructInNativeFunction, CUTF8String("`struct/class %s` used as a parameter/result in a native function must also be native.", DataClass->Definition()->AsNameCString()));
                }
                else if (Context == EValidateTypeIsNativeContext::Member && DataClass->IsStruct())
                {
                    AppendGlitch(DefineeAst, EDiagnostic::ErrSemantic_NonNativeStructInNativeClass, CUTF8String("`struct %s` contained as a member in a native type must also be native.", DataClass->Definition()->AsNameCString()));
                }
            }
        }
        else if (const CTupleType* TupleType = Type.AsNullable<CTupleType>())
        {
            // If it is a tuple it auto infers that it needs to be native
            // - so it must ensure that all its elements are also capable of being native
            for (const CTypeBase* ElemType : TupleType->GetElements())
            {
                ValidateTypeIsNative(ElemType, Context, DefineeAst);
            }
        }
    };

    //-------------------------------------------------------------------------------------------------
    // Process member definitions of a class/module/snippet
    void AnalyzeMemberDefinitions(CScope* Scope, CMemberDefinitions& Definitions, SExprCtx ExprCtx)
    {
        TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Scope);
        for (int32_t MemberIndex = 0; MemberIndex < Definitions.Members().Num(); ++MemberIndex)
        {
            TSRef<CExpressionBase> Member = Definitions.Members()[MemberIndex];
            if (TSPtr<CExpressionBase> NewMember = AnalyzeDefinitionExpression(Member, ExprCtx.WithResultIsUsed(nullptr)))
            {
                Definitions.SetMember(Move(NewMember.AsRef()), MemberIndex);
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    enum class EQualifierAction { Set, Compare };
    void ProcessQualifier(CScope* Scope, CDefinition* Definition, const TSPtr<CExpressionBase> Qualifier, CExpressionBase* DefinitionAst, const SExprCtx& ExprCtx, EQualifierAction WhatToDo = EQualifierAction::Set)
    {
        if (Qualifier || WhatToDo == EQualifierAction::Compare)
        {
            EnqueueDeferredTask(Deferred_Type, [this, Scope, Definition, Qualifier, DefinitionAst, ExprCtx, WhatToDo]()
                {
                    TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Scope);
                    const SQualifier QualifierType = AnalyzeQualifier(Qualifier, *DefinitionAst, ExprCtx);
                    switch (WhatToDo)
                    {
                    case EQualifierAction::Set: 
                        Definition->_Qualifier = QualifierType;
                        break;

                    case EQualifierAction::Compare:
                        if (Definition->_Qualifier._Definition != QualifierType._Definition)
                        {
                            AppendGlitch(*DefinitionAst, EDiagnostic::ErrSemantic_MismatchedPartialQualifier);
                        }
                        break;

                    default:
                        ULANG_UNREACHABLE();
                        break;
                    }
                });
        }
    }

    //-------------------------------------------------------------------------------------------------
    const CTypeBase* DefinitionAsType(const CDefinition& Definition)
    {
        switch(Definition.GetKind())
        {
        case CDefinition::EKind::Class: return &Definition.AsChecked<CClassDefinition>();
        case CDefinition::EKind::Enumeration: return &Definition.AsChecked<CEnumeration>();
        case CDefinition::EKind::Interface: return &Definition.AsChecked<CInterface>();
        case CDefinition::EKind::TypeAlias: return Definition.AsChecked<CTypeAlias>().GetType();
        case CDefinition::EKind::TypeVariable: return &Definition.AsChecked<CTypeVariable>();

        case CDefinition::EKind::Data:
        case CDefinition::EKind::Enumerator:
        case CDefinition::EKind::Function:
        case CDefinition::EKind::Module:
        case CDefinition::EKind::ModuleAlias:
        default:
            return nullptr;
        };
    }

    //-------------------------------------------------------------------------------------------------
    const CFunctionType* DefinitionAsFunctionOfType(const CDefinition& Definition)
    {
        if (const CFunction* Function = Definition.AsNullable<CFunction>())
        {
            return Function->_Signature.GetFunctionType();
        }
        else if (const CDataDefinition* DataDefinition = Definition.AsNullable<CDataDefinition>())
        {
            if (const CTypeBase* DataDefinitionType = DataDefinition->GetType())
            {
                return DataDefinitionType->GetNormalType().AsNullable<CFunctionType>();
            }
        }
        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    bool IsDefinitionInExternalPackage(const CDefinition& Definition)
    {
        if (const CModule* Module = Definition.AsNullable<CModule>())
        {
            // If the definition is a module, only consider it external if all parts are in external packages.
            for (const CModulePart* Part : Module->GetParts())
            {
                if (Part->GetPackage()->_Role != ExternalPackageRole)
                {
                    return false;
                }
            }
            return true;
        }
        else
        {
            return Definition._EnclosingScope.GetPackage()->_Role == ExternalPackageRole;
        }
    }

    //-------------------------------------------------------------------------------------------------
    bool IsDefinitionInTreatModulesAsImplicitPackage(const CDefinition& Definition)
    {
        if (const CModule* Module = Definition.AsNullable<CModule>())
        {
            // If the definition is a module, consider it sufficient if any part is in a _bTreatModulesAsImplicit package.
            for (const CModulePart* Part : Module->GetParts())
            {
                if (Part->GetPackage()->_bTreatModulesAsImplicit)
                {
                    return true;
                }
            }
            return false;
        }
        else
        {
            return Definition._EnclosingScope.GetPackage()->_bTreatModulesAsImplicit;
        }
    }

    //-------------------------------------------------------------------------------------------------
    void CollectConflictingDefinitions(
        TArrayG<const CDefinition*, TInlineElementAllocator<4>>& ConflictingDefinitions,
        const CDefinition& Definition,
        const CSymbol& Name,
        const CTypeBase* ParamType)
    {
        // NOTE: (yiliang.siew) If it is explicitly (i.e. `(local:)Identifier`) qualified, then we just need to make sure
        // that no shadowing occurs within the function's body and enclosed scopes themselves.
        SResolvedDefinitionArray ResolvedDefns = Definition._EnclosingScope.ResolveDefinition(Name, Definition._Qualifier, Definition._EnclosingScope.GetPackage());
        // Spare ourselves the iteration if there are no conflicts
        if (ResolvedDefns.IsEmpty() || (ResolvedDefns.Num() == 1 && ResolvedDefns[0]._Definition == &Definition))
        {
            return;
        }

        // Iterate over all the definitions with the same name visible from the definition's enclosing scope.
        const CAstPackage* Package = Definition._EnclosingScope.GetPackage();
        const CModule* DefinitionModule = Definition.AsNullable<CModule>();
        const bool bIsImplicitModuleDefinition = DefinitionModule && !DefinitionModule->IsExplicitDefinition();
        const bool bDefinitionIsInExternalPackage = IsDefinitionInExternalPackage(Definition);
        const bool bDefinitionIsInTreatModulesAsImplicitPackage = IsDefinitionInTreatModulesAsImplicitPackage(Definition);

        for (const SResolvedDefinition& ExistingResolvedDefn : ResolvedDefns)
        {
            // Don't report a conflict with the definition itself.
            if (ExistingResolvedDefn._Definition == &Definition)
            {
                continue;
            }

            if (&ExistingResolvedDefn._Definition->_EnclosingScope.GetLogicalScope() != &Definition._EnclosingScope.GetLogicalScope())
            {
                // Don't report conflicts with definitions in different scopes if this definition is in an external package.
                if (bDefinitionIsInExternalPackage)
                {
                    continue;
                }

                // Allow implicit module definitions to shadow anything except definitions in the same enclosing scope
                if (bIsImplicitModuleDefinition)
                {
                    continue;
                }

                // Hack for asset manifests: In packages marked TreatModulesAsImplicit, allow any definitions in different scopes to shadow each other 
                if (bDefinitionIsInTreatModulesAsImplicitPackage)
                {
                    continue;
                }

                // Allow two definitions that override the same base definition as long as they're in different scopes.
                if (&ExistingResolvedDefn._Definition->GetBaseOverriddenDefinition() == &Definition.GetBaseOverriddenDefinition())
                {
                    continue;
                }
            }

            // Allow a definition in a scope that can't access the other definition.
            if (!ExistingResolvedDefn._Definition->IsAccessibleFrom(Definition._EnclosingScope))
            {
                continue;
            }

            // Allow a conflicting definition if it cannot be seen from the original definition's package
            // TODO: Make this work with modules (currently you cannot tell the package of a module definition because it's the parts that are defined in packages)
            if (Package && !Package->CanSeeDefinition(*ExistingResolvedDefn._Definition))
            {
                continue;
            }

            // Allow two definitions that are valid overloads.
            const CFunctionType* FunctionOfType2 = DefinitionAsFunctionOfType(*ExistingResolvedDefn._Definition);
            if (ParamType && FunctionOfType2 && SemanticTypeUtils::AreDomainsDistinct(ParamType, &FunctionOfType2->GetParamsType()))
            {
                continue;
            }

            ConflictingDefinitions.Add(ExistingResolvedDefn._Definition);
        }
    }

    //-------------------------------------------------------------------------------------------------
    void RequireUnambiguousDefinition(const CDefinition& Definition, const char* AssertionContext)
    {
        const Vst::Node* ContextVstNode = _Context._VstNode;

        EnqueueDeferredTask(Deferred_FinalValidation, [this, &Definition, AssertionContext, ContextVstNode]
        {
            ULANG_ASSERTF(Definition.GetAstNode(), "Expected definition to have valid AST mapping (%s @ ~%s)", AssertionContext, SGlitchLocus(ContextVstNode).AsFormattedString().AsCString());
            ULANG_ASSERTF(Definition.GetAstNode()->GetMappedVstNode(), "Expected valid VST node for error reporting (%s @ ~%s)", AssertionContext, SGlitchLocus(ContextVstNode).AsFormattedString().AsCString());

            TArrayG<const CDefinition*, TInlineElementAllocator<4>> ConflictingDefinitions;

            {
                const CFunctionType* FunctionOfType1 = DefinitionAsFunctionOfType(Definition);
                const CTypeBase* ParamType = FunctionOfType1 ? &FunctionOfType1->GetParamsType() : nullptr;
                CollectConflictingDefinitions(ConflictingDefinitions, Definition, Definition.GetName(), ParamType);
            }

            // If this is inside a class/interface and it's a function then check against extension methods
            if (Definition._EnclosingScope.GetKind() == CScope::EKind::Class || Definition._EnclosingScope.GetKind() == CScope::EKind::Interface)
            {
                if (const CFunction* Function = Definition.AsNullable<CFunction>())
                {
                    if (const CFunctionType* FunctionOfType = DefinitionAsFunctionOfType(*Function))
                    {
                        // Check that there are no conflicting extension methods
                        // Need name of possible extension method ...
                        CUTF8String ExtensionName(_Program->_IntrinsicSymbols.MakeExtensionFieldOpName(Function->GetName()));
                        const TOptional<CSymbol> ExtensionSymbol = _Program->GetSymbols()->Add(ExtensionName);
                        if (ExtensionSymbol.IsSet()) // Ignore if it's to long
                        {
                            // ... and its parameter type.
                            CTupleType::ElementArray ExtensionParamTypes;
                            ExtensionParamTypes.Add(Function->GetParentScope()->ScopeAsType());
                            ExtensionParamTypes.Add(&FunctionOfType->GetParamsType());
                            const CTypeBase* ExtensionParamType = CFunctionType::GetOrCreateParamType(*_Program, Move(ExtensionParamTypes));
                            CollectConflictingDefinitions(ConflictingDefinitions, Definition, ExtensionSymbol.GetValue(), ExtensionParamType);
                        }
                    }
                }
            }

            if (ConflictingDefinitions.Num() == 1)
            {
                const CDefinition& ConflictingDefinition = *ConflictingDefinitions[0];

                // If there are two definitions in the same scope that conflict with each other, only report
                // the conflict for the second definition.
                if (&Definition._EnclosingScope.GetLogicalScope() == &ConflictingDefinition._EnclosingScope.GetLogicalScope()
                    && Definition._ParentScopeOrdinal < ConflictingDefinition._ParentScopeOrdinal)
                {
                    return;
                }

                if (const CDataDefinition* DataDefinition = Definition.AsNullable<CDataDefinition>())
                {
                    if (const CDataDefinition* ConflictingDataDefinition = ConflictingDefinition.AsNullable<CDataDefinition>())
                    {
                        // If this conflicting data definition looks like it was meant to be a set of a previously defined var,
                        // suggest that change in the error.
                        if (!DataDefinition->IsVar()
                            && !DataDefinition->GetAstNode()->ValueDomain().IsValid()
                            && DataDefinition->GetAstNode()->Value().IsValid()
                            && ConflictingDataDefinition->IsVar()
                            && DataDefinition->_EnclosingScope.IsControlScope())
                        {
                            AppendGlitch(
                                *Definition.GetAstNode(),
                                EDiagnostic::ErrSemantic_AmbiguousDefinitionDidYouMeanToSet,
                                CUTF8String(
                                    "The %s is ambiguous with the %s. Did you mean to write 'set %s = ...'?",
                                    DescribeAmbiguousDefinition(Definition).AsCString(),
                                    DescribeAmbiguousDefinition(*ConflictingDataDefinition).AsCString(),
                                    Definition.AsNameCString()));
                            return;
                        }
                    }
                }
            }

            if (ConflictingDefinitions.IsFilled())
            {
                AppendGlitch(
                    *Definition.GetAstNode(),
                    EDiagnostic::ErrSemantic_AmbiguousDefinition,
                    CUTF8String(
                        "The %s is ambiguous with %s:%s",
                        DescribeAmbiguousDefinition(Definition).AsCString(),
                        ConflictingDefinitions.Num() == 1 ? "this definition" : "these definitions",
                        FormatConflictList(ConflictingDefinitions).AsCString()));
            }
        });
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Module(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx, const SExprArgs& ExprArgs)
    {
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        // Require that the MacroCall occurs directly as the Value subexpression of a Definition node.
        if (!ExprArgs.MacroCallDefinitionContext)
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_NominalTypeInAnonymousContext);
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        // Only allow module at snippet or parent module scope.
        if (!_Context._Scope->IsModuleOrSnippet())
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_Unimplemented, "Modules must be defined at snippet or module scope.");
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        // Does this definition have any attributes?
        const bool bHasAttributes = ExprArgs.MacroCallDefinitionContext->_NameAttributes.Num() || ExprArgs.MacroCallDefinitionContext->_DefAttributes.Num();

        // Are we supposed to treat this definition as implicit?
        const CAstPackage* Package = _Context._Scope->GetPackage();
        const bool bPackageTreatsModulesAsImplicit = Package && Package->_bTreatModulesAsImplicit;
        const bool bTreatAsImplicit = !bHasAttributes && bPackageTreatsModulesAsImplicit;

        // Check if a module with this name already exist in this scope
        const CSymbol ModuleName = ExprArgs.MacroCallDefinitionContext->_Name;
        CModule* Module = _Context._Scope->GetLogicalScope().FindFirstDefinitionOfKind<CModule>(ModuleName, EMemberOrigin::Original);
        bool bOtherExplicitDefinitionExists = false;
        if (Module)
        {
            bOtherExplicitDefinitionExists = Module->GetAstNode() && Module->GetAstNode()->_SemanticModule->IsExplicitDefinition();
            if (bOtherExplicitDefinitionExists && Package && Package->_VerseScope == EVerseScope::PublicUser)
            {
                // We allow partial module definitions if either one is in a package with the _bTreatModulesAsImplicit attribute
                const bool bAllowThisDefinition = bPackageTreatsModulesAsImplicit;
                const bool bAllowOtherDefinition = Module->GetAstNode() && Module->GetAstNode()->_SemanticModule->GetPackage()->_bTreatModulesAsImplicit;
                if (!bAllowThisDefinition && !bAllowOtherDefinition)
                {
                    AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_AmbiguousDefinition, CUTF8String("Duplicate explicit module definition (module has been previously defined as `%s`).", GetQualifiedNameString(*Module).AsCString()));
                    return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
                }
            }
        }
        else
        {
            // Create a new semantic module
            Module = &_Context._Scope->CreateModule(ModuleName);
            Module->SetAstPackage(_Context._Scope->GetPackage());
        }

        // Point semantic module to current module definition
        CModulePart* Part = &Module->CreatePart(_Context._Scope, !bTreatAsImplicit);
        Part->SetAstPackage(_Context._Scope->GetPackage());

        // Create the module definition AST node.
        CExprMacroCall::CClause& MembersClause = MacroCallAst.Clauses()[MacroCallAst.Clauses().Num() - 1];
        TSRef<CExprModuleDefinition> DefinitionAst = TSRef<CExprModuleDefinition>::New(
            *Part,
            Move(MembersClause.Exprs())
        );
        DefinitionAst->SetResultType(&_Program->_voidType);
        // Make sure that the module's AST node points to the first _explicit_ definition
        if (!bOtherExplicitDefinitionExists)
        {
            Module->SetAstNode(DefinitionAst.Get());
        }

        // Queue up jobs that process the attributes
        TArray<SAttribute> NameAttributes = Move(ExprArgs.MacroCallDefinitionContext->_NameAttributes);
        TArray<SAttribute> DefAttributes = Move(ExprArgs.MacroCallDefinitionContext->_DefAttributes);

        if (!bOtherExplicitDefinitionExists)
        {
            // Gather attributes
            EnqueueDeferredTask(Deferred_Attributes, [this, Module, Part, NameAttributes, DefAttributes, DefinitionAst]()
                {
                    TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Part->GetParentScope());
                    Module->_Attributes = AnalyzeNameAndDefAttributes(NameAttributes, DefAttributes, CAttributable::EAttributableScope::Module);
                    Module->SetAccessLevel(GetAccessLevelFromAttributes(*DefinitionAst->GetMappedVstNode(), *Module));
                    ValidateExperimentalAttribute(*Module);
                });
        }
        else
        {
            // Validate attributes (access level only)
            EnqueueDeferredTask(Deferred_ValidateAttributes, [this, Module, Part, NameAttributes, DefAttributes, DefinitionAst]()
                {
                    TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Part->GetParentScope());
                    CAttributable Attributes{ AnalyzeNameAndDefAttributes(NameAttributes, DefAttributes, CAttributable::EAttributableScope::Module) };
                    TOptional<SAccessLevel> AccessLevel = GetAccessLevelFromAttributes(*DefinitionAst->GetMappedVstNode(), Attributes);
                    if (Module->SelfAccessLevel() != AccessLevel)
                    {
                        // Generate a glitch per conflicting definition
                        AppendGlitch(*Module->GetAstNode(), EDiagnostic::ErrSemantic_MismatchedPartialAttributes);
                        AppendGlitch(*Part->GetAstNode(), EDiagnostic::ErrSemantic_MismatchedPartialAttributes);
                    }
                });
        }

        // Analyze the members of this module.
        AnalyzeMemberDefinitions(Part, *DefinitionAst, SExprCtx::Default().With(EffectSets::ModuleDefault));

        RequireUnambiguousDefinition(*Module, "module macro");
        ProcessQualifier(Part->GetParentScope(), Module, ExprArgs.MacroCallDefinitionContext->_Qualifier, DefinitionAst, ExprCtx, bOtherExplicitDefinitionExists ? EQualifierAction::Compare : EQualifierAction::Set);
        return ReplaceMapping(MacroCallAst, Move(DefinitionAst));
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Class(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx, const SExprArgs& ExprArgs, EStructOrClass StructOrClass)
    {
        if (!ValidateMacroForm<ESimpleMacroForm::m1m2>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        
        if (!ExprArgs.MacroCallDefinitionContext)
        {
            // Require that the MacroCall occurs directly as the Value subexpression of a Definition node.
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_NominalTypeInAnonymousContext);
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        // For now, only allow class definitions at module scope.
        if (_Context._Self || (_Context._Function && !_Context._Function->GetParentScope()->IsModuleOrSnippet()))
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_Unimplemented, "Class definitions are not yet implemented outside of a module scope.");
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        // Create the class definition.
        CClassDefinition* Class = &_Context._Scope->CreateClass(ExprArgs.MacroCallDefinitionContext->_Name, nullptr, {}, StructOrClass);

        TGuardValue<TArray<const CDefinition*>> DefinitionsGuard(_Context._EnclosingDefinitions);
        _Context._EnclosingDefinitions.Add(Class);

        TArray<SAttribute> NameAttributes = Move(ExprArgs.MacroCallDefinitionContext->_NameAttributes);
        TArray<SAttribute> DefAttributes = Move(ExprArgs.MacroCallDefinitionContext->_DefAttributes);

        // Determine the class's effects.
        Class->_ConstructorEffects = GetEffectsFromAttributes(*MacroCallAst.Name(), EffectSets::ClassDefault);

        // Don't allow any class to have the converges effect.
        if (!Class->_ConstructorEffects[EEffect::diverges])
        {
            AppendGlitch(*MacroCallAst.Name(), EDiagnostic::ErrSemantic_InvalidEffectDeclaration, "The 'converges' effect is only allowed on native definitions.");
        }

        // Don't allow any class to have more effects than transacts.
        RequireEffects(*MacroCallAst.Name(), Class->_ConstructorEffects, EffectSets::Transacts, "class's effect declaration", "Verse");
        
        CExprMacroCall::CClause* SuperTypesClause = MacroCallAst.Clauses().Num() == 1 ? nullptr : &MacroCallAst.Clauses()[0];

        // Create the class definition AST node.
        CExprMacroCall::CClause& MembersClause = MacroCallAst.Clauses()[MacroCallAst.Clauses().Num() - 1];
        TSRef<CExprClassDefinition> DefinitionAst = TSRef<CExprClassDefinition>::New(
            *Class,
            SuperTypesClause ? Move(SuperTypesClause->Exprs()) : TArray<TSRef<CExpressionBase>>{},
            Move(MembersClause.Exprs()));
        MacroCallAst.GetMappedVstNode()->AddMapping(DefinitionAst.Get());

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Queue up jobs that process any class attributes
        Class->_Definition->_EffectAttributable._Attributes = Move(MacroCallAst.Name()->_Attributes);
        const bool bIsParametric = ExprArgs.MacroCallDefinitionContext->_bIsParametric;
        EnqueueDeferredTask(Deferred_AttributeClassAttributes, [this, Class, StructOrClass, NameAttributes, DefAttributes, &MacroCallAst, bIsParametric]
        {
            // Not inside the function yet
            const bool bIsAttributeClass = Class->IsClass(*_Program->_attributeClass);

            const CAttributable::EAttributableScope AttributedExprScope =
                bIsAttributeClass
                ? CAttributable::EAttributableScope::AttributeClass
                : StructOrClass == EStructOrClass::Class
                ? CAttributable::EAttributableScope::Class
                : CAttributable::EAttributableScope::Struct;

            auto ProcessAttributes = [this, Class, StructOrClass, AttributedExprScope, NameAttributes, DefAttributes, bIsParametric]()
            {
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Class->GetParentScope());
                Class->_Attributes = AnalyzeNameAndDefAttributes(NameAttributes, DefAttributes, AttributedExprScope);
                AnalyzeAttributes(
                    Class->_Definition->_EffectAttributable._Attributes,
                    AttributedExprScope,
                    StructOrClass == EStructOrClass::Class ? EAttributeSource::ClassEffect : EAttributeSource::StructEffect);
                if (bIsParametric)
                {
                    // Set parametric classes as public, which will be combined with the access
                    // level of the outer function.
                    ULANG_ASSERTF(!Class->_Attributes.Num(), "Expected parametric classes to be missing attributes");
                    Class->SetAccessLevel(TOptional<SAccessLevel>{SAccessLevel::EKind::Public}); // This could possibly be done through a different default accessibility on the scope?
                }
                else
                {
                    Class->SetAccessLevel(GetAccessLevelFromAttributes(*Class->GetAstNode()->GetMappedVstNode(), *Class));
                }
                ValidateExperimentalAttribute(*Class);
                Class->_ConstructorAccessLevel = GetAccessLevelFromAttributes(*Class->GetAstNode()->GetMappedVstNode(), Class->_EffectAttributable);
                if (Class->DerivedConstructorAccessLevel()._Kind == SAccessLevel::EKind::Private)
                {
                    AppendGlitch(
                        *Class->GetAstNode(),
                        EDiagnostic::ErrSemantic_InvalidAccessLevel,
                        CUTF8String("`private` access level not allowed on `class` or `struct` (would make it impossible to create)."));
                } 
                if (Class->DerivedConstructorAccessLevel()._Kind == SAccessLevel::EKind::Protected)
                {
                    AppendGlitch(
                        *Class->GetAstNode(),
                        EDiagnostic::ErrSemantic_InvalidAccessLevel,
                        CUTF8String(
                            AttributedExprScope == CAttributable::EAttributableScope::Struct
                            ? "`protected` access level not allowed on `struct`."
                            : "`protected` access level not allowed on `class` (use `abstract` instead)."));
                }
            };
        
            if (bIsAttributeClass)
            {
                // Process attributes on attribute classes right away, before processing other attributes.
                ProcessAttributes();
                        
                if (Class->HasAttributeClass(_Program->_attributeScopeName, *_Program)
                    && Class->HasAttributeClass(_Program->_attributeScopeEffect, *_Program))
                {
                    AppendGlitch(
                        MacroCallAst,
                        EDiagnostic::ErrSemantic_ConflictingAttributeScope,
                        CUTF8String("`attribscope_name` can't be mixed with `attribscope_effect`."));
                }
            }
            else
            {
                // Defer processing of attributes on non-attribute classes
                EnqueueDeferredTask(Deferred_Attributes, ProcessAttributes);
            }
        
        
            // The class attributes are further validated based on any superclass - see Deferred_ValidateAttributes below.
            // If further class attribute validation is needed regardless of whether a class has a superclass then move
            // the Deferred_ValidateAttributes task above to this location.
        });

        // Analyze the class definition.
        AnalyzeClass(Class, DefinitionAst, ExprCtx, StructOrClass);

        ProcessQualifier(Class->GetParentScope(), Class->_Definition, ExprArgs.MacroCallDefinitionContext->_Qualifier, DefinitionAst, ExprCtx);

        // Require that the class doesn't shadow any other definitions.
        RequireUnambiguousDefinition(*Class, "class");

        return Move(DefinitionAst);
    }

    // Inheriting from an abstract base type that resides in different module is not allowed because changing the base-type can break
    //  the derived type out in the wild. However, we allow it in the case where both base and derived types are in epic_internal modules
    //  because we're taking responsibility for revision locking the two modules together; avoiding the problem.
    void ValidateConcreteClassAbstractProperAncestors(const CClass& Class, const CAstNode& AstNode)
    {
        const CModule* ClassModule = Class.Definition()->_EnclosingScope.GetModule();
        for (const CClass* I = Class._Superclass; I && !I->IsConcrete(); I = I->_Superclass)
        {
            if (I->IsAbstract() 
                && I->Definition()->_EnclosingScope.GetModule() != ClassModule
                && (!I->Definition()->_EnclosingScope.GetModule()->IsAuthoredByEpic() || !ClassModule->IsAuthoredByEpic()))
            {
                AppendGlitch(
                    AstNode,
                    EDiagnostic::ErrSemantic_AbstractConcreteClass,
                    CUTF8String(
                        "`concrete` classes must not inherit from `abstract` classes of other modules.  `concrete` class `%s` inherits from `abstract` class `%s`.",
                        Class.Definition()->AsNameCString(),
                        I->Definition()->AsNameCString()));
            }
        }
    }

    void ValidateConcreteClassDataMemberValues(const CClass& Class, const CAstNode& AstNode)
    {
        // Do this late or we miss definitions like _False in this example (taken from SOL-5983).
        //   False:false = _False:false

        enum EDeferredPri DeferredTo =
            VerseFN::UploadedAtFNVersion::StricterCheckForDefaultInConcreteClasses(_Context._Package->_UploadedAtFNVersion)
            ? Deferred_FinalValidation
            : Deferred_ValidateAttributes;  // This is too early, _False is not visible yet

        EnqueueDeferredTask(DeferredTo, [this, &Class, &AstNode]
        {
            struct SOverridingDataMember
            {
                const CDataDefinition* BaseOverriddenDataMember;
                bool HasValue;
            };

            TArray<SOverridingDataMember> OverridingDataMembers;
            for (const CClass* I = &Class; I; I = I->_Superclass)
            {
                for (const CDataDefinition* DataMember : I->GetDefinitionsOfKind<CDataDefinition>())
                {
                    OverridingDataMembers.Add({
                        &DataMember->GetBaseOverriddenDefinition(),
                        DataMember->GetAstNode()->Value() });
                }
            }

            uLang::Algo::Sort(OverridingDataMembers, [](const SOverridingDataMember& Left, const SOverridingDataMember& Right)
                {
                    if (Left.BaseOverriddenDataMember < Right.BaseOverriddenDataMember)
                    {
                        return true;
                    }
                    if (Right.BaseOverriddenDataMember < Left.BaseOverriddenDataMember)
                    {
                        return false;
                    }
                    // Data definitions with initializers should appear earlier,
                    // allowing all later data definitions to be skipped.
                    return Right.HasValue < Left.HasValue;
                });

            const CDataDefinition* PrevBaseOverriddenDataMember = nullptr;
            for (auto [BaseOverriddenDataMember, HasValue] : OverridingDataMembers)
            {
                if (BaseOverriddenDataMember == PrevBaseOverriddenDataMember)
                {
                    continue;
                }
                if (!HasValue)
                {
                    const CClass& Superclass = static_cast<const CClass&>(BaseOverriddenDataMember->_EnclosingScope);
                    const CClass* ConcreteSuperclass = Superclass.FindConcreteBase();
                    const CClass* InitialConcreteClass = Class.FindInitialConcreteBase();

                    // The Initializer class is the first class where this data member requires initialization
                    //  If the Superclass is already concrete, then it is also the Initializer class
                    //  If not, then initializer class is the first concrete class that is derived from the Superclass
                    const CClass* InitializerClass = (ConcreteSuperclass != nullptr) ? &Superclass : InitialConcreteClass;

                    // If the Class isn't the same as the Initializer class, then we'll report nothing
                    //  and assume the appropriate base class will report the missing initializer.
                    //  This is to avoid an error pile-up
                    if (&Class == InitializerClass)
                    {
                        CUTF8String Message;
                        if (&Class == &Superclass)
                        {
                            Message = CUTF8String(
                                "Data member `%s` of class `%s` must have an initializer. (Reason: %sclass '%s' is `concrete`)",
                                BaseOverriddenDataMember->AsNameCString(),
                                Superclass.Definition()->AsNameCString(),
                                InitializerClass != InitialConcreteClass ? "inherited " : "",
                                InitialConcreteClass->Definition()->AsNameCString());
                        }
                        else
                        {
                            Message = CUTF8String(
                                "Data member `%s` of class `%s` must have an initializer in class '%s'. (Reason: %sclass '%s' is `concrete`)",
                                BaseOverriddenDataMember->AsNameCString(),
                                Superclass.Definition()->AsNameCString(),
                                InitializerClass->Definition()->AsNameCString(),
                                InitializerClass != InitialConcreteClass ? "inherited " : "",
                                InitialConcreteClass->Definition()->AsNameCString());
                        }
                        AppendGlitch(
                            AstNode,
                            EDiagnostic::ErrSemantic_ConcreteClassDataMemberLacksValue,
                            Move(Message));
                    }
                }
                PrevBaseOverriddenDataMember = BaseOverriddenDataMember;
            }
        });
    }


    void ValidatePersistableClass(const CClass& Class, const CAstNode& AstNode)
    {
        if (Class._Superclass)
        {
            AppendGlitch(
                AstNode,
                EDiagnostic::ErrSemantic_NotPersistableSuperclass,
                CUTF8String(
                    "`persistable` class `%s` must not have a superclass.",
                    Class.Definition()->AsNameCString()));
        }
        if (Class.GetParentScope()->GetKind() == CScope::EKind::Function)
        {
            AppendGlitch(
                AstNode,
                EDiagnostic::ErrSemantic_Unimplemented,
                "`persistable` parametric classes are not supported.");
        }
        if (!Class._Definition->_EffectAttributable.HasAttributeClass(_Program->_finalClass, *_Program))
        {
            AppendGlitch(
                AstNode,
                EDiagnostic::ErrSemantic_PersistableClassMustBeFinal);
        }
        if (Class._Definition->_EffectAttributable.HasAttributeClass(_Program->_uniqueClass, *_Program))
        {
            AppendGlitch(
                AstNode,
                EDiagnostic::ErrSemantic_PersistableClassMustNotBeUnique);
        }
    }

    void ValidatePersistableDataMemberType(const CClass& Class, const CDataDefinition& DataMember)
    {
        if (!Constrain(DataMember.GetType(), &_Program->_persistableType))
        {
            AppendGlitch(
                *DataMember.GetAstNode(),
                EDiagnostic::ErrSemantic_PersistableClassDataMemberNotPersistable,
                CUTF8String(
                    "Data member `%s` of `persistable` %s `%s` must be `persistable`.  "
                    "`persistable` types include primitive types; array, map, and option types "
                    "made up of `persistable` types; `class`es defined as `class<persistable>`; "
                    "and `struct`s defined as `struct<persistable>`.",
                    DataMember.AsNameCString(),
                    Class.IsStruct() ? "struct" : "class",
                    Class.Definition()->AsNameCString()));
        }
        DataMember.MarkPersistenceCompatConstraint();
    }

    void ValidatePersistableClassDataMemberTypes(const CClass& Class)
    {
        for (const CDataDefinition* DataMember : Class.GetDefinitionsOfKind<CDataDefinition>())
        {
            ValidatePersistableDataMemberType(Class, *DataMember);
        }
    }

    void AddSuperType(CClass& Class, const CTypeBase* NegativeSuperType, const CTypeBase* PositiveSuperType, const CAstNode& AstNode)
    {
        // Don't allow inheriting from an attribute class.
        const Vst::Node* VstNode = AstNode.GetMappedVstNode();
        if (!Class.IsAuthoredByEpic())
        {
            ValidateNonAttributeType(NegativeSuperType, VstNode);
            ValidateNonAttributeType(PositiveSuperType, VstNode);
        }

        const CNormalType& NegativeSuperNormalType = NegativeSuperType->GetNormalType();
        const CNormalType& PositiveSuperNormalType = PositiveSuperType->GetNormalType();
        if (const CInterface* SuperInterface = SemanticTypeUtils::AsSingleInterface(NegativeSuperNormalType, PositiveSuperNormalType))
        {
            Class._SuperInterfaces.Add(const_cast<CInterface*>(SuperInterface));
            Class._NegativeClass->_SuperInterfaces.Add(SuperInterface->_NegativeInterface);
        }
        else if (const CClass* SuperClass = SemanticTypeUtils::AsSingleClass(NegativeSuperNormalType, PositiveSuperNormalType))
        {
            // scoped definitions are attributes which are technically classes under the covers, but you're not allowed to derive from them
            EnqueueDeferredTask(Deferred_ValidateType, [this, SuperClass, VstNode]
            {
                if (SuperClass->IsSubclassOf(*_Program->_scopedClass))
                {
                    AppendGlitch(VstNode, EDiagnostic::ErrSemantic_ExpectedInterfaceOrClass);
                }
            });

            if (Class._Superclass)
            {
                AppendGlitch(AstNode, EDiagnostic::ErrSemantic_MultipleSuperClasses);
            }
            Class.SetSuperclass(const_cast<CClass*>(SuperClass));
            Class._NegativeClass->SetSuperclass(SuperClass->_NegativeClass);
        }
        else
        {
            AppendGlitch(AstNode, EDiagnostic::ErrSemantic_ExpectedInterfaceOrClass);
        }
    }

	void ValidateClassUniqueAttribute(CClass* Class, const TSRef<CExprClassDefinition>& DefinitionAst)
	{
		// If the <unique> attribute is present, then it requires at least <allocates>
		if (Class->IsUnique())
		{
            if (_Context._Package->_EffectiveVerseVersion >= Verse::Version::UniqueAttributeRequiresAllocatesEffect)
            {
                RequireEffects(*DefinitionAst, EffectSets::Allocates, Class->_ConstructorEffects, "<unique> specifier", "the class's declared constructor effects");
            }
            else
            {
                RequireEffects(*DefinitionAst, EffectSets::Allocates, Class->_ConstructorEffects, "<unique> specifier in a future version of the Verse language", "the class's declared constructor effects", EDiagnostic::WarnSemantic_DeprecatedUniqueWithoutAllocates);
            }
		}
	}

    //-------------------------------------------------------------------------------------------------
    // insert "OnChange" hook for each member variable of anything that
    // subclasses /Verse.org/ParameterCollections/parameter_collection
    bool IsParameterCollectionSubclass(const CClass* Class)
    {
        for (const CClass* Super = Class->_Superclass; Super; Super = Super->_Superclass)
        {
            const CUTF8String P = Super->GetScopePath('/', CScope::EPathMode::PrefixSeparator);
            if (P == "/Verse.org/ParameterCollections/parameter_collection")
            {
                return true;
            }
        }

        return false;
    }

    TSPtr<CExprInvocation> MakeParameterCollectionOnChangeFuncInvocation(const CUTF8String& VarName, TSPtr<CExpressionBase> CalleeContext)
    {
        auto Callee = TSRef<CExprIdentifierUnresolved>::New(_Program->GetSymbols()->AddChecked("OnPropertyChangedFromVerse"));
        Callee->_bAllowUnrestrictedAccess = true;
        Callee->SetContext(CalleeContext);
        auto Arg = TSRef<CExprString>::New(VarName);
        return TSPtr<CExprInvocation>::New(CExprInvocation::EBracketingStyle::Parentheses, Move(Callee), Move(Arg));
    }

    bool IsParameterCollectionClassVar(const CDataDefinition& Data)
    {
        return Data.IsVar() && !Data.IsNative();
    }

    void TrySynthesizeParameterCollectionVarOnChangeHooks(CClass* Class,
                                                          const TSRef<CExprClassDefinition>& ClassDefinition,
                                                          const SExprCtx& ExprCtx,
                                                          const EStructOrClass StructOrClass)
    {
        if (IsDefinitionInExternalPackage(*Class->_Definition))
        {
            return;
        }

        ULANG_ENSURE(StructOrClass == EStructOrClass::Class);
        ULANG_ENSURE(_CurrentTaskPhase >= Deferred_ValidateAttributes);

        if (!IsParameterCollectionSubclass(Class))
        {
            return;
        }

        TArray<TSPtr<CExpressionBase>> SynthesizedMembers;
        for (TSRef<CExpressionBase> ClassMember : ClassDefinition->Members())
        {
            auto DataDef = AsNullable<CExprDataDefinition>(ClassMember);
            if (!DataDef || !IsParameterCollectionClassVar(*DataDef->_DataMember))
            {
                continue;
            }

            auto&& AddVstMapping = [DataDef](auto AstNode) {
                if (const Vst::Node* VstNode = DataDef->GetMappedVstNode())
                {
                    AstNode->SetNonReciprocalMappedVstNode(VstNode);
                    VstNode->AddMapping(AstNode);
                }
                return AstNode;
            };

            const CUTF8String VarName = DataDef->_DataMember->GetName().AsString();

            TSPtr<CExpressionBase> FuncBody = MakeParameterCollectionOnChangeFuncInvocation(VarName, nullptr);

            // we prefix the OnChanged function name with "___ParameterCollection_" to avoid naming collisions with potential
            // user-defined functions
            const CSymbol OnChangedFuncName{_Program->GetSymbols()->AddChecked(CUTF8String{"___ParameterCollection_On_"} + VarName + "_Changed")};
            TSRef<CExpressionBase> FuncDef = AddVstMapping(
                TSRef<CExprDefinition>::New(
                    AddVstMapping(
                        TSPtr<CExprInvocation>::New(
                            CExprInvocation::EBracketingStyle::Parentheses,
                            TSRef<CExprIdentifierUnresolved>::New(OnChangedFuncName),
                            TSRef<CExprMakeTuple>::New()
                        )
                    ),
                    TSPtr<CExprIdentifierUnresolved>::New(_Program->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Void))),
                    Move(FuncBody)
                )
            );

            {
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Class);
                FuncDef = AnalyzeDefinitionExpression(FuncDef, ExprCtx.WithResultIsUsed(nullptr));
                ULANG_ASSERT(FuncDef && FuncDef->GetNodeType() == EAstNodeType::Definition_Function);
                static_cast<CExprFunctionDefinition&>(*FuncDef)._Function->SetAccessLevel({SAccessLevel::EKind::EpicInternal});
                SynthesizedMembers.Add(FuncDef);
            }
        }

        for (TSPtr<CExpressionBase> Member : SynthesizedMembers)
        {
            ClassDefinition->AppendMember(Move(Member.AsRef()));
        }
    }

    //-------------------------------------------------------------------------------------------------
    // Process a class
    void AnalyzeClass(CClass* Class, const TSRef<CExprClassDefinition>& DefinitionAst, const SExprCtx& ExprCtx, EStructOrClass StructOrClass)
    {
        if (_Context._Scope->GetKind() == CScope::EKind::Function)
        {
            for (const CTypeVariable* TypeVariable : static_cast<const CFunction*>(_Context._Scope)->GetDefinitionsOfKind<CTypeVariable>())
            {
                // @see AnalyzeParam(TSRef<CExprDefinition>, SParamsInfo&)
                Class->_TypeVariableSubstitutions.Emplace(TypeVariable, TypeVariable, TypeVariable);
            }
        }

        // Analyze the members of this class.
        {
            ULANG_ASSERTF(!_Context._Self, "Unexpected nested class");
            TGuardValue<const CTypeBase*> CurrentClassGuard(_Context._Self, Class);
            AnalyzeMemberDefinitions(Class, *DefinitionAst, ExprCtx.With(Class->_ConstructorEffects));
            EnqueueDeferredTask(Deferred_Type, [Class]
            {
                SetNegativeClassMemberDefinitionTypes(*Class);
            });
        }

        // Analyze various parts of classes and structs
        if (StructOrClass == EStructOrClass::Class)
        {
            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Validate number and kinds of super types
            EnqueueDeferredTask(Deferred_Type, [this, DefinitionAst, Class, ExprCtx]()
            {
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Class->GetParentScope());

                // Process the super types.
                for (int32_t SuperTypeIndex = 0; SuperTypeIndex < DefinitionAst->SuperTypes().Num(); ++SuperTypeIndex)
                {
                    TSRef<CExpressionBase> SuperTypeAst = DefinitionAst->SuperTypes()[SuperTypeIndex];

                    // Analyze the super type expression.
                    if (TSPtr<CExpressionBase> NewSuperTypeAst = AnalyzeExpressionAst(SuperTypeAst, ExprCtx.WithResultIsUsedAsType()))
                    {
                        SuperTypeAst = Move(NewSuperTypeAst.AsRef());
                        DefinitionAst->SetSuperType(TSRef<CExpressionBase>(SuperTypeAst), SuperTypeIndex);
                    }

                    // Interpret each super type clause child node as a type.
                    STypeTypes SuperTypes = GetTypeTypes(*SuperTypeAst);
                    if (SuperTypes._Tag == ETypeTypeTag::Type)
                    {
                        AddSuperType(*Class, SuperTypes._NegativeType, SuperTypes._PositiveType, *SuperTypeAst);
                    }
                }
            });

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // We cannot reliably infer anything from the inheritance structure, as all classes
            // are not connected here yet (we're in the process of doing that). So, we need
            // another pass.
            EnqueueDeferredTask(Deferred_ValidateCycles, [this, DefinitionAst, Class]()
            {
                // Validate that there is no cycle in the inheritance hierarchy
                VisitStampType VisitStamp = CScope::GenerateNewVisitStamp();
                for (CClass* AncestorClass = Class->_Superclass;
                    AncestorClass;
                    AncestorClass = AncestorClass->_Superclass)
                {
                    if (!AncestorClass->TryMarkVisited(VisitStamp))
                    {
                        AppendGlitch(
                            AncestorClass->_Definition->GetAstNode() && AncestorClass->_Definition->GetAstNode()->GetMappedVstNode()
                                ? *AncestorClass->_Definition->GetAstNode()
                                : *DefinitionAst,
                            EDiagnostic::ErrSemantic_InterfaceOrClassInheritsFromItself);
                        AncestorClass->_Superclass = nullptr;
                        AncestorClass->_NegativeClass->_Superclass = nullptr;
                        break;
                    }
                }
                Class->_bHasCyclesBroken = true;
                Class->_NegativeClass->_bHasCyclesBroken = true;

                TArray<CInterface*> RedundantInterfaces;
                Class->_AllInheritedInterfaces = GetAllInheritedInterfaces(Class, RedundantInterfaces);
                // Validate that the class doesn't redundantly inherit any interfaces.
                for (CInterface* RedundantInterface : RedundantInterfaces)
                {
                    AppendGlitch(
                        *DefinitionAst,
                        EDiagnostic::ErrSemantic_RedundantInterfaceInheritance,
                        CUTF8String("Class `%s` redundantly inherits from interface `%s` (or '%s' is part of a cycle).", Class->Definition()->AsNameCString(), RedundantInterface->Definition()->AsNameCString(), RedundantInterface->Definition()->AsNameCString()));

                }
            });
            
            EnqueueDeferredTask(Deferred_ValidateAttributes, [this, DefinitionAst, Class, ExprCtx, StructOrClass]
            {
                //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                // Ensure no inherited data members are shadowed.
                // This must be done after the class hierarchy and data members are in place, and attributes are analyzed.
                for (const CDataDefinition* DataMember : Class->GetDefinitionsOfKind<CDataDefinition>())
                {
                    SQualifier DataDefinitionQualifier = SQualifier::Unknown(); // TODO: should pass in DataDefinition->_Qualifier.
                    SmallDefinitionArray OverriddenMembers = Class->FindInstanceMember(DataMember->GetName(), EMemberOrigin::Inherited, DataDefinitionQualifier, Class->GetPackage());

                    if (OverriddenMembers.Num() == 0)
                    {
                        // if we are not overriding a member, we shouldn't have the <override> attribute on ourselves
                        if (DataMember->HasAttributeClass(_Program->_overrideClass, *_Program))
                        {
                            AppendGlitch(
                                *DataMember->GetAstNode(),
                                EDiagnostic::ErrSemantic_IncorrectOverride,
                                CUTF8String(
                                    "Instance data member `%s` is marked with the <override> specifier, but it doesn't override anything",
                                    GetQualifiedNameString(*DataMember).AsCString()));
                        }
                    }
                    else
                    {
                        const CDefinition* OverriddenMember = OverriddenMembers[0];

                        // glitch if the data member doesn't have the <override> attribute
                        if (!DataMember->HasAttributeClass(_Program->_overrideClass, *_Program))
                        {
                            AppendGlitch(
                                *DataMember->GetAstNode(),
                                EDiagnostic::ErrSemantic_AmbiguousDefinition,
                                CUTF8String(
                                    "Instance data member `%s` is already defined in `%s`, did you mean to add the <override> specifier?",
                                    GetQualifiedNameString(*DataMember).AsCString(),
                                    OverriddenMember->_EnclosingScope.GetScopeName().AsCString()));
                        }

                        // glitch if the data member is overriding a data member that has custom accessors
                        if (auto Overridden = OverriddenMember->AsNullable<CDataDefinition>();
                            Overridden && Overridden->_OptionalAccessors)
                        {
                            AppendGlitch(
                                *DataMember->GetAstNode(),
                                EDiagnostic::ErrSemantic_IncorrectOverride,
                                CUTF8String("Data member `%s` cannot be overridden because it has the <getter(...)> and <setter(...)> attributes.",
                                            GetQualifiedNameString(*DataMember).AsCString())
                            );
                        }
                    }

                    // HACK The attribute class can only be inherited by classes that have attributes, e.g., <@attribscope_function>
                    if (Class != _Program->_attributeClass && SemanticTypeUtils::IsAttributeType(Class) && Class->_Definition->_Attributes.IsEmpty())
                    {
                        AppendGlitch(
                            *DefinitionAst,
                            EDiagnostic::ErrSemantic_Unimplemented,
                            CUTF8String("Only classes with attributes can inherit from '%s'", _Program->_attributeClass->GetScopeName().AsCString()));
                    }
                }

                bool bClassIsConcrete = Class->IsConcrete();

                if (Class->IsAbstract())
                {
                    if (bClassIsConcrete)
                    {
                        AppendGlitch(
                            *DefinitionAst,
                            EDiagnostic::ErrSemantic_AbstractConcreteClass,
                            CUTF8String(
                                "`concrete` classes must not be `abstract.  `concrete` class %s is `abstract`.",
                                Class->Definition()->AsNameCString()));
                    }
                }
                else
                {
                    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                    // Validate that the class implements all the functions
                    // inherited from interfaces. The interface function bodies
                    // aren't processed until the Code phase, so this needs to
                    // happen after that.
                    for (const CFunction* Function : Class->GetDefinitionsOfKind<CFunction>())
                    {
                        if (!Function->HasImplementation())
                        {
                            AppendGlitch(
                                *Function->GetAstNode(),
                                EDiagnostic::ErrSemantic_AbstractFunctionInNonAbstractClass,
                                CUTF8String("Non-abstract class cannot declare abstract function `%s`.", Function->AsNameCString()));
                        }
                    }
                    Class->ForEachAncestorClassOrInterface([this, Class, &DefinitionAst] (CLogicalScope* ClassScope, CClass* Superclass, CInterface* Interface) {
                        // Check that the class implements all the interface functions and abstract functions it inherits.
                        for (const CFunction* AbstractFunction : ClassScope->GetDefinitionsOfKind<CFunction>())
                        {
                            AbstractFunction = AbstractFunction->GetPrototypeDefinition();
                            if (AbstractFunction->HasImplementation())
                            {
                                continue;
                            }
                            // Check if implementation is required but absent
                            const SmallDefinitionArray Definitions = Class->FindInstanceMember(AbstractFunction->GetName(), EMemberOrigin::InheritedOrOriginal, AbstractFunction->_Qualifier, Class->GetPackage());
                            bool bHasFunctionImpl = false;
                            for (const CDefinition* Definition : Definitions)
                            {
                                if (const CFunction* Function = Definition->AsNullable<CFunction>())
                                {
                                    Function = Function->GetPrototypeDefinition();
                                    if (Function->GetBaseOverriddenDefinition().GetPrototypeDefinition() == AbstractFunction->GetBaseOverriddenDefinition().GetPrototypeDefinition())
                                    {
                                        bHasFunctionImpl = Function->HasImplementation();
                                        break;
                                    }
                                }
                            }
                            if (!bHasFunctionImpl)
                            {
                                AppendGlitch(
                                    *DefinitionAst,
                                    EDiagnostic::ErrSemantic_AbstractFunctionInNonAbstractClass,
                                    CUTF8String(
                                        "Non-abstract class inherits abstract function `%s` from `%s` but does not provide an implementation.",
                                        AbstractFunction->AsNameCString(),
                                        Interface ? Interface->Definition()->AsNameCString() : Superclass->Definition()->AsNameCString()));
                            }
                        }
                    });
                }

                if (bClassIsConcrete)
                {
                    ValidateConcreteClassAbstractProperAncestors(*Class, *DefinitionAst);
                    ValidateConcreteClassDataMemberValues(*Class, *DefinitionAst);
                }

                bool bClassIsPersistable = Class->IsPersistable();

                if (bClassIsPersistable)
                {
                    if (const CModule* ParentModule = Class->GetModule())
                    {
                        ParentModule->MarkPersistenceCompatConstraint();
                    }
                    ValidatePersistableClass(*Class, *DefinitionAst);
                    ValidatePersistableClassDataMemberTypes(*Class);
                }
                
                for (const CInterface* SuperInterface : Class->_SuperInterfaces)
                {
                    RequireConstructorAccessible(DefinitionAst->GetMappedVstNode(), *Class, *SuperInterface);
                }

                const CClass* SuperClass = Class->_Superclass;
                if (SuperClass)
                {
                    // Require that the super class constructor is accessible from this class.
                    RequireConstructorAccessible(DefinitionAst->GetMappedVstNode(), *Class, *SuperClass->_Definition);

                    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                    // Validate class based attributes that are dependent on superclass
                            
                    if (SuperClass->_Definition->_EffectAttributable.HasAttributeClass(_Program->_finalClass, *_Program))
                    {
                        // Tried to use [final] superclass
                        AppendGlitch(
                            *DefinitionAst,
                            EDiagnostic::ErrSemantic_FinalSuperclass,
                            CUTF8String(
                                "Class `%s` cannot be a subclass of the class `%s` which has the `final` attribute.",
                                Class->Definition()->AsNameCString(),
                                SuperClass->Definition()->AsNameCString()));
                    }

                    // Does the class has the <native> attribute?
                    if (Class->IsNative())
                    {
                        // Yes, ensure that the superclass also has the <native> attribute.
                        if (!SuperClass->IsNative())
                        {
                            AppendGlitch(
                                *DefinitionAst,
                                EDiagnostic::ErrSemantic_NonNativeSuperClass,
                                CUTF8String(
                                    "Any superclass of the native class `%s` must also be a native class and the superclass `%s` is non-native.",
                                    Class->Definition()->AsNameCString(),
                                    SuperClass->Definition()->AsNameCString()));
                        }
                    }

                    // The class must have at least the same effects as its super-class.
                    RequireEffects(*DefinitionAst, SuperClass->_ConstructorEffects, Class->_ConstructorEffects, "class's super-class", "this class's effect declaration");
                }

                //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                // Validate routine based attributes that are dependent on superclass

                for (const TSRef<CDefinition>& Definition : Class->GetDefinitions())
                {
                    // Look for overridden final members.
                    const CDefinition* OverriddenDefinition = Definition->GetOverriddenDefinition();
                    if (OverriddenDefinition && OverriddenDefinition->IsFinal())
                    {
                        AppendGlitch(
                            *OverriddenDefinition->GetAstNode(),
                            EDiagnostic::ErrSemantic_CannotOverrideFinalMember,
                            CUTF8String(
                                "Cannot define `%s` because it overrides `%s`, which has the `final` specifier.",
                                GetQualifiedNameString(*Definition).AsCString(),
                                GetQualifiedNameString(*OverriddenDefinition).AsCString()
                            ));
                    }
                }

				ValidateClassUniqueAttribute(Class, DefinitionAst);
                TrySynthesizeParameterCollectionVarOnChangeHooks(Class, DefinitionAst, ExprCtx, StructOrClass);
            });
        }
        else // StructOrClass == EStructOrClass::Struct
        {
            Class->_bHasCyclesBroken = true;
            Class->_NegativeClass->_bHasCyclesBroken = true;

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Structs may not have super types
            if (DefinitionAst->SuperTypes().IsFilled())
            {
                AppendGlitch(*DefinitionAst->SuperTypes()[0], EDiagnostic::ErrSemantic_StructSuperType);
            }

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Structs may not have functions
            for (const CFunction* Function : Class->GetDefinitionsOfKind<CFunction>())
            {
                AppendGlitch(*Function->GetAstNode(), EDiagnostic::ErrSemantic_StructFunction);
            }

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Structs may not have mutable data members
            for (const CDataDefinition* DataDefinition : Class->GetDefinitionsOfKind<CDataDefinition>())
            {
                if (DataDefinition->IsVar())
                {
                    AppendGlitch(*DataDefinition->GetAstNode(), EDiagnostic::ErrSemantic_StructMutable);
                }
            }

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Structs may not contain themselves, either directly or indirectly
            EnqueueDeferredTask(Deferred_NonFunctionExpressions, [this, DefinitionAst, Class]()
                {
                    // Any changes here should probably be reflected in `RecursivelyPopulateStruct()` lambda in `GenerateUClassesForPackage()` in `SolarisClassGenerator.cpp`
                    VisitStampType VisitStamp = CScope::GenerateNewVisitStamp();
                    CUTF8StringBuilder MemberChainStr;
                    const Verse::Vst::Node* TopmostMemberVST = nullptr;  // Topmost member for error location

                    auto ContainsCycle = [VisitStamp, &MemberChainStr, &TopmostMemberVST, Class](const CTypeBase* Type, const auto& ContainsCycleRef)
                    {
                        const CNormalType& NormalType = Type->GetNormalType();
                        if (const CClass* ClassType = NormalType.AsNullable<CClass>())
                        {
                            if (!ClassType->IsStruct())
                            {
                                // Classes are okay since they are references and can break a circular loop
                                return false;
                            }

                            if (!ClassType->TryMarkVisited(VisitStamp))
                            {
                                // If it has been tested or in the middle of testing ignore
                                // - unless it is also the original top-level type then found circular nesting!
                                return ClassType == Class;
                            }

                            for (const CDataDefinition* DataMember : ClassType->GetDefinitionsOfKind<CDataDefinition>())
                            {
                                if (ContainsCycleRef(DataMember->GetType()->GetNormalType().GetInnerType(), ContainsCycleRef))
                                {
                                    CUTF8String PostFix = MemberChainStr.MoveToString();
                                    MemberChainStr.Append('.');
                                    MemberChainStr.Append(DataMember->AsNameStringView());
                                    MemberChainStr.Append(PostFix);
                                    TopmostMemberVST = DataMember->GetAstNode()->GetMappedVstNode();

                                    return true;
                                }
                            }
                        }
                        else if (const CTupleType* TupleType = NormalType.AsNullable<CTupleType>())
                        {
                            // If we have already tested this tuple type before, ignore it.
                            if (!TupleType->TryMarkVisited(VisitStamp))
                            {
                                return false;
                            }

                            int ElemIdx = 0;

                            for (const uLang::CTypeBase* ElementType : TupleType->GetElements())
                            {
                                if (ContainsCycleRef(ElementType->GetNormalType().GetInnerType(), ContainsCycleRef))
                                {
                                    CUTF8String PostFix = MemberChainStr.MoveToString();
                                    MemberChainStr.AppendFormat("(%i)", ElemIdx);
                                    MemberChainStr.Append(PostFix);

                                    return true;
                                }

                                ElemIdx++;
                            }
                        }

                        return false;
                    };

                    if (ContainsCycle(Class, ContainsCycle))
                    {
                        AppendGlitch(
                            TopmostMemberVST ? TopmostMemberVST : DefinitionAst->GetMappedVstNode(),
                            EDiagnostic::ErrSemantic_StructContainsItself,
                            CUTF8String("Structs may not contain themselves - examine member chain `%s`.", MemberChainStr.AsCString()));
                    }
                });

            EnqueueDeferredTask(Deferred_ValidateAttributes, [this, DefinitionAst, Class]
            {
                bool bClassIsConcrete = Class->IsConcrete();
                bool bClassIsPersistable = Class->IsPersistable();

                if (bClassIsPersistable)
                {
                    if (const CModule* ParentModule = Class->GetModule())
                    {
                        ParentModule->MarkPersistenceCompatConstraint();
                    }
                    if (Class->GetParentScope()->GetKind() == CScope::EKind::Function)
                    {
                        AppendGlitch(
                            *DefinitionAst,
                            EDiagnostic::ErrSemantic_Unimplemented,
                            "`persistable` parametric structs are not supported.");
                    }
                }

                for (const CDataDefinition* DataMember : Class->GetDefinitionsOfKind<CDataDefinition>())
                {
                    // Ensure all data members have initializers if the struct is `concrete`.
                    if (bClassIsConcrete && !DataMember->GetAstNode()->Value())
                    {
                        AppendGlitch(
                            *DataMember->GetAstNode(),
                            EDiagnostic::ErrSemantic_ConcreteClassDataMemberLacksValue,
                            CUTF8String(
                                "Data member `%s` of `concrete` struct `%s` lacks an initializer.",
                                DataMember->AsNameCString(),
                                Class->Definition()->AsNameCString()));
                    }
                    if (bClassIsPersistable)
                    {
                        ValidatePersistableDataMemberType(*Class, *DataMember);
                    }
                }
            });
        }

        // Only consider classes defined in source packages as part of the statistics, since we don't really want
        // to compare against things that aren't directly user-controlled such as native class implementations etc.
        // Also don't bother counting `epic_internal` classes as part of the statistics since it's definitely not
        // from user code either.
        if (CAstPackage* Package = Class->GetPackage(); Package && Package->_VerseScope == EVerseScope::PublicUser && !Class->IsAuthoredByEpic())
        {
            _Diagnostics->AppendClassDefinition(1);
        }
    }
    
    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Enum(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx, const SExprArgs& ExprArgs)
    {
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        else
        {
            // Require that the MacroCall occurs directly as the Value subexpression of a Definition node.
            if (!ExprArgs.MacroCallDefinitionContext)
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_NominalTypeInAnonymousContext);
                return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
            }
            const CSymbol EnumName = ExprArgs.MacroCallDefinitionContext->_Name;
            
            // Only allow enums at snippet scope.
            if (!_Context._Scope->IsModuleOrSnippet())
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_Unimplemented, "Enums must be defined at module or snippet scope.");
                return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
            }

            CExprMacroCall::CClause& MacroClause = MacroCallAst.Clauses()[0];

            CEnumeration& Enumeration = _Context._Scope->CreateEnumeration(EnumName);

            TGuardValue<TArray<const CDefinition*>> DefinitionsGuard(_Context._EnclosingDefinitions);
            _Context._EnclosingDefinitions.Add(&Enumeration);

            TArray<TSRef<CExpressionBase>> Members = Move(MacroClause.Exprs());
            for (int32_t EnumIdx = 0; EnumIdx < Members.Num(); EnumIdx += 1)
            {
                TSRef<CExpressionBase>& Member = Members[EnumIdx];
                if (Member->GetNodeType() != EAstNodeType::Identifier_Unresolved)
                {
                    AppendGlitch(*Member, EDiagnostic::ErrSemantic_ExpectedIdentifier);
                }
                else
                {
                    // NOTE: (yiliang.siew) Enumerators can't have qualifiers on them because that would somehow mean the enumerator
                    // is originally defined elsewhere, which is not supported in Verse.
                    const CExprIdentifierUnresolved& MemberUnresolvedIdentifier = static_cast<CExprIdentifierUnresolved&>(*Member);
                    if (MemberUnresolvedIdentifier.Qualifier())
                    {
                        AppendGlitch(*Member, EDiagnostic::ErrSemantic_UnexpectedQualifier, "An enumerator cannot have qualifiers, as it would mean that it was originally defined elsewhere and is unsupported.");
                        continue;
                    }

                    // Create the CEnumerator.
                    CEnumerator& Enumerator = Enumeration.CreateEnumerator(MemberUnresolvedIdentifier._Symbol, EnumIdx);

                    // Replace the CExprIdentifierUnresolved with a CExprEnumLiteral.
                    TSRef<CExprEnumLiteral> EnumLiteralAst = TSRef<CExprEnumLiteral>::New(&Enumerator);
                    Enumerator._Attributes = Member->_Attributes;
                    // Queue up job that analyzes any enumeration attributes.
                    if (Member->HasAttributes())
                    {
                        EnqueueDeferredTask(Deferred_Attributes, [this, &Enumerator]() { AnalyzeAttributes(Enumerator._Attributes, CAttributable::EAttributableScope::Enumerator, EAttributeSource::Definition); });
                    }
                    Enumerator.SetAstNode(EnumLiteralAst.Get());
                    Member = ReplaceMapping(*Member, Move(EnumLiteralAst));
                }
            }
            
            TArray<SAttribute> NameAttributes = Move(ExprArgs.MacroCallDefinitionContext->_NameAttributes);
            TArray<SAttribute> DefAttributes = Move(ExprArgs.MacroCallDefinitionContext->_DefAttributes);
            Enumeration._EffectAttributable._Attributes = Move(MacroCallAst.Name()->_Attributes);

            // Queue up job that processes any enumerator attributes
            EnqueueDeferredTask(Deferred_Attributes, [this, &Enumeration, NameAttributes, DefAttributes]()
            {
                // Not inside the function yet
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &Enumeration._EnclosingScope);
                Enumeration._Attributes = AnalyzeNameAndDefAttributes(NameAttributes, DefAttributes, CAttributable::EAttributableScope::Enum);
                AnalyzeAttributes(Enumeration._EffectAttributable._Attributes, CAttributable::EAttributableScope::Enum, EAttributeSource::EnumEffect);
                Enumeration.SetAccessLevel(GetAccessLevelFromAttributes(*Enumeration.GetAstNode()->GetMappedVstNode(), Enumeration));
                ValidateExperimentalAttribute(Enumeration);
            });
            EnqueueDeferredTask(Deferred_ValidateAttributes, [&Enumeration]
            {
                if (Enumeration.IsPersistable())
                {
                    if (const CModule* ParentModule = Enumeration.GetModule())
                    {
                        ParentModule->MarkPersistenceCompatConstraint();
                    }
                }
            });

            TSRef<CExprEnumDefinition> EnumDefinitionAst = TSRef<CExprEnumDefinition>::New(Enumeration, Move(Members));

            ProcessQualifier(_Context._Scope, &Enumeration, ExprArgs.MacroCallDefinitionContext->_Qualifier, EnumDefinitionAst, ExprCtx);
            RequireUnambiguousDefinition(Enumeration, "enumeration");

            if (VerseFN::UploadedAtFNVersion::EnforceUnambiguousEnumerators(_Context._Package->_UploadedAtFNVersion))			
            {
                for (uLang::TSRef<uLang::CDefinition> Enumerator : Enumeration.GetDefinitions())
                {
                    RequireUnambiguousDefinition(*Enumerator, "enumerator");
                }
            }

            return ReplaceMapping(MacroCallAst, Move(EnumDefinitionAst));
        }
    }

    //-------------------------------------------------------------------------------------------------
    struct SPathAnalysis
    {
        enum class EDisposition
        {
            Valid,
            DoesNotStartWithSlash,
            EmptySegment,
        };
        EDisposition _Disposition;
        TArrayG<CUTF8StringView, TInlineElementAllocator<4>> _Segments;
        char _IllegalCharacter;
        uintptr_t _ErrorOffset;
    };

    SPathAnalysis TryAnalyzePath(CUTF8StringView PathString)
    {
        SPathAnalysis Result;

        CUTF8StringView ResidualPathString = PathString;

        // Paths that come from non-Verse contexts (e.g. vpackage files) might not even start with a slash.
        if (ResidualPathString.FirstByte() != '/')
        {
            Result._Disposition = SPathAnalysis::EDisposition::DoesNotStartWithSlash;
            return Result;
        }

        bool bIsFirstLabel = true;
        while (ResidualPathString.IsFilled())
        {
            ULANG_ASSERTF(ResidualPathString.FirstByte() == '/', "Should not reach here unless the next character is a slash");
            ResidualPathString.PopFirstByte();

            CUTF8StringView Segment = ResidualPathString;
            while (ResidualPathString.IsFilled() && ResidualPathString.FirstByte() != '/')
            {
                const UTF8Char Char = ResidualPathString.PopFirstByte();
                if (bIsFirstLabel && Char == '@')
                {
                    bIsFirstLabel = false;
                }
            }
            Segment = Segment.SubViewTrimEnd(ResidualPathString.ByteLen());

            if (Segment.IsEmpty())
            {
                Result._Disposition = SPathAnalysis::EDisposition::EmptySegment;
                Result._ErrorOffset = ResidualPathString.Data() - PathString.Data();
                return Result;
            }

            Result._Segments.Add(Segment);
            bIsFirstLabel = false;
        }

        Result._Disposition = SPathAnalysis::EDisposition::Valid;
        return Result;
    }

    SPathAnalysis AnalyzePath(CUTF8StringView PathString, const CAstNode& GlitchAst)
    {
        const SPathAnalysis Result = TryAnalyzePath(PathString);
        switch (Result._Disposition)
        {
        case SPathAnalysis::EDisposition::Valid: break;
        case SPathAnalysis::EDisposition::DoesNotStartWithSlash:
            AppendGlitch(GlitchAst, EDiagnostic::ErrSemantic_InvalidScopePath, "Verse path does not start with a '/'");
            break;
        case SPathAnalysis::EDisposition::EmptySegment:
            AppendGlitch(GlitchAst, EDiagnostic::ErrSemantic_InvalidScopePath, "Verse path contains empty segment");
            break;
        default: ULANG_UNREACHABLE();
        }
        return Result;
    }

    //-------------------------------------------------------------------------------------------------
    CLogicalScope* GetRootScope(EPackageRole PackageRole)
    {
        if (PackageRole == EPackageRole::GeneralCompatConstraint)
        {
            return _Program->_GeneralCompatConstraintRoot.Get();
        }
        if (PackageRole == EPackageRole::PersistenceCompatConstraint)
        {
            return _Program->_PersistenceCompatConstraintRoot.Get();
        }
        if (PackageRole == EPackageRole::PersistenceSoftCompatConstraint)
        {
            return _Program->_PersistenceSoftCompatConstraintRoot.Get();
        }
        return _Program.Get();
    }

    //-------------------------------------------------------------------------------------------------
    CLogicalScope* GetRootScope()
    {
        ULANG_ASSERTF(_Context._Package, "GetRootScope must be called in a context with a package.");
        return GetRootScope(_Context._Package->_Role);
    }

    //-------------------------------------------------------------------------------------------------
    bool IsRootScope(const CScope& Scope)
    {
        return &Scope == GetRootScope(GetConstraintPackageRole(Scope.GetPackage()));
    }

    //-------------------------------------------------------------------------------------------------
    const CLogicalScope* ResolvePathToLogicalScope(const CUTF8String& VersePath, const CAstNode& GlitchAst)
    {
        const SPathAnalysis PathAnalysis = AnalyzePath(VersePath, GlitchAst);
        if (PathAnalysis._Disposition != SPathAnalysis::EDisposition::Valid)
        {
            return nullptr;
        }
        const CLogicalScope* CurrentScope = nullptr;
        const CLogicalScope* ParentScope = GetRootScope();
        for (CUTF8StringView Segment : PathAnalysis._Segments)
        {
            const CSymbol ModuleName = VerifyAddSymbol(GlitchAst, Segment);
            SmallDefinitionArray Definitions = ParentScope->FindDefinitions(ModuleName);
            if (Definitions.Num() > 1)
            {
                AppendGlitch(GlitchAst, EDiagnostic::ErrSemantic_InvalidScopePath,
                    CUTF8String("The identifier '%s' is ambiguous in %s; could be:%s",
                        CUTF8String(Segment).AsCString(),
                        ParentScope->GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString(),
                        FormatConflictList(Definitions).AsCString()
                    ));
                return nullptr;
            }
            else if (!Definitions.Num())
            {
                AppendGlitch(GlitchAst, EDiagnostic::ErrSemantic_InvalidScopePath,
                    CUTF8String("Can't resolve '%s' in %s",
                        CUTF8String(Segment).AsCString(),
                        ParentScope->GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString()
                    ));
                return nullptr;
            }
            else
            {
                // Definitions.Num() == 1
                CDefinition* Definition = Definitions[0];
                CurrentScope = Definition->DefinitionAsLogicalScopeNullable();
                if (!CurrentScope)
                {
                    AppendGlitch(GlitchAst, EDiagnostic::ErrSemantic_InvalidScopePath,
                        CUTF8String("The identifier '%s' in %s refers to a %s, but a logical scope was expected.",
                            CUTF8String(Segment).AsCString(),
                            ParentScope->GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString(),
                            DefinitionKindAsCString(Definition->GetKind())
                        ));
                    return nullptr;
                }
            }
            ParentScope = CurrentScope;
        }
        return CurrentScope;
    }

    //-------------------------------------------------------------------------------------------------
    const CModule* ResolvePathToModule(const CUTF8String& VersePath, const CAstNode& GlitchAst)
    {
        const CLogicalScope* LogicalScope = ResolvePathToLogicalScope(VersePath, GlitchAst);
        if (LogicalScope)
        {
            if (LogicalScope->GetKind() == CScope::EKind::Module)
            {
                return static_cast<const CModule*>(LogicalScope);
            }
            AppendGlitch(GlitchAst, EDiagnostic::ErrSemantic_InvalidScopePath,
                CUTF8String("The path '%s' refers to a %s, but a module was expected.",
                    VersePath.AsCString(),
                    CScope::KindToCString(LogicalScope->GetKind())
                ));
        }
        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    CModulePart* FindOrCreateModuleByPath(const SPathAnalysis& PathAnalysis, CAstPackage& Package)
    {
        CModule* Module = nullptr;
        CModulePart* ModulePart = nullptr;
        CLogicalScope* ParentLogicalScope = GetRootScope();
        CScope* ParentScope = ParentLogicalScope;
        for (CUTF8StringView Segment : PathAnalysis._Segments)
        {
            const CSymbol ModuleName = _Program->GetSymbols()->AddChecked(Segment); // Not safe
            Module = ParentLogicalScope->FindFirstDefinitionOfKind<CModule>(ModuleName, EMemberOrigin::Original);
            if (!Module)
            {
                Module = &ParentScope->CreateModule(ModuleName);
                Module->SetAstPackage(&Package);
                // Assume all modules implicitly defined via a VersePath are public
                Module->SetAccessLevel({SAccessLevel::EKind::Public});
            }

            ModulePart = &Module->CreatePart(ParentScope, false);
            ModulePart->SetAstPackage(&Package);
            
            ParentLogicalScope = Module;
            ParentScope = ModulePart;
        }

        return ModulePart;
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Using(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        CExprMacroCall::CClause& Clause = MacroCallAst.Clauses()[0];
        if (Clause.Exprs().Num() != 1)
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_MalformedMacro, "`using` clause must contain a single path.");
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        bool bModuleOrSnippet = _Context._Scope->IsModuleOrSnippet();
        bool bControlScope    = _Context._Scope->IsControlScope();

        if (!(bModuleOrSnippet || bControlScope))
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_InvalidContextForUsing);
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        TSRef<CExprUsing> UsingExpr = TSRef<CExprUsing>::New(Move(Clause.Exprs()[0]));
        const Vst::Node*  VstNode   = MacroCallAst.GetMappedVstNode();

        if (bControlScope)
        {
            // Specified `using` in a local scope - this generally occurs during the `Deferred_OpenFunctionBodyExpressions` phase
            SExprArgs ExprArgs = {};

            ExprArgs.AnalysisContext = EAnalysisContext::IsInUsingExpression;

            TSPtr<CExpressionBase> AnalyzedContext = AnalyzeExpressionAst(UsingExpr->_Context, ExprCtx.WithResultIsDotted(), ExprArgs);

            if (AnalyzedContext)
            {
                UsingExpr->_Context = AnalyzedContext.AsRef();
            }

            EAstNodeType UsingExprType = UsingExpr->_Context->GetNodeType();

            if (UsingExprType == EAstNodeType::Identifier_Data)
            {
                const CTypeBase* UsingType = UsingExpr->_Context->GetResultType(*_Program);

                // Ensure self type is not same type or subtype of the using context type.
                // - an unrelated type is best - though a using context type which is a subtype is
                // permissible but it will need to qualify use of any overlapping conflict.
                // `CScope::AddUsingInstance()` uses a similar test mechanism.
                // [Note that `IsSubtype()` also matches for same type.]
                if (_Context._Self && UsingType && SemanticTypeUtils::IsSubtype(_Context._Self, UsingType))
                {
                    // The domain of the using type is entirely overlapped by the Self type and so cannot be differentiated
                    AppendGlitch(
                        MacroCallAst,
                        EDiagnostic::ErrSemantic_ScopedUsingSelfSubtype,
                        CUTF8String(
                            "The `Self` type is `%s` which is the same type or a subtype of this local `using{%s}` which has a context variable of type `%s`. "
                            "Members would not be able to be inferred since it will always be ambiguous which context to use. "
                            "Remove this `using` and use `%s.[Member]` instead.",
                            _Context._Self->AsCode().AsCString(),
                            UsingExpr->_Context->GetErrorDesc().AsCString(),
                            UsingType->AsCode().AsCString(),
                            UsingExpr->_Context->GetErrorDesc().AsCString()));
                }
                else
                {

                    CExprIdentifierData& UsingContext = static_cast<CExprIdentifierData&>(*UsingExpr->_Context);

                    if (UsingContext.Context())
                    {
                        // Additional context on identifier not yet supported - see end of ResolveIdentifierToDefinitions()
                        // @Revisit - Don't support for now - should support this in the future - see Jira SOL-4877
                        // One way to implement this would be to generate a variable and assign it to whatever complex
                        // expression is here, then use the generated variable instead.
                        AppendGlitch(
                            MacroCallAst,
                            EDiagnostic::ErrSemantic_ScopedUsingContextUnsupported,
                            CUTF8String(
                                "Only simple identifiers without additional context are currently supported by a local `using` macro - `%s.%s` has additional context. For now you can assign it to another variable and put that in a `using` instead.",
                                UsingContext.Context()->GetErrorDesc().AsCString(),
                                UsingContext.GetErrorDesc().AsCString()));
                    }

                    // Add to tracked using and return any conflict
                    if (const CDataDefinition* ConflictingContext = _Context._Scope->AddUsingInstance(&UsingContext._DataDefinition))
                    {
                        if (&UsingContext._DataDefinition == ConflictingContext)
                        {
                            // Not added since same variable already added
                            AppendGlitch(
                                MacroCallAst,
                                EDiagnostic::ErrSemantic_ScopedUsingIdentAlreadyPresent,
                                CUTF8String(
                                    "The `%s` variable specified by the local `using` macro is already being inferred.",
                                    UsingContext.GetErrorDesc().AsCString()));
                        }
                        else
                        {
                            // Not added since type/subtype already being inferred
                            AppendGlitch(
                                MacroCallAst,
                                EDiagnostic::ErrSemantic_ScopedUsingExistingSubtype,
                                CUTF8String(
                                    "There is a previous local `using{%s}` which has the context variable type `%s` which is the same type or a subtype of this `using{%s}` which has a context variable of type `%s`. "
                                    "Members would not be able to be inferred since it will always be ambiguous which context to use. "
                                    "Remove this `using` and use `%s.[Member]` instead or remove earlier `using{}`.",
                                    ConflictingContext->AsNameCString(),
                                    ConflictingContext->GetType()->AsCode().AsCString(),
                                    UsingContext.GetErrorDesc().AsCString(),
                                    UsingType ? UsingType->AsCode().AsCString() : "-unknown-",
                                    UsingContext.GetErrorDesc().AsCString()));
                        }
                    }
                }
            }
            else if (UsingExprType == EAstNodeType::Invoke_ReferenceToValue)
            {
                // If `var` variable then this could be a CExprReferenceToValue -> CExprPointerToReference -> CExperIdentifierData

                // @Revisit - Don't support for now - could support this in the future - see Jira SOL-4877
                // This will usually be used with class instances as a context so var non-class instances might not really ever be used in local `using`...
                AppendGlitch(
                    MacroCallAst,
                    EDiagnostic::ErrSemantic_ScopedUsingContextUnsupported,
                    "Local scope `using` only currently supports non `var` variable identifiers as the context.");
            }
            else if (UsingExprType == EAstNodeType::Literal_Path)
            {
                // Specifying a module path context for a limited local scope

                // @Revisit - Don't support for now - could support this in the future - see Jira SOL-4877
                AppendGlitch(
                    MacroCallAst,
                    EDiagnostic::ErrSemantic_ScopedUsingContextUnsupported,
                    "Local scope `using` only supports local variable identifiers as the context - module paths are only supported in module scope. Move this to a module scope.");
            }
            else if ((UsingExprType == EAstNodeType::Identifier_Module) || (UsingExprType == EAstNodeType::Identifier_ModuleAlias))
            {
                // Specifying a module identifier context for a limited local scope

                // @Revisit - Don't support for now - could support this in the future - see Jira SOL-4877
                AppendGlitch(
                    MacroCallAst,
                    EDiagnostic::ErrSemantic_ScopedUsingContextUnsupported,
                    "Local scope `using` only supports local variable identifiers as the context - module identifiers are only supported in module scope. Move this to a module scope.");
            }
            else
            {
                // Error on any other context expression
                AppendGlitch(
                    MacroCallAst,
                    EDiagnostic::ErrSemantic_ScopedUsingContextUnsupported,
                    "Local scope `using` only supports local variable identifiers as the context.");
            }
        }

        if (bModuleOrSnippet)
        {
            // We need to defer analyzing import and using statements to allow importing of modules
            // that will get defined only later, e.g.
            // using { A } A = module{}
            // using { /path/to/the/end/of/the/galaxy }
            EnqueueDeferredTask(Deferred_ModuleReferences, [this, UsingExpr, VstNode, ExprCtx]()
            {
                const CModule* Module = nullptr;
                // Is the argument a path literal?
                if (UsingExpr->_Context->GetNodeType() == EAstNodeType::Literal_Path)
                {
                    // Yes, figure module from the path
                    CExprPath& Path = static_cast<CExprPath&>(*UsingExpr->_Context);
                    AnalyzePathLiteral(Path, ExprCtx.WithResultIsImported(_Program->_pathType));

                    // Check for a /localhost path, and error if so.
                    const CUTF8StringView View(Path._Path.ToStringView());
                    const char LocalHost[] = "/localhost";

                    if (View.StartsWith(LocalHost))
                    {
                        // Does the path exactly match "/localhost"?
                        const bool ExactMatch = View.ByteLen() == (sizeof(LocalHost) - 1);
                        // Or is it a path that begins with "/localhost/*"?
                        const bool BeginsWith = (size_t(View.ByteLen()) >= sizeof(LocalHost)) && (View[sizeof(LocalHost) - 1] == '/');

                        if (ExactMatch || BeginsWith)
                        {
                            AppendGlitch(VstNode, EDiagnostic::ErrSemantic_MalformedMacro, "`using` clause must not use paths in \"/localhost\"");
                        }
                    }

                    Module = ResolvePathToModule(Path._Path, Path);
                }
                else
                {
                    // No, just do a generic semantic analysis and see if what comes back is a module
                    // NOTE: (yiliang.siew) If we are in a `using` macro, we pass that information along so that the
                    // semantic analysis requirements can be relaxed for identifiers within those since type definitions
                    // would not have been analyzed yet in that case.
                    SExprArgs ExprArgs = {};
                    ExprArgs.AnalysisContext = EAnalysisContext::IsInUsingExpression;
                    TSPtr<CExpressionBase> AnalyzedContext = AnalyzeExpressionAst(UsingExpr->_Context, ExprCtx.WithResultIsDotted(), ExprArgs);
                    if (AnalyzedContext)
                    {
                        UsingExpr->_Context = AnalyzedContext.AsRef();
                    }

                    if (const CTypeBase* PathType = UsingExpr->_Context->GetResultType(*_Program))
                    {
                        const CNormalType& ResultType = PathType->GetNormalType();
                        Module = ResultType.AsNullable<CModule>();
                        if (!Module && !SemanticTypeUtils::IsUnknownType(&ResultType))
                        {
                            AppendGlitch(VstNode, EDiagnostic::ErrSemantic_ExpectedModule);
                        }
                    }
                    else
                    {
                        AppendGlitch(VstNode, EDiagnostic::ErrSemantic_ExpectedModule);
                    }
                }

                if (Module)
                {
                    UsingExpr->_Module = Module;
                    _Context._Scope->AddUsingScope(Module);

                    EnqueueDeferredTask(Deferred_ValidateAttributes, [this, Module, VstNode, Scope = _Context._Scope]
                    {
                        // Build an array with the definition for each segment of the path from the root to this module.
                        TArrayG<const CDefinition*, TInlineElementAllocator<10>> PathToModule;
                        for (const CScope* TestScope = Module; TestScope; TestScope = TestScope->GetParentScope())
                        {
                            if (const CDefinition* Definition = TestScope->ScopeAsDefinition())
                            {
                                PathToModule.Add(Definition);
                            }
                        }

                        // Check each definition from outermost to innermost for accessibility from the current scope.
                        for (int32_t Index = PathToModule.Num() - 1; Index >= 0; --Index)
                        {
                            if (!RequireAccessible(VstNode, *Scope, *PathToModule[Index]))
                            {
                                break;
                            }
                        }
                    });
                }
            });
        }

        return ReplaceMapping(MacroCallAst, UsingExpr);
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Profile(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        // profile(args) {codeblock}
        if (!ValidateMacroForm<ESimpleMacroForm::m1m2>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        CExprMacroCall::CClause* ArgumentsClause = nullptr;
        CExprMacroCall::CClause* CodeBlockClause = nullptr;

        if (MacroCallAst.Clauses().Num() == 2)
        {
            // Args form: profile(args): subblock
            ArgumentsClause = &MacroCallAst.Clauses()[0];
            CodeBlockClause = &MacroCallAst.Clauses()[1];
        }
        else
        {
            // NoArgs form: profile: subblock
            ArgumentsClause = nullptr;
            CodeBlockClause = &MacroCallAst.Clauses()[0];
        }

        // Create the profile block AST node.
        TSRef<CExprProfileBlock> ProfileBlockAst = TSRef<CExprProfileBlock>::New();
        MacroCallAst.GetMappedVstNode()->AddMapping(ProfileBlockAst);

        // ----------------------
        // We only allow profile statements inside code blocks. You can't
        // use them to initialize class members for example because the required 
        // management contexts won't exist when that's run.
        if (!_Context._Scope->IsControlScope())
        {
            AppendGlitch(MacroCallAst,
                EDiagnostic::ErrSemantic_ProfileOnlyAllowedInFunctions,
                "`profile` blocks are only allowed inside of functions");
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        // ----------------------
        // Validate the Arguments
        if (ArgumentsClause)
        {
            if (ArgumentsClause->Exprs().Num() == 1 && ArgumentsClause->Exprs()[0].IsValid())
            {
                ProfileBlockAst->_UserTag = Move(ArgumentsClause->Exprs()[0]);

                // Analyze the user-tag expression.
                if (TSPtr<CExpressionBase> NewTagAst = AnalyzeExpressionAst(ProfileBlockAst->_UserTag.AsRef(), ExprCtx.WithResultIsUsed(_Program->_stringAlias->GetType())))
                {
                    ProfileBlockAst->_UserTag = Move(NewTagAst.AsRef());
                }

                if (!SemanticTypeUtils::IsStringType(ProfileBlockAst->_UserTag->GetResultType(*_Program)->GetNormalType()))
                {
                    AppendGlitch(MacroCallAst,
                        EDiagnostic::ErrSemantic_MalformedParameter,
                        "`profile` argument must be a string expression");
                    return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
                }
            }
            else
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_MalformedParameter, "`profile` argument must be a string expression");
                return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
            }
        }

        // ----------------------
        // Validate the CodeBlock
        if (CodeBlockClause->Exprs().Num() == 0)
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_MalformedMacro, "`profile` codeblock must not be empty");
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        // ----------------------
        // Analyze the macro clause as a code block, and set it as the profile block body.
        const SExprCtx BodyExprCtx = ExprCtx
            .AllowReturnFromLeadingStatementsAsSubexpressionOfReturn()
            .With(ExprCtx.AllowedEffects.With(EEffect::suspends, false));       // We don't-yet support suspends effects inside of a profile block

        ProfileBlockAst->SetExpr(AnalyzeMacroClauseAsCodeBlock(*CodeBlockClause, ProfileBlockAst->GetMappedVstNode(), BodyExprCtx));

        // Code block return type is the type of the last expression
        ProfileBlockAst->SetResultType(ProfileBlockAst->Expr()->GetResultType(*_Program));

        return Move(ProfileBlockAst);
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Type(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        CExprMacroCall::CClause& Clause = MacroCallAst.Clauses()[0];
        if (Clause.Exprs().Num() != 1)
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_MalformedMacro, "`type` clause must contain a single expression.");
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        ULANG_ASSERTF(_CurrentTaskPhase >= Deferred_Type, "Should not reach here until after type definitions are analyzed");

        // Create a new CTypedefScope and use it as the current scope when analyzing the
        // abstract value expression.
        TSRef<CTypeScope> TypeScope = _Context._Scope->CreateNestedTypeScope();
        TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, TypeScope.Get());

        // Analyze the abstract value subexpression.
        TSRef<CExpressionBase> AbstractValueAst = Clause.Exprs()[0];
        if (TSPtr<CExpressionBase> NewAbstractValueAst = AnalyzeExpressionAst(AbstractValueAst, SExprCtx::Default().With(EffectSets::Computes).AllowReservedUnderscoreFunctionIdentifier()))
        {
            AbstractValueAst = Move(NewAbstractValueAst.AsRef());
        }

        // Require that the abstract value subexpression is in the form of a function
        // definition. This should gradually be relaxed, but needs to be limited not to
        // accept subexpressions we can't precisely represent in the type system.
        const CTypeBase* NegativeType;
        const CTypeBase* PositiveType;
        if (AbstractValueAst->GetNodeType() == EAstNodeType::Definition_Function)
        {
            const CExprFunctionDefinition& FunctionDefinitionAst = *AbstractValueAst.As<CExprFunctionDefinition>();
            const CFunction& Function = *FunctionDefinitionAst._Function;
            NegativeType = Function._NegativeType;
            PositiveType = Function._Signature.GetFunctionType();

            // Don't allow function definitions with a body.
            if (FunctionDefinitionAst.Value())
            {
                AppendGlitch(
                    FunctionDefinitionAst,
                    EDiagnostic::ErrSemantic_Unsupported,
                    "`type` does not yet support function definitions with a body");
            }
            else
            {
                ULANG_ASSERTF(FunctionDefinitionAst.ValueDomain(),
                    "Expected CExprFunctionDefinition to have a ValueDomain because it doesn't have a Value");
            }
        }
        else if (AbstractValueAst->GetNodeType() == EAstNodeType::Definition_Where)
        {
            PositiveType = AbstractValueAst->GetResultType(*_Program);
            const CNormalType& PositiveNormalType = PositiveType->GetNormalType();
            ULANG_ENSUREF(PositiveNormalType.IsA<CIntType>() || PositiveNormalType.IsA<CFloatType>(), "Where clauses only support constrained ints/floats right now");
            NegativeType = PositiveType;
        }
        else
        {
            if (!SemanticTypeUtils::IsUnknownType(AbstractValueAst->GetResultType(*_Program)))
            {
                AppendGlitch(
                    *AbstractValueAst,
                    EDiagnostic::ErrSemantic_Unsupported,
                    "type does not yet support subexpressions other than function declarations and 'where' clauses.");
            }
            TSRef<CExprError> ErrorNode = TSRef<CExprError>::New();
            ErrorNode->AppendChild(Move(AbstractValueAst));
            return ReplaceMapping(MacroCallAst, Move(ErrorNode));
        }

        return ReplaceMapping(
            MacroCallAst,
            TSRef<CExprType>::New(Move(AbstractValueAst),
            _Program->GetOrCreateTypeType(NegativeType, PositiveType)));
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CScopedAccessLevelDefinition> CreateNewScopedAttributeClass(TOptional<CSymbol> Name = TOptional<CSymbol>())
    {
        TSRef<CScopedAccessLevelDefinition> NewAccessLevel = _Context._Scope->CreateAccessLevelDefinition(Name);
        GetProgram()->AddStandardAccessLevelAttributes(NewAccessLevel);
        
        return NewAccessLevel;
    };

    void ResolveScopedModulePaths(CScopedAccessLevelDefinition* AccessLevelDefinition, TArray<TSRef<CExpressionBase>>& ModuleRefExprs, const Vst::Node* VstNode, const SExprCtx& ExprCtx)
    {
        // We need to defer analyzing module refs to allow module import
        for (int32_t ModuleRefExprIdx = 0; ModuleRefExprIdx < ModuleRefExprs.Num(); ModuleRefExprIdx++)
        {
            TSRef<CExpressionBase> ModuleRefExpr = ModuleRefExprs[ModuleRefExprIdx];

            const CModule* Module = nullptr;

            if (ModuleRefExpr->GetNodeType() == EAstNodeType::Literal_Path)
            {
                // Figure module from the path expression
                CExprPath& PathExpr = static_cast<CExprPath&>(*ModuleRefExpr);
                AnalyzePathLiteral(PathExpr, ExprCtx.WithResultIsImported(_Program->_pathType));
                Module = ResolvePathToModule(PathExpr._Path, PathExpr);
            }
            else if (ModuleRefExpr->GetNodeType() == EAstNodeType::Identifier_Unresolved)
            {
                // Probably just a module reference, so do a generic semantic analysis and see if what comes back is a module
                TSPtr<CExpressionBase> AnalyzedPath = AnalyzeExpressionAst(ModuleRefExpr, ExprCtx.WithResultIsDotted());

                if (AnalyzedPath.IsValid())
                {
                    ModuleRefExpr = ReplaceMapping(*ModuleRefExpr, Move(AnalyzedPath.AsRef()));
                    ModuleRefExprs[ModuleRefExprIdx] = ModuleRefExpr;
                }

                const CNormalType& ResultType = ModuleRefExpr->GetResultType(*_Program)->GetNormalType();
                Module = ResultType.AsNullable<CModule>();

                if (!Module && !SemanticTypeUtils::IsUnknownType(&ResultType))
                {
                    AppendGlitch(VstNode,
                        EDiagnostic::ErrSemantic_ExpectedModule,
                        CUTF8String("Found %s in scoped-macro and expected a module reference", ModuleRefExpr->GetErrorDesc().AsCString()));
                }
            }
            else
            {
                // Whatever is here, it isn't a module path or resolvable to a module path
                AppendGlitch(*ModuleRefExpr,
                    EDiagnostic::ErrSemantic_ExpectedModule,
                    CUTF8String("Found %s in scoped-macro and expected a module reference", ModuleRefExpr->GetErrorDesc().AsCString()));
            }

            if (Module)
            {
                AccessLevelDefinition->_Scopes.AddUnique(Module);
            }
        }

    }

    TSRef<CExpressionBase> AnalyzeAnonymousScopedAccessLevelDefinition(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx, const SExprArgs& ExprArgs)
    {
        CExprMacroCall::CClause& MacroClause = MacroCallAst.Clauses()[0];
        TArray<TSRef<CExpressionBase>> ModuleRefExprs = Move(MacroClause.Exprs());

        TSRef<CScopedAccessLevelDefinition> AccessLevelDefinition = CreateNewScopedAttributeClass();

        TSRef<CExprScopedAccessLevelDefinition> NewAccessLevel = TSRef<CExprScopedAccessLevelDefinition>::New(AccessLevelDefinition);

        EnqueueDeferredTask(Deferred_ModuleReferences, [this, AccessLevelDefinition, InModuleRefExprs = ModuleRefExprs, NewAccessLevel, VstNode = MacroCallAst.GetMappedVstNode(), ExprCtx]()
            {
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &AccessLevelDefinition->_EnclosingScope);

                TArray<TSRef<CExpressionBase>> ModuleRefExprs = InModuleRefExprs;
                ResolveScopedModulePaths(AccessLevelDefinition, ModuleRefExprs, VstNode, ExprCtx);

                NewAccessLevel->_ScopeReferenceExprs = ModuleRefExprs;
            });

        return NewAccessLevel;
    }

    TSRef<CExpressionBase> AnalyzeNamedScopedAccessLevelDefinition(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx, const SExprArgs& ExprArgs)
    {
        CSymbol AccessLevelName = ExprArgs.MacroCallDefinitionContext->_Name;

        CExprMacroCall::CClause& MacroClause = MacroCallAst.Clauses()[0];
        TArray<TSRef<CExpressionBase>> ModuleRefExprs = Move(MacroClause.Exprs());

        TSRef<CScopedAccessLevelDefinition> AccessLevelDefinition = CreateNewScopedAttributeClass(AccessLevelName);

        TArray<SAttribute> NameAttributes = Move(ExprArgs.MacroCallDefinitionContext->_NameAttributes);

        EnqueueDeferredTask(Deferred_Attributes, [this, AccessLevelDefinition, NameAttributes]()
            {
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &AccessLevelDefinition->_EnclosingScope);

                TArray<SAttribute> Result = NameAttributes;
                AnalyzeAttributes(Result, CAttributable::EAttributableScope::ScopedAccessLevel, EAttributeSource::Name);
                AccessLevelDefinition->_Attributes.Append(Result);
                AccessLevelDefinition->SetAccessLevel(GetAccessLevelFromAttributes(*AccessLevelDefinition->GetAstNode()->GetMappedVstNode(), *AccessLevelDefinition));
                ValidateExperimentalAttribute(*AccessLevelDefinition);
            });

        TSRef<CExprScopedAccessLevelDefinition> NewAccessLevel = TSRef<CExprScopedAccessLevelDefinition>::New(AccessLevelDefinition);

        EnqueueDeferredTask(Deferred_ModuleReferences, [this, AccessLevelDefinition, InModuleRefExprs = ModuleRefExprs, NewAccessLevel, VstNode = MacroCallAst.GetMappedVstNode(), ExprCtx]()
            {
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &AccessLevelDefinition->_EnclosingScope);

                TArray<TSRef<CExpressionBase>> ModuleRefExprs = InModuleRefExprs;
                ResolveScopedModulePaths(AccessLevelDefinition, ModuleRefExprs, VstNode, ExprCtx);

                NewAccessLevel->_ScopeReferenceExprs = ModuleRefExprs;
            });

        return NewAccessLevel;
    }

    TSRef<CExpressionBase> AnalyzeMacroCall_Scoped(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx, const SExprArgs& ExprArgs)
    {
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        if (MacroCallAst.Clauses()[0].Exprs().Num() <= 0)
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_MalformedMacro, "`scoped` clause must contain 1 or more module references.");
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        if (ExprArgs.MacroCallDefinitionContext == nullptr)
        {
            return ReplaceMapping(MacroCallAst, AnalyzeAnonymousScopedAccessLevelDefinition(MacroCallAst, ExprCtx, ExprArgs));
        }
        else
        {
            return ReplaceMapping(MacroCallAst, AnalyzeNamedScopedAccessLevelDefinition(MacroCallAst, ExprCtx, ExprArgs));
        }
    }

    //-------------------------------------------------------------------------------------------------
    TArray<CInterface*> GetAllInheritedInterfaces(const CClass* Class, TArray<CInterface*>& OutRedundantInterfaces)
    {
        TArray<CInterface*> InheritedInterfaces;

        // Do the explicit interfaces
        VisitAllInheritedInterfaces(true, Class->_SuperInterfaces, Class->_SuperInterfaces, InheritedInterfaces, OutRedundantInterfaces);

        // Follow the class inheritence
        for (const CClass* SuperClass = Class->_Superclass; SuperClass; SuperClass = SuperClass->_Superclass)
        {
            VisitAllInheritedInterfaces(false, Class->_SuperInterfaces, SuperClass->_SuperInterfaces, InheritedInterfaces, OutRedundantInterfaces);
        }
        return InheritedInterfaces;
    }
    
    //-------------------------------------------------------------------------------------------------
    TArray<CInterface*> GetAllInheritedInterfaces(CInterface* Interface, TArray<CInterface*>& OutRedundantInterfaces, bool& bHasCycle)
    {
        TArray<CInterface*> InheritedInterfaces;

        // Do the explicit interfaces
        VisitAllInheritedInterfaces(true, Interface->_SuperInterfaces, Interface->_SuperInterfaces, InheritedInterfaces, OutRedundantInterfaces);

        bHasCycle = InheritedInterfaces.Contains(Interface);
        if (!bHasCycle)
        {
            InheritedInterfaces.Add(Interface);
        }

        return InheritedInterfaces;
    }

    //-------------------------------------------------------------------------------------------------
    // VisitAllInheritedInterfaces assumes that the first call is with the same interfaces in FirstLevelInterfaces and InSuperInterfaces.
    // It's possible to do more calls keeping the same value for FirstLevelInterfaces but changing InSuperInterfaces. This will extend VisitedInterfaces and OutRedundantInterfaces with newly visited and redundant interfaces. 
    void VisitAllInheritedInterfaces(bool bIsFirstLevel, const TArray<CInterface*>& FirstLevelInterfaces, const TArray<CInterface*>& InSuperInterfaces, TArray<CInterface*>& VisitedInterfaces, TArray<CInterface*>& OutRedundantInterfaces)
    {
        TArray<CInterface*> PendingInterfaces;
        // If bIsFirstLevel then check for duplicated interfaces, e.g., t := interface(interface_a, interface_a) and add those to OutRedundatInterfaces
        // Only process top level interfaces, the super interfaces of those are added to pending interfaces.
        if (bIsFirstLevel)
        {
            for (CInterface* VisitInterface : InSuperInterfaces)
            {
                if (VisitedInterfaces.Contains(VisitInterface)) // care about repetition
                {
                    OutRedundantInterfaces.AddUnique(VisitInterface);
                }
                else
                {
                    VisitedInterfaces.Add(VisitInterface);
                    PendingInterfaces.Append(VisitInterface->_SuperInterfaces);
                }
            }
        }
        else // Not first level
        {
            PendingInterfaces = InSuperInterfaces;
        }

        // Process all non top level interfaces. 
        // Check if any of the FirstLevelInterfaces are visited, if they are then they are redundant (or part of a cycle). 
        while (PendingInterfaces.Num())
        {
            CInterface* VisitInterface = PendingInterfaces.Pop();

            if (FirstLevelInterfaces.Contains(VisitInterface)) // No need to add if already done as first level interface
            {
                OutRedundantInterfaces.AddUnique(VisitInterface); // ... but add that this first level interface is redundant.
            }
            else if (!VisitedInterfaces.Contains(VisitInterface))
            {
                VisitedInterfaces.Add(VisitInterface);
                PendingInterfaces.Append(VisitInterface->_SuperInterfaces);
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Interface(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx, const SExprArgs& ExprArgs)
    {
        if (!ValidateMacroForm<ESimpleMacroForm::m1m2>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        else
        {
            // Require that the MacroCall occurs directly as the Value subexpression of a Definition node.
            if (!ExprArgs.MacroCallDefinitionContext)
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_NominalTypeInAnonymousContext);
                return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
            }

            // For now, only allow interface definitions at module scope.
            if (_Context._Self || (_Context._Function && !_Context._Function->GetParentScope()->IsModuleOrSnippet()))
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_Unimplemented, "Interface definitions are not yet implemented outside of a module scope.");
                return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
            }

            const CSymbol InterfaceName = ExprArgs.MacroCallDefinitionContext->_Name;

            const Vst::Node* MacroCallVst = MacroCallAst.GetMappedVstNode();
            CExprMacroCall::CClause* SuperInterfacesClause = MacroCallAst.Clauses().Num() == 1 ? nullptr : &MacroCallAst.Clauses()[0];
            CExprMacroCall::CClause& MembersClause     = MacroCallAst.Clauses()[MacroCallAst.Clauses().Num() - 1];

            // Create the interface type.
            CInterface* Interface = &_Context._Scope->CreateInterface(InterfaceName);

            TGuardValue<TArray<const CDefinition*>> DefinitionsGuard(_Context._EnclosingDefinitions);
            _Context._EnclosingDefinitions.Add(Interface);

            TArray<SAttribute> NameAttributes = Move(ExprArgs.MacroCallDefinitionContext->_NameAttributes);
            TArray<SAttribute> DefAttributes = Move(ExprArgs.MacroCallDefinitionContext->_DefAttributes);
            Interface->_EffectAttributable._Attributes = Move(MacroCallAst.Name()->_Attributes);

            // Queue up jobs that processes any attributes on the interface
            const bool bIsParametric = ExprArgs.MacroCallDefinitionContext->_bIsParametric;
            EnqueueDeferredTask(Deferred_Attributes, [this, Interface, NameAttributes, DefAttributes, bIsParametric]()
            {
                // Not inside the function yet
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Interface->GetParentScope());
                Interface->_Attributes = AnalyzeNameAndDefAttributes(NameAttributes, DefAttributes, CAttributable::EAttributableScope::Interface);
                AnalyzeAttributes(Interface->_EffectAttributable._Attributes, CAttributable::EAttributableScope::Interface, EAttributeSource::InterfaceEffect);
                if (bIsParametric)
                {
                    // Set parametric interfaces as public, which will be combined with the access level of the outer function.
                    ULANG_ASSERTF(!Interface->_Attributes.Num(), "Expected parametric interfaces to be missing attributes");
                    Interface->SetAccessLevel(TOptional<SAccessLevel>{SAccessLevel::EKind::Public});
                }
                else
                {
                    Interface->SetAccessLevel(GetAccessLevelFromAttributes(*Interface->GetAstNode()->GetMappedVstNode(), *Interface));
                    ValidateExperimentalAttribute(*Interface);
                }
                Interface->_ConstructorAccessLevel = GetAccessLevelFromAttributes(*Interface->GetAstNode()->GetMappedVstNode(), Interface->_EffectAttributable);
                ULANG_ASSERTF((Interface->DerivedConstructorAccessLevel()._Kind != Cases<SAccessLevel::EKind::Private, SAccessLevel::EKind::Protected>), "GetAccessLevelFromAttributes should have already handled this glitch.");
            });

            // Create the interface definition AST node.
            TSRef<CExprInterfaceDefinition> DefinitionAst = TSRef<CExprInterfaceDefinition>::New(
                *Interface,
                SuperInterfacesClause ? Move(SuperInterfacesClause->Exprs()) : TArray<TSRef<CExpressionBase>>{},
                Move(MembersClause.Exprs()));

            // Analyze the interface definition.
            AnalyzeInterface(Interface, DefinitionAst, MacroCallVst, ExprCtx);

            ProcessQualifier(Interface->GetParentScope(), Interface, ExprArgs.MacroCallDefinitionContext->_Qualifier, DefinitionAst, ExprCtx);

            // Require that the interface doesn't shadow any other definitions.
            RequireUnambiguousDefinition(*Interface, "interface");

            return ReplaceMapping(MacroCallAst, Move(DefinitionAst));
        }
    }
    
    //-------------------------------------------------------------------------------------------------
    void AnalyzeInterface(CInterface* Interface, const TSRef<CExprInterfaceDefinition>& DefinitionAst, const Vst::Node* MacroCallVst, const SExprCtx& ExprCtx)
    {
        if (_Context._Scope->GetKind() == CScope::EKind::Function)
        {
            for (const CTypeVariable* TypeVariable : static_cast<const CFunction*>(_Context._Scope)->GetDefinitionsOfKind<CTypeVariable>())
            {
                // @see AnalyzeParam(TSRef<CExprDefinition>, SParamsInfo&)
                Interface->_TypeVariableSubstitutions.Emplace(TypeVariable, TypeVariable, TypeVariable);
            }
        }

        // Analyze the interface's members.
        {
            ULANG_ASSERTF(!_Context._Self, "Unexpected nested interface");
            TGuardValue<const CTypeBase*> CurrentClassGuard(_Context._Self, Interface);
            AnalyzeMemberDefinitions(Interface, *DefinitionAst, ExprCtx);
            EnqueueDeferredTask(Deferred_Type, [Interface]
            {
                SetNegativeInterfaceMemberDefinitionTypes(*Interface);
            });
        }

        // Create glitch if default definition given for functions
        for (TSRef<CExpressionBase> Member : DefinitionAst->Members())
        {
            if (Member->GetNodeType() == EAstNodeType::Definition_Function)
            { 
                CExprFunctionDefinition& FunctionDefinition = static_cast<CExprFunctionDefinition&>(*Member);
                if (FunctionDefinition.Value().IsValid())
                {
                    AppendGlitch(
                        FunctionDefinition.GetMappedVstNode(),
                        EDiagnostic::ErrSemantic_OnlyFunctionsInInterfaceBody,
                        CUTF8String("Interfaces cannot contain default implementations."));
                }
            }
        }

        EnqueueDeferredTask(Deferred_Type, [this, DefinitionAst, Interface, ExprCtx]()
        {
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Interface);

            // Process the super interfaces.
            for (int32_t SuperInterfaceIndex = 0; SuperInterfaceIndex < DefinitionAst->SuperInterfaces().Num(); ++SuperInterfaceIndex)
            {
                TSRef<CExpressionBase> SuperInterfaceAst = DefinitionAst->SuperInterfaces()[SuperInterfaceIndex];

                // Analyze the super interface expression.
                if (TSPtr<CExpressionBase> NewSuperInterfaceAst = AnalyzeExpressionAst(SuperInterfaceAst, ExprCtx.WithResultIsUsedAsType()))
                {
                    SuperInterfaceAst = Move(NewSuperInterfaceAst.AsRef());
                    DefinitionAst->SetSuperInterface(TSRef<CExpressionBase>(SuperInterfaceAst), SuperInterfaceIndex);
                }

                // Interpret each super interface expression as a type.
                STypeTypes SuperTypeTypes = GetTypeTypes(*SuperInterfaceAst);
                if (SuperTypeTypes._Tag == ETypeTypeTag::Type)
                {
                    const CNormalType& NegativeSuperType = SuperTypeTypes._NegativeType->GetNormalType();
                    const CNormalType& PositiveSuperType = SuperTypeTypes._PositiveType->GetNormalType();
                    if (const CInterface* SuperInterface = SemanticTypeUtils::AsSingleInterface(NegativeSuperType, PositiveSuperType))
                    {
                        Interface->_SuperInterfaces.Add(const_cast<CInterface*>(SuperInterface));
                        Interface->_NegativeInterface->_SuperInterfaces.Add(SuperInterface->_NegativeInterface);
                    }
                    else
                    {
                        AppendGlitch(*SuperInterfaceAst, EDiagnostic::ErrSemantic_ExpectedInterface);
                    }
                }
            }
        });

        // After all interfaces' direct superinterfaces have been processed, check for cycles in the inheritance hierarchy.
        EnqueueDeferredTask(Deferred_ValidateCycles, [this, Interface, MacroCallVst]()
        {
            TArray<CInterface*> RedundantInterfaces;
            bool bHasCycle;
            GetAllInheritedInterfaces(Interface, RedundantInterfaces, bHasCycle);
            if (bHasCycle)
            {
                Interface->_SuperInterfaces = { };
                Interface->_NegativeInterface->_SuperInterfaces = { };
                AppendGlitch(
                    MacroCallVst,
                    EDiagnostic::ErrSemantic_InterfaceOrClassInheritsFromItself,
                    CUTF8String("Interface `%s` inherits from itself.", Interface->AsNameCString()));
            }
            else
            {   // Only emit redundancies if this isn't a cycle, otherwise there will be a lot of spurious error messages. 
                for (CInterface* RedundantInterface : RedundantInterfaces)
                {
                    AppendGlitch(
                        MacroCallVst,
                        EDiagnostic::ErrSemantic_RedundantInterfaceInheritance,
                        CUTF8String("Interface `%s` redundantly inherits from interface `%s` (or '%s' is part of a cycle).", Interface->AsNameCString(), RedundantInterface->AsNameCString(), RedundantInterface->AsNameCString()));
                }
            }
            Interface->_bHasCyclesBroken = true;
            Interface->_NegativeInterface->_bHasCyclesBroken = true;
        });

        EnqueueDeferredTask(Deferred_ValidateAttributes, [this, Interface]()
        {
            for (CInterface* SuperInterface : Interface->_SuperInterfaces)
            {
                RequireConstructorAccessible(Interface->GetAstNode()->GetMappedVstNode(), *Interface, *SuperInterface);
            }
        });
    }
    
    //-------------------------------------------------------------------------------------------------
    // This exists because we need to know if a function has the suspends or decides attribute at the time
    // that we create the class or function's type. However, that runs during the types phase, which is before
    // the attributes phase.
    // If the same effect is decoded more than once, then bReportDuplicate should only be true for one calls to reduce redundant error messages.
    CExprIdentifierBase* GetBuiltInAttributeHack(const CAttributable& Attributable, const CClass* AttributeClass, bool bReportDuplicates)
    {
        return GetBuiltInAttributeHack(Attributable._Attributes, AttributeClass, bReportDuplicates);
    }

    CExprIdentifierBase* GetBuiltInAttributeHack(const TArray<SAttribute>& Attributes, const CClass* AttributeClass, bool bReportDuplicates)
    {
        auto First = Attributes.GetData();
        auto Last = Attributes.GetData() + Attributes.Num();
        const SAttribute* Attribute = FindAttributeHack(First, Last, AttributeClass, *_Program);
        if (Attribute == Last)
        {
            return nullptr;
        }
        if (bReportDuplicates)
        {
            for (auto I = FindAttributeHack(Attribute + 1, Last, AttributeClass, *_Program);
                 I != Last;
                 I = FindAttributeHack(I + 1, Last, AttributeClass, *_Program))
            {
                AppendGlitch(
                    *I->_Expression,
                    EDiagnostic::ErrSemantic_InvalidEffectDeclaration,
                    CUTF8String("Redundant effect attribute <%s>", AttributeClass->Definition()->AsNameCString()));
            }
        }
        return static_cast<CExprIdentifierBase*>(Attribute->_Expression.Get());
    }

    struct SAttributeIdentiferSearchResult
    {
        CExprIdentifierBase* _Identifier;
        const CClass* _Class;
    };

    TArray<SAttributeIdentiferSearchResult> FindAllAttributeIdentifiersHack(const CAttributable& Attributable, const TArray<const CClass*>& AttributeClasses, bool bReportDuplicates)
    {
        return FindAllAttributeIdentifiersHack(Attributable._Attributes, AttributeClasses, bReportDuplicates);
    }

    TArray<SAttributeIdentiferSearchResult> FindAllAttributeIdentifiersHack(const TArray<SAttribute>& Attributes, const TArray<const CClass*>& AttributeClasses, bool bReportDuplicates)
    {
        TArray<SAttributeIdentiferSearchResult> Result;

        for (const SAttribute& Attribute : Attributes)
        {
            for (const CClass* AttributeClass : AttributeClasses)
            {
                if (IsAttributeHack(Attribute, AttributeClass, *_Program))
                {
                    Result.Add({ static_cast<CExprIdentifierBase*>(Attribute._Expression.Get()), Move(AttributeClass) });
                    break;
                }
            }
        }

        if (bReportDuplicates)
        {
            uint32_t ResultCount = Result.Num();
            for (uint32_t i = 0; i < ResultCount; ++i)
            {
                for (uint32_t j = i + 1; j < ResultCount; ++j)
                {
                    if (Result[i]._Class == Result[j]._Class)
                    {
                        AppendGlitch(
                            *Result[i]._Identifier,
                            EDiagnostic::ErrSemantic_InvalidEffectDeclaration,
                            CUTF8String("Redundant effect attribute <%s>", Result[i]._Class->Definition()->AsNameCString()));
                    }
                }
            }
        }

        return Result;
    }

    SEffectSet GetEffectsFromAttributes(const CExpressionBase& AttributedNode, SEffectSet DefaultEffects)
    {
        uLang::SEffectSet ResultEffects = DefaultEffects;

        const TArray<const CClass*>& AllEffectClasses = _Program->GetAllEffectClasses();

        struct SEffectInstance
        {
            const CExprIdentifierBase* _Identifier;
            const CClass* _AttributeClass;
            const SEffectDescriptor& _Descriptor;
        };

        // 1. Find all effect attributes that are present
        TArray<SAttributeIdentiferSearchResult> AttributesFound = FindAllAttributeIdentifiersHack(AttributedNode._Attributes, AllEffectClasses, true);

        TArray<SEffectInstance> AllAttributeInstances;
        for (const auto& AttributePair : AttributesFound)
        {
            AllAttributeInstances.Add({ AttributePair._Identifier, AttributePair._Class, _Program->FindEffectDescriptorChecked(AttributePair._Class, _Context._Package->_UploadedAtFNVersion) });
        }

        // 2. For each attribute we discovered, make sure it is not mutually exclusive with the other attributes we found
        int32_t FoundAttributesCount = AllAttributeInstances.Num();
        for (int i = 0; i < FoundAttributesCount; ++i)
        {
            const SEffectInstance& EffectInstanceA = AllAttributeInstances[i];

            // Look for any mutual exclusions for this effect in subsequent effects
            for (int32_t j = i + 1; j < FoundAttributesCount; ++j)
            {
                const SEffectInstance& EffectInstanceB = AllAttributeInstances[j];

                if (EffectInstanceB._Descriptor._MutualExclusions.Contains(EffectInstanceA._AttributeClass))
                {
                    AppendGlitch(
                        *EffectInstanceA._Identifier,
                        EDiagnostic::ErrSemantic_InvalidEffectDeclaration,
                        CUTF8String("Effect attribute <%s> cannot be combined with <%s>",
                            EffectInstanceA._Identifier->GetErrorDesc().AsCString(),
                            EffectInstanceB._Identifier->GetErrorDesc().AsCString()));

                    // don't over-report
                    break;
                }
            }

            // 3. For each effect, rescind any default effects
            ResultEffects &= ~EffectInstanceA._Descriptor._RescindFromDefault;
        }
        
        // 4. Now that we've stripped off any defaults we don't want anymore, OR all the effects together to get the new effect set
        for (const SEffectInstance& EffectInstance : AllAttributeInstances)
        {
            ResultEffects |= EffectInstance._Descriptor._EffectSet;
        }
        
        return ResultEffects;
    }

    void RequireEffects(const CAstNode& ErrorNode, SEffectSet RequiredEffects, const SEffectSet AllowedEffects, const char* RequiredEffectSourceString, const char* AllowedEffectSourceString = "its context", EDiagnostic Diagnostic = EDiagnostic::ErrSemantic_EffectNotAllowed)
    {
        TArray<const char*> MissingEffectNames;

        // Produce a more helpful error if the decides effect is missing, since the generic error message is confusing for new users.
        if (RequiredEffects[EEffect::decides]
            && !AllowedEffects[EEffect::decides]
            && AllowedEffects.HasAll(RequiredEffects.With(EEffect::decides, false)))
        {
            AppendGlitch(
                ErrorNode,
                Diagnostic,
                CUTF8String(
                    "This %s has the 'decides' effect, which is not allowed by %s. "
                    "The 'decides' effect indicates that the %s might fail, and so must occur in a failure context that will handle the failure. "
                    "Some examples of failure contexts are the condition clause of an 'if', the left operand of 'or', or the clause of the 'logic' macro.",
                    RequiredEffectSourceString,
                    AllowedEffectSourceString,
                    RequiredEffectSourceString));
            return;
        }

        // If all of the sub-effects of transacts are required, then check for it as a whole instead
        // checking the individual sub-effects, to try to condense the list a bit.
        if (RequiredEffects.HasAll(EffectSets::Transacts))
        {
            if (!AllowedEffects.HasAll(EffectSets::Transacts))
            {
                MissingEffectNames.Add("transacts");
            }
            RequiredEffects &= ~EffectSets::Transacts;
        }

        // Check that each required effect is allowed.
        for (const SEffectInfo& EffectInfo : AllEffectInfos())
        {
            if (RequiredEffects[EffectInfo._Effect] && !AllowedEffects[EffectInfo._Effect])
            {
                MissingEffectNames.Add(EffectInfo._AttributeName);
            }
        }

        // Report missing effects.
        if (MissingEffectNames.Num() == 1)
        {
            AppendGlitch(
                    ErrorNode,
                    Diagnostic,
                    CUTF8String("This %s has the '%s' effect, which is not allowed by %s.", RequiredEffectSourceString, MissingEffectNames[0], AllowedEffectSourceString));
        }
        else if (MissingEffectNames.Num())
        {
            CUTF8StringBuilder MissingEffectListBuilder;
            for (const char* MissingEffectName : MissingEffectNames)
            {
                MissingEffectListBuilder.Append("\n    ");
                MissingEffectListBuilder.Append(MissingEffectName);
            }
            AppendGlitch(
                ErrorNode,
                Diagnostic,
                CUTF8String("This %s has effects that are not allowed by %s:%s", RequiredEffectSourceString, AllowedEffectSourceString, MissingEffectListBuilder.AsCString()));
        }
    }

    //-------------------------------------------------------------------------------------------------
    struct SExplicitParam
    {
        const CExprIdentifierUnresolved* ExprIdentifierUnresolved = nullptr;
        const CExprInvocation* ExprInvocation = nullptr;
        TArray<SExplicitParam> InvocationExplicitParams;
        int32_t InvocationFirstNamedIndex = -1;
        TSPtr<CDataDefinition> DataDefinition;
    };

    //-------------------------------------------------------------------------------------------------
    struct SImplicitParam
    {
        TSPtr<CTypeVariable> TypeVariable;
    };

    //-------------------------------------------------------------------------------------------------
    // This is created prior to analyzing parameters and it accumulates context across successive
    // parameters such as tracking encountered ?named parameters.
    struct SParamsInfo
    {
        CFunction*              _Function;
        bool                    _bConstructor;
        TArray<SExplicitParam>  _ExplicitParams;
        TArray<SImplicitParam>  _ImplicitParams;

        int32_t _FirstNamedIndex = -1;

        // Used to track current index
        int32_t _ExplicitIndex = 0;
        int32_t _ImplicitIndex = 0;

        SParamsInfo(CFunction* Function, bool bConstructor)
            : _Function(Function)
            , _bConstructor(bConstructor)
        {
        }

        void ResetIndices()  { _ExplicitIndex = _ImplicitIndex = 0; }
    };


    CSymbol GenerateUnnamedParamName(const CFunction& Function)
    {
        return GenerateUniqueName("__unnamed_parameter", Function);
    }

    TSPtr<CTypeVariable> CreateImplicitParamTypeVariable(CFunction* Function, CExprDefinition& ParamAst)
    {
        // Note this is similar to `CreateExplicitParamDataDefinition`

        if (!ParamAst.Element())
        {
            // Unnamed
            if (!Function)
            {
                return nullptr;
            }
            return Function->CreateTypeVariable(GenerateUnnamedParamName(*Function), nullptr);
        }

        SDefinitionElementAnalysis ParamAnalysis = TryAnalyzeDefinitionLhs(ParamAst, /*bNameCanHaveAttributes=*/false);
        if (ParamAnalysis.AnalysisResult != EDefinitionElementAnalysisResult::Definition)
        {
            // Assignment or malformed definition
            AppendGlitch(ParamAst, EDiagnostic::ErrSemantic_MalformedParameter, "Parameter is malformed - expected `ParamName:type`.");
            if (!Function)
            {
                return nullptr;
            }
            return Function->CreateTypeVariable(GenerateUnnamedParamName(*Function), nullptr);
        }

        const CExprIdentifierUnresolved* IdentifierAst = ParamAnalysis.IdentifierAst;
        CExpressionBase* Value = ParamAst.Value();

        if (Value)
        {
            AppendGlitch(
                *Value,
                EDiagnostic::ErrSemantic_DefaultMustBeNamed,
                CUTF8String(
                    "Implicit parameter`%s` may not have a default value - not supported.",
                    IdentifierAst->GetErrorDesc().AsCString()));
        }

        if (ParamAnalysis.VarAst)
        {
            AppendGlitch(
                *ParamAnalysis.VarAst,
                EDiagnostic::ErrSemantic_Unsupported,
                "Mutable implicit parameters are unsupported.");
        }

        if (ParamAnalysis.InvocationAst)
        {
            AppendGlitch(
                *ParamAnalysis.InvocationAst,
                EDiagnostic::ErrSemantic_Unsupported,
                "Function implicit parameters are unsupported.");
        }

        if (!Function)
        {
            return nullptr;
        }

        ValidateDefinitionIdentifier(*ParamAnalysis.IdentifierAst, *Function);
        TSRef<CTypeVariable> TypeVariable = Function->CreateTypeVariable(ParamAnalysis.IdentifierAst->_Symbol, nullptr);
        TypeVariable->SetAstNode(&ParamAst);
        RequireUnambiguousDefinition(*TypeVariable, "implicit parameter type variable");

        // Ensure the parameter doesn't have any attributes.
        const TArray<SAttribute>& NameAttributes = ParamAnalysis.IdentifierAst->_Attributes;
        const TArray<SAttribute>& DefAttributes = ParamAst._Attributes;
        if (NameAttributes.Num() || DefAttributes.Num())
        {
            const CExpressionBase* AttributeExpr = DefAttributes.Num() ? DefAttributes[0]._Expression : NameAttributes[0]._Expression;
            AppendGlitch(
                *AttributeExpr,
                EDiagnostic::ErrSemantic_InvalidAttributeScope,
                "Attributes are not allowed on parameters.");
        }

        return Move(TypeVariable);
    }

    void AddImplicitParamTypeVariable(
        CFunction* Function,
        CExprDefinition& ParamAst,
        TArray<SImplicitParam>& ImplicitParams)
    {
        ImplicitParams.Add({CreateImplicitParamTypeVariable(Function, ParamAst)});
    }

    void AddImplicitParamTypeVariableFromExpression(
        CFunction* Function,
        CExpressionBase& ParamAst,
        TArray<SImplicitParam>& ImplicitParams)
    {
        if (ParamAst.GetNodeType() == EAstNodeType::Definition)
        {
            AddImplicitParamTypeVariable(
                Function,
                static_cast<CExprDefinition&>(ParamAst),
                ImplicitParams);
        }
        else
        {
            AppendGlitch(
                ParamAst.GetMappedVstNode(),
                EDiagnostic::ErrSemantic_MalformedImplicitParameter,
                CUTF8String("Implicit parameter is malformed."));
        }
    }

    void AddParamDefinitions(
        SParamsInfo& ParamsInfo,
        const CExprMakeTuple& ParamAst,
        const SExprCtx& ExprCtx)
    {
        for (const TSPtr<CExpressionBase>& SubExpr : ParamAst.GetSubExprs())
        {
            AddParamDefinitionsFromExpression(ParamsInfo, *SubExpr, ExprCtx);
        }
    }

    void AddParamDefinitions(
        SParamsInfo& ParamsInfo,
        const CExprWhere& ParamAst,
        const SExprCtx& ExprCtx)
    {
        AddParamDefinitionsFromExpression(ParamsInfo, *ParamAst.Lhs(), ExprCtx);

        if (ParamsInfo._Function)
        {
            for (const TSPtr<CExpressionBase>& ImplicitParam : ParamAst.Rhs())
            {
                AddImplicitParamTypeVariableFromExpression(
                    ParamsInfo._Function,
                    *ImplicitParam,
                    ParamsInfo._ImplicitParams);
            }
        }
        else
        {
            AppendGlitch(
                ParamAst,
                EDiagnostic::ErrSemantic_Unimplemented,
                "Higher-rank types aren't yet implemented");
        }
    }

    SExplicitParam CreateExplicitParamDataDefinition(SParamsInfo& ParamsInfo, CExprDefinition& ParamAst, const SExprCtx& ExprCtx)
    {
        // Note this is similar to CreateImplicitParamTypeVariable()

        ++ParamsInfo._ExplicitIndex;

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Check for parameter without name
        SExplicitParam Result;
        if (!ParamAst.Element())
        {
            if (ParamsInfo._FirstNamedIndex != -1)
            {
                AppendGlitch(
                    ParamAst,
                    EDiagnostic::ErrSemantic_NamedMustFollowNamed,
                    CUTF8String(
                        "Parameter #%d must be named. Once an earlier parameter is named (prefixed with `?`) any parameters that follow must also be named.",
                        ParamsInfo._ExplicitIndex));
                ParamsInfo._FirstNamedIndex = -1;
            }

            // Unnamed
            if (!ParamsInfo._Function)
            {
                return Result;
            }

            Result.DataDefinition = ParamsInfo._Function->CreateDataDefinition(GenerateUnnamedParamName(*ParamsInfo._Function));
            return Result;
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Examine definition to ensure it is in the correct form
        SDefinitionElementAnalysis ParamAnalysis = TryAnalyzeDefinitionLhs(ParamAst, /*bNameCanHaveAttributes=*/false);
        if (ParamAnalysis.AnalysisResult != EDefinitionElementAnalysisResult::Definition)
        {
            // Assignment or malformed definition
            AppendGlitch(
                ParamAst,
                EDiagnostic::ErrSemantic_MalformedParameter,
                CUTF8String("Parameter #%d is malformed - expected `[?]ParamName:type[= DefaultExpr]`.", ParamsInfo._ExplicitIndex));

            if (!ParamsInfo._Function)
            {
                return Result;
            }
            Result.DataDefinition = ParamsInfo._Function->CreateDataDefinition(GenerateUnnamedParamName(*ParamsInfo._Function));
            return Result;
        }

        const CExprIdentifierUnresolved* IdentifierAst = ParamAnalysis.IdentifierAst;

        Result.ExprIdentifierUnresolved = IdentifierAst;

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Check for named parameter
        if (ParamAst.IsNamed())
        {
            // Track that a named parameter was encountered
            if (ParamsInfo._FirstNamedIndex == -1)
            {
                ParamsInfo._FirstNamedIndex = ParamsInfo._ExplicitIndex - 1;
            }
        }
        else
        {
            if (ParamsInfo._FirstNamedIndex != -1)
            {
                AppendGlitch(
                    ParamAst,
                    EDiagnostic::ErrSemantic_NamedMustFollowNamed,
                    CUTF8String(
                        "Parameter #%d must be named `?%s`. Once an earlier parameter is named (prefixed with `?`) any parameters that follow must also be named.",
                        ParamsInfo._ExplicitIndex,
                        IdentifierAst->GetErrorDesc().AsCString()));
                ParamsInfo._FirstNamedIndex = -1;
            }
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Check for default value
        CExpressionBase* Value = ParamAst.Value();

        if (Value && !ParamAst.IsNamed())
        {
            AppendGlitch(
                ParamAst,
                EDiagnostic::ErrSemantic_DefaultMustBeNamed,
                CUTF8String(
                    "Parameter #%d should be `?%s` with a prefixed question mark indicating it matches with a named argument when specifying a value other than the default.",
                    ParamsInfo._ExplicitIndex,
                    IdentifierAst->GetErrorDesc().AsCString()));

            // For now, pretend that `?` was present to progress analysis
            ParamAst.SetName(ParamAnalysis.IdentifierAst->_Symbol);
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        if (ParamAnalysis.VarAst)
        {
            AppendGlitch(
                *ParamAnalysis.VarAst,
                EDiagnostic::ErrSemantic_Unimplemented,
                CUTF8String(
                    "Parameter #%d %s - mutable parameters are not yet implemented.",
                    ParamsInfo._ExplicitIndex,
                    IdentifierAst->GetErrorDesc().AsCString()));
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        if (ParamAnalysis.InvocationAst)
        {
            Result.ExprInvocation = ParamAnalysis.InvocationAst;
            SParamsInfo InvocationParamsInfo(nullptr, false);
            AddParamDefinitionsFromExpression(InvocationParamsInfo, *ParamAnalysis.InvocationAst->GetArgument(), ExprCtx);
            Result.InvocationExplicitParams = Move(InvocationParamsInfo._ExplicitParams);
            Result.InvocationFirstNamedIndex = InvocationParamsInfo._FirstNamedIndex;
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        if (!ParamsInfo._Function)
        {
            return Result;
        }

        ValidateDefinitionIdentifier(*ParamAnalysis.IdentifierAst, *ParamsInfo._Function);
        Result.DataDefinition = ParamsInfo._Function->CreateDataDefinition(ParamAnalysis.IdentifierAst->_Symbol);
        Result.DataDefinition->_bNamed = ParamAst.IsNamed();
        Result.DataDefinition->SetAstNode(&ParamAst);

        CDataDefinition* ParamDefinition = Result.DataDefinition;

        // Analyze the qualifier of the parameter definition, if any.
        EnqueueDeferredTask(Deferred_Type,
            [this, ParamDefinition, &ParamAst, ParamAnalysis, ExprCtx]()
            {
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &ParamDefinition->_EnclosingScope);
                AnalyzeDefinitionQualifier(ParamAnalysis.IdentifierAst->Qualifier(), *ParamDefinition, ParamAst, ExprCtx);
            });

        RequireUnambiguousDefinition(*ParamDefinition, "explicit parameter data definition");

        // Ensure the parameter doesn't have any attributes.
        const TArray<SAttribute>& NameAttributes = ParamAnalysis.IdentifierAst->_Attributes;
        const TArray<SAttribute>& DefAttributes = ParamAst._Attributes;
        if (NameAttributes.Num() || DefAttributes.Num())
        {
            const CExpressionBase* AttributeExpr = DefAttributes.Num() ? DefAttributes[0]._Expression : NameAttributes[0]._Expression;
            AppendGlitch(
                *AttributeExpr,
                EDiagnostic::ErrSemantic_InvalidAttributeScope,
                "Attributes are not allowed on parameters.");
        }

        return Result;
    }

    void AddParamDefinitionsFromExpression(
        SParamsInfo& ParamsInfo,
        CExpressionBase& ParamAst,
        const SExprCtx& ExprCtx)
    {
        if (ParamAst.GetNodeType() == EAstNodeType::Invoke_MakeTuple)
        {
            AddParamDefinitions(ParamsInfo, static_cast<const CExprMakeTuple&>(ParamAst), ExprCtx);
        }
        else if (ParamAst.GetNodeType() == EAstNodeType::Definition_Where)
        {
            AddParamDefinitions(ParamsInfo, static_cast<CExprWhere&>(ParamAst), ExprCtx);
        }
        else if (ParamAst.GetNodeType() == EAstNodeType::Definition)
        {
            ParamsInfo._ExplicitParams.Add(CreateExplicitParamDataDefinition(ParamsInfo, static_cast<CExprDefinition&>(ParamAst), ExprCtx));
        }
        else
        {
            AppendGlitch(
                FindMappedVstNode(ParamAst),
                EDiagnostic::ErrSemantic_MalformedParameter,
                CUTF8String("Parameter is malformed."));
        }
    }

    void AnalyzeImplicitParam(
        CExprDefinition& ParamAst,
        const TArray<SImplicitParam>& Params,
        int32_t& ParamOffset)
    {
        const CTypeBase* NegativeParamType;
        const CTypeBase* PositiveParamType;
        if (ParamAst.ValueDomain())
        {
            if (TSPtr<CExpressionBase> NewTypeAst = AnalyzeExpressionAst(
                ParamAst.ValueDomain().AsRef(),
                SExprCtx::Default().WithResultIsUsedAsType()))
            {
                ParamAst.SetValueDomain(Move(NewTypeAst).AsRef());
            }
            STypeTypes ParamTypes = GetTypeTypes(*ParamAst.ValueDomain());
            NegativeParamType = ParamTypes._NegativeType;
            PositiveParamType = ParamTypes._PositiveType;
            if (NegativeParamType->GetNormalType().GetKind() != Cases<ETypeKind::Type, ETypeKind::Unknown> ||
                PositiveParamType->GetNormalType().GetKind() != Cases<ETypeKind::Type, ETypeKind::Unknown>)
            {
                AppendGlitch(
                    ParamAst,
                    EDiagnostic::ErrSemantic_Unimplemented,
                    "Implicit parameters of non-`type` and non-`subtype` type aren't yet implemented.");
                NegativeParamType = _Program->GetDefaultUnknownType();
                PositiveParamType = _Program->GetDefaultUnknownType();
            }
        }
        else
        {
            AppendGlitch(
                ParamAst,
                EDiagnostic::ErrSemantic_MalformedImplicitParameter,
                CUTF8String("Implicit parameter missing type."));
            NegativeParamType = _Program->GetDefaultUnknownType();
            PositiveParamType = _Program->GetDefaultUnknownType();
        }
        if (ParamAst.Element())
        {
            ParamAst.Element()->SetResultType(PositiveParamType);
        }
        ParamAst.SetResultType(PositiveParamType);
        if (const TSPtr<CTypeVariable>& TypeVariable = Params[ParamOffset].TypeVariable)
        {
            TypeVariable->_NegativeType = NegativeParamType;
            TypeVariable->SetType(PositiveParamType);
        }
        ++ParamOffset;
    }

    void AnalyzeImplicitParamExpression(
        CExpressionBase& ParamAst,
        const TArray<SImplicitParam>& Params,
        int32_t& ParamOffset)
    {
        if (ParamAst.GetNodeType() == EAstNodeType::Definition)
        {
            AnalyzeImplicitParam(
                static_cast<CExprDefinition&>(ParamAst),
                Params,
                ParamOffset);
        }
    }

    struct SParamType
    {
        const CTypeBase* _NegativeType;
        const CTypeBase* _PositiveType;
    };

    SParamType AnalyzeParam(
        CExprMakeTuple& ParamAst,
        SParamsInfo& ParamsInfo)
    {
        const TSPtrArray<CExpressionBase>& SubExprs = ParamAst.GetSubExprs();
        int32_t NumSubExprs = SubExprs.Num();
        CTupleType::ElementArray NegativeElementTypes;
        NegativeElementTypes.Reserve(NumSubExprs);
        CTupleType::ElementArray PositiveElementTypes;
        PositiveElementTypes.Reserve(NumSubExprs);
        int32_t FirstNamedIndex = NumSubExprs;
        for (int32_t SubExprIndex = 0; SubExprIndex < SubExprs.Num(); ++SubExprIndex)
        {
            TSRef<CExpressionBase> SubExpr = SubExprs[SubExprIndex].AsRef();
            SParamType ElementType = AnalyzeParamExpression(SubExpr, ParamsInfo);
            NegativeElementTypes.Add(ElementType._NegativeType);
            PositiveElementTypes.Add(ElementType._PositiveType);

            // Compute FirstNamedIndex for the tuple itself, rather than using ParamsInfo._FirstNamedIndex,
            // which is computed in terms of the overall flattened list of parameters.
            if (ElementType._PositiveType->AsNamedType() != nullptr)
            {
                if (FirstNamedIndex == NumSubExprs)
                {
                    FirstNamedIndex = SubExprIndex;
                }
            }
        }
        CTupleType& NegativeParamType = _Program->GetOrCreateTupleType(Move(NegativeElementTypes), FirstNamedIndex);
        CTupleType& PositiveParamType = _Program->GetOrCreateTupleType(Move(PositiveElementTypes), FirstNamedIndex);
        ParamAst.SetResultType(&PositiveParamType);
        return {&NegativeParamType, &PositiveParamType};
    }

    SParamType AnalyzeParam(
        CExprWhere& ParamAst,
        SParamsInfo& ParamsInfo)
    {
        SParamType ParamType = AnalyzeParamExpression(ParamAst.Lhs().AsRef(), ParamsInfo);
        if (ParamsInfo._Function)
        {
            for (const TSPtr<CExpressionBase>& ImplicitParam : ParamAst.Rhs())
            {
                AnalyzeImplicitParamExpression(
                    *ImplicitParam,
                    ParamsInfo._ImplicitParams,
                    ParamsInfo._ImplicitIndex);
            }
        }
        ParamAst.SetResultType(ParamType._PositiveType);
        return ParamType;
    }

    SParamType AnalyzeParam(
        TSRef<CExprDefinition> ParamAst,
        SParamsInfo& ParamsInfo)
    {
        SExplicitParam& ExplicitParam = ParamsInfo._ExplicitParams[ParamsInfo._ExplicitIndex];
        const CTypeBase* NegativeParamType;
        const CTypeBase* PositiveParamType;
        if (ParamAst->ValueDomain())
        {
            if (TSPtr<CExpressionBase> NewTypeAst = AnalyzeExpressionAst(
                ParamAst->ValueDomain().AsRef(),
                SExprCtx::Default().WithResultIsUsedAsType()))
            {
                ParamAst->SetValueDomain(Move(NewTypeAst).AsRef());
            }
            STypeTypes ParamTypes = GetTypeTypes(*ParamAst->ValueDomain());
            NegativeParamType = ParamTypes._NegativeType;
            PositiveParamType = ParamTypes._PositiveType;
        }
        else
        {
            AppendGlitch(
                *ParamAst,
                EDiagnostic::ErrSemantic_MalformedParameter,
                CUTF8String("Parameter missing type."));
            const CUnknownType* UnknownType = _Program->GetDefaultUnknownType();
            NegativeParamType = UnknownType;
            PositiveParamType = UnknownType;
        }

        if (ExplicitParam.ExprInvocation)
        {
            SParamsInfo InvocationParamsInfo(nullptr, false);
            InvocationParamsInfo._ExplicitParams = Move(ExplicitParam.InvocationExplicitParams);
            InvocationParamsInfo._FirstNamedIndex = ExplicitParam.InvocationFirstNamedIndex;
            SParamType ParamsType = AnalyzeParamExpression(ExplicitParam.ExprInvocation->GetArgument().AsRef(), InvocationParamsInfo);
            ExplicitParam.InvocationExplicitParams = Move(InvocationParamsInfo._ExplicitParams);
            const CTypeBase* NegativeReturnType = NegativeParamType;
            const CTypeBase* PositiveReturnType = PositiveParamType;
            const SEffectSet ParamFunctionEffects = GetEffectsFromAttributes(*ExplicitParam.ExprInvocation, EffectSets::FunctionDefault);
            NegativeParamType = &_Program->GetOrCreateFunctionType(
                *ParamsType._PositiveType,
                *NegativeReturnType,
                ParamFunctionEffects);
            PositiveParamType = &_Program->GetOrCreateFunctionType(
                *ParamsType._NegativeType,
                *PositiveReturnType,
                ParamFunctionEffects);
            ExplicitParam.ExprInvocation->GetCallee()->SetResultType(PositiveParamType);
        }

        if (ParamAst->Element())
        {
            ParamAst->Element()->SetResultType(PositiveParamType);
        }

        if (const TSPtr<CDataDefinition>& DataDefinition = ExplicitParam.DataDefinition)
        {
            const CNormalType& NegativeNormalType = NegativeParamType->GetNormalType();
            const CNormalType& PositiveNormalType = PositiveParamType->GetNormalType();
            // Must use `IsA` rather than `IsSubtype`.  `CTypeVariable`s aren't
            // yet fully initialized and cannot yet be inspected by `IsSubtype`.
            if (NegativeNormalType.IsA<CTypeType>() && PositiveNormalType.IsA<CTypeType>())
            {
                // Rewrite
                // @code
                // F(..., t:type, ..., :t, ...):..., t, ...
                // @endcode
                // to
                // @code
                // F(..., :type(u, u), ..., :t, ... where u:type, t:type):..., t, ...
                // @endcode
                // Uses of `t` inside `F` will use type variable `t`.  `u` is
                // bounded below by the original lower bound and above by `t`.
                // `t` is bounded below by `u` and above by the original upper
                // bound.  `CTypeVariable` instantiation results in behavior equivalent to
                // @code
                // F(..., :type(u, t), ..., :u, ... where u:type, t:type):..., t, ...
                // @endcode
                // However, we must be clever here such that references to `t`
                // in `F` bind to the upper bound, while also ensuring that
                // instantiation can tell if `u` should be used (for negative
                // uses) or `t` should be used (for positive uses) without
                // manually substituting in `F` (instead, doing such
                // substitution in instantiation).  Note the reference to `t` in
                // `type` is a negative use.
                AssertConstrain(&_Program->GetOrCreateTypeType(&_Program->_anyType, &_Program->_falseType), NegativeParamType);
                AssertConstrain(PositiveParamType, _Program->_typeType);

                const CTypeType& TypeVariableNegativeType = NegativeNormalType.AsChecked<CTypeType>();
                const CTypeType& TypeVariableType = PositiveNormalType.AsChecked<CTypeType>();

                // `t` is bounded below by `u`, above by the original upper
                // bound.  However, this trips up the recursive type check.
                // Luckily, only one - either `t` or `u` - need mention the
                // constraint `u <= t`.
                TSRef<CTypeVariable> TypeVariable = ParamsInfo._Function->CreateTypeVariable(
                    DataDefinition->GetName(),
                    &TypeVariableType);
                TypeVariable->_NegativeType = &TypeVariableNegativeType;
                TypeVariable->SetAstNode(ParamAst.Get());
                ParamsInfo._ImplicitParams.Add({ TypeVariable });

                // `u` is bounded below by the original lower bound, above by `t`.
                const CTypeType& NegativeTypeVariableNegativeType = _Program->GetOrCreateTypeType(
                    TypeVariableType.NegativeType(),
                    TypeVariable);
                const CTypeType& NegativeTypeVariableType = _Program->GetOrCreateTypeType(
                    TypeVariableNegativeType.NegativeType(),
                    TypeVariable);
                TSRef<CTypeVariable> NegativeTypeVariable = ParamsInfo._Function->CreateTypeVariable(
                    GenerateUnnamedParamName(*ParamsInfo._Function),
                    &NegativeTypeVariableType);
                NegativeTypeVariable->_NegativeType = &NegativeTypeVariableNegativeType;
                NegativeTypeVariable->SetAstNode(ParamAst.Get());
                ParamsInfo._ImplicitParams.Add({NegativeTypeVariable});

                DataDefinition->SetName(GenerateUnnamedParamName(*ParamsInfo._Function));
                RequireUnambiguousDefinition(*TypeVariable, "type parameter");

                DataDefinition->_ImplicitParam = TypeVariable.Get();
                TypeVariable->_ExplicitParam = DataDefinition.Get();
                TypeVariable->_NegativeTypeVariable = NegativeTypeVariable;
                NegativeTypeVariable->_ExplicitParam = DataDefinition.Get();

                NegativeParamType = &_Program->GetOrCreateTypeType(NegativeTypeVariable.Get(), NegativeTypeVariable.Get());
                PositiveParamType = NegativeParamType;
            }
            else if (!ParamsInfo._bConstructor
                && !ParamsInfo._Function->GetReturnTypeAst()
                && ParamAst->ValueDomain())
            {
                AppendGlitch(
                    *ParamAst->ValueDomain(),
                    EDiagnostic::ErrSemantic_Unimplemented,
                    "Parameters of a function without a specified return type must be of type `type`.");
            }

            ExplicitParam.DataDefinition->_NegativeType = NegativeParamType;
            ExplicitParam.DataDefinition->SetType(PositiveParamType);
        }

        ParamAst->SetResultType(PositiveParamType);

        if (ParamAst->Value())
        {
            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Defer analysis of parameter default expression values until after more types, data and
            // function signatures are analyzed.
            // [Same time as data member defaults and before function body analysis.]
            EnqueueDeferredTask(Deferred_NonFunctionExpressions, [this, ParamAst, ParamDefinition = ExplicitParam.DataDefinition, NegativeParamType]
            {
                if (ParamDefinition) { _Context._DataMembers.Push(ParamDefinition); }
                TGuard DataMembersGuard([this, ParamDefinition] { if (ParamDefinition) { _Context._DataMembers.Pop(); } });

                // Analyze the value expression.
                if (TSPtr<CExpressionBase> NewValueAst = AnalyzeExpressionAst(ParamAst->Value().AsRef(), SExprCtx::Default().WithResultIsUsed(NegativeParamType)))
                {
                    ParamAst->SetValue(Move(NewValueAst.AsRef()));
                }

                if (TSPtr<CExpressionBase> NewValue = ApplyTypeToExpression(
                    *NegativeParamType,
                    ParamAst->Value().AsRef(),
                    EDiagnostic::ErrSemantic_IncompatibleArgument,
                    "This parameter expects to have a default of", "this default value"))
                {
                    ParamAst->SetValue(Move(NewValue.AsRef()));
                }
            });
        }

        if (ParamAst->IsNamed())
        {
            NegativeParamType = &_Program->GetOrCreateNamedType(ParamAst->GetName(), NegativeParamType, ParamAst->Value());
            PositiveParamType = &_Program->GetOrCreateNamedType(ParamAst->GetName(), PositiveParamType, ParamAst->Value());
        }

        ++ParamsInfo._ExplicitIndex;
        return {NegativeParamType, PositiveParamType};
    }

    SParamType AnalyzeParamExpression(
        const TSRef<CExpressionBase>& ParamAst,
        SParamsInfo& ParamsInfo)
    {
        if (ParamAst->GetNodeType() == EAstNodeType::Invoke_MakeTuple)
        {
            return AnalyzeParam(static_cast<CExprMakeTuple&>(*ParamAst), ParamsInfo);
        }
        else if (ParamAst->GetNodeType() == EAstNodeType::Definition_Where)
        {
            return AnalyzeParam(static_cast<CExprWhere&>(*ParamAst), ParamsInfo);
        }
        else if (ParamAst->GetNodeType() == EAstNodeType::Definition)
        {
            return AnalyzeParam(ParamAst.As<CExprDefinition>(), ParamsInfo);
        }
        const CUnknownType* UnknownType = _Program->GetDefaultUnknownType();
        return {UnknownType, UnknownType};
    }

    CSymbol GetFunctionBodyMacroName(const CAstNode& AstNode, const CFunction& Function)
    {
        CUTF8StringBuilder Builder;
        Builder.Append(Function.AsNameStringView());
        Builder.Append('(');
        char const* Separator = "";
        for (const CDataDefinition* Param : Function._Signature.GetParams())
        {
            CUTF8StringView ParamName;
            if (const CTypeVariable* ImplicitParam = Param->_ImplicitParam)
            {
                ParamName = ImplicitParam->AsNameStringView();
            }
            else
            {
                ParamName = Param->AsNameStringView();
            }
            Builder.Append(Separator);
            Builder.Append(ParamName);
            Separator = ",";
        }
        Builder.Append(')');
        return VerifyAddSymbol(AstNode, Builder.MoveToString());
    }

    void ValidateConstructorFunctionBody(const CFunction& Function)
    {
        const CTypeBase& NegativeReturnType = Function._NegativeType->GetReturnType();
        const CTypeBase& PositiveReturnType = Function._Signature.GetFunctionType()->GetReturnType();
        const TSPtr<CExpressionBase>& Value = Function.GetAstNode()->Value();
        if (_Context._Scope->GetPackage()->_Role == EPackageRole::External)
        {
            if (Value->GetNodeType() != EAstNodeType::External)
            {
                AppendGlitch(*Function.GetAstNode(), EDiagnostic::ErrSemantic_ExpectedExternal);
            }
        }
        else if (Value->GetNodeType() == EAstNodeType::Invoke_ArchetypeInstantiation)
        {
            const CTypeBase* ResultType = Value->GetResultType(*_Program);
            if (const CClass* ResultClass = ResultType->GetNormalType().AsNullable<CClass>())
            {
                if (!Constrain(&PositiveReturnType, ResultClass->_NegativeClass) || !Constrain(ResultClass, &NegativeReturnType))
                {
                    AppendGlitch(*Value, EDiagnostic::ErrSemantic_ConstructorFunctionBodyResultType);
                }
            }
        }
        else
        {
            AppendGlitch(*Value, EDiagnostic::ErrSemantic_ConstructorFunctionBody);
        }
        if (const CClass* ReturnClassType = PositiveReturnType.GetNormalType().AsNullable<CClass>())
        {
            if (ReturnClassType->IsStruct())
            {
                AppendGlitch(
                    *Value,
                    EDiagnostic::ErrSemantic_Unimplemented,
                    "Struct constructor functions are not yet implemented.");
            }
        }
    }

    void ValidateFunctionBody(const CFunction& Function)
    {
        if (Function.GetReturnTypeAst())
        {
            if (_Context._Scope->GetPackage()->_Role == EPackageRole::External)
            {
                // If this package is external the function body must be a single external{} macro
                if (Function.GetAstNode()->Value()->GetNodeType() != EAstNodeType::External)
                {
                    AppendGlitch(*Function.GetAstNode(), EDiagnostic::ErrSemantic_ExpectedExternal);
                }
            }
            else
            {
                // Validate control flow before inserting possibly unreachable functor applications
                // for the implicit return.
                SConditionalSkipFlags SkipFlags = ValidateControlFlow(Function.GetAstNode()->Value());

                if (SkipFlags._Unconditional == ESkipFlags::Return)
                {
                    // This means that the expression at the end of the program returns unconditionally, like:
                    //
                    //     foo(x:int):int =
                    //         loop:
                    //             if (bar[x]):
                    //                 return x
                    //
                    // The loop expression returns void, so if we try to validate its type, we will be sad.
                    // But fortunately we have detected that the only way out of the loop is that it returns
                    // prematurely. In this case, we can ignore the loop body expression's type.
                }
                else
                {
                    // Validate the body's result against the return type.
                    if (TSPtr<CExpressionBase> NewBodyAst = ApplyTypeToExpression(
                        Function._NegativeType->GetReturnType(),
                        Function.GetAstNode()->Value().AsRef(),
                        EDiagnostic::ErrSemantic_IncompatibleReturnValue,
                        "This function returns", "the function body's result"))
                    {
                        Function.GetAstNode()->SetValue(Move(NewBodyAst.AsRef()));
                    }
                }
            }
        }
        else
        {
            const TSPtr<CExpressionBase>& Value = Function.GetAstNode()->Value();

            ValidateControlFlow(Value);
            
            ConstrainExpressionToType(
                Value.AsRef(),
                Function._NegativeType->GetReturnType(),
                EDiagnostic::ErrSemantic_IncompatibleReturnValue,
                "This function returns", "the function body's result");

            if (MaybeTypeTypes(*Value)._Tag == ETypeTypesTag::NotType)
            {
                // Only allow eliding the return type if this function evaluates
                // to a type - i.e., is a parametric type.
                AppendGlitch(
                    *Function.GetDefineeAst(),
                    EDiagnostic::ErrSemantic_InvalidReturnType,
                    "Missing return type for function.");
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeFunctionDefinition(CExprDefinition& DefinitionAst, const SDefinitionElementAnalysis& ElementAnalysis, const SExprCtx& InExprCtx)
    {
        // All definitions are of the form: Element:ValueDomain=Value, Element:ValueDomain, or Element=Value
        // For a function definition 'f(x:Int):Int={return x*x}' this looks as follows:
        //      Element      -   f(x:Int)
        //      ValueDomain  -   :Int
        //      Value        -   {return x*x}
        //
        // A valid function definition requires an Element, and either ValueDomain (aka Type), Value(aka Body), or both
        // e.g. f(x:Int)={return x*x}
        // e.g. <@native>f(x:Int):Void

        if (ElementAnalysis.VarAst)
        {
            AppendGlitch(
                DefinitionAst,
                EDiagnostic::ErrSemantic_Unimplemented,
                "Function mutable variables with `var F()` syntax are not yet implemented.");
        }

        if(!InExprCtx.bAllowReservedUnderscoreFunctionIdentifier || !(ElementAnalysis.IdentifierSymbol.AsStringView() == "_"))
        {
            RequireNonReservedSymbol(*ElementAnalysis.IdentifierAst, ElementAnalysis.IdentifierSymbol);
        }

        // if we end up passing this ExprCtx along further, it should be done without the
        // underscore allowance flag set. hoping this whole flag is very temporary
        SExprCtx ExprCtx = InExprCtx.DisallowReservedUnderscoreFunctionIdentifier();

        TSRef<Vst::Node> const DefVst = const_cast<Vst::Node*>(DefinitionAst.GetMappedVstNode())->AsShared();
        // Peek at the VST attributes to determine pieces important to the signature.
        ULANG_ASSERTF(ElementAnalysis.InvocationAst, "Expected an invocation in the element of a function definition");

        bool bConstructor = static_cast<bool>(GetBuiltInAttributeHack(
            *ElementAnalysis.IdentifierAst,
            _Program->_constructorClass,
            true));

        SEffectSet DefaultEffects;
        if (bConstructor || DefinitionAst.ValueDomain())
        {
            DefaultEffects = EffectSets::FunctionDefault;
        }
        else 
        {
            DefaultEffects = EffectSets::Computes;
        }

        const SEffectSet Effects = GetEffectsFromAttributes(*ElementAnalysis.InvocationAst, DefaultEffects);

        if (_Context._Scope->IsControlScope())
        {
            AppendGlitch(
                DefinitionAst,
                EDiagnostic::ErrSemantic_Unimplemented,
                CUTF8String("Functions declared at this scope are not supported."));
        }

        // Not the same as ElementAnalysis.IdentifierSymbol if this is an extension method
        CSymbol FuncName = ElementAnalysis.IdentifierAst->_Symbol;
        TSRef<CFunction> Function = _Context._Scope->CreateFunction(FuncName);

        TGuardValue<TArray<const CDefinition*>> DefinitionsGuard(_Context._EnclosingDefinitions);
        _Context._EnclosingDefinitions.Add(Function.Get());

        if (ElementAnalysis.IdentifierSymbol != FuncName)
        {
            Function->_ExtensionFieldAccessorKind = EExtensionFieldAccessorKind::ExtensionMethod;
        }

        _Context._Scope->CreateNegativeFunction(*Function);

        Function->SetRevision(_NextRevision);
        TArray<SAttribute> NameAttributes = Move(ElementAnalysis.IdentifierAst->_Attributes);
        TArray<SAttribute> DefAttributes = Move(DefinitionAst._Attributes);

        // Create a CExprFunctionDefinition to replace the CExprDefinition.
        TSRef<CExprFunctionDefinition> FunctionDefinitionAst = TSRef<CExprFunctionDefinition>::New(
            Function,
            DefinitionAst.TakeElement(),
            DefinitionAst.TakeValueDomain(),
            DefinitionAst.TakeValue());
        ULANG_ASSERTF(Function->GetDefineeAst(), "This should have been set above. If this is null in some lambda it's probably because that lambda isn't capturing FunctionDefinitionAst");
        ReplaceMapping(DefinitionAst, *FunctionDefinitionAst);

        DeferredRequireOverrideDoesntChangeAccessLevel(FunctionDefinitionAst.As<CExpressionBase>(), *Function);

        if (NameAttributes.Num() || DefAttributes.Num() || !Effects[EEffect::diverges])
        {
            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Queue up process to validate function attributes
            EnqueueDeferredTask(Deferred_ValidateAttributes, [this, Function, FunctionDefinitionAst, Effects]
            {
                CScope::EKind ScopeKind = Function->_EnclosingScope.GetLogicalScope().GetKind();
                const bool bHasNativeAttribute = Function->IsNative();
                const bool bHasNativeCallAttribute = Function->HasAttributeClass(_Program->_nativeCallClass, *_Program);
                const bool bHasConstructorAttribute = Function->HasAttributeClass(_Program->_constructorClass, *_Program);

                if (ScopeKind == CScope::EKind::Class)
                {
                    // If the function has the native or native_call attribute, verify that the parent class also has the native attribute.
                    if (bHasNativeAttribute || bHasNativeCallAttribute)
                    {
                        const CClass& ScopeClass = static_cast<const CClass&>(Function->_EnclosingScope.GetLogicalScope());
                        if (!ScopeClass.IsNative())
                        {
                            AppendGlitch(*FunctionDefinitionAst, EDiagnostic::ErrSemantic_NativeMemberOfNonNativeClass);
                        }
                    }
                }
                else if (ScopeKind == CScope::EKind::Interface)
                {
                    if (bHasNativeAttribute)
                    {
                        AppendGlitch(*FunctionDefinitionAst, EDiagnostic::ErrSemantic_Unimplemented, "Interface functions cannot be marked as `<native>`.");
                    }
                }

                if (bHasNativeAttribute)
                {
                    // If function is native, check that all struct parameters/return values are also native
                    for (const CDataDefinition* Parameter : Function->_Signature.GetParams())
                    {
                        // TODO - Should use DataMember->GetAstNode() but its _AstNode is not set
                        ValidateTypeIsNative(Parameter->GetType(), EValidateTypeIsNativeContext::Parameter, *FunctionDefinitionAst);
                    }

                    ValidateTypeIsNative(Function->_Signature.GetReturnType(), EValidateTypeIsNativeContext::Parameter, *FunctionDefinitionAst);
                }
                else
                {
                    // Don't allow the converges effect on non-native functions.
                    if (!Effects[EEffect::diverges])
                    {
                        AppendGlitch(*FunctionDefinitionAst, EDiagnostic::ErrSemantic_InvalidEffectDeclaration, "The 'converges' effect is only allowed on native functions.");
                    }
                }

                if (bHasConstructorAttribute)
                {
                    if (bHasNativeCallAttribute)
                    {
                        AppendGlitch(
                            *FunctionDefinitionAst,
                            EDiagnostic::ErrSemantic_AttributeNotAllowed,
                            "Constructor functions cannot be marked `<native_callable>`.");
                    }
                    if (Effects[EEffect::suspends])
                    {
                        AppendGlitch(
                            *FunctionDefinitionAst,
                            EDiagnostic::ErrSemantic_AttributeNotAllowed,
                            "Constructor functions cannot be marked `<suspends>`.");
                    }
                    if (!Function->_EnclosingScope.IsModuleOrSnippet())
                    {
                        AppendGlitch(
                            *FunctionDefinitionAst,
                            EDiagnostic::ErrSemantic_AttributeNotAllowed,
                            "Only module functions may be marked <constructor>.");
                    }
                }
            });
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Queue up job that processes the qualifier, parameters, and return type
        EnqueueDeferredTask(Deferred_Type, [this, bConstructor, Effects, Function, FunctionDefinitionAst, ElementAnalysis, ExprCtx]
        {
            TGuardValue<const CFunction*> CurrentFunctionGuard(_Context._Function, Function);
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &Function->_EnclosingScope);

            //
            // Analyze explicit qualifier
            //
            AnalyzeDefinitionQualifier(
                ElementAnalysis.IdentifierAst->Qualifier(),
                *Function,
                *FunctionDefinitionAst,
                ExprCtx);

            //
            // Process parameters
            //
            ULANG_ASSERTF(ElementAnalysis.InvocationAst, "Expected an invocation in the element of a function definition");
            const CExprInvocation& DefinitionInvocation = *ElementAnalysis.InvocationAst;
            ULANG_ASSERTF(Function->GetDefinitions().Num() == 0, "Expected function parameters to start at ordinal 0");

            SParamsInfo ParamsInfo(Function, bConstructor);
            AddParamDefinitionsFromExpression(ParamsInfo, *DefinitionInvocation.GetArgument(), ExprCtx);

            const CTypeBase* NegativeParamType;
            const CTypeBase* PositiveParamType;
            {
                TGuardValue<CScope*> FunctionScopeGuard(_Context._Scope, Function);
                ParamsInfo.ResetIndices();
                SParamType ParamType = AnalyzeParamExpression(DefinitionInvocation.GetArgument().AsRef(), ParamsInfo);
                NegativeParamType = ParamType._NegativeType;
                PositiveParamType = ParamType._PositiveType;
            }

            // Add implicit parameters
            TArray<const CTypeVariable*> TypeVariables;
            TypeVariables.Reserve(ParamsInfo._ImplicitParams.Num());
            for (const SImplicitParam& ImplicitParam : ParamsInfo._ImplicitParams)
            {
                TypeVariables.Add(ImplicitParam.TypeVariable.Get());
            }

            // Process return type
            const CTypeBase* NegativeReturnType;
            const CTypeBase* PositiveReturnType;
            if (Function->GetReturnTypeAst())
            {
                {
                    TGuardValue<CScope*> FunctionScopeGuard(_Context._Scope, Function);
                    if (TSPtr<CExpressionBase> NewReturnTypeAst = AnalyzeExpressionAst(Function->GetReturnTypeAst().AsRef(), SExprCtx::Default().WithResultIsUsedAsType()))
                    {
                        Function->GetAstNode()->SetValueDomain(Move(NewReturnTypeAst.AsRef()));
                    }
                }
                if (Function->GetReturnTypeAst()->GetNodeType() == EAstNodeType::Flow_CodeBlock)
                {
                    AppendGlitch(
                        *Function->GetReturnTypeAst(),
                        EDiagnostic::ErrSemantic_MultipleReturnValuesUnsupported,
                        CUTF8String("Multiple return values are not yet supported for function %s.", Function->AsNameCString()));
                }
                STypeTypes ReturnTypes = GetTypeTypes(*Function->GetReturnTypeAst());
                NegativeReturnType = ReturnTypes._NegativeType;
                PositiveReturnType = ReturnTypes._PositiveType;

                // @HACK: SOL-972, need better (fuller) support for attributes
                EnqueueDeferredTask(Deferred_ValidateType, [this, Context = _Context, PositiveReturnType, Function]
                {
                    TGuardValue ContextGuard(_Context, Context);
                    if (_Context._Scope->GetKind() == CScope::EKind::Class && !static_cast<const CClass*>(_Context._Scope)->IsClass(*_Program->_attributeClass))
                    {
                        ValidateNonAttributeType(PositiveReturnType, Function->GetReturnTypeAst()->GetMappedVstNode());
                    }
                });

                if (PositiveReturnType->GetNormalType().IsA<CLogicType>() && Effects[EEffect::decides])
                {
                    AppendGlitch(
                        *Function->GetDefineeAst(),
                        EDiagnostic::ErrSemantic_InvalidReturnType,
                        CUTF8String("Function `%s` returns `logic` and can also fail. This combination is not allowed for semantic clarity.", Function->AsNameCString()));
                }
            }
            else
            {
                const CFlowType& NegativeFlowType = _Program->CreateNegativeFlowType();
                const CFlowType& PositiveFlowType = _Program->CreatePositiveFlowType();
                NegativeFlowType.AddFlowEdge(&PositiveFlowType);
                PositiveFlowType.AddFlowEdge(&NegativeFlowType);
                NegativeReturnType = &NegativeFlowType;
                PositiveReturnType = &PositiveFlowType;
            }

            // Validate existence of return type for constructors.
            EnqueueDeferredTask(Deferred_ValidateAttributes, [this, Context = _Context, Function, FunctionDefinitionAst]
            {
                TGuardValue ContextGuard(_Context, Context);
                if (Function->HasAttributeClass(_Program->_constructorClass, *_Program))
                {
                    if (_Context._Scope->GetPackage()->_Role == EPackageRole::External)
                    {
                        if (!Function->GetReturnTypeAst())
                        {
                            AppendGlitch(
                                *FunctionDefinitionAst,
                                EDiagnostic::ErrSemantic_InvalidReturnType,
                                "External constructor functions must declare a return type.");
                        }
                    }
                    else if (Function->IsNative())
                    {
                        if (!Function->GetReturnTypeAst())
                        {
                            AppendGlitch(
                                *FunctionDefinitionAst,
                                EDiagnostic::ErrSemantic_InvalidReturnType,
                                "Native constructor functions must declare a return type.");
                        }
                    }
                    else if (Function->GetReturnTypeAst())
                    {
                        AppendGlitch(
                            *FunctionDefinitionAst,
                            EDiagnostic::ErrSemantic_InvalidReturnType,
                            "Constructor functions must not declare a return type.");
                    }
                }
            });

            Function->GetDefineeAst()->SetResultType(PositiveReturnType);

            // Require that the function definitions is unambiguous.
            RequireUnambiguousDefinition(*Function, "function");

            const CFunctionType* NegativeFunctionType = &_Program->GetOrCreateFunctionType(
                *PositiveParamType,
                *NegativeReturnType,
                Effects,
                TArray<const CTypeVariable*>(TypeVariables));

            const CFunctionType* PositiveFunctionType = &_Program->GetOrCreateFunctionType(
                *NegativeParamType,
                *PositiveReturnType,
                Effects,
                Move(TypeVariables));

            if (!PositiveFunctionType->GetTypeVariables().IsEmpty())
            {
                EnqueueDeferredTask(Deferred_ValidateType, [this, PositiveFunctionType, FunctionDefinitionAst]
                {
                    ValidateFunctionTypeVariables(*PositiveFunctionType, *FunctionDefinitionAst);
                });
            }

            Function->_NegativeType = NegativeFunctionType;

            Function->MapSignature(*PositiveFunctionType, _NextRevision);

            // Set the result type for the function definition and definition element AST nodes.
            FunctionDefinitionAst->SetResultType(PositiveFunctionType);

            ElementAnalysis.IdentifierAst->SetResultType(PositiveFunctionType);
        });
        
        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Queue up job that processes any function attributes, or lack of them
        EnqueueDeferredTask(Deferred_Attributes, [this, DefVst, Function, NameAttributes, DefAttributes]
        {
            // Not inside the function yet
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &Function->_EnclosingScope);

            {
                // For function attributes, we need to ensure the function pointer is in the context through various deferred tasks
                // in order to handle parametric attribute type functions.
                TGuardValue<const CFunction*> CurrentFunctionGuard(_Context._Function, Function);
                Function->_Attributes = AnalyzeNameAndDefAttributes(NameAttributes, DefAttributes, CAttributable::EAttributableScope::Function);
            }

            Function->SetAccessLevel(GetAccessLevelFromAttributes(*Function->GetDefineeAst()->GetMappedVstNode(), *Function));
            ValidateExperimentalAttribute(*Function);
            AnalyzeFinalAttribute(*Function->GetDefineeAst(), *Function);

            // This relies on the fact that we process bodies of functions that have a single
            // class as their body inside Deferred_Type stage, and therefore we have a body
            // for such functions by the Deferred_Attributes stage.

            CDefinition* AttributePropagationTarget = nullptr;
            if (TSPtr<CExprClassDefinition> InnerClass = Function->GetBodyClassDefinitionAst())
            {
                AttributePropagationTarget = InnerClass->_Class._Definition;
            }
            else if (TSPtr<CExprInterfaceDefinition> InnerInterface = Function->GetBodyInterfaceDefinitionAst())
            {
                AttributePropagationTarget = &InnerInterface->_Interface;
            }

            // Push all our attributes from parametric functions to target class types
            if (AttributePropagationTarget)
            {
                EnqueueDeferredTask(Deferred_PropagateAttributes, [this, Function, AttributePropagationTarget]() mutable
                {
                    if (TOptional<SAttribute> CustomAttribute = Function->FindAttribute(_Program->_nativeClass, *_Program))
                    {
                        AttributePropagationTarget->AddAttribute(Move(*CustomAttribute));
                        Function->RemoveAttributeClass(_Program->_nativeClass, *_Program);
                        ULANG_ASSERT(AttributePropagationTarget->IsNative());
                        ULANG_ASSERT(!Function->IsNative());
                    }

                    if (TOptional<SAttribute> CustomAttribute = Function->FindAttribute(_Program->_customAttributeHandler, *_Program))
                    {
                        AttributePropagationTarget->AddAttribute(Move(*CustomAttribute));
                    }

                    if (TOptional<SAttribute> ScopeClassAttribute = Function->FindAttribute(_Program->_attributeScopeClass, *_Program))
                    {
                        AttributePropagationTarget->AddAttribute(Move(*ScopeClassAttribute));
                    }

                    if (TOptional<SAttribute> ScopeStructAttribute = Function->FindAttribute(_Program->_attributeScopeStruct, *_Program))
                    {
                        AttributePropagationTarget->AddAttribute(Move(*ScopeStructAttribute));
                    }

                    if (TOptional<SAttribute> ScopeDataAttribute = Function->FindAttribute(_Program->_attributeScopeData, *_Program))
                    {
                        AttributePropagationTarget->AddAttribute(Move(*ScopeDataAttribute));
                    }
                });
            }

            if (!Function->HasImplementation()
                && _Context._Scope->GetKind() != CScope::EKind::Class
                && _Context._Scope->GetKind() != CScope::EKind::Interface
                && _Context._Scope->GetKind() != CScope::EKind::Type)
            {
                AppendGlitch(DefVst, EDiagnostic::ErrSemantic_UnexpectedAbstractFunction);
            }
        });

        // Process `native` after function body analysis to make matching the body syntax simpler
        EnqueueDeferredTask(Deferred_FinalValidation, [this, DefVst, Function]
        {
            if (const TSPtr<CExpressionBase>& BodyAst = Function->GetBodyAst())
            {
                // Does this assignment-type declaration have the <native> attribute?
                if (BodyAst->GetNodeType() != Cases<EAstNodeType::Definition_Class, EAstNodeType::Definition_Interface>
                    && Function->IsNative())
                {
                    // Yes - we don't allow that
                    AppendGlitch(DefVst, EDiagnostic::ErrSemantic_NativeWithBody);
                }
            }
        });

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Queue up job that validates function attributes
        EnqueueDeferredTask(Deferred_ValidateAttributes, [this, DefVst, Function, FunctionDefinitionAst, Effects]()
        {
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &Function->_EnclosingScope);
            DetectIncorrectOverrideAttribute(*Function);
            DetectInaccessibleTypeDependencies(
                *Function,
                Function->_Signature.GetFunctionType(),
                Function->GetDefineeAst()->GetMappedVstNode());

            // Check dependency on task class if async
            if (Effects[EEffect::suspends])
            {
                if (!_Program->GetTaskFunction())
                {
                    AppendGlitch(*Function->GetDefineeAst(), EDiagnostic::ErrSemantic_AsyncRequiresTaskClass);
                }

                if (Effects[EEffect::decides])
                {
                    AppendGlitch(
                        *Function->GetDefineeAst(),
                        EDiagnostic::ErrSemantic_MutuallyExclusiveEffects,
                        "The suspends and decides effects are mutually exclusive and may not be used together.");
                }
            }
        });

        if (Function->GetBodyAst())
        {
            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Queue up job that processes the routine body
            auto ProcessFunctionBody = [this, Context = _Context, Function, FunctionDefinitionAst, bConstructor, ExprCtx]
            {
                TGuardValue<SContext> ContextGuard(_Context, Context);
                TGuardValue<const CFunction*> CurrentFunctionGuard(_Context._Function, Function);
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Function.Get());

                const CTypeBase& NegativeReturnType = Function->_NegativeType->GetReturnType();
                SExprCtx BodyExprCtx = ExprCtx.WithResultIsReturned(&NegativeReturnType).With(Function->_Signature.GetEffects());
                SExprArgs BodyExprArgs;
                SMacroCallDefinitionContext BodyMacroCallDefinitionContext(GetFunctionBodyMacroName(*FunctionDefinitionAst, *Function));
                BodyMacroCallDefinitionContext._bIsParametric = true;
                BodyExprArgs.MacroCallDefinitionContext = &BodyMacroCallDefinitionContext;
                if (TSPtr<CExpressionBase> NewValueAst = AnalyzeExpressionAst(
                    Function->GetAstNode()->Value().AsRef(),
                    BodyExprCtx,
                    BodyExprArgs))
                {
                    Function->GetAstNode()->SetValue(Move(NewValueAst.AsRef()));
                }

                if (bConstructor)
                {
                    ValidateConstructorFunctionBody(*Function);
                }
                else
                {
                    ValidateFunctionBody(*Function);
                }
            };

            // If the function is provided an explicit return type, do not
            // consider it to be dependent on functions referenced in its body.
            // However, the body must still be analyzed.
            if (Function->GetReturnTypeAst())
            {
                EnqueueDeferredTask(Deferred_OpenFunctionBodyExpressions, ProcessFunctionBody);
            }
            else
            {
                GetFunctionVertex(*Function)._ProcessFunctionBody = ProcessFunctionBody;
                // The only non-constructor functions allowed to elide a return
                // type must return a `type` type.  Such functions are primarily
                // useful as parametric types and are expected to have complete
                // type information by the end of `Deferred_Type`.
                EnqueueDeferredTask(bConstructor? Deferred_OpenFunctionBodyExpressions : Deferred_ClosedFunctionBodyExpressions, [this, Function, FunctionDefinitionAst]
                {
                    int32_t FunctionIndex = Function->Index();
                    SFunctionVertex& FunctionVertex = GetFunctionVertex(FunctionIndex);
                    if (FunctionVertex._Number == -1)
                    {
                        StrongConnectFunctionVertex(FunctionIndex, FunctionVertex);
                        if (RequireTypeIsNotRecursive(Function->_NegativeType, FunctionDefinitionAst.Get()))
                        {
                            RequireTypeIsNotRecursive(Function->_Signature.GetFunctionType(), FunctionDefinitionAst.Get());
                        }
                    }
                });
            }
        }
        else
        {
            EnqueueDeferredTask(Deferred_ValidateAttributes, [this, Function]
            {
                if (Function->IsFinal() && !Function->IsNative())
                {
                    if (VerseFN::UploadedAtFNVersion::EnableFinalSpecifierFixes(_Context._Package->_UploadedAtFNVersion))
                    {
                        AppendGlitch(*Function->GetDefineeAst(),
                            EDiagnostic::ErrSemantic_MissingFinalFieldInitializer,
                            CUTF8String("Final function '%s' is not initialized. Since it cannot be overridden, it must be initialized here.",
                                Function->AsNameCString()));
                    }
                }
            });
        }

        // Only consider functions defined in source packages as part of the statistics, since we don't really want
        // to compare against things that aren't directly user-controlled such as native function implementations etc.
        // Also don't bother counting `epic_internal` functions as part of the statistics since it's definitely not
        // from user code either.
        if (CAstPackage* Package = Function->GetPackage(); Package && Package->_VerseScope == EVerseScope::PublicUser && !Function->IsAuthoredByEpic())
        {
            _Diagnostics->AppendFunctionDefinition(1);
        }

        return Move(FunctionDefinitionAst);
    }

    //-------------------------------------------------------------------------------------------------
    TArray<SAttribute> AnalyzeNameAndDefAttributes(const TArray<SAttribute>& NameAttributes, const TArray<SAttribute>& DefAttributes, CAttributable::EAttributableScope AttributedExprType)
    {
        TArray<SAttribute> Result = NameAttributes;
        AnalyzeAttributes(Result, AttributedExprType, EAttributeSource::Name);
        TArray<SAttribute> MyDefAttributes = DefAttributes;
        AnalyzeAttributes(MyDefAttributes, AttributedExprType, EAttributeSource::Definition);
        Result += Move(MyDefAttributes);
        return Result;
    }

    enum class EAttributeSource
    {
        Name,
        Effect,
        ClassEffect,
        StructEffect,
        InterfaceEffect,
        EnumEffect,
        Definition,
        Identifier,
        Expression,
        Var
    };

    const CClass* TryGetFunctionReturnTypeClass(TSPtr<CExpressionBase> Expr)
    {
        auto Func = AsNullable<CExprIdentifierFunction>(Expr);
        if (!Func)
        {
            return {};
        }

        const CTypeBase* ReturnType = Func->_Function._Signature.GetReturnType();
        return ReturnType->GetNormalType().AsNullable<CClass>();
    }

    bool IsAccessorFunctionAttributeClass(const CClass* X)
    {
        return X == _Program->_getterClass || X == _Program->_setterClass;
    }
    
    bool FindAccessorFunctions(const TSPtr<CExprDataDefinition> Expr, const TArray<SAttribute>& Attributes, SClassVarAccessorFunctions& Result)
    {
        int NumGetterAttrs{}, NumSetterAttrs{};
        for (const SAttribute& Attr : Attributes)
        {
            auto Invocation = AsNullable<CExprInvocation>(Attr._Expression);
            if (!Invocation)
            {
                continue;
            }

            const CClass* AttrClass = TryGetFunctionReturnTypeClass(Invocation->GetCallee());
            if (!IsAccessorFunctionAttributeClass(AttrClass))
            {
                continue;
            }

            auto&& SaveAccessor = [Invocation, &Attr, this](TMap<int, const CFunction*>& Accessors, CSymbol& OutName)
            {
                if (auto Func = AsNullable<CExprIdentifierFunction>(Invocation->GetArgument()))
                {
                    Accessors.Insert(Func->_Function._Signature.NumParams(), &Func->_Function);
                    OutName = Func->_Function.GetName();
                    return true;
                }
                if (auto OverloadedFunc = AsNullable<CExprIdentifierOverloadedFunction>(Invocation->GetArgument()))
                {
                    for (const auto& Overload : OverloadedFunc->_FunctionOverloads)
                    {
                        Accessors.Insert(Overload->_Signature.NumParams(), Overload);
                    }
                    OutName = OverloadedFunc->_FunctionOverloads[0]->GetName();
                    return true;
                }

                return false;
            };

            if (AttrClass == _Program->_getterClass)
            {
                ++NumGetterAttrs;
                if (!SaveAccessor(Result.Getters, Result.GetterName))
                {
                    return false;
                }
            }

            if (AttrClass == _Program->_setterClass)
            {
                ++NumSetterAttrs;
                if (!SaveAccessor(Result.Setters, Result.SetterName))
                {
                    return false;
                }
            }
        }

        if (!((NumGetterAttrs == 0 || NumGetterAttrs == 1) && NumGetterAttrs == NumSetterAttrs))
        {
            AppendGlitch(*Expr,
                         EDiagnostic::ErrSemantic_InvalidAttribute,
                         "<getter(...)> and <setter(...)> may appear at most once.");
            return false;
        }

        if (Result && Expr->_DataMember->IsNative())
        {
            AppendGlitch(*Expr,
                         EDiagnostic::ErrSemantic_InvalidAttribute,
                         "Data definitions that use <getter(...)> and <setter(...)> "
                         "cannot also be <native> (it has no effect).");
            return false;
        }

        return bool{Result};
    }

    bool AnalyzeAccessorFunctions(
        TSRef<CExprDataDefinition> DataDefAst,
        CExprVar* Var,
        CSymbol VarName,
        SClassVarAccessorFunctions& Accessors,
        SExprCtx ExprCtx)
    {
        if (!Var || CScope::EKind::Class != DataDefAst->_DataMember->_EnclosingScope.GetKind())
        {
            AppendGlitch(*DataDefAst,
                         EDiagnostic::ErrSemantic_InvalidAttribute,
                         "<getter(...)> and <setter(...)> attributes may only be used with class `var` fields.");
            return {};
        }


        if (DataDefAst->Value())
        {
            static constexpr const char* InitializerErrorMessage =
                "Data members with `<getter(...)>` and `<setter(...)>` must be either uninitialized "
                "or initialized with `= external{}`.";

            auto MacroAst = AsNullable<CExprMacroCall>(DataDefAst->Value());
            if (!MacroAst)
            {
                AppendGlitch(*DataDefAst, EDiagnostic::ErrSemantic_MissingDataMemberInitializer, InitializerErrorMessage);
                return {};
            }

            auto MacroName = AsNullable<CExprIdentifierBuiltInMacro>(
                AnalyzeInPlace(
                    MacroAst->Name(), &CSemanticAnalyzerImpl::AnalyzeExpressionAst, ExprCtx.WithResultIsCalledAsMacro(), SExprArgs{}
                )
            );

            if (!(MacroName && MacroName->_Symbol == _InnateMacros._external))
            {
                AppendGlitch(*MacroAst, EDiagnostic::ErrSemantic_MissingDataMemberInitializer, InitializerErrorMessage);
                return {};
            }
        }

        if (!Accessors)
        {
            AppendGlitch(*DataDefAst,
                         EDiagnostic::ErrSemantic_InvalidAttribute,
                         "Both <getter(...)> and <setter(...)> must be present (or neither).");
            return {};
        }

        const CTypeBase* VarType = DataDefAst->_DataMember->GetType()->GetNormalType().AsChecked<CPointerType>().NegativeValueType();
        if (!VarType->CanBeCustomAccessorDataType())
        {
            AppendGlitch(*DataDefAst,
                         EDiagnostic::ErrSemantic_Unimplemented,
                         CUTF8String("The type `%s` does not support `<getter(...)>` and `<setter(...)>` attributes.",
                                     VarType->AsCode().AsCString()));
            return {};
        }

        struct FFieldNode
        {
            FFieldNode* Parent{};
            int Depth{};
            const CTypeBase* InputType{};
            const CTypeBase* OutputType{};
        };

        TArray<TUPtr<FFieldNode>> AllNodes;

        // start at the AST node of Var, walk its entire fields subtree and find the longest
        // possible access path, and compute the tightest possible accessor signature
        // by keeping track of accessor inputs/outputs and using `any` whenever these
        // are heterogeneous:
        TArray<FFieldNode*> RequiredAccessor;
        {
            FFieldNode* InnermostAccess{};
            TMap<int, TSet<const CTypeBase*>> LevelInputs, LevelOutputs;
            TArray<FFieldNode*> Remaining;

            auto&& MakeNode =
                [&AllNodes, &Remaining, &LevelInputs, &LevelOutputs](FFieldNode* Parent, int Depth, const CTypeBase* Input, const CTypeBase* Output)
            {
                auto Node = TUPtr<FFieldNode>::New();
                Remaining.Add(Node.Get());
                Node->Parent = Parent;
                Node->Depth = Depth;
                Node->InputType = Input;
                Node->OutputType = Output;
                AllNodes.Add(Move(Node));

                {
                    using TypePtr = const CTypeBase*;
                    LevelInputs.FindOrInsert(int{Depth})._Value.Insert(TypePtr{Input});
                    LevelOutputs.FindOrInsert(int{Depth})._Value.Insert(TypePtr{Output});
                }
            };

            MakeNode(nullptr, 0, nullptr, VarType);

            while (!Remaining.IsEmpty())
            {
                FFieldNode* Node = Remaining.Pop();
                ULANG_ASSERT(Node);

                const int NewDepth = Node->Depth + 1;
                if (!InnermostAccess || Node->Depth > InnermostAccess->Depth)
                {
                    InnermostAccess = Node;
                }

                if (const auto* Array = Node->OutputType->GetNormalType().AsNullable<CArrayType>())
                {
                    MakeNode(Node, NewDepth, _Program->_intType, Array->GetElementType());
                }
                else if (const auto* Class = Node->OutputType->GetNormalType().AsNullable<CClass>();
                         Class && Class->IsStruct())
                {
                    for (const auto& Field : Class->GetDefinitionsOfKind<CDataDefinition>())
                    {
                        if (Field->GetType()->CanBeCustomAccessorDataType())
                        {
                            MakeNode(Node, NewDepth, _Program->_stringAlias->GetType(), Field->_NegativeType);
                        }
                    }
                }
            }

            for (FFieldNode* N = InnermostAccess; N; N = N->Parent)
            {
                if (const auto* Indexes = LevelInputs.Find(N->Depth);
                    Indexes && N->InputType && Indexes->Num() > 1)
                {
                    N->InputType = &_Program->_anyType;
                }

                if (const auto* Indexes = LevelOutputs.Find(N->Depth);
                    Indexes && N->OutputType && Indexes->Num() > 1)
                {
                    N->OutputType = &_Program->_anyType;
                }

                RequiredAccessor.Insert(N, 0);
            }
        }

        CEnumeration* Sentinel{};
        if (CModule* VerseDotOrg = _Program->FindFirstDefinitionOfKind<CModule>(_Program->GetSymbols()->AddChecked("Verse.org")))
        {
            if (CModule* Verse = VerseDotOrg->FindFirstDefinitionOfKind<CModule>(_Program->GetSymbols()->AddChecked("Verse")))
            {
                Sentinel = Verse->FindFirstDefinitionOfKind<CEnumeration>(_Program->GetSymbols()->AddChecked("accessor"));
            }
        }
        ULANG_ASSERT(Sentinel);

        struct FAccessorErrorKey {
            int RequiredPos;
            bool bSetter;
            operator const void*() const
            {
                // this conversion exists to allow TMap to look up keys of this type
                return reinterpret_cast<const void*>((uintptr_t(RequiredPos) + 1) * (bSetter + 1));
            }
        };

        enum class EAccessorErrorType { Missing, Wrong };
        struct FAccessorError { EAccessorErrorType Type; int Arity; TArray<CUTF8String> Messages; };
        TMap<FAccessorErrorKey, FAccessorError> AccessorErrors;

        auto&& CheckAccessors =
            [this, &RequiredAccessor, Sentinel](TMap<int, const CFunction*>& Accessors, const bool bSetters, auto& Errors)
        {
            for (int I = 0; I < RequiredAccessor.Num(); ++I)
            {
                const int Arity = I + 1 + bSetters;

                auto&& AddError = [&Errors, I, Arity, bSetters](EAccessorErrorType Type, const CUTF8String& Msg)
                {
                    auto& Error = Errors.FindOrInsert({I, bSetters})._Value;
                    Error.Type = Type;
                    Error.Arity = Arity;
                    Error.Messages.Add(Msg);
                };

                const CFunction** Accessor = Accessors.Find(Arity);
                if (!Accessor)
                {
                    AddError(EAccessorErrorType::Missing, "missing definition");
                    continue;
                }

                const SSignature& Signature = (*Accessor)->_Signature;

                if (!Signature.GetFunctionType()->GetEffects().HasAll(EffectSets::Transacts))
                {
                    AddError(EAccessorErrorType::Wrong, "needs the <transacts> effect");
                }

                if (Signature.GetFunctionType()->GetEffects().HasAny(EEffect::suspends))
                {
                    AddError(EAccessorErrorType::Wrong, "must not have the <suspends> effect");
                }

                const bool ReturnTypeOk = bSetters
                    ? Constrain(&_Program->_voidType, Signature.GetReturnType())
                    : Constrain(RequiredAccessor[I]->OutputType, Signature.GetReturnType());
                if (!ReturnTypeOk)
                {
                    AddError(EAccessorErrorType::Wrong,
                             CUTF8String("incorrect return type `%s`; expected `%s`",
                                         Signature.GetReturnType()->AsCode().AsCString(),
                                         bSetters ? "void" : RequiredAccessor[I]->OutputType->AsCode().AsCString()));
                }

                for (int J = 1; J < Signature.NumParams() - bSetters; ++J)
                {
                    const auto& Actual = Signature.GetParamType(J);
                    const auto& Expected = RequiredAccessor[J]->InputType;
                    if (!Constrain(Actual, Expected))
                    {
                        AddError(EAccessorErrorType::Wrong,
                                 CUTF8String("parameter %d has incorrect type `%s`; expected `%s`",
                                             J, Actual->AsCode().AsCString(), Expected->AsCode().AsCString()));
                    }
                }

                if (bSetters)
                {
                    const auto LastParamIndex = Signature.NumParams() - 1;
                    const auto& Actual = Signature.GetParamType(LastParamIndex);
                    const auto& Expected = RequiredAccessor[I]->OutputType;
                    if (!Constrain(Actual, Expected))
                    {
                        AddError(EAccessorErrorType::Wrong,
                                 CUTF8String("last parameter has incorrect type `%s`; expected `%s`",
                                             Actual->AsCode().AsCString(),
                                             Expected->AsCode().AsCString()));
                    }
                }

                (*Accessor)->_bIsAccessorOfSomeClassVar = true;
            }
        };

        auto&& RequiredAccessorAsString = [&RequiredAccessor](const int N, const CSymbol& Name, const bool bSetter)
        {
            CUTF8StringBuilder Result;

            Result.AppendFormat("%s(", Name.AsCString());
            for (int I = 0; I <= N; ++I)
            {
                if (const CTypeBase* InputType = RequiredAccessor[I]->InputType)
                {
                    Result.AppendFormat(",:%s", InputType->AsCode().AsCString());
                }
                else
                {
                    Result.Append(":accessor");
                }
            }
            if (bSetter)
            {
                Result.AppendFormat(",:%s)<transacts>:void", RequiredAccessor[N]->OutputType->AsCode().AsCString());
            }
            else
            {
                Result.AppendFormat(")<transacts>:%s", RequiredAccessor[N]->OutputType->AsCode().AsCString());
            }

            return Result.MoveToString();
        };

        auto&& UserAccessorAsString = [](const CFunction* Func)
        {
            CUTF8StringBuilder Result;
            Result.AppendFormat("%s", Func->GetDecoratedName(uint16_t(EFunctionStringFlag::NoScope_)).AsCString());
            Func->_Signature.GetFunctionType()->BuildEffectAttributeCode(Result);
            Result.AppendFormat(":%s", Func->_Signature.GetReturnType()->AsCode().AsCString());
            return Result.MoveToString();
        };

        CheckAccessors(Accessors.Getters, false, AccessorErrors);
        CheckAccessors(Accessors.Setters, true, AccessorErrors);
        if (!AccessorErrors.Num())
        {
                return true;
        }

        CUTF8StringBuilder MissingDefinitions, WrongDefinitions;
        for (const auto& [Key, Error] : AccessorErrors)
        {
            const auto& AccessorName = Key.bSetter ? Accessors.SetterName : Accessors.GetterName;
            auto& UserAccessors = Key.bSetter ? Accessors.Setters : Accessors.Getters;

            switch (Error.Type)
            {
            case EAccessorErrorType::Missing:
                if (MissingDefinitions.IsEmpty())
                {
                    MissingDefinitions.Append("Missing definitions:\n");
                }
                MissingDefinitions.AppendFormat("\t%s\n", RequiredAccessorAsString(Key.RequiredPos, AccessorName, Key.bSetter).AsCString());
                break;

            case EAccessorErrorType::Wrong:
                if (WrongDefinitions.IsEmpty())
                {
                    WrongDefinitions.Append("Incorrect definitions:\n");
                }
                WrongDefinitions.AppendFormat("\t%s\n", UserAccessorAsString(UserAccessors[Error.Arity]).AsCString());
                for (const auto& ErrorMsg : Error.Messages)
                {
                    WrongDefinitions.AppendFormat("\t\t- %s\n", ErrorMsg.AsCString());
                }
                WrongDefinitions.AppendFormat("\t\t- the type signature of this accessor should be: %s\n",
                                              RequiredAccessorAsString(Key.RequiredPos, AccessorName, Key.bSetter).AsCString());
                break;

            default:
                ULANG_UNREACHABLE();
            }
        }

        AppendGlitch(*DataDefAst,
                     EDiagnostic::ErrSemantic_CustomClassVarAccessorTypeMismatch,
                     CUTF8String("`%s`'s accessors contain the following errors:\n%s\n%s",
                                 VarName.AsCString(),
                                 MissingDefinitions.MoveToString().AsCString(),
                                 WrongDefinitions.MoveToString().AsCString()));
        return {};
    }


    //-------------------------------------------------------------------------------------------------
    void AnalyzeAttributes(TArray<SAttribute>& Attributes, CAttributable::EAttributableScope AttributedExprType, EAttributeSource AttributeSource)
    {
        SExprCtx ExprCtx = SExprCtx::Default().WithResultIsUsedAsAttribute(_Program->_attributeClass);

        for (int32_t AttributeIndex = 0; AttributeIndex < Attributes.Num(); ++AttributeIndex)
        {
            // Analyze the attribute.
            if (TSPtr<CExpressionBase> NewAttributeExpr = AnalyzeExpressionAst(Attributes[AttributeIndex]._Expression, ExprCtx.With(EffectSets::Computes)))
            {
                Attributes[AttributeIndex]._Expression = Move(NewAttributeExpr);
            }

            SAttribute& Attribute = Attributes[AttributeIndex];
            CExpressionBase& AttributeExpr = *Attribute._Expression;

            // Ensure AttrExpr has expected result type
            const CTypeBase* AttrType = nullptr;
            // @HACK: SOL-972, need better (fuller) support for attribute functions/ctors

            const EAstNodeType AstNodeType = AttributeExpr.GetNodeType();
            if (AstNodeType == EAstNodeType::Invoke_Invocation)
            {
                // attribute with a single string argument
                // example: @doc("Do the thing!")
                const CExpressionBase& AttrCalleeAst = *static_cast<CExprInvocation&>(AttributeExpr).GetCallee();
                if (AttrCalleeAst.GetNodeType() == EAstNodeType::Identifier_Function)
                {
                    const CExprIdentifierFunction& CalleeFunctionAst = static_cast<const CExprIdentifierFunction&>(AttrCalleeAst);
                    const CTypeBase* AttrFunctionReturnType = CalleeFunctionAst._Function._Signature.GetReturnType();
                    AttrType = AttrFunctionReturnType;
                }

                if (!AttrType)
                {
                    AppendGlitch(AttributeExpr, EDiagnostic::ErrSemantic_InvalidAttribute);
                    AttrType = _Program->GetDefaultUnknownType();
                }
            }
            else if (AstNodeType == EAstNodeType::Invoke_ArchetypeInstantiation)
            {
                // attribute with a class type and named fields
                // example: @editable{ToolTip:="Some Text", MinValue:=123}
                // example: @editable_slider(int){args}
                AttrType = AttributeExpr.GetResultType(*_Program);
            }
            else
            {
                // attribute with no arguments
                // example: @customattribhandler
                AttrType = GetTypePositiveType(AttributeExpr)._Type;
            }

            if (!SemanticTypeUtils::IsAttributeType(AttrType))
            {
                if (!SemanticTypeUtils::IsUnknownType(AttrType))
                {
                    AppendGlitch(
                        AttributeExpr,
                        EDiagnostic::ErrSemantic_InvalidAttribute,
                        CUTF8String("Incompatible attribute expression result - expected subclass of type `attribute` and got `%s`.", AttrType->AsCode().AsCString()));
                }
                continue;
            }

            const CClass* AttrClass = &AttrType->GetNormalType().AsChecked<CClass>();

            EnqueueDeferredTask(Deferred_ValidateAttributes, [this, AttributeSource, AttributedExprType, Attribute, AttrClass, &AttributeExpr]()
            {
				// Call out cases of deprecated <varies>
                if (AttrClass == _Program->_variesClassDeprecated)
                {
                    // varies is deprecated, so version-gate it
                    if (VerseFN::UploadedAtFNVersion::DeprecateVariesEffect(_Context._Package->_UploadedAtFNVersion))
                    {
                        // error
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidEffectDeclaration,
                            CUTF8String(
                                "The `<%s>` effect has been removed. It can be replaced with `<reads><allocates>`.",
                                AttrClass->AsCode().AsCString()));
                    }
                    else
                    {
                        // warning
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::WarnSemantic_UseOfDeprecatedDefinition,
                            CUTF8String(
                                "The `<%s>` effect has been deprecated. It can be replaced with `<reads><allocates>`.",
                                AttrClass->AsCode().AsCString()));
                    }
                }

                // Check some cases separately in order to give good error messages.
                if (AttributeSource != EAttributeSource::ClassEffect
                    && AttributedExprType == CAttributable::EAttributableScope::Class
                    && AttrClass->HasAttributeClass(_Program->_attributeScopeClassMacro, *_Program)
                    && !AttrClass->HasAttributeClass(_Program->_attributeScopeName, *_Program))
                {
                    AppendGlitch(
                        AttributeExpr,
                        EDiagnostic::ErrSemantic_InvalidAttributeScope,
                        CUTF8String(
                            "Attribute %s should be used on the class macro name, like `c := class<%s> ...`.",
                            AttrClass->AsCode().AsCString(), AttrClass->AsCode().AsCString()));
                    return;
                }
                if (AttributeSource != EAttributeSource::StructEffect
                    && AttributedExprType == CAttributable::EAttributableScope::Struct
                    && AttrClass->HasAttributeClass(_Program->_attributeScopeStructMacro, *_Program)
                    && !AttrClass->HasAttributeClass(_Program->_attributeScopeName, *_Program))
                {
                    AppendGlitch(
                        AttributeExpr,
                        EDiagnostic::ErrSemantic_InvalidAttributeScope,
                        CUTF8String(
                            "Attribute %s should be used on the struct macro name, like `s := struct<%s> ...`.",
                            AttrClass->AsCode().AsCString(), AttrClass->AsCode().AsCString()));
                    return;
                }
                if (AttributeSource != EAttributeSource::InterfaceEffect
                    && AttributedExprType == CAttributable::EAttributableScope::Interface
                    && AttrClass->HasAttributeClass(_Program->_attributeScopeInterfaceMacro, *_Program)
                    && !AttrClass->HasAttributeClass(_Program->_attributeScopeName, *_Program))
                {
                    AppendGlitch(
                        AttributeExpr,
                        EDiagnostic::ErrSemantic_InvalidAttributeScope,
                        CUTF8String(
                            "Attribute %s should be used on the interface macro name, like `i := interface<%s> ...`.",
                            AttrClass->AsCode().AsCString(), AttrClass->AsCode().AsCString()));
                    return;
                }
                if (AttributeSource != EAttributeSource::EnumEffect
                    && AttributedExprType == CAttributable::EAttributableScope::Enum
                    && AttrClass->HasAttributeClass(_Program->_attributeScopeEnumMacro, *_Program)
                    && !AttrClass->HasAttributeClass(_Program->_attributeScopeName, *_Program))
                {
                    AppendGlitch(
                        AttributeExpr,
                        EDiagnostic::ErrSemantic_InvalidAttributeScope,
                        CUTF8String(
                            "Attribute %s should be used on the enum macro name, like `e := enum<%s> ...`.",
                            AttrClass->AsCode().AsCString(), AttrClass->AsCode().AsCString()));
                    return;
                }

                if (AttributedExprType == CAttributable::EAttributableScope::Function
                    && AttrClass == _Program->_abstractClass)
                {
                    AppendGlitch(
                        AttributeExpr,
                        EDiagnostic::ErrSemantic_InvalidAttributeScope,
                        CUTF8String(
                            "Attribute abstract is disallowed on functions; a function is abstract iff it lacks a body.",
                            AttrClass->AsCode().AsCString(), AttrClass->AsCode().AsCString()));
                    return;
                }

                // For the definition of parametric type functions, we need to allow some 
                // attributes through so eventually they can be moved to the class type.
                CAttributable::EAttributableScope NewAttributedExprType = AttributedExprType;
                if (AttributedExprType == CAttributable::EAttributableScope::Function
                    && AttributeSource == EAttributeSource::Definition
                    && _Context._Function != nullptr)
                {
                    if (TSPtr<CExprClassDefinition> InnerClass = _Context._Function->GetBodyClassDefinitionAst())
                    {
                        if (InnerClass->_Class.IsClass(*_Program->_attributeClass))
                        {
                            NewAttributedExprType = CAttributable::EAttributableScope::AttributeClassTypeFunction;
                        }
                        else
                        {
                            NewAttributedExprType = CAttributable::EAttributableScope::ClassTypeFunction;
                        }
                    }

                    if (TSPtr<CExprInterfaceDefinition> InnerInterface = _Context._Function->GetBodyInterfaceDefinitionAst())
                    {
                        NewAttributedExprType = CAttributable::EAttributableScope::InterfaceTypeFunction;
                    }
                }

                // Map the attributed expression scope to the attribute class that is used to tag attribute classes as valid for this scope.
                const char* AttributedExprTypeDesc{ "unknown" };
                bool bHasRequiredAttributeScope{ false };
                switch (NewAttributedExprType)
                {
                case CAttributable::EAttributableScope::Module:                 AttributedExprTypeDesc = "modules";            bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeModule, *_Program); break;
                case CAttributable::EAttributableScope::Class:                  AttributedExprTypeDesc = "classes";            bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeClass, *_Program); break;
                case CAttributable::EAttributableScope::Struct:                 AttributedExprTypeDesc = "structs";            bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeStruct, *_Program); break;
                case CAttributable::EAttributableScope::Data:                   AttributedExprTypeDesc = "data members";       bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeData, *_Program); break;
                case CAttributable::EAttributableScope::Function:               AttributedExprTypeDesc = "functions";          bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeFunction, *_Program); break;
                case CAttributable::EAttributableScope::Enum:                   AttributedExprTypeDesc = "enums";              bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeEnum, *_Program); break;
                case CAttributable::EAttributableScope::Enumerator:             AttributedExprTypeDesc = "enumerator";         bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeEnumerator, *_Program); break;
                case CAttributable::EAttributableScope::AttributeClass:         AttributedExprTypeDesc = "attribute classes";  bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeClass, *_Program)
                                                                                                                                                    || AttrClass->HasAttributeClass(_Program->_attributeScopeAttributeClass, *_Program); break;
                case CAttributable::EAttributableScope::Interface:              AttributedExprTypeDesc = "interfaces";         bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeInterface, *_Program); break;
                case CAttributable::EAttributableScope::Expression:             AttributedExprTypeDesc = "expressions";        bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeExpression, *_Program); break;
                case CAttributable::EAttributableScope::TypeDefinition:         AttributedExprTypeDesc = "type definition";    bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeTypeDefinition, *_Program); break;
                case CAttributable::EAttributableScope::ScopedAccessLevel:      AttributedExprTypeDesc = "scoped definition";  bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeScopedDefinition, *_Program); break;

                case CAttributable::EAttributableScope::InterfaceTypeFunction:  AttributedExprTypeDesc = "interface functions";bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeFunction, *_Program)
                                                                                                                                                    || AttrClass->HasAttributeClass(_Program->_attributeScopeInterface, *_Program); break;
                case CAttributable::EAttributableScope::ClassTypeFunction:      AttributedExprTypeDesc = "class functions";    bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeFunction, *_Program)
                                                                                                                                                    || AttrClass->HasAttributeClass(_Program->_attributeScopeClass, *_Program); break;
                case CAttributable::EAttributableScope::AttributeClassTypeFunction:AttributedExprTypeDesc = "attribute class functions"; bHasRequiredAttributeScope = AttrClass->HasAttributeClass(_Program->_attributeScopeFunction, *_Program)
                                                                                                                                                    || AttrClass->HasAttributeClass(_Program->_attributeScopeClass, *_Program)
                                                                                                                                                    || AttrClass->HasAttributeClass(_Program->_attributeScopeAttributeClass, *_Program); break;
                    break;
                default:
                    ULANG_UNREACHABLE();
                }

                // Check that the attribute class has the attribute class that tags it as valid for this scope.
                if (!bHasRequiredAttributeScope)
                {
                    AppendGlitch(
                        AttributeExpr,
                        EDiagnostic::ErrSemantic_InvalidAttributeScope,
                        CUTF8String("Attribute %s cannot be used with %s.", AttrClass->AsCode().AsCString(), AttributedExprTypeDesc));
                    return;
                }

                // Check that attributes can only be used as prefix attributes, and specifiers can only be used as suffix specifiers
                if (AttrClass->HasAttributeClass(_Program->_attributeScopeSpecifier, *_Program))
                {
                    if (Attribute._Type != SAttribute::EType::Specifier)
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s can only be used as a <specifier>.", AttrClass->AsCode().AsCString()));
                        return;
                    }
                }
                else if (AttrClass->HasAttributeClass(_Program->_attributeScopeAttribute, *_Program))
                {
                    if (Attribute._Type != SAttribute::EType::Attribute)
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s can only be used as an @attribute.", AttrClass->AsCode().AsCString()));
                        return;
                    }
                }
                else
                {
                    // user-defined attributes fall here, where we don't yet have a way to signal whether they
                    // are attributes or specifiers, so we accept everything
                }

                switch (AttributeSource)
                {
                case EAttributeSource::Name:
                    if (!AttrClass->HasAttributeClass(_Program->_attributeScopeName, *_Program))
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s cannot be used with names.", AttrClass->AsCode().AsCString()));
                    }
                    break;
                case EAttributeSource::Effect:
                    if (!AttrClass->HasAttributeClass(_Program->_attributeScopeEffect, *_Program))
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s cannot be used as an effect.", AttrClass->AsCode().AsCString()));
                    }
                    break;
                case EAttributeSource::ClassEffect:
                    if (!AttrClass->HasAttributeClass(_Program->_attributeScopeEffect, *_Program)
                        && !AttrClass->HasAttributeClass(_Program->_attributeScopeClassMacro, *_Program))
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s cannot be used as a `class` effect.", AttrClass->AsCode().AsCString()));
                    }
                    break;
                case EAttributeSource::StructEffect:
                    if (!AttrClass->HasAttributeClass(_Program->_attributeScopeEffect, *_Program)
                        && !AttrClass->HasAttributeClass(_Program->_attributeScopeStructMacro, *_Program))
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s cannot be used as a `struct` effect.", AttrClass->AsCode().AsCString()));
                    }
                    break;
                case EAttributeSource::InterfaceEffect:
                    if (!AttrClass->HasAttributeClass(_Program->_attributeScopeInterfaceMacro, *_Program))
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s cannot be used as an `interface` effect.", AttrClass->AsCode().AsCString()));
                    }
                    break;
                case EAttributeSource::EnumEffect:
                    if (!AttrClass->HasAttributeClass(_Program->_attributeScopeEnumMacro, *_Program))
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s cannot be used as an `enum` effect.", AttrClass->AsCode().AsCString()));
                    }
                    break;
                case EAttributeSource::Definition:
                    if (AttrClass->HasAttributeClass(_Program->_attributeScopeName, *_Program))
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s can only be used with names.", AttrClass->AsCode().AsCString()));
                    }
                    if (AttrClass->HasAttributeClass(_Program->_attributeScopeEffect, *_Program))
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s can only be used as an effect.", AttrClass->AsCode().AsCString()));
                    }
                    break;
                case EAttributeSource::Identifier:
                    if (!AttrClass->HasAttributeClass(_Program->_attributeScopeIdentifier, *_Program) &&
                        !AttrClass->HasAttributeClass(_Program->_attributeScopeExpression, *_Program))
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s cannot be used with identifiers.", AttrClass->AsCode().AsCString()));
                    }
                    break;
                case EAttributeSource::Var:
                    if (!AttrClass->HasAttributeClass(_Program->_attributeScopeVar, *_Program))
                    {
                        AppendGlitch(
                            AttributeExpr,
                            EDiagnostic::ErrSemantic_InvalidAttributeScope,
                            CUTF8String("Attribute %s cannot be used with var.", AttrClass->AsCode().AsCString()));
                    }
                    break;
                case EAttributeSource::Expression:
                    // Use outside of a definition on an arbitrary expression.  Expression scope checked earlier.
                    break;
                default:
                    ULANG_UNREACHABLE();
                }

                // Check for disallowed native attribute
                if ((AttrClass == _Program->_nativeClass || AttrClass == _Program->_nativeCallClass)
                    && !_Context._Scope->GetPackage()->_bAllowNative)
                {
                    AppendGlitch(
                        AttributeExpr,
                        EDiagnostic::ErrSemantic_InvalidAttribute,
                        CUTF8String("Native attributes are not allowed for package %s.", *_Context._Scope->GetPackage()->_Name));
                    return;
                }

                // 'localizes' attribute is not allowed on function-local data per SOL-3472
                if (AttrClass == _Program->_localizes
                    && _Context._Scope->IsControlScope())
                {
                    AppendGlitch(
                        AttributeExpr,
                        EDiagnostic::ErrSemantic_AttributeNotAllowedOnLocalVars,
                        CUTF8String("Attribute %s is not allowed on local variables.", AttrClass->AsCode().AsCString()));
                    return;
                }
            });

            EnqueueDeferredTask(Deferred_PropagateAttributes, [this, AttrClass, &AttributeExpr]()
            {
                // Attributes of attributes are not propagated, nor should they necessarily be.  However, because
                // we need to be able to detect whether or not we should enqueue a deferred linking task, so we copy over the
                // custom handler attribute so we can check and only process ones we know are expected to be handled later.
                if (AttrClass->HasAttributeClass(_Program->_customAttributeHandler, *_Program))
                {
                    AttributeExpr.AddAttributeClass(_Program->_customAttributeHandler);
                }
            });
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeFinalAttribute(CExpressionBase& AstNode, CDefinition& Definition)
    {
        if (Definition.IsFinal() && !Definition.IsInstanceMember())
        {
            if ((Definition._EnclosingScope.IsModuleOrSnippet() && Definition.IsA<CFunction>())
                || VerseFN::UploadedAtFNVersion::EnableFinalSpecifierFixes(_Context._Package->_UploadedAtFNVersion))
            {
                AppendGlitch(AstNode,
                    EDiagnostic::ErrSemantic_FinalNonFieldDefinition,
                    CUTF8String("Definition '%s' has the final specifier, but 'final' is only meaningful for fields of classes and structs.",
                        Definition.AsNameCString()));
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeExpressionAst(
        const TSRef<CExpressionBase>& AstNode,
        const SExprCtx& ExprCtx,
        const SExprArgs& ExprArgs = SExprArgs())
    {
        const EAstNodeType NodeType = AstNode->GetNodeType();
        if (!AstNode->MayHaveAttributes())
        {
            MaybeAppendAttributesNotAllowedError(*AstNode);
        }

        TGuardValue VstNodeGuard(_Context._VstNode, AstNode->GetMappedVstNode() ? AstNode->GetMappedVstNode() : _Context._VstNode);

        bool bCouldBeAnalyzingArgument = NodeType == Cases<EAstNodeType::Invoke_MakeNamed, EAstNodeType::Invoke_MakeTuple>;
        TGuardValue<bool> MightBeAnalyzingArgument(_Context._bIsAnalyzingArgumentsInInvocation, bCouldBeAnalyzingArgument ? _Context._bIsAnalyzingArgumentsInInvocation : false);

        switch (NodeType)
        {
            case EAstNodeType::Error_:
                // Ignore errors, as they correspond to a glitch produced during desugaring.
                return nullptr;

            case EAstNodeType::Placeholder_: return AnalyzePlaceholder(static_cast<CExprPlaceholder&>(*AstNode), ExprCtx);

            // Helper expressions
            case EAstNodeType::External: goto analysis_error;
            case EAstNodeType::PathPlusSymbol: return AnalyzePathPlusSymbol(static_cast<CExprPathPlusSymbol&>(*AstNode), ExprCtx);

            // Literals
            case EAstNodeType::Literal_Logic: return nullptr; break;
            case EAstNodeType::Literal_Number: return AnalyzeNumberLiteral(static_cast<CExprNumber&>(*AstNode), ExprCtx);
            case EAstNodeType::Literal_Char: return AnalyzeCharLiteral(static_cast<CExprChar&>(*AstNode), ExprCtx);
            case EAstNodeType::Literal_String: return AnalyzeStringLiteral(static_cast<CExprString&>(*AstNode), ExprCtx);
            case EAstNodeType::Literal_Path: return AnalyzePathLiteral(static_cast<CExprPath&>(*AstNode), ExprCtx);
            case EAstNodeType::Literal_Enum: goto analysis_error;
            case EAstNodeType::Literal_Type: goto analysis_error;
            case EAstNodeType::Literal_Function:  return AnalyzeFunctionLiteral(static_cast<CExprFunctionLiteral&>(*AstNode), ExprCtx);

            // Identifiers
            case EAstNodeType::Identifier_Unresolved: return AnalyzeIdentifier(static_cast<CExprIdentifierUnresolved&>(*AstNode), ExprCtx, ExprArgs);
            case EAstNodeType::Identifier_Class: goto analysis_error;
            case EAstNodeType::Identifier_Module: goto analysis_error;
            case EAstNodeType::Identifier_ModuleAlias: goto analysis_error;
            case EAstNodeType::Identifier_Enum: goto analysis_error;
            case EAstNodeType::Identifier_Interface: goto analysis_error;
            case EAstNodeType::Identifier_Data: goto analysis_error;
            case EAstNodeType::Identifier_TypeAlias: goto analysis_error;
            case EAstNodeType::Identifier_TypeVariable: goto analysis_error;

            // this returns nullptr because it can be validly encountered if you create a CExprInvocation with an already resolved 
            // CExprIdentifierFunction callee, and there is not currently any work the analyzer needs to do to a CExprIdentifierFunction
            case EAstNodeType::Identifier_Function: return nullptr;

            case EAstNodeType::Identifier_OverloadedFunction: goto analysis_error;
            case EAstNodeType::Identifier_Self: goto analysis_error;
            case EAstNodeType::Identifier_BuiltInMacro: goto analysis_error;
            case EAstNodeType::Identifier_Local: goto analysis_error;

            // Multi purpose syntax
            case EAstNodeType::Definition: return AnalyzeDefinition(static_cast<CExprDefinition&>(*AstNode), ExprCtx);

            // Macro
            case EAstNodeType::MacroCall: return AnalyzeMacroCall(AstNode.As<CExprMacroCall>(), ExprCtx, ExprArgs);

            // Invocations
            case EAstNodeType::Invoke_Invocation: return AnalyzeInvocation(AstNode.As<CExprInvocation>(), ExprCtx, ExprArgs);
            case EAstNodeType::Invoke_UnaryArithmetic: return AnalyzeUnaryArithmetic(AstNode.As<CExprUnaryArithmetic>(), ExprCtx);
            case EAstNodeType::Invoke_BinaryArithmetic: return AnalyzeBinaryArithmetic(AstNode.As<CExprBinaryArithmetic>(), ExprCtx);
            case EAstNodeType::Invoke_ShortCircuitAnd: return AnalyzeBinaryOpLogicalAnd(static_cast<CExprShortCircuitAnd&>(*AstNode), ExprCtx);
            case EAstNodeType::Invoke_ShortCircuitOr: return AnalyzeBinaryOpLogicalOr(static_cast<CExprShortCircuitOr&>(*AstNode), ExprCtx);
            case EAstNodeType::Invoke_LogicalNot: return AnalyzeLogicalNot(static_cast<CExprLogicalNot&>(*AstNode), ExprCtx);
            case EAstNodeType::Invoke_Comparison: return AnalyzeComparison(AstNode.As<CExprComparison>(), ExprCtx);
            case EAstNodeType::Invoke_QueryValue: return AnalyzeQueryValue(AstNode.As<CExprQueryValue>(), ExprCtx);
            case EAstNodeType::Invoke_MakeOption: goto analysis_error;
            case EAstNodeType::Invoke_MakeArray: goto analysis_error;
            case EAstNodeType::Invoke_MakeMap: goto analysis_error;
            case EAstNodeType::Invoke_MakeTuple: return AnalyzeTupleValue(static_cast<CExprMakeTuple&>(*AstNode), ExprCtx, ExprArgs);
            case EAstNodeType::Invoke_TupleElement: goto analysis_error;
            case EAstNodeType::Invoke_MakeRange: return AnalyzeMakeRange(static_cast<CExprMakeRange&>(*AstNode), ExprCtx);
            case EAstNodeType::Invoke_Type: goto analysis_error;
            case EAstNodeType::Invoke_PointerToReference: return AnalyzePointerToReference(AstNode.As<CExprPointerToReference>(), ExprCtx);
            case EAstNodeType::Invoke_Set: return AnalyzeSet(AstNode.As<CExprSet>(), ExprCtx);
            case EAstNodeType::Invoke_NewPointer: goto analysis_error;
            case EAstNodeType::Invoke_ReferenceToValue: goto analysis_error;

            case EAstNodeType::Assignment: return AnalyzeAssignment(AsNullable<CExprAssignment>(AstNode), ExprCtx);

            // TypeFormers
            case EAstNodeType::Invoke_ArrayFormer: return AnalyzeArrayTypeFormer(static_cast<CExprArrayTypeFormer&>(*AstNode), ExprCtx);
            case EAstNodeType::Invoke_GeneratorFormer: goto analysis_error;
            case EAstNodeType::Invoke_MapFormer: return AnalyzeMapTypeFormer(static_cast<CExprMapTypeFormer&>(*AstNode), ExprCtx);
            case EAstNodeType::Invoke_OptionFormer: return AnalyzeOptionTypeFormer(static_cast<CExprOptionTypeFormer&>(*AstNode), ExprCtx);
            case EAstNodeType::Invoke_Subtype: goto analysis_error;
            case EAstNodeType::Invoke_TupleType: goto analysis_error;
            case EAstNodeType::Invoke_Arrow: return AnalyzeArrow(static_cast<CExprArrow&>(*AstNode), ExprCtx);

            case EAstNodeType::Invoke_ArchetypeInstantiation:  goto analysis_error;

            case EAstNodeType::Invoke_MakeNamed: return AnalyzeMakeNamed(static_cast<CExprMakeNamed&>(*AstNode), ExprCtx);

            // Flow Control
            case EAstNodeType::Flow_CodeBlock: AnalyzeCodeBlock(static_cast<CExprCodeBlock&>(*AstNode), ExprCtx); return nullptr;
            case EAstNodeType::Flow_Let: goto analysis_error;
            case EAstNodeType::Flow_Defer:  goto analysis_error;
            case EAstNodeType::Flow_Return: return AnalyzeReturn(static_cast<CExprReturn&>(*AstNode), ExprCtx);
            case EAstNodeType::Flow_If:  return AnalyzeIf(static_cast<CExprIf&>(*AstNode), ExprCtx);
            case EAstNodeType::Flow_Iteration:  goto analysis_error;
            case EAstNodeType::Flow_Loop:  goto analysis_error;
            case EAstNodeType::Flow_Break:  return AnalyzeBreak(static_cast<CExprBreak&>(*AstNode), ExprCtx);
            case EAstNodeType::Flow_ProfileBlock: goto analysis_error;

            // Concurrency Primitives
            case EAstNodeType::Concurrent_Sync:  goto analysis_error;
            case EAstNodeType::Concurrent_Rush:  goto analysis_error;
            case EAstNodeType::Concurrent_Race:  goto analysis_error;
            case EAstNodeType::Concurrent_SyncIterated:  goto analysis_error;
            case EAstNodeType::Concurrent_RushIterated:  goto analysis_error;
            case EAstNodeType::Concurrent_RaceIterated:  goto analysis_error;
            case EAstNodeType::Concurrent_Branch:  goto analysis_error;
            case EAstNodeType::Concurrent_Spawn:  goto analysis_error;

            // Definitions
            case EAstNodeType::Definition_Module: goto analysis_error;
            case EAstNodeType::Definition_Enum: goto analysis_error;
            case EAstNodeType::Definition_Interface: goto analysis_error;
            case EAstNodeType::Definition_Class: goto analysis_error;
            case EAstNodeType::Definition_Data: goto analysis_error;
            case EAstNodeType::Definition_IterationPair: goto analysis_error;
            case EAstNodeType::Definition_Function: goto analysis_error;
            case EAstNodeType::Definition_TypeAlias: goto analysis_error;
            case EAstNodeType::Definition_Using: goto analysis_error;
            case EAstNodeType::Definition_Import: goto analysis_error;
            case EAstNodeType::Definition_Where: return AnalyzeWhere(static_cast<CExprWhere&>(*AstNode), ExprCtx);
            case EAstNodeType::Definition_Var: return AnalyzeVar(static_cast<CExprVar&>(*AstNode), ExprCtx);
            case EAstNodeType::Definition_ScopedAccessLevel: goto analysis_error;

            // Containing Context - may contain expressions though they aren't expressions themselves
            case EAstNodeType::Context_Project:  goto analysis_error;
            case EAstNodeType::Context_CompilationUnit: goto analysis_error;
            case EAstNodeType::Context_Package:  goto analysis_error;
            case EAstNodeType::Context_Snippet:  goto analysis_error;

            // IR nodes should not be here
            case EAstNodeType::Ir_For: goto analysis_error;
            case EAstNodeType::Ir_ForBody: goto analysis_error;
            case EAstNodeType::Ir_ArrayAdd: goto analysis_error;
            case EAstNodeType::Ir_MapAdd: goto analysis_error;
            case EAstNodeType::Ir_ArrayUnsafeCall: goto analysis_error;
            case EAstNodeType::Ir_ConvertToDynamic: goto analysis_error;
            case EAstNodeType::Ir_ConvertFromDynamic: goto analysis_error;

            default:
            analysis_error:
            {
                ULANG_ENSUREF( false, "AnalyzeExpressionAst does not know how to handle %s at %s(%d,%d : %d,%d)",
                    AstNode->GetErrorDesc().AsCString(),
                    AstNode->GetMappedVstNode()->GetSnippetPath().AsCString(),
                    AstNode->GetMappedVstNode()->Whence().BeginRow() + 1,
                    AstNode->GetMappedVstNode()->Whence().BeginColumn() + 1,
                    AstNode->GetMappedVstNode()->Whence().EndRow() + 1,
                    AstNode->GetMappedVstNode()->Whence().EndColumn() + 1);
                return TSPtr<CExpressionBase>();
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeCodeBlock(CExprCodeBlock& CodeBlock, const SExprCtx& ExprCtx)
    {
        if (!CodeBlock._AssociatedScope.IsValid())
        {
            CodeBlock._AssociatedScope = _Context._Scope->CreateNestedControlScope();
        }
        TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, CodeBlock._AssociatedScope);

        const SExprCtx LeadingStatementExprCtx = ExprCtx.AllowReturnFromLeadingStatementsAsSubexpressionOfReturn().WithResultIsIgnored();
        const SExprCtx LastStatementExprCtx =
            ExprCtx.ResultContext == ResultIsSpawned
            ? ExprCtx.WithResultIsUsed(ExprCtx.RequiredType)
            : ExprCtx;

        // Analyze the statements in the code block.
        for (int32_t StatementIndex = 0; StatementIndex < CodeBlock.GetSubExprs().Num(); ++StatementIndex)
        {
            const bool bIsLastExpression = StatementIndex == CodeBlock.GetSubExprs().Num() - 1;
            if (TSPtr<CExpressionBase> NewStatement = AnalyzeExpressionAst(
                CodeBlock.GetSubExprs()[StatementIndex].AsRef(),
                bIsLastExpression ? LastStatementExprCtx : LeadingStatementExprCtx))
            {
                CodeBlock.ReplaceSubExpr(Move(NewStatement), StatementIndex);
            }
        }

        // @TODO: SOL-1423, DetermineInvokeTime() re-traverses the expression tree, which could add up time wise
        //        (approaching on n^2) -- there should be a better way to check this on the initial ProcessExpression()
        // NOTE: The converse (i.e. async expressions used in an immediate context) is handled at the locus where it occurs
        if (ExprCtx.ResultContext == ResultIsSpawned
            && CodeBlock.DetermineInvokeTime(*_Program) != EInvokeTime::Async
            && !SemanticTypeUtils::IsUnknownType(CodeBlock.GetResultType(*_Program)))
        {
            AppendGlitch(
                CodeBlock,
                EDiagnostic::ErrSemantic_ExpectedAsyncExprs,
                "Expected async expression(s) (such as a coroutine or concurrency primitive) and only found immediate expression(s) (such as an immediate function call).");
        }
        // NOTE: We've switched to testing for errors in the `Args._ExpectedInvokeTime == EInvokeTime::Immediate`
        //       scenario with each of the async expression types instead. This allows for us to catch multiple
        //       erroring expressions (not just the first), and saves us on the re-traversal cost of 
        //       `FindFirstAsyncSubExpr()`
    }

    TSRef<CExprMakeTuple> WrapExpressionListAsTuple(TSRefArray<CExpressionBase>&& Expressions, const Vst::Node* NonReciprocalVstNode)
    {
        TSRef<CExprMakeTuple> Tuple = TSRef<CExprMakeTuple>::New(Expressions.Num());
        Tuple->SetSubExprs(Move(Expressions));
        Tuple->SetNonReciprocalMappedVstNode(NonReciprocalVstNode);
        return Tuple;
    }

    TSRef<CExprCodeBlock> WrapExpressionListAsCodeBlock(TSRefArray<CExpressionBase>&& Expressions, const Vst::Node* NonReciprocalVstNode)
    {
        TSRef<CExprCodeBlock> Block = TSRef<CExprCodeBlock>::New(Expressions.Num());
        Block->SetSubExprs(Move(Expressions));
        Block->SetNonReciprocalMappedVstNode(NonReciprocalVstNode);
        return Block;
    }

    TSRef<CExprCodeBlock> AnalyzeMacroClauseAsCodeBlock(CExprMacroCall::CClause& Clause, const Vst::Node* NonReciprocalVstNode, const SExprCtx& ExprCtx)
    {
        TSRefArray<CExpressionBase> Expressions;
        for (TSRef<CExpressionBase>& Expression : Clause.Exprs())
        {
            Expressions.Add(Move(Expression));
        }

        if (Expressions.Num() > 1 && Clause.Form() == Vst::Clause::EForm::NoSemicolonOrNewline)
        {
            // If there are multiple comma separated subexpressions, wrap them in a CExprMakeTuple that is
            // the sole subexpression of the resulting code block.
            TSRef<CExpressionBase> Tuple = WrapExpressionListAsTuple(Move(Expressions), NonReciprocalVstNode);
            Expressions = { Tuple };
        }

        TSRef<CExprCodeBlock> CodeBlock = WrapExpressionListAsCodeBlock(Move(Expressions), NonReciprocalVstNode);
        EnqueueDeferredTask(Deferred_NonFunctionExpressions, [this, CodeBlock, ExprCtx]
        {
            AnalyzeCodeBlock(*CodeBlock, ExprCtx);
        });
        return CodeBlock;
    }

    TSRef<CExpressionBase> InterpretMacroClauseAsExpression(CExprMacroCall::CClause& Clause, const Vst::Node* NonReciprocalVstNode)
    {
        TSRefArray<CExpressionBase> Expressions;
        for (TSRef<CExpressionBase>& Expression : Clause.Exprs())
        {
            Expressions.Add(Move(Expression));
        }

        TSPtr<CExpressionBase> Result;
        if (Expressions.Num() == 1)
        {
            // If this is a single expression, use it directly.
            return Expressions[0];
        }
        else if (Clause.Form() == Vst::Clause::EForm::NoSemicolonOrNewline)
        {
            // If this is an empty or comma separated list, create a tuple for the subexpressions.
            return WrapExpressionListAsTuple(Move(Expressions), NonReciprocalVstNode);
        }
        else
        {
            // Otherwise, create a code block for the subexpressions.
            return WrapExpressionListAsCodeBlock(Move(Expressions), NonReciprocalVstNode);
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzePlaceholder(CExprPlaceholder& Placeholder, const SExprCtx& ExprCtx)
    {
        // TODO: see SOL-2765.  The related error message should incorporate context.
        AppendGlitch(Placeholder, uLang::EDiagnostic::ErrSemantic_Placeholder);
        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzePathPlusSymbol(CExprPathPlusSymbol& PathPlusSymbolAst, const SExprCtx& ExprCtx)
    {
        // SOL-3214: this should get replaced with a CExprPath once the path data type is implemented
        TSRef<CExprString> Result = TSRef<CExprString>::New(
            CUTF8String(
                "%s%s%s",
                _Context._Scope->GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString(),
                PathPlusSymbolAst._Symbol.IsNull() ? "" : "/",
                PathPlusSymbolAst._Symbol.AsCString()));
        Result->SetNonReciprocalMappedVstNode(_Context._VstNode);

        if (TSPtr<CExpressionBase> ReplaceResult = AnalyzeStringLiteral(*Result, ExprCtx))
        {
            return ReplaceResult;
        }
        return Result;
    }

    // Produce an error message for a skipping jump (break/return) that is in an invalid context.
    bool ValidateSkippingJumpContext(CAstNode& AstNode)
    {
        if (_Context._Breakable)
        {
            if (_Context._Breakable->GetNodeType() == EAstNodeType::Flow_Defer)
            {
                AppendGlitch(AstNode, EDiagnostic::ErrSemantic_MayNotSkipOutOfDefer);
                return true;
            }
            else if (_Context._Breakable->GetNodeType() == EAstNodeType::Concurrent_Spawn)
            {
                AppendGlitch(AstNode, EDiagnostic::ErrSemantic_MayNotSkipOutOfSpawn);
                return true;
            }
            else if (_Context._Breakable->GetNodeType() == EAstNodeType::Concurrent_Branch)
            {
                AppendGlitch(AstNode, EDiagnostic::ErrSemantic_MayNotSkipOutOfBranch);
                return true;
            }
            else if (_Context._Breakable->GetNodeType() == EAstNodeType::Invoke_ArchetypeInstantiation)
            {
                AppendGlitch(AstNode, EDiagnostic::ErrSemantic_MayNotSkipOutOfArchetype);
                return true;
            }
        }

        return false;
    }

    TSPtr<CExpressionBase> AnalyzeBreak(CExprBreak& Break, const SExprCtx& ExprCtx)
    {       
        if (!_Context._Function)
        {
            AppendGlitch(
                Break,
                EDiagnostic::ErrSemantic_UnexpectedIdentifier,
                "`break` may only be used in a function.");
            return ReplaceMapping(Break, TSRef<CExprError>::New());
        }
        else
        {
            Break.SetResultType(&_Program->_trueType);

            // First check for an error about breaking out of specific contexts.
            if (!ValidateSkippingJumpContext(Break))
            {
                // If there wasn't an error about this specific context, check for a generic error
                // about breaking out of a non-breakable context.
                if (!_Context._Breakable || _Context._Breakable->GetNodeType() != EAstNodeType::Flow_Loop)
                {
                    AppendGlitch(Break, EDiagnostic::ErrSemantic_BreakNotInBreakableContext);
                }
            }

            if (ExprCtx.AllowedEffects[EEffect::decides])
            {
                AppendGlitch(Break, EDiagnostic::ErrSemantic_BreakInFailureContext);
            }

            // Link the associated control flow to the break AST node.
            Break._AssociatedControlFlow = _Context._Breakable;

            // Analyze the attributes on the break expression.
            EnqueueDeferredTask(Deferred_Attributes, [this, &Break]
            {
                AnalyzeAttributes(Break._Attributes, CAttributable::EAttributableScope::Expression, EAttributeSource::Expression);
            });

            return nullptr;
        }
    }

    NullPtrType AnalyzeMakeNamed(CExprMakeNamed& Expression, const SExprCtx& ExprCtx)
    {
        if (!_Context._bIsAnalyzingArgumentsInInvocation)
        {
            AppendGlitch(
                Expression,
                EDiagnostic::ErrSemantic_NamedMustBeInApplicationContext,
                CUTF8String("Named parameter '%s' only supported in a function application context", 
                    Expression.GetName().AsCString()));
        }

        if (TSPtr<CExpressionBase> Argument = AnalyzeExpressionAst(Expression.GetValue().AsRef(), ExprCtx))
        {
            Expression.SetValue(Move(Argument));
        }
        const CTypeBase* ValueType = Expression.GetValue()->GetResultType(*_Program);
        Expression.SetResultType(&_Program->GetOrCreateNamedType(Expression.GetName(), ValueType, false));
        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeReturn(CExprReturn& Return, const SExprCtx& ExprCtx)
    {
        if (!_Context._Function)
        {
            AppendGlitch(
                Return,
                EDiagnostic::ErrSemantic_UnexpectedIdentifier,
                "`return` may only be used in a routine.");
            return ReplaceMapping(Return, TSRef<CExprError>::New());
        }
        Return.SetFunction(_Context._Function);

        if (ExprCtx.ResultContext != ResultIsReturned)
        {
            if (ExprCtx.ReturnContext & ReturnIsDisallowedDueToFailureContext)
            {
                AppendGlitch(Return, EDiagnostic::ErrSemantic_ReturnInFailureContext);
            }
            else if (ExprCtx.ReturnContext & ReturnIsDisallowedDueToSubexpressionOfAnotherReturn)
            {
                AppendGlitch(Return, EDiagnostic::ErrSemantic_InvalidPositionForReturn);
            }
        }

        // Produce an error if the return was in a context that can't be skipped out of.
        ValidateSkippingJumpContext(Return);

        // Analyze the result subexpression.
        const CTypeBase* ExpectedReturnType = &_Context._Function->_NegativeType->GetReturnType();
        const bool bExpectedReturnTypeIsVoid = ExpectedReturnType->GetNormalType().IsA<CVoidType>();
        if (Return.Result())
        {
            const EReturnContext ResultReturnContext = ExprCtx.ReturnContext | ReturnIsDisallowedDueToSubexpressionOfAnotherReturn;
            if (TSPtr<CExpressionBase> NewResult = AnalyzeExpressionAst(Return.Result().AsRef(), ExprCtx.WithResultIsReturned(ExpectedReturnType).With(ResultReturnContext)))
            {
                Return.SetResult(Move(NewResult));
            }

            // If this function's return type is void, and the value being returned isn't a value of type true, produce a warning.
            if (bExpectedReturnTypeIsVoid)
            {
                const CTypeBase* ResultType = Return.Result()->GetResultType(*_Program);
                if (!IsSubtype(ResultType, &_Program->_trueType))
                {
                    AppendGlitch(*Return.Result(), EDiagnostic::WarnSemantic_VoidFunctionReturningValue);
                }
            }

            // Validate the result subexpression's type, and apply the return functor/type.
            if (TSPtr<CExpressionBase> NewResult = ApplyTypeToExpression(
                *ExpectedReturnType,
                Return.Result().AsRef(),
                EDiagnostic::ErrSemantic_IncompatibleReturnValue,
                "This function returns", "this return's argument"))
            {
                Return.SetResult(Move(NewResult));
            }
        }
        else if (!bExpectedReturnTypeIsVoid)
        {
            AppendGlitch(
                Return,
                EDiagnostic::ErrSemantic_IncompatibleReturnValue,
                CUTF8String("This function returns a value of type %s, but this return does provide a value to return.",
                    ExpectedReturnType->AsCode().AsCString()));
        }

        // Analyze the attributes on the return expression.
        EnqueueDeferredTask(Deferred_Attributes, [this, &Return]
        {
            AnalyzeAttributes(Return._Attributes, CAttributable::EAttributableScope::Expression, EAttributeSource::Expression);
        });

        return nullptr;
    }

    TSRef<CExpressionBase> ReplaceWhereWithError(CExprWhere& Where)
    {
        TSRef<CExprError> Error = TSRef<CExprError>::New();

        // Retain the where lhs+rhses as children of the error to ensure they are around for deferred
        // error reporting: e.g. reporting ambiguous definition errors using CDefinition::GetAstNode
        // for a definition introduced by the LHS of the where.
        Error->AppendChild(Where.Lhs());
        for (const TSPtr<CExpressionBase>& Rhs : Where.Rhs())
        {
            Error->AppendChild(Rhs);
        }
        return ReplaceMapping(Where, Move(Error));
    }

    TSRef<CExpressionBase> ReplaceNodeWithError(const TSRef<CExpressionBase>& Node)
    {
        TSRef<CExprError> Error = TSRef<CExprError>::New();
        Error->AppendChild(Node);
        return ReplaceMapping(*Node, Move(Error));
    }

    // Note this function will update in place the values for MaybeImmediate and MaybeIdentifier this is important because we expect the analyzed nodes to be alive for later analysis.
    template<typename OutNumberType>
    TSPtr<CExpressionBase> AnalyzeAndExtractWhereBound(CExprWhere& Where, TSRef<CExpressionBase>& Definition, TSRef<CExpressionBase>& MaybeImmediate, TSRef<CExpressionBase>& MaybeIdentifier, OutNumberType& BoundOut, Vst::BinaryOpCompare::op& Comparator)
    {
        auto NotRange = [this, &Where](const char* context = "") -> TSPtr<CExpressionBase>
        {
            AppendGlitch(Where, EDiagnostic::ErrSemantic_Unimplemented, CUTF8String("The 'where' operator is limited to numeric range-like clauses with literal bounds. e.g. `type{X:int where 0 <= X, X < 256}` or `type{X:int where X > 0}`. %s", context));
            return ReplaceWhereWithError(Where);
        };

        auto IsInf = [&](TSRef<CExpressionBase> Expression) -> bool 
        {
            if constexpr (std::is_same_v<OutNumberType, double>)
            {
                bool IsNegative = false;

                if (Expression->GetNodeType() == EAstNodeType::Invoke_UnaryArithmetic)
                {
                    IsNegative = true;
                    Expression = Expression.As<CExprUnaryArithmetic>()->Operand();
                }

                if (Expression->GetNodeType() != EAstNodeType::Identifier_Data)
                {
                    return false;
                }

                if (&Expression.As<CExprIdentifierData>()->_DataDefinition == _Program->_InfDefinition)
                {
                    BoundOut = IsNegative ? -INFINITY : INFINITY;
                    return true;
                }
            }

            return false;
        };

        auto IsLhsDataDefinition = [&](const TSPtr<CExpressionBase>& Expression)
        {
            if (Expression->GetNodeType() != EAstNodeType::Identifier_Data)
            {
                return false;
            }

            return &Expression.As<CExprIdentifierData>()->_DataDefinition == Definition.As<CExprDataDefinition>()->_DataMember;
        };

        TSPtr<CExpressionBase> GetDefinition = AnalyzeExpressionAst(MaybeIdentifier, SExprCtx::Default());
        if (GetDefinition)
        {
            MaybeIdentifier = Move(GetDefinition);
        }

        GetDefinition = AnalyzeExpressionAst(MaybeImmediate, SExprCtx::Default());
        if (GetDefinition)
        {
            MaybeImmediate = Move(GetDefinition);
        }

        if (IsInf(MaybeIdentifier))
        {
            if (!IsLhsDataDefinition(MaybeImmediate))
            {
                return NotRange("Right hand side of 'where' refers to a definition that is not the one on the left side.");
            }

            Comparator = BinaryCompareOpFlip(Comparator);
            return nullptr;
        }

        if (!IsLhsDataDefinition(MaybeIdentifier))
        {
            return NotRange("Right hand side of 'where' refers to a definition that is not the one on the left side.");
        }

        if (IsInf(MaybeImmediate))
        {
            return nullptr;
        }

        if (MaybeImmediate->GetNodeType() == EAstNodeType::Literal_Number)
        {
            const TSPtr<CExprNumber>& Immediate = MaybeImmediate.As<CExprNumber>();
            if constexpr (std::is_same_v<int64_t, OutNumberType>)
            {
                if (Immediate->IsFloat())
                {
                    return NotRange("Right side of 'where' is comparing an int to a floating point literal");
                }

                BoundOut = Immediate->GetIntValue();
                return nullptr;
            }

            if constexpr (std::is_same_v<double, OutNumberType>)
            {
                if (!Immediate->IsFloat())
                {
                    return NotRange("Right side of 'where' is comparing an float to an integer literal");
                }

                BoundOut = Immediate->GetFloatValue();
                return nullptr;
            }

            // This shouldn't be reachable but I can't figure out how to get MSVC to let me put an assert here and not error.
        }

        return NotRange("Clause had non-literal, non-`Inf` number as part of a sub-expression");
    }

    template<typename BoundType, typename Functor> 
    TSPtr<CExpressionBase> AnalyzeWhereRhsExpressions(CExprWhere& Where, TSRef<CExpressionBase>& Definition, Functor& UpdateBounds)
    {
        auto NotRange = [this, &Where](const char* context = "") -> TSPtr<CExpressionBase>
        {
            AppendGlitch(Where, EDiagnostic::ErrSemantic_Unimplemented, CUTF8String("The 'where' operator is limited to numeric range-like clauses with literal bounds. e.g. `type{X:int where 0 <= X, X < 256}` or `type{X:int where X > 0}`. %s", context));
            return ReplaceWhereWithError(Where);
        };

        TSPtrArray<CExpressionBase>& Rhs = Where.Rhs();

        for (int32_t i = 0; i < Rhs.Num(); ++i)
        {
            TSPtr<CExpressionBase> RightHandAst = Rhs[i];
            if (RightHandAst->GetNodeType() != EAstNodeType::Invoke_Comparison)
            {
                return NotRange("A clause in the right hand side of the `where` is not a comparison");
            }

            TSPtr<CExprComparison>& ComparisonAst = RightHandAst.As<CExprComparison>();
            TSPtr<CExprMakeTuple> Tuple = ComparisonAst->GetArgument().As<CExprMakeTuple>();

            Vst::BinaryOpCompare::op Comparator = ComparisonAst->Op();
            ULANG_ASSERTF(Tuple->GetNodeType() == EAstNodeType::Invoke_MakeTuple && Tuple->GetSubExprs().Num() == 2, "Comparison should be invoked with a tuple of size 2");

            TSRef<CExpressionBase> CompareLhs = Tuple->GetSubExprs()[0].AsRef();
            TSRef<CExpressionBase> CompareRhs = Tuple->GetSubExprs()[1].AsRef();
            BoundType Bound;

            if (CompareLhs->GetNodeType() != EAstNodeType::Identifier_Unresolved)
            {
                // Canonicalize so that comparisons are X <op> <number>
                Comparator = BinaryCompareOpFlip(Comparator);
                if (TSPtr<CExpressionBase> Error = AnalyzeAndExtractWhereBound(Where, Definition, CompareLhs, CompareRhs, Bound, Comparator))
                {
                    return Error;
                }
            }
            else
            {
                if (TSPtr<CExpressionBase> Error = AnalyzeAndExtractWhereBound(Where, Definition, CompareRhs, CompareLhs, Bound, Comparator))
                {
                    return Error;
                }
            }

            ComparisonAst->SetResultType(CompareLhs->GetResultType(*_Program));
            Tuple->SetResultType(&_Program->GetOrCreateTupleType({ CompareLhs->GetResultType(*_Program), CompareRhs->GetResultType(*_Program) }));

            Tuple->ReplaceSubExpr(Move(CompareLhs), 0);
            Tuple->ReplaceSubExpr(Move(CompareRhs), 1);

            if (TSPtr<CExpressionBase> Error = UpdateBounds(Comparator, Bound))
            {
                return Error;
            }
        }

        return nullptr;
    }

    // To work around the fact that we only support constrained numerics in 'where' this analysis does the hacky thing of just deleting part of the AST below the 'where'
    TSPtr<CExpressionBase> AnalyzeWhere(CExprWhere& Where, const SExprCtx& ExprCtx)
    {
        auto NotRange = [this, &Where](const char* context = "") -> TSPtr<CExpressionBase>
        {
            AppendGlitch(Where, EDiagnostic::ErrSemantic_Unimplemented, CUTF8String("The 'where' operator is limited to numeric range-like clauses with literal bounds. e.g. `type{X:int where 0 <= X, X < 256}` or `type{X:int where X > 0}`. %s", context));
            return ReplaceWhereWithError(Where);
        };

        const TSPtrArray<CExpressionBase> Rhs = Where.Rhs();

        TSRef<CExpressionBase> Definition = Where.Lhs().AsRef();
        if (TSPtr<CExpressionBase> NewDefinition = AnalyzeExpressionAst(Definition, SExprCtx::Default()))
        {
            Definition = Move(NewDefinition.AsRef());
        }
        Where.SetLhs(Definition);
        if (Definition->GetNodeType() != EAstNodeType::Definition_Data)
        {
            return NotRange("The left hand side of the `where` should be a data definition");
        }

        const CTypeBase* LhsType = Definition->GetResultType(*_Program);

        // The only forms supported are when Lhs is a numeric and the Rhs looks like a range.
        if (LhsType->GetNormalType().IsA<CIntType>())
        {
			CIntType::Constraint Min(INT64_MIN);
			CIntType::Constraint Max(INT64_MAX);

            auto UpdateBounds = [&](Vst::BinaryOpCompare::op Comparator, CIntType::Constraint Bound) -> TSPtr<CExpressionBase>
            {
                switch (Comparator)
                {
                case Vst::BinaryOpCompare::op::lt:
                {
                    // Avoid underflow, since constraints are inclusive of the range.
                    if (Bound == INT64_MIN)
                    {
                        AppendGlitch(Where, EDiagnostic::ErrSemantic_Unimplemented, "ints are currently only 64-bit so it's not possible for an int to be strictly less than the minimum int64");
                        return ReplaceWhereWithError(Where);
                    }
					Bound.GetValue()--;
                    ULANG_FALLTHROUGH;
                }
                case Vst::BinaryOpCompare::op::lteq:
                {
                    Max = CMath::Min(Max, Bound);
                    break;
                }
                case Vst::BinaryOpCompare::op::gt:
                {
                    // Avoid overflow, since constraints are inclusive of the range.
                    if (Bound == INT64_MAX)
                    {
                        AppendGlitch(Where, EDiagnostic::ErrSemantic_Unimplemented, "ints are currently only 64-bit so it's not possible for an int to be strictly greater than the maximum int64");
                        return ReplaceWhereWithError(Where);
                    }
                    Bound.GetValue()++;
                    ULANG_FALLTHROUGH;
                }
                case Vst::BinaryOpCompare::op::gteq:
                {
                    Min = CMath::Max(Min, Bound);
                    break;
                }

                case Vst::BinaryOpCompare::op::eq:
                case Vst::BinaryOpCompare::op::noteq:
                    return NotRange("A comparison in the rhs is not one of <, <=, >, >=.");
                default:
                    ULANG_UNREACHABLE();
                }

                return nullptr;
            };

            if (TSPtr<CExpressionBase> Error = AnalyzeWhereRhsExpressions<int64_t>(Where, Definition, UpdateBounds))
            {
                return Error;
            }

            const CIntType& Type = _Program->GetOrCreateConstrainedIntType(Min, Max);
            Where.SetResultType(&Type);
            Definition->RefineResultType(&Type);

            return nullptr;
        }
        else if (LhsType->GetNormalType().IsA<CFloatType>())
        {
            // These are ok as bounds because we don't accept nan as a bound right now.
            double Min = -INFINITY;
            double Max = INFINITY;

            auto UpdateBounds = [&](Vst::BinaryOpCompare::op Comparator, double Bound) -> TSPtr<CExpressionBase>
            {
                switch (Comparator)
                {
                case Vst::BinaryOpCompare::op::lt:
                {
                    // Avoid underflow, since constraints are inclusive of the range.
                    if (Bound == -INFINITY)
                    {
                        AppendGlitch(Where, EDiagnostic::ErrSemantic_Unimplemented, "It's not possible for a float to be strictly less than negative infinity");
                        return ReplaceMapping(Where, TSRef<CExprError>::New());
                    }
                    if (Bound == INFINITY)
                    {
                        Bound = std::numeric_limits<double>::max();
                    }
                    else
                    {
                        // If Bound is DLB_MIN then this technically sets error codes but we'll get the right answer.
                        // Also we have to use -0.0 since Verse doesn't distinguish between zeros.
                        Bound = std::nexttoward(Bound ? Bound : -0.0, -INFINITY);
                    }
                    ULANG_FALLTHROUGH;
                }
                case Vst::BinaryOpCompare::op::lteq:
                {
                    Max = CMath::Min(Max, Bound);
                    break;
                }
                case Vst::BinaryOpCompare::op::gt:
                {
                    // Avoid overflow, since constraints are inclusive of the range.
                    if (Bound == INFINITY)
                    {
                        AppendGlitch(Where, EDiagnostic::ErrSemantic_Unimplemented, "It's not possible for a float to be strictly greater than infinity");
                        return ReplaceMapping(Where, TSRef<CExprError>::New());
                    }
                    if (Bound == -INFINITY)
                    {
                        Bound = -std::numeric_limits<double>::max();
                    }
                    else
                    {
                        // If Bound is DLB_MIN then this technically sets error codes but we'll get the right answer.
                        // Also we have to use 0.0 since Verse doesn't distinguish between zeros.
                        Bound = std::nexttoward(Bound ? Bound : 0.0, INFINITY);
                    }
                    ULANG_FALLTHROUGH;
                }
                case Vst::BinaryOpCompare::op::gteq:
                {
                    Min = CMath::Max(Min, Bound);
                    break;
                }

                case Vst::BinaryOpCompare::op::eq:
                case Vst::BinaryOpCompare::op::noteq:
                    return NotRange("A comparison in the rhs is not one of <, <=, >, >=.");
                default:
                    ULANG_UNREACHABLE();
                }

                return nullptr;
            };

            if (TSPtr<CExpressionBase> Error = AnalyzeWhereRhsExpressions<double>(Where, Definition, UpdateBounds))
            {
                return Error;
            }

            const CFloatType& Type = _Program->GetOrCreateConstrainedFloatType(Min, Max);
            Where.SetResultType(&Type);
            Definition->RefineResultType(&Type);

            // We need to remove the sub-expressions here so we don't end up generating them.
            Where.SetRhs({});

            return nullptr;
        }

        // where is not yet implemented as a general expression.
        AppendGlitch(Where, EDiagnostic::ErrSemantic_Unimplemented, "The 'where' operator is only supported in function parameter definitions, or as a range-like condition on numerics");
        return ReplaceWhereWithError(Where);
    }

    TSPtr<CExpressionBase> AnalyzeVar(CExprVar& Var, const SExprCtx& ExprCtx)
    {
        // `var` is not yet implemented as a general expression.
        AppendGlitch(
            Var,
            EDiagnostic::ErrSemantic_Unimplemented,
            "`var` is only supported on the left-hand side of a definition.");
        return ReplaceMapping(Var, TSRef<CExprError>::New());
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeNumberLiteral( CExprNumber& NumLiteralAst, const SExprCtx& ExprCtx, bool bIsNegative=false )
    {
        const Vst::Node* VstNode = NumLiteralAst.GetMappedVstNode();

        errno = 0;

        if (VstNode->IsA<Vst::FloatLiteral>())
        {
            const Vst::FloatLiteral& FloatLiteral = VstNode->As<Vst::FloatLiteral>();
            const CUTF8String& String = FloatLiteral.GetSourceText();

            if (FloatLiteral._Format != Vst::FloatLiteral::EFormat::F64)
            {
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_Unsupported,
                    FloatLiteral._Format == Vst::FloatLiteral::EFormat::Unspecified
                    ? CUTF8String("Rational number literal '%s' isn't supported", String.AsCString())
                    : "Unsupported float format, only 'f64' is supported");
                return ReplaceMapping(NumLiteralAst, TSRef<CExprError>::New());
            }

            char* StringEnd = nullptr;
            double ValueF64 = ::strtod(String.AsCString(), &StringEnd);
            if (errno==ERANGE)
            {
                // Ignore underflow.
                if (ValueF64 == HUGE_VAL || ValueF64 == -HUGE_VAL)
                {
                    AppendGlitch(
                        VstNode,
                        EDiagnostic::ErrSemantic_FloatLiteralOutOfRange,
                        "Float literal must be smaller than 1.7976931348623158e+308.");
                    ValueF64 = 0.0;
                    return ReplaceMapping(NumLiteralAst, TSRef<CExprError>::New());
                }
            }
            else
            {
                ULANG_ASSERTF(!errno, "errno set after calling strtod");
            }

            NumLiteralAst.SetFloatValue(*_Program, Float(bIsNegative ? -ValueF64 : ValueF64));
        }
        else
        {
            const CUTF8String& String = VstNode->As<Vst::IntLiteral>().GetSourceText();
            char* StringEnd = nullptr;

            // If the number is prefixed with a negative sign, then bIsNegative is passed into 
            // this function and the only thing we parse here is the magnitude, so we use the 
            // unsigned conversion function and figure out if the number is in range later.
            const unsigned long long ValueMagnitude = ::strtoull(String.AsCString(), &StringEnd, 0);

            bool bIsInRange = true;

            // if the magnitude is out of range, strtoull returns ULLONG_MAX and sets errno to ERANGE
            if ((ValueMagnitude == ULLONG_MAX) && (errno == ERANGE))
            {
                bIsInRange = false;
            }

            // we don't allow negative magnitudes beyond what is valid
            if (bIsNegative)
            {
                if (ValueMagnitude > Int64MaxMagnitude)
                {
                    bIsInRange = false;
                }
            }
            else
            {
                if (ValueMagnitude > Int64Max)
                {
                    bIsInRange = false;
                }
            }

            if (!bIsInRange)
            {
                AppendGlitch(
                    NumLiteralAst,
                    EDiagnostic::ErrSemantic_IntegerLiteralOutOfRange,
                    CUTF8String("Integer literal must be in the range %lld to %lld.", Int64Min, Int64Max));
                return ReplaceMapping(NumLiteralAst, TSRef<CExprError>::New());
            }

            int64_t ValueI64 = 0;

            if (bIsInRange)
            {
                if (bIsNegative)
                {
                    ValueI64 = static_cast<int64_t>(~ValueMagnitude + 1);
                }
                else
                {
                    ValueI64 = (int64_t)ValueMagnitude;
                }
            }

            NumLiteralAst.SetIntValue(*_Program, ValueI64);
        }

        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeCharLiteral(CExprChar& CharLiteral, const SExprCtx& ExprCtx)
    {
        switch (CharLiteral._Type)
        {
        case CExprChar::EType::UTF8CodeUnit:
            CharLiteral.SetResultType(&_Program->_char8Type);
            break;
        case CExprChar::EType::UnicodeCodePoint:
            CharLiteral.SetResultType(&_Program->_char32Type);
            break;
        default:
            ULANG_UNREACHABLE();
        }
            
        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeStringLiteral(CExprString& StringLiteral, const SExprCtx& ExprCtx)
    {
        StringLiteral.SetResultType(_Program->_stringAlias->GetType());
        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzePathLiteral(CExprPath& PathLiteral, const SExprCtx& ExprCtx)
    {
        if (ExprCtx.ResultContext != Cases<ResultIsImported, ResultIsUsedAsQualifier>)
        {
            AppendGlitch(PathLiteral, EDiagnostic::ErrSemantic_Unimplemented, "Path literals are only implemented for use as a qualifier, or as an argument to the using macro or import function.");
        }
        PathLiteral.SetResultType(&_Program->_pathType);
        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeFunctionLiteral(CExprFunctionLiteral& FunctionLiteralAst, const SExprCtx& ExprCtx)
    {
        AppendGlitch(FunctionLiteralAst, EDiagnostic::ErrSemantic_Unimplemented, "Function literals are not yet implemented.");
        return ReplaceMapping(FunctionLiteralAst, TSRef<CExprError>::New());
    }

    //-------------------------------------------------------------------------------------------------
    bool RequireUnqualifiedIdentifier(const CExprIdentifierUnresolved& IdentifierAst)
    {
        if (IdentifierAst.Qualifier())
        {
            AppendGlitch(IdentifierAst, EDiagnostic::ErrSemantic_Unsupported, "Qualified identifiers are not yet supported");
            return false;
        }
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    //template<typename DefinitionType, typename Allocator>
    static CUTF8String FormatDefinitionPairs(const SResolvedDefinitionArray& Definitions, CUTF8StringView Conjunction = "")
    {
        CUTF8StringBuilder StringBuilder;
        for (int32_t DefinitionIndex = 0; DefinitionIndex < Definitions.Num(); ++DefinitionIndex)
        {
            const SResolvedDefinition& ResolvedDefn = Definitions[DefinitionIndex];
            if (DefinitionIndex != 0)
            {
                StringBuilder.Append(Definitions.Num() > 2 ? ", " : " ");
                if (DefinitionIndex + 1 == Definitions.Num())
                {
                    StringBuilder.Append(Conjunction);
                }
            }
            StringBuilder.AppendFormat("%s.%s",
                ResolvedDefn._Definition->_EnclosingScope.GetScopePath().AsCString(),
                ResolvedDefn._Definition->AsNameCString());
        }
        return StringBuilder.MoveToString();
    }

    //-------------------------------------------------------------------------------------------------
    template<typename DefinitionType, typename Allocator>
    static CUTF8String FormatDefinitionList(const TArrayG<DefinitionType*, Allocator>& Definitions, CUTF8StringView Conjunction = "")
    {
        CUTF8StringBuilder StringBuilder;
        for (int32_t DefinitionIndex = 0; DefinitionIndex < Definitions.Num(); ++DefinitionIndex)
        {
            const CDefinition* Definition = Definitions[DefinitionIndex];
            if (DefinitionIndex != 0)
            {
                StringBuilder.Append(Definitions.Num() > 2 ? ", " : " ");
                if (DefinitionIndex + 1 == Definitions.Num())
                {
                    StringBuilder.Append(Conjunction);
                }
            }
            StringBuilder.AppendFormat("%s.%s",
                Definition->_EnclosingScope.GetScopePath().AsCString(),
                Definition->AsNameCString());
        }
        return StringBuilder.MoveToString();
    }
    
    //-------------------------------------------------------------------------------------------------
    static CUTF8String FormatParameterList(CFunctionType::ParamTypes const& ParamTypes)
    {
        CUTF8StringBuilder StringBuilder;
        bool bFirst = true;
        for (const CTypeBase* ParamType : ParamTypes)
        {
            if (bFirst)
            {
                bFirst = false;
            }
            else
            {
                StringBuilder.Append(',');
            }
            StringBuilder.Append(':');
            StringBuilder.Append(ParamType->AsCode(ETypeSyntaxPrecedence::Definition));
        }
        return StringBuilder.MoveToString();
    }
    
    //-------------------------------------------------------------------------------------------------
    struct SOverload
    {
        const CDefinition* Definition;
        const TArray<SInstantiatedTypeVariable> InstantiatedTypeVariables;
        const CFunctionType* FunctionType;
        const CTypeBase* NegativeReturnType;
    };

    //-------------------------------------------------------------------------------------------------
    static CUTF8String DescribeAmbiguousDefinition(const CDefinition& Definition)
    {
        CUTF8StringBuilder StringBuilder;
        StringBuilder.Append(DefinitionKindAsCString(Definition.GetKind()));
        StringBuilder.Append(' ');
        StringBuilder.Append(GetQualifiedNameString(Definition));
        if (const CFunction* Function = Definition.AsNullable<CFunction>())
        {
            if (Function->_Signature.GetFunctionType())
            {
                StringBuilder.Append('(');
                StringBuilder.Append(FormatParameterList(Function->_Signature.GetFunctionType()->GetParamTypes()));
                StringBuilder.Append(')');
            }
        }
        const CAstPackage* Package = Definition._EnclosingScope.GetPackage();
        if (Package)
        {
            StringBuilder.Append(" in package ");
            StringBuilder.Append(Package->_Name);
        }
        else if (Definition.GetKind() == CDefinition::EKind::Module)
        {
            const CModule& Module = static_cast<const CModule&>(Definition);
            StringBuilder.Append(Module.GetParts().Num() == 1 ? " in package " : " in packages ");
            for (const CModulePart* Part : Module.GetParts())
            {
                Package = Part->GetPackage();
                if (ULANG_ENSUREF(Package, "Every module part must have a package."))
                {
                    if (Part != Module.GetParts()[0])
                    {
                        StringBuilder.Append(", ");
                    }
                    StringBuilder.Append(Package->_Name);
                }
            }
        }
        return StringBuilder.MoveToString();
    }

    //-------------------------------------------------------------------------------------------------
    template<typename Allocator>
    static CUTF8String FormatOverloadList(const TArrayG<SOverload, Allocator>& Overloads)
    {
        CUTF8StringBuilder StringBuilder;
        for (int32_t OverloadIndex = 0; OverloadIndex < Overloads.Num(); ++OverloadIndex)
        {
            const SOverload& Overload = Overloads[OverloadIndex];
            StringBuilder.Append("\n    ");
            // TODO (SOL-2673) add file and line number information.
            if (!Overload.FunctionType)
            {
                StringBuilder.Append("type function introduced by ");
            }
            StringBuilder.Append(DescribeAmbiguousDefinition(*Overload.Definition));
        }
        return StringBuilder.MoveToString();
    }

    //-------------------------------------------------------------------------------------------------
    template<typename DefinitionType, typename Allocator>
    static CUTF8String FormatConflictList(const TArrayG<DefinitionType, Allocator>& Definitions)
    {
        CUTF8StringBuilder StringBuilder;
        for (int32_t Index = 0; Index < Definitions.Num(); ++Index)
        {
            DefinitionType Definition = Definitions[Index];
            StringBuilder.Append("\n    ");
            StringBuilder.Append(DescribeAmbiguousDefinition(*Definition));
        }
        return StringBuilder.MoveToString();
    }

    //-------------------------------------------------------------------------------------------------
    template<typename Allocator>
    static CUTF8String FormatOverloadList(const TArrayG<const CDefinition*, Allocator>& Definitions)
    {
        TArrayG<SOverload, Allocator> Overloads;
        Overloads.Reserve(Definitions.Num());
        for (const CDefinition* Definition : Definitions)
        {
            if (const CFunction* Function = Definition->AsNullable<CFunction>())
            {
                Overloads.Add(SOverload{Function, {}, Function->_Signature.GetFunctionType(), nullptr});
            }
            else
            {
                Overloads.Add(SOverload{Definition, {}, nullptr, nullptr});
            }
        }
        return FormatOverloadList(Overloads);
    }

    //-------------------------------------------------------------------------------------------------
    template<typename Allocator>
    static CUTF8String FormatOverloadList(const TArrayG<const CFunction*, Allocator>& Functions)
    {
        TArrayG<SOverload, Allocator> Overloads;
        Overloads.Reserve(Functions.Num());
        for (const CFunction* Function : Functions)
        {
            Overloads.Add(SOverload{Function, {}, Function->_Signature.GetFunctionType(), nullptr});
        }
        return FormatOverloadList(Overloads);
    }

    bool IsQualifierNamed(const TSRef<CExpressionBase>& Qualifier, const CSymbol& Name)
    {
        const EAstNodeType QualifierNodeType = Qualifier->GetNodeType();
        if (QualifierNodeType == EAstNodeType::Identifier_Unresolved)
        {
            CExprIdentifierUnresolved& QualifierIdentifier = static_cast<CExprIdentifierUnresolved&>(*Qualifier);
            return QualifierIdentifier._Symbol == Name;
        }
        // TODO: (yiliang.siew) This will need to become more generic as we support qualifiers on more identifier types.
        else if (QualifierNodeType != EAstNodeType::Literal_Path)
        {
            AppendGlitch(*Qualifier, EDiagnostic::ErrSemantic_Unsupported, CUTF8String{"Unsupported qualifier: %s", Qualifier->GetErrorDesc().AsCString()});
        }
        return false;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> ResolveIdentifierToDefinitions(
        CExprIdentifierUnresolved& Identifier,
        bool bIsExtensionField, 
        const SResolvedDefinitionArray& Definitions,
        TSPtr<CExpressionBase>&& Context, 
        TSPtr<CExpressionBase>&& Qualifier,
        const SExprCtx& ExprCtx,
        const SExprArgs ExprArgs)
    {
        // The desugarer creates identifiers for infix operator calls that don't have an associated VST node,
        // so we have to use FindMappedVstNode instead of GetMappedVstNode to find an appropriate error context.
        const Vst::Node* VstNode = FindMappedVstNode(Identifier);

        // Handle overloaded functions.
        if (Definitions.Num() > 1)
        {
            const CDefinition* TypeDefinition = nullptr;
            int NumberOfTypeDefinitions = 0;
            bool bAreAllDefinitionsFunctions = true; // Small lie, one type is ok
            for (const SResolvedDefinition& ResolvedDefn : Definitions)
            {
                // Extract class, if any, while checking if all the other are functions
                if (DefinitionAsType(*ResolvedDefn._Definition))
                {
                    // If multiple types with the same name is defined, then we keep the first.
                    if (!TypeDefinition) { TypeDefinition = ResolvedDefn._Definition; }
                    ++NumberOfTypeDefinitions;
                }
                else if (!ResolvedDefn._Definition->IsA<CFunction>())
                {
                    // Note: this is a conservative approximation, because it ignores data definitions of function type.
                    bAreAllDefinitionsFunctions = false;
                }
            }

            if (NumberOfTypeDefinitions > 1)
            {
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_AmbiguousIdentifier,
                    CUTF8String(NumberOfTypeDefinitions == Definitions.Num()
                        ? "Identifier %s could be one of many types: %s"
                        : "Identifier %s could be either type or function: %s",
                        Identifier._Symbol.AsCString(), 
                        FormatDefinitionPairs(Definitions, "or ").AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New(TURef<CUnknownType>::New(Identifier._Symbol, *_Context._Scope)));
            }

            if (bAreAllDefinitionsFunctions)
            {
                // Once it was allowed to overload one type and one or more functions (with some restrictions),
                // this hos not been true for some time. However, if only the type is accessible, then it's fine. 
                if (TypeDefinition && (ExprCtx.ResultContext == ResultIsUsedAsType || ExprCtx.ResultContext == ResultIsCalledAsMacro || ExprCtx.ResultContext == ResultIsUsedAsQualifier))
                {
                    if (NumberOfTypeDefinitions > 1)
                    {
                        AppendGlitch(
                            VstNode,
                            EDiagnostic::ErrSemantic_AmbiguousIdentifier,
                            CUTF8String("Ambiguous identifier; there are %d types with the name %s.", NumberOfTypeDefinitions, Identifier._Symbol.AsCString()));
                        return ReplaceMapping(Identifier, TSRef<CExprError>::New(TURef<CUnknownType>::New(Identifier._Symbol, *_Context._Scope)));
                    }

                    // If using this overloaded identifier as a type, then none of the other functions can be accessible.
                    EnqueueDeferredTask(Deferred_FinalValidation, [this, TypeDefinition, bAllowUnrestrictedAccess = Identifier._bAllowUnrestrictedAccess, Context = _Context, Definitions, VstNode]
                    {
                        SResolvedDefinitionArray AccessibleDefinitions;

                        for (const SResolvedDefinition& ResolvedDefn : Definitions)
                        {
                            if (ResolvedDefn._Definition != TypeDefinition &&
                                (bAllowUnrestrictedAccess || ResolvedDefn._Definition->IsAccessibleFrom(*Context._Scope)))
                            {
                                AccessibleDefinitions.Add(ResolvedDefn);
                            }
                        }

                        if (!AccessibleDefinitions.IsEmpty())
                        {
                            AppendGlitch(
                                VstNode,
                                EDiagnostic::ErrSemantic_AmbiguousIdentifier,
                                CUTF8String(
                                    "The type %s is ambigious with the following functions: %s",
                                    DescribeAmbiguousDefinition(*TypeDefinition).AsCString(),
                                    FormatDefinitionPairs(AccessibleDefinitions, "or ").AsCString()));
                        }
                    });

                    return ResolveIdentifierToDefinition(Identifier, bIsExtensionField, TypeDefinition, Move(Context), Move(Qualifier), ExprCtx, ExprArgs);
                }
                // Even if the result is unused, we should still raise a semantic analysis error since otherwise
                // syntax such as `Thing.OverloadedFunction` would still pass semantic analysis and the code generator has to account for it.
                else if (ExprCtx.ResultContext != ResultIsCalled
                         && ExprCtx.ResultContext != ResultIsCalledAsMacro
                         && !ExprCtx.bAllowNonInvokedReferenceToOverloadedFunction)
                {
                    // Only allow overloaded functions in contexts that immediately call them.
                    if (ExprCtx.ResultContext == ResultIsIgnored)
                    {  // Result isn't used, return the first one to make later compiler pass happy.
                        return ResolveIdentifierToDefinition(Identifier, bIsExtensionField, Definitions[0]._Definition, Move(Context), Move(Qualifier), ExprCtx, ExprArgs);
                    } 
                    else
                    {
                        AppendGlitch(
                            VstNode,
                            EDiagnostic::ErrSemantic_Unimplemented,
                            CUTF8String(
                                "Referencing an overloaded function without immediately calling it is not yet implemented; %s",
                                FormatDefinitionPairs(Definitions, "or ").AsCString())
                        );
                        return ReplaceMapping(Identifier, TSRef<CExprError>::New(TURef<CUnknownType>::New(Identifier._Symbol, *_Context._Scope)));
                    }
                }
                else
                {
                    TArray<const CFunction*> OverloadedFunctions;
                    for (const SResolvedDefinition& ResolvedDefn : Definitions)
                    {
                        if (const CFunction* Function = ResolvedDefn._Definition->AsNullable<CFunction>())
                        {
                            OverloadedFunctions.Add(Function);
                        }
                        else if (DefinitionAsType(*ResolvedDefn._Definition))
                        {
                            // Already done above, if there is one then it's in TypeDefinition
                        }
                        else
                        {
                            ULANG_ERRORF("Unexpected non-function definition");
                        }
                    }

                    TSRef<CExprIdentifierOverloadedFunction> FunctionIdentifier = TSRef<CExprIdentifierOverloadedFunction>::New(
                        Move(OverloadedFunctions),
                        false,
                        Identifier._Symbol,
                        TypeDefinition? DefinitionAsType(*TypeDefinition) : nullptr,
                        Move(Context),
                        Move(Qualifier),
                        &_Program->_anyType);

                    FunctionIdentifier->_bAllowUnrestrictedAccess = Identifier._bAllowUnrestrictedAccess;
                    FunctionIdentifier->_Attributes = Move(Identifier._Attributes);

                    if (FunctionIdentifier->HasAttributes())
                    {
                        EnqueueDeferredTask(Deferred_Attributes, [this, FunctionIdentifier, Context = _Context]
                        {
                            TGuardValue CurrentContextGuard(_Context, Context);
                            AnalyzeAttributes(FunctionIdentifier->_Attributes, CAttributable::EAttributableScope::Function, EAttributeSource::Identifier);
                            FunctionIdentifier->_bConstructor = FunctionIdentifier->HasAttributeClass(_Program->_constructorClass, *_Program);
                        });
                        EnqueueDeferredTask(Deferred_ValidateAttributes, [this, FunctionIdentifier, VstNode, Context = _Context, ExprArgs]
                        {
                            TGuardValue CurrentContextGuard(_Context, Context);
                            if (FunctionIdentifier->_bConstructor && ExprArgs.ArchetypeInstantiationContext != ConstructorInvocationCallee)
                            {
                                AppendGlitch(VstNode, EDiagnostic::ErrSemantic_IdentifierConstructorAttribute);
                            }
                        });
                    }

                    return ReplaceMapping(
                        Identifier,
                        Move(FunctionIdentifier));
                }
            }
            else
            {
                // Is there some ambiguity?
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_AmbiguousIdentifier,
                    CUTF8String("Ambiguous identifier; could be %s", FormatDefinitionPairs(Definitions, "or ").AsCString()));

                // If only one isn't a module, use that one. It isn't correct in any way but it generates less spurious errors 
                // in the case this is old code that didn't know modules could clash with other identifiers.
                // The same is true for new code that by accident defines a new local identifier that clashes with a module identifier.
                const CDefinition* PreferredDefinition = nullptr;
                int NonModule = 0;
                for (const SResolvedDefinition& ResolvedDefn : Definitions)
                {
                    if (!ResolvedDefn._Definition->IsA<CModule>())
                    {
                        if (++NonModule > 1) 
                        {
                            break;
                        }
                        PreferredDefinition = ResolvedDefn._Definition;
                    }
                }
                if (NonModule == 1)
                {
                    return ResolveIdentifierToDefinition(Identifier, bIsExtensionField, PreferredDefinition, Move(Context), Move(Qualifier), ExprCtx, ExprArgs);
                }
                return ReplaceMapping(Identifier, TSRef<CExprError>::New(TURef<CUnknownType>::New(Identifier._Symbol, *_Context._Scope)));
            }
        }

        ULANG_ASSERTF(Definitions.Num(), "Expected at least one definition");
        const SResolvedDefinition& ResolvedDefn = Definitions[0];

        // If Definition has a context use it
        if (ResolvedDefn._Context)
        {
            TSRef<CExprIdentifierData> ExprIdentData = TSRef<CExprIdentifierData>::New(*_Program, *ResolvedDefn._Context);
            ExprIdentData->SetNonReciprocalMappedVstNode(VstNode);  // Map it to member VST for debugging / lookup
             
            // Cannot have both a paired inferred context and a specified context so replacing is okay.
            Context = ExprIdentData;
        }

        return ResolveIdentifierToDefinition(Identifier, bIsExtensionField, ResolvedDefn._Definition, Move(Context), Move(Qualifier), ExprCtx, ExprArgs);
    }

    void ValidateDefinitionUse(const CDefinition& Definition, const Vst::Node* VstNode)
    {
        EnqueueDeferredTask(Deferred_ValidateAttributes, [this, &Definition, VstNode]
        {
            if (Definition.IsDeprecated())
            {
                bool bAllowDeprecated = false;
                for (const CDefinition* UsingDefinition : _Context._EnclosingDefinitions)
                {
                    if (UsingDefinition->IsDeprecated())
                    {
                        bAllowDeprecated = true;
                        break;
                    }
                }

                if (!bAllowDeprecated)
                {
                    AppendGlitch(
                        VstNode,
                        EDiagnostic::WarnSemantic_UseOfDeprecatedDefinition,
                        CUTF8String("'%s' is deprecated", GetQualifiedNameString(Definition).AsCString()));
                }
            }

            if (Definition.IsExperimental())
            {
                bool AllowExperimental = _Context._Package->_bAllowExperimental;
                for (const CDefinition* UsingDefinition : _Context._EnclosingDefinitions)
                {
                    if (UsingDefinition->IsExperimental())
                    {
                        AllowExperimental = true;
                        break;
                    }
                }

                if (!AllowExperimental)
                {
                    AppendGlitch(
                        VstNode, 
                        EDiagnostic::ErrSemantic_UseOfExperimentalDefinition,
                        CUTF8String("'%s' is experimental, and its use will prevent you from publishing your project. To silence this message, enable experimental features via the settings panel.", GetQualifiedNameString(Definition).AsCString()));
                }

                // Track uses of experimental definitions in user packages.
                if (_Context._Package->_VerseScope == Cases<EVerseScope::PublicUser, EVerseScope::InternalUser>)
                {
                    _Diagnostics->AppendUseOfExperimentalDefinition();
                }
            }
        });
    }

    void ValidateExperimentalAttribute(CDefinition& Definition)
    {
        ULANG_ASSERT(_CurrentTaskPhase >= EDeferredPri::Deferred_AttributeClassAttributes);
        if (Definition.IsExperimental() && Definition._EnclosingScope.IsControlScope())
        {
            AppendGlitch(*Definition.GetAstNode(), EDiagnostic::ErrSemantic_InvalidAttribute, "The @experimental attribute cannot be used on local definitions.");
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> ResolveIdentifierToDefinition(
        CExprIdentifierUnresolved& Identifier,
        bool bIsExtensionField,
        const CDefinition* Definition,
        TSPtr<CExpressionBase>&& Context,
        TSPtr<CExpressionBase>&& Qualifier,
        const SExprCtx& ExprCtx,
        const SExprArgs& ExprArgs)
    {
        const Vst::Node* VstNode = FindMappedVstNode(Identifier);

        ULANG_ASSERTF(
            !bIsExtensionField || Definition->IsA<CFunction>(),
            "Unexpected extension field accessor that isn't a function: %s",
            DescribeAmbiguousDefinition(*Definition).AsCString());

        if (!Identifier._bAllowUnrestrictedAccess)
        {
            // Validate access permissions
            DeferredRequireAccessible(VstNode, *_Context._Scope, *Definition);
        }

        ValidateDefinitionUse(*Definition, VstNode);

        // Resolve the definition to the appropriate identifier node.
        switch (Definition->GetKind())
        {
        case CDefinition::EKind::Class:
        {
            // Is it a class type identifier?
            const CClassDefinition* Class = &Definition->AsChecked<CClassDefinition>();

            MaybeAppendAttributesNotAllowedError(Identifier);
            if (ExprCtx.ResultContext != ResultIsUsedAsType
                && ExprCtx.ResultContext != ResultIsUsedAsAttribute
                 && ExprCtx.ResultContext != ResultIsCalledAsMacro) 
            {
                EnqueueDeferredTask(Deferred_ValidateType, [this, Class, VstNode]
                {
                    if (SemanticTypeUtils::IsAttributeType(Class))
                    {
                        AppendGlitch(
                            VstNode,
                            EDiagnostic::ErrSemantic_IncorrectUseOfAttributeType,
                            CUTF8String("The identifier '%s' is an attribute, not a class", Class->GetScopeName().AsCString()));
                    }
                });
            }

            return ReplaceMapping(Identifier, TSRef<CExprIdentifierClass>::New(Class->GetTypeType(), Move(Context), Move(Qualifier)));
        }
        case CDefinition::EKind::Enumeration:
        {
            // Is it an enumeration type identifier?
            const CEnumeration* Enumeration = &Definition->AsChecked<CEnumeration>();

            MaybeAppendAttributesNotAllowedError(Identifier);

            const CTypeType& TypeType = _Program->GetOrCreateTypeType(Enumeration, Enumeration);
            return ReplaceMapping(Identifier, TSRef<CExprEnumerationType>::New(&TypeType, Move(Context), Move(Qualifier)));
        }
        case CDefinition::EKind::Enumerator:
        {
            // Is it an enumerator literal?
            const CEnumerator* Enumerator = &Definition->AsChecked<CEnumerator>();

            MaybeAppendAttributesNotAllowedError(Identifier);

            return ReplaceMapping(Identifier, TSRef<CExprEnumLiteral>::New(Enumerator, Move(Context)));
        }
        case CDefinition::EKind::Interface:
        {
            // Is it a interface type identifier?
            const CInterface* Interface = &Definition->AsChecked<CInterface>();

            MaybeAppendAttributesNotAllowedError(Identifier);

            const CTypeType& TypeType = _Program->GetOrCreateTypeType(Interface->_NegativeInterface, Interface);
            return ReplaceMapping(Identifier, TSRef<CExprInterfaceType>::New(&TypeType, Move(Context), Move(Qualifier)));
        }
        case CDefinition::EKind::Function:
        {
            // Is it a function identifier?
            const CFunction* Function = &Definition->AsChecked<CFunction>();

            const bool bSuperQualified = Qualifier && IsQualifierNamed(Qualifier.AsRef(), _SuperName);
            if (bSuperQualified && VerseFN::UploadedAtFNVersion::CheckSuperQualifiers(_Context._Package->_UploadedAtFNVersion))
            {
                const CFunction* SuperFunction = Function->GetOverriddenDefinition();
                if (!SuperFunction)
                {
                    CUTF8String ErrorMessage("Not possible to use (super:) on %s since it doesn't override another function.", Function->AsNameCString());
                    AppendGlitch(VstNode, EDiagnostic::ErrSemantic_Unsupported, Move(ErrorMessage));
                    TSRef<CExprError> Error = TSRef<CExprError>::New();
                    return ReplaceMapping(Identifier, Move(Error));
                }
                Function = SuperFunction;
            }

            const bool bResultIsCalled = Function->_ExtensionFieldAccessorKind == EExtensionFieldAccessorKind::ExtensionDataMember || (ExprCtx.ResultContext == ResultIsCalled);
            const bool bResultIsIgnored = ExprCtx.ResultContext == ResultIsIgnored;
            const bool bIsIntrinsic = Function->HasAttributeClass(_Program->_intrinsicClass, *_Program);

            SInstantiatedFunction InstFunction = Instantiate(*Function);
            const CFunctionType* FunctionType = InstFunction._Type;
            const CTypeBase* NegativeReturnType = InstFunction._NegativeReturnType;
            if (!FunctionType || !NegativeReturnType)
            {
                TSRef<CExprError> Error = TSRef<CExprError>::New();
                Error->AppendChild(Move(Context));
                Error->AppendChild(Move(Qualifier));
                return ReplaceMapping(Identifier, Move(Error));
            }

            if (FunctionType && ExprCtx.ResultContext != ResultIsUsedAsAttribute)
            {
                EnqueueDeferredTask(Deferred_ValidateType, [this, Function, FunctionType, VstNode]
                {
                    if (!SemanticTypeUtils::IsUnknownType(&FunctionType->GetReturnType()) // No glitch if unknown type, it's already reported somewhere else
                        && SemanticTypeUtils::IsAttributeType(&FunctionType->GetReturnType()))
                    {
                        AppendGlitch(
                            VstNode,
                            EDiagnostic::ErrSemantic_IncorrectUseOfAttributeType,
                            CUTF8String("The identifier '%s' is an attribute, not a function", Function->AsNameCString()));
                    }
                });
            }

            TSRef<CExprIdentifierFunction> FunctionIdentifier = TSRef<CExprIdentifierFunction>::New(
                *Function,
                Move(InstFunction._InstantiatedTypeVariables),
                FunctionType,
                nullptr,
                Move(Context),
                Move(Qualifier),
                bSuperQualified); // Qualifier is consumed if bSuperQualified is true!

            FunctionIdentifier->_Attributes = Move(Identifier._Attributes);

            if (FunctionIdentifier->HasAttributes())
            {
                EnqueueDeferredTask(Deferred_Attributes, [this, FunctionIdentifier, NegativeReturnType, Context = _Context]
                {
                    TGuardValue CurrentContextGuard(_Context, Context);
                    AnalyzeAttributes(FunctionIdentifier->_Attributes, CAttributable::EAttributableScope::Function, EAttributeSource::Identifier);
                    if (FunctionIdentifier->HasAttributeClass(_Program->_constructorClass, *_Program))
                    {
                        FunctionIdentifier->_ConstructorNegativeReturnType = NegativeReturnType;
                    }
                });
                EnqueueDeferredTask(Deferred_ValidateAttributes, [this, VstNode, FunctionIdentifier, Context = _Context, ExprArgs]
                {
                    TGuardValue CurrentContextGuard(_Context, Context);
                    if (FunctionIdentifier->_ConstructorNegativeReturnType && ExprArgs.ArchetypeInstantiationContext != ConstructorInvocationCallee)
                    {
                        AppendGlitch(VstNode, EDiagnostic::ErrSemantic_IdentifierConstructorAttribute);
                    }
                });
            }

            // Only allow generic function identifiers to be used directly as the callee of a call.
            if (!bResultIsCalled && !bResultIsIgnored && (bSuperQualified || bIsIntrinsic))
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unimplemented, "References to overloaded, super qualified, and intrinsic functions are not yet implemented.");
                TSRef<CExprError> Error = TSRef<CExprError>::New();
                Error->AppendChild(Move(FunctionIdentifier));
                return ReplaceMapping(Identifier, Move(Error));
            }
            else
            {
                const bool bShouldReplaceWithInvocation = Function->_ExtensionFieldAccessorKind == EExtensionFieldAccessorKind::ExtensionDataMember;
                if (bShouldReplaceWithInvocation)
                {
                    // in this case 'using' the identifier actually needs to be replaced with an actual invocation
                    TSRef<CExprInvocation> InvocationAst = TSRef<CExprInvocation>::New(
                        CExprInvocation::EBracketingStyle::Undefined,
                        Move(FunctionIdentifier),
                        TSRef<CExprMakeTuple>::New());

                    // Analyze the invocation node.
                    if (TSPtr<CExpressionBase> Result = AnalyzeInvocation(InvocationAst, ExprCtx.WithResultIsCalled()))
                    {
                        return ReplaceMapping(Identifier, Move(Result.AsRef()));
                    }
                    else
                    {
                        return ReplaceMapping(Identifier, Move(InvocationAst));
                    }
                }
                else
                {
                    return ReplaceMapping(Identifier, Move(FunctionIdentifier));
                }
            }
        }
        case CDefinition::EKind::Data:
        {
            // Is it a data definition?
            const CDataDefinition* DataDefinition = &Definition->AsChecked<CDataDefinition>();

            MaybeAppendAttributesNotAllowedError(Identifier);

            const CLogicalScope& EnclosingLogicalScope = DataDefinition->_EnclosingScope.GetLogicalScope();
            const CSnippet* EnclosingSnippet = DataDefinition->_EnclosingScope.GetSnippet();

            // Don't allow a variable initializer to reference other variables
            // that precede them. This reflects the evaluation order of
            // initializers.
            if (!DataDefinition->IsInstanceMember() &&
                uLang::AnyOf(_Context._DataMembers, [&](const CDataDefinition* DataMember)
                {
                    return
                        !DataMember->IsInstanceMember() &&
                        &DataMember->_EnclosingScope.GetLogicalScope() == &EnclosingLogicalScope &&
                        (DataMember->_EnclosingScope.GetSnippet() != EnclosingSnippet ||
                        DataMember->_ParentScopeOrdinal <= DataDefinition->_ParentScopeOrdinal);

                 }))
            {
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_Unimplemented,
                    "Accessing a variable from the initializer of a variable that precedes it in the same snippet, or is located in a different snippet, isn't implemented yet. "
                    "You can fix this by either (1) changing the order of definitions in the same snippet, or (2) moving definitions from another snippet to this one, or (3) place the definitions in the other snippet into a submodule.");
                return ReplaceMapping(Identifier, TSRef<CExprError>::New(TURef<CUnknownType>::New(Identifier._Symbol, *_Context._Scope)));
            }

            auto IdentifierContext = Context;
            TSRef<CExpressionBase> Result = ReplaceMapping(Identifier, TSRef<CExprIdentifierData>::New(
                *_Program,
                *DataDefinition,
                Move(Context),
                Move(Qualifier)));

            if (DataDefinition->IsVar())
            {
                TSRef<CExpressionBase> IdentifierData = Result;
                Result = ReplaceMapping(*IdentifierData, TSRef<CExprPointerToReference>::New(Move(Result)));
                if (ExprCtx.ReferenceableContext == NotInReferenceableContext
                    || !DataDefinition->IsVarWritableFrom(*_Context._Scope))
                {
                    TSRef<CExpressionBase> PointerToReference = Result;

                    RequireEffects(*IdentifierData, EffectSets::Reads, ExprCtx.AllowedEffects, "mutable data read");

                    Result = ReplaceMapping(*PointerToReference, TSRef<CExprReferenceToValue>::New(Move(Result)));
                    EnqueueDeferredTask(Deferred_Type, [this, DataDefinition, Result, PointerToReference]
                    {
                        if (DataDefinition->GetType())
                        {
                            const CNormalType& DataType = DataDefinition->GetType()->GetNormalType();
                            const CPointerType& DataPointerType = DataType.AsChecked<CPointerType>();
                            Result->SetResultType(DataPointerType.PositiveValueType());
                        }
                        else
                        {
                            AppendGlitch(*PointerToReference, EDiagnostic::ErrSemantic_Unimplemented, "Can't access a data definition's value from a preceding expression.");
                            Result->SetResultType(_Program->GetDefaultUnknownType());
                        }
                    });
                }
                if (DataDefinition->_EnclosingScope.GetLogicalScope().GetKind() == CScope::EKind::Module
                    && ExprArgs.ReadWriteContext != EReadWriteContext::Partial)
                {
                    AppendGlitch(
                        *Result,
                        EDiagnostic::ErrSemantic_Unimplemented,
                        "Module-scoped `var` may only be partially read or written, e.g. `ModuleVar[Player]` or `set ModuleVar[Player] = ...`.");
                }
            }
            else
            {
                EnqueueDeferredTask(Deferred_Type, [this, DataDefinition, Result]
                {
                    if (!DataDefinition->GetType())
                    {
                        AppendGlitch(*Result, EDiagnostic::ErrSemantic_Unimplemented, "Can't access a data definition's value from a preceding expression.");
                    }
                });
            }

            // Explicit `Move` required because conversion constructor is required.
            return Move(Result);
        }
        case CDefinition::EKind::TypeAlias:
        {
            const CTypeAlias* TypeAlias = &Definition->AsChecked<CTypeAlias>();

            MaybeAppendAttributesNotAllowedError(Identifier);

            return ReplaceMapping(Identifier, TSRef<CExprIdentifierTypeAlias>::New(*TypeAlias, Move(Context), Move(Qualifier)));
        }
        case CDefinition::EKind::TypeVariable:
        {
            const CTypeVariable* TypeVariable = &Definition->AsChecked<CTypeVariable>();

            MaybeAppendAttributesNotAllowedError(Identifier);

            ULANG_ASSERTF(_CurrentTaskPhase >= Deferred_Type, "Should not reach here until after type definitions are analyzed");

            TSRef<CExprIdentifierTypeVariable> TypeVariableIdentifier = TSRef<CExprIdentifierTypeVariable>::New(*TypeVariable, Move(Context), Move(Qualifier));
            return ReplaceMapping(Identifier, Move(TypeVariableIdentifier));
        }
        case CDefinition::EKind::Module:
        {
            const CModule* Module = &Definition->AsChecked<CModule>();

            MaybeAppendAttributesNotAllowedError(Identifier);

            if(ExprCtx.ResultContext != EResultContext::ResultIsDotted && ExprCtx.ResultContext != EResultContext::ResultIsUsedAsQualifier) // Only allowed in lhs of . and as qualifier
            {
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_Unimplemented,
                    "Unexpected module.");
            }

            return ReplaceMapping(Identifier, TSRef<CExprIdentifierModule>::New(Module, Move(Context), Move(Qualifier)));
        }
        case CDefinition::EKind::ModuleAlias:
        {
            const CModuleAlias* ModuleAlias = &Definition->AsChecked<CModuleAlias>();

            MaybeAppendAttributesNotAllowedError(Identifier);

            if (ExprCtx.ResultContext != EResultContext::ResultIsDotted && ExprCtx.ResultContext != EResultContext::ResultIsUsedAsQualifier) // Only allowed in lhs of . and as qualifier
            {
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_Unimplemented,
                    "Unexpected module alias.");
            }

            TSRef<CExprIdentifierModuleAlias> ModuleAliasIdentifier = TSRef<CExprIdentifierModuleAlias>::New(*ModuleAlias, Move(Context), Move(Qualifier));
            EnqueueDeferredTask(Deferred_ModuleReferences, [this, ModuleAliasIdentifier]
            {
                const CTypeBase* ResultType = ModuleAliasIdentifier->_ModuleAlias.Module();
                if (!ResultType)
                {
                    ResultType = _Program->GetDefaultUnknownType();
                }
                ModuleAliasIdentifier->SetResultType(ResultType);
            });
            return ReplaceMapping(Identifier, Move(ModuleAliasIdentifier));
        }
        default:
            ULANG_UNREACHABLE();
        };
    }

    void DetectFunctionOverrideQualifierWarnings(CExprIdentifierUnresolved& Identifier, const CNominalType* QualifierType, const CFunction* TargetFunctionDefinition)
    {
        if (_Context._Function != TargetFunctionDefinition)
        {
            // early out if the call target doesn't point back to the current function
            return;
        }
        
        // Find the class of the function implied when this function calls (super:)function.
        //  It's not always the _SuperClass of this function's class
        const CClassDefinition* NextOverrideClass = nullptr;
        {
            if (const CFunction* OverriddenFunctionDefinition = _Context._Function->GetOverriddenDefinition())
            {
                if (const CScope* OverriddenFunctionClassScope = OverriddenFunctionDefinition->GetScopeOfKind(CScope::EKind::Class))
                {
                    if (const CDefinition* OverriddenClass = OverriddenFunctionClassScope->ScopeAsDefinition())
                    {
                        NextOverrideClass = OverriddenClass->AsNullable<CClassDefinition>();
                    }
                }
            }
        }
        
        if (const CClassDefinition* CallQualifierTargetClass = QualifierType->AsNullable<CClassDefinition>())
        {
            // Explicitly referencing the current class doesn't warn
            if (_Context._Self != CallQualifierTargetClass)
            {
                if (CallQualifierTargetClass == NextOverrideClass)
                {
                    // user is calling the base of the immediate override this function sits on - they probably intended to use super
                    AppendGlitch(
                        FindMappedVstNode(Identifier),
                        EDiagnostic::WarnSemantic_ScopeQualifierShouldBeSuper,
                        CUTF8String("Class-scope qualifier (%s:) won't invoke the base-method. Perhaps (super:) was intended.",
                            QualifierType->AsCode().AsCString()));
                }
                else
                {
                    // user is calling a non-immediate base. They can't use super in this case, while the call-result is similar, we can't make the same suggestions
                    AppendGlitch(
                        FindMappedVstNode(Identifier),
                        EDiagnostic::WarnSemantic_ScopeQualifierBeyondSuper,
                        CUTF8String("Class-scope qualifier (%s:) won't invoke the base-method. Explicitly calling ancestor-versions of overridden functions beyond the immediate base is not allowed.",
                            QualifierType->AsCode().AsCString()));
                }
            }
        }
    }

    bool ValidateBuiltInQualifier(const CExpressionBase& Qualifier, const CSymbol& Symbol)
    {
        if (Qualifier.GetNodeType() == EAstNodeType::Identifier_Unresolved)
        {
            const CExprIdentifierUnresolved& Identifier = static_cast<const CExprIdentifierUnresolved&>(Qualifier);

            ULANG_ASSERTF(Identifier._Symbol == Symbol, "Expected `(%s:)` qualifier.", Symbol.AsCString());

            if (Identifier.Qualifier())
            {
                AppendGlitch(*Identifier.Qualifier(),
                    EDiagnostic::ErrSemantic_InvalidQualifierCombination,
                    CUTF8String("Cannot qualify  a `(%s:)` qualifier.", Symbol.AsCString()));
                ReplaceMapping(*Identifier.Qualifier(), TSRef<CExprError>::New());

                return false;
            }

            if (Identifier.Context())
            {
                AppendGlitch(*Identifier.Context(),
                    EDiagnostic::ErrSemantic_InvalidQualifierCombination,
                    CUTF8String("A `(%s:) qualifiier cannot have a context.`", Identifier.Context()->GetErrorDesc().AsCString(), Identifier._Symbol.AsCString()));
                ReplaceMapping(*Identifier.Context(), TSRef<CExprError>::New());

                return false;
            }
        }

        return true;
    }	

    static CUTF8String ConvertFullVersePathToRelativeDotSyntax(const CUTF8StringView& FullVersePath, const CUTF8StringView& BaseVersePath)
    {
        const UTF8Char* ChFull, * ChBase;
        const UTF8Char* CommonFull = nullptr;

        // Find common portion of path
        for (ChFull = FullVersePath._Begin, ChBase = BaseVersePath._Begin; ChFull < FullVersePath._End && ChBase < BaseVersePath._End; ++ChFull, ++ChBase)
        {
            if (*ChFull == '/' && *ChBase == '/')
            {
                CommonFull = ChFull;
            }
            else if (CUnicode::ToUpper_ASCII(*ChFull) != CUnicode::ToUpper_ASCII(*ChBase))
            {
                break;
            }
        }

        if (ChFull == FullVersePath._End || ChBase == BaseVersePath._End)
        {
            CommonFull = ChFull;
        }

        if (!CommonFull)
        {
            return CUTF8String();
        }

        // Skip slash in full path if any
        if (CommonFull < FullVersePath._End && *CommonFull == '/')
        {
            ++CommonFull;
        }

        CUTF8String Ret = CUTF8StringView(CommonFull, FullVersePath._End);

        Ret = Ret.Replace("/", ".");

        return Ret;
    }

    // As long as there are 2 or more definitions present, remove definitions that are not visible from the current package
    void FilterByPackageVisibility(SResolvedDefinitionArray& Definitions) const
    {
        if (Definitions.Num() >= 2)
        {
            const CAstPackage* Package = _Context._Scope->GetPackage();
            for (int32_t Index = Definitions.Num() - 1; Index >= 0; --Index)
            {
                if (!Package->CanSeeDefinition(*Definitions[Index]._Definition))
                {
                    Definitions.RemoveAtSwap(Index);
                    if (Definitions.Num() < 2)
                    {
                        break;
                    }
                }
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeIdentifier(CExprIdentifierUnresolved& Identifier, const SExprCtx& ExprCtx, const SExprArgs& ExprArgs)
    {
        // The desugarer creates identifiers for infix operator calls that don't have an associated VST node,
        // so we have to use FindMappedVstNode instead of GetMappedVstNode to find an appropriate error context.
        const Vst::Node* VstNode = FindMappedVstNode(Identifier);

        // Don't allow references to arbitrary operators.
        if (!Identifier._bAllowReservedOperators && IsReservedOperatorSymbol(Identifier._Symbol))
        {
            AppendGlitch(
                FindMappedVstNode(Identifier),
                EDiagnostic::ErrSemantic_ReservedOperatorName,
                CUTF8String("The operator name %s is reserved for future use.", Identifier._Symbol.AsCString()));
            return ReplaceMapping(Identifier, TSRef<CExprError>::New());
        }

        // If we are using any "special" qualifiers, do some initial validation (this doesn't actually resolve the definition of the identifier yet)
        bool bIsExplicitlySuperQualified = false;
        if (Identifier.Qualifier())
        {
            if (IsQualifierNamed(Identifier.Qualifier().AsRef(), _SuperName))
            {
                bIsExplicitlySuperQualified = true;
                ValidateBuiltInQualifier(*Identifier.Qualifier(), _SuperName);
            }
            else if (IsQualifierNamed(Identifier.Qualifier().AsRef(), _LocalName) && _Context._Package->_EffectiveVerseVersion >= Verse::Version::LocalQualifiers)
            {
                ValidateBuiltInQualifier(*Identifier.Qualifier(), _LocalName);
            }
        }

        SQualifier Qualifier = SQualifier::Unknown();

        // If a `(super:)`/`(local:)` qualifier is present, then the qualifier is already consumed
        if (!bIsExplicitlySuperQualified && Identifier.Qualifier())
        {
            Qualifier = AnalyzeQualifier(Identifier.Qualifier(), Identifier, ExprCtx, ExprArgs);
            if (Qualifier.IsUnspecified())
            {
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
        }

        auto AnalyzeInnateMacroCalledAsMacro = [&]
        {
            if (bIsExplicitlySuperQualified)
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unsupported, CUTF8String("Qualifier (%s:) cannot be used on macros (in this case '%s')", _SuperName.AsCString(), Identifier._Symbol.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else if (!Qualifier.IsUnspecified())
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unsupported, CUTF8String("Qualifier (%s:) cannot be used on macros (in this case '%s')", Qualifier._Definition->AsCode().AsCString(), Identifier._Symbol.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else
            {
                TSRef<CExprIdentifierBuiltInMacro> BuiltInMacroIdentifier = TSRef<CExprIdentifierBuiltInMacro>::New(Identifier._Symbol, &_Program->_anyType);
                // Copy the attributes but leave them as-is for the macro application to analyze them.
                BuiltInMacroIdentifier->_Attributes = Move(Identifier._Attributes);
                return ReplaceMapping(Identifier, Move(BuiltInMacroIdentifier));
            }
        };

        // Handle <context>.<symbol> expressions.
        if (Identifier.Context())
        {
            // Analyze the context subexpression.
            TSPtr<CExpressionBase> Context = Identifier.TakeContext();
            if (ExprArgs.AnalysisContext != EAnalysisContext::ContextAlreadyAnalyzed)
            {
                if (TSPtr<CExpressionBase> NewContext = AnalyzeExpressionAst(Context.AsRef(), ExprCtx.WithResultIsDotted()))
                {
                    Context = Move(NewContext);
                }
            }

            CUTF8String ErrorMessage;

            const CTypeBase* ContextResultType = Context->GetResultType(*_Program);

            // punch through reference for this analysis

            const CNormalType& ContextNormalType = ContextResultType->GetNormalType();
            const CReferenceType* ContextReferenceType = ContextNormalType.AsNullable<CReferenceType>();
            const CNormalType* ContextNormalValueType;
            if (ContextReferenceType)
            {
                ContextNormalValueType = &ContextReferenceType->PositiveValueType()->GetNormalType();
            }
            else
            {
                ContextNormalValueType = &ContextNormalType;
            }
            // `GetNormalType` will return `CFlowType`'s child.  The result
            // corresponds to a lower bound of a corresponding type variable.
            // This is made to also be the upper bound via `AssertConstrain`.
            // However, `CTypeVariable` needs to be explicitly handled here.
            // Because `CTypeVariable` cannot be constrained, only the upper
            // bound is queried.
            if (const CTypeVariable* ContextTypeVariable = ContextNormalValueType->AsNullable<CTypeVariable>())
            {
                if (const CTypeType* ContextTypeType = ContextTypeVariable->GetType()->GetNormalType().AsNullable<CTypeType>())
                {
                    ContextNormalValueType = &ContextTypeType->PositiveType()->GetNormalType();
                }
            }

            if (bIsExplicitlySuperQualified)
            {
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_UnknownIdentifier,
                    CUTF8String("Qualifier (%s:) cannot be used when an identifier already has a context.", _SuperName.AsCString()));
            }
            else if (!Qualifier.IsUnspecified() && !IsSubtype(ContextNormalValueType, Qualifier._Definition))
            {
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_UnknownIdentifier,
                    CUTF8String("`%s` is not a subtype of qualifier `%s`.", ContextResultType->AsCode().AsCString(), Qualifier._Definition->AsCode().AsCString()));
            }
            else
            {
                SResolvedDefinitionArray Definitions;
                SResolvedDefinitionArray OutOfScopeDefinitions;
                bool bIsExtensionField = false;
                if (ContextNormalValueType->IsA<CArrayType>() && Identifier._Symbol == _Program->_IntrinsicSymbols._FieldNameLength)
                {
                    const CTypeBase* NegativeArrayType = &_Program->GetOrCreateArrayType(&_Program->_anyType);
                    if (ContextReferenceType)
                    {
                        const CTypeBase* PositiveArrayType = &_Program->GetOrCreateArrayType(&_Program->_falseType);
                        NegativeArrayType = &_Program->GetOrCreateReferenceType(PositiveArrayType, NegativeArrayType);
                    }
                    AssertConstrain(ContextResultType, NegativeArrayType);
                    // HACK: make the postfix dot operators work for arrays and maps without exposing too much postfix dot operator power to abuse.
                    CSymbol PostfixOperatorName = VerifyAddSymbol(Identifier, CUTF8String("operator'array.%s'", Identifier._Symbol.AsCString()));
                    Definitions = _Program->_VerseModule->ResolveDefinition(PostfixOperatorName);
                    bIsExtensionField = true;
                }
                else if (
                    const CMapType* ContextMapType = ContextNormalValueType->AsNullable<CMapType>();
                    ContextMapType
                    && !ContextMapType->IsWeak()
                    && Identifier._Symbol == _Program->_IntrinsicSymbols._FieldNameLength)
                {
                    const CTypeBase* NegativeMapType = &_Program->GetOrCreateMapType(&_Program->_anyType, &_Program->_anyType);
                    if (ContextReferenceType)
                    {
                        const CTypeBase* PositiveMapType = &_Program->GetOrCreateMapType(&_Program->_falseType, &_Program->_falseType);
                        NegativeMapType = &_Program->GetOrCreateReferenceType(PositiveMapType, NegativeMapType);
                    }
                    AssertConstrain(ContextResultType, NegativeMapType);
                    // HACK: make the postfix dot operators work for arrays and maps without exposing too much postfix dot operator power to abuse.
                    CSymbol PostfixOperatorName = VerifyAddSymbol(Identifier, CUTF8String("operator'map.%s'", Identifier._Symbol.AsCString()));
                    Definitions = _Program->_VerseModule->ResolveDefinition(PostfixOperatorName);
                    bIsExtensionField = true;
                }
                else
                {
                    CScope::ResolvedDefnsAppend(&Definitions, ContextNormalValueType->FindInstanceMember(Identifier._Symbol, EMemberOrigin::InheritedOrOriginal, Qualifier, _Context._Package));
                    for (const SResolvedDefinition& ResolvedDefn : Definitions)
                    {
                        const CTypeBase* PositiveScopeType = ResolvedDefn._Definition->_EnclosingScope.ScopeAsType();
                        if (!PositiveScopeType)
                        {
                            continue;
                        }
                        const CNormalType& PositiveNormalScopeType = PositiveScopeType->GetNormalType();
                        const CTypeBase* NegativeScopeType;
                        if (const CClass* PositiveScopeClass = PositiveNormalScopeType.AsNullable<CClass>())
                        {
                            NegativeScopeType = PositiveScopeClass->_NegativeClass;
                        }
                        else if (const CInterface* PositiveScopeInterface = PositiveNormalScopeType.AsNullable<CInterface>())
                        {
                            NegativeScopeType = PositiveScopeInterface->_NegativeInterface;
                        }
                        else
                        {
                            NegativeScopeType = PositiveScopeType;
                        }
                        if (ContextReferenceType)
                        {
                            NegativeScopeType = &_Program->GetOrCreateReferenceType(PositiveScopeType, NegativeScopeType);
                        }
                        // Do not use `AssertConstrain`.  When `Constrain` fails, an error has already been issued.
                        Constrain(ContextResultType, NegativeScopeType);
                    }
                    if (ExprCtx.ResultContext == ResultIsCalled)
                    {
                        const CSymbol ExtensionName = VerifyAddSymbol(Identifier, _Program->_IntrinsicSymbols.MakeExtensionFieldOpName(Identifier._Symbol));
                        SResolvedDefinitionArray ExtensionDefinitions = _Context._Scope->ResolveDefinition(ExtensionName, Qualifier, _Context._Package);
                        Definitions += ExtensionDefinitions;
                        FilterByPackageVisibility(Definitions);

                        if (Definitions.IsEmpty())
                        {
                            _Program->IterateRecurseLogicalScopes([this, &OutOfScopeDefinitions, &ExtensionName, &Qualifier](const CLogicalScope& LogicalScope)
                                {
                                    if (LogicalScope.GetKind() != CScope::EKind::Module)
                                    {
                                        return EVisitResult::Continue;
                                    }
                                    else
                                    {
                                        OutOfScopeDefinitions = LogicalScope.ResolveDefinition(ExtensionName, Qualifier, _Context._Package);
                                        return OutOfScopeDefinitions.Num() ? EVisitResult::Stop : EVisitResult::Continue;
                                    }
                                });
                        }
                    }
                }

                if (!Definitions.IsEmpty())
                {
                    return ResolveIdentifierToDefinitions(Identifier, bIsExtensionField, Definitions, Move(Context), Identifier.TakeQualifier(), ExprCtx, ExprArgs);
                }
                else if (!SemanticTypeUtils::IsUnknownType(ContextNormalValueType))
                {
                    if (OutOfScopeDefinitions.Num() == 0)
                    {
                        AppendGlitch(
                            VstNode,
                            EDiagnostic::ErrSemantic_UnknownIdentifier,
                            CUTF8String("Unknown member `%s` in `%s`.", Identifier._Symbol.AsCString(), ContextNormalType.AsCode().AsCString()));
                    }
                    else
                    {
                        AppendGlitch(
                            VstNode,
                            EDiagnostic::ErrSemantic_UnknownIdentifier_WithUsing,
                            CUTF8String("Unknown member `%s` in '%s'. Did you forget to specify using { %s }?", Identifier._Symbol.AsCString(), ContextNormalType.AsCode().AsCString(), OutOfScopeDefinitions[0]._Definition->_EnclosingScope.GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString()));
                    }
                }
            }

            TSRef<CExprError> ErrorNode = TSRef<CExprError>::New(TURef<CUnknownType>::New(Identifier._Symbol, *_Context._Scope));
            ErrorNode->AppendChild(Move(Context));
            return ReplaceMapping(Identifier, Move(ErrorNode));
        } // End of handle expressions with a context
        else if (Identifier._Symbol == _InnateMacros._array
            || Identifier._Symbol == _InnateMacros._block
            || Identifier._Symbol == _InnateMacros._let
            || Identifier._Symbol == _InnateMacros._branch
            || Identifier._Symbol == _InnateMacros._break
            || Identifier._Symbol == _InnateMacros._case
            || Identifier._Symbol == _InnateMacros._class
            || Identifier._Symbol == _InnateMacros._defer
            || Identifier._Symbol == _InnateMacros._enum
            || Identifier._Symbol == _InnateMacros._external
            || Identifier._Symbol == _InnateMacros._for
            || Identifier._Symbol == _InnateMacros._interface
            || Identifier._Symbol == _InnateMacros._loop
            || Identifier._Symbol == _InnateMacros._map
            || Identifier._Symbol == _InnateMacros._module
            || Identifier._Symbol == _InnateMacros._option
            || Identifier._Symbol == _InnateMacros._race
            || Identifier._Symbol == _InnateMacros._rush
            || Identifier._Symbol == _InnateMacros._scoped
            || Identifier._Symbol == _InnateMacros._spawn
            || Identifier._Symbol == _InnateMacros._sync
            || Identifier._Symbol == _InnateMacros._struct
            || Identifier._Symbol == _InnateMacros._using
            || (Identifier._Symbol == _InnateMacros._profile && VerseFN::UploadedAtFNVersion::EnableProfileMacro(_Context._Package->_UploadedAtFNVersion)))
        {
            if (ExprCtx.ResultContext != ResultIsCalledAsMacro)
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unimplemented, "Can't use built-in macros other than to invoke them.");
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            return AnalyzeInnateMacroCalledAsMacro();
        }
        else if (Identifier._Symbol == _InnateMacros._type && ExprCtx.ResultContext == ResultIsCalledAsMacro)
        {
            return AnalyzeInnateMacroCalledAsMacro();
        }
        else if (Identifier._Symbol == _LogicLitSym_True
            // Don't interpret true as a logic value if it's being used as a type.
            && ExprCtx.ResultContext != ResultIsUsedAsType)
        {
            // Handle "true"

            MaybeAppendAttributesNotAllowedError(Identifier);

            if (bIsExplicitlySuperQualified)
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unsupported, CUTF8String("Qualifier (super:) cannot be used on '%s')", _LogicLitSym_True.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else if (!Qualifier.IsUnspecified())
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unsupported, CUTF8String("Qualifier (%s:) cannot be used on '%s'", Qualifier._Definition->AsCode().AsCString(), _LogicLitSym_True.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else if(ExprCtx.RequiredType && ExprCtx.RequiredType->GetNormalType().IsA<CTypeType>())
            {
                return ReplaceMapping(
                    Identifier,
                    TSRef<CExprIdentifierTypeAlias>::New(*_Program->_trueAlias));
            }
            else
            {
                return ReplaceMapping(Identifier, TSRef<CExprLogic>::New(*_Program, true));
            }
        }
        else if (Identifier._Symbol == _LogicLitSym_False
            // Don't interpret false as a logic or option value if it's being used as a type.
            && ExprCtx.ResultContext != ResultIsUsedAsType)
        {
            // Handle "false"

            MaybeAppendAttributesNotAllowedError(Identifier);

            if (bIsExplicitlySuperQualified)
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unsupported, CUTF8String("Qualifier (super:) cannot be used on '%s')", _LogicLitSym_False.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else if (!Qualifier.IsUnspecified())
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unsupported, CUTF8String("Qualifier (%s:) cannot be used on '%s'", Qualifier._Definition->AsCode().AsCString(), _LogicLitSym_False.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else if (ExprCtx.RequiredType && ExprCtx.RequiredType->GetNormalType().IsA<COptionType>())
            {
                return ReplaceMapping(
                    Identifier,
                    TSRef<CExprMakeOption>::New(&_Program->GetOrCreateOptionType(&_Program->_falseType), nullptr));
            }
            else if (ExprCtx.RequiredType && ExprCtx.RequiredType->GetNormalType().IsA<CTypeType>())
            {
                return ReplaceMapping(
                    Identifier,
                    TSRef<CExprIdentifierTypeAlias>::New(*_Program->_falseAlias));
            }
            else
            {
                return ReplaceMapping(Identifier, TSRef<CExprLogic>::New(*_Program, false));
            }
        }
        else if (Identifier._Symbol == _SelfName)
        {
            // Handle "Self"

            MaybeAppendAttributesNotAllowedError(Identifier);

            if (bIsExplicitlySuperQualified)
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unsupported, CUTF8String("Qualifier (super:) cannot be used on '%s')", _SelfName.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else if (!Qualifier.IsUnspecified())
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unsupported, CUTF8String("Qualifier (%s:) cannot be used on '%s'", Qualifier._Definition->AsCode().AsCString(), _SelfName.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else if(!_Context._Self)
            {
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_UnexpectedIdentifier,
                    CUTF8String("`%s` may only be used in an instance scope.", _SelfName.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else
            {
                if (uLang::AnyOf(_Context._DataMembers, &CDefinition::IsInstanceMember))
                {
                    AppendGlitch(
                        VstNode,
                        EDiagnostic::ErrSemantic_Unimplemented,
                        CUTF8String("`%s` in an instance variable initializer is not yet implemented.", _SelfName.AsCString()));
                }

                return ReplaceMapping(Identifier, TSRef<CExprSelf>::New(_Context._Self));
            }
        }
        else if (Identifier._Symbol == _SuperName)
        {
            // Handle super

            MaybeAppendAttributesNotAllowedError(Identifier);

            if (bIsExplicitlySuperQualified)
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unsupported, CUTF8String("Qualifier (super:) cannot be used on '%s')", _SuperName.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else if (!Qualifier.IsUnspecified())
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_Unsupported, CUTF8String("Qualifier (%s:) cannot be used on %s", Qualifier._Definition->AsCode().AsCString(), _SuperName.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else if (!_Context._Function)
            {
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_UnexpectedIdentifier,
                    "`super` may only be used in a routine.");
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }

            if (!_Context._Self || !_Context._Self->GetNormalType().IsA<CClass>())
            {
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_UnexpectedIdentifier,
                    CUTF8String("`%s` may only be used for classes.", _SuperName.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }

            const CClass* FunctionClass = static_cast<const CClass*>(_Context._Self);
            const CClass* SuperClass = FunctionClass->_Superclass;

            if (SuperClass == nullptr)
            {
                AppendGlitch(
                    VstNode,
                    EDiagnostic::ErrSemantic_NoSuperclass,
                    CUTF8String("Class `%s` does not have a superclass.", FunctionClass->Definition()->AsNameCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }

            return ReplaceMapping(Identifier, TSRef<CExprIdentifierClass>::New(SuperClass->GetTypeType()));
        }
        // Support for `(local:)` qualifier; this is called when a caller requests to analyze the `(local:)` qualifier itself.
        // NOTE: (yiliang.siew) The semantics of `(local:)` are: it only searches all scopes within a function for definitions.
        // If you don't find a definition or find multiple definitions, that's an error.
        // We have to make sure we check that this is being used as a qualifier so that someone can't just type `local` on its own.
        else if (Identifier._Symbol == _LocalName
                 && _Context._Package->_EffectiveVerseVersion >= Verse::Version::LocalQualifiers)
        {
            MaybeAppendAttributesNotAllowedError(Identifier);
            if (!_Context._Function)
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_UnexpectedIdentifier, CUTF8String("You can only use (%s:) in a function.", _LocalName.AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            if (ExprCtx.ResultContext != EResultContext::ResultIsUsedAsQualifier)
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_LocalMustBeUsedAsQualifier);
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            else if (!Qualifier.IsUnspecified())
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_InvalidQualifier, CUTF8String("You cannot use: (%s:) to qualify a built-in qualifier.", Qualifier._Definition->AsCode().AsCString()));
                return ReplaceMapping(Identifier, TSRef<CExprError>::New());
            }
            return ReplaceMapping(Identifier, TSRef<CExprLocal>::New(*_Context._Function));
        }
        else
        {
            // Look up a definition in the current scope with the identifier's symbol.
            // Except if (super:) is used, then take the enclosing scope function.
            SResolvedDefinitionArray Definitions; 
            if (bIsExplicitlySuperQualified)
            {
                if (!_Context._Function || _Context._Function->GetName() != Identifier._Symbol)
                {
                    AppendGlitch(Identifier, EDiagnostic::ErrSemantic_InvalidQualifier, "Only possible to use (super:) on the same function as being defined.");
                    return ReplaceMapping(Identifier, TSRef<CExprError>::New());
                }
                else if (!_Context._Function->GetOverriddenDefinition())
                {
                    AppendGlitch(Identifier, EDiagnostic::ErrSemantic_InvalidQualifier, "Only possible to use (super:) on overridden function.");
                    return ReplaceMapping(Identifier, TSRef<CExprError>::New());
                }
                else if (!_Context._Function->GetOverriddenDefinition()->HasImplementation())
                {
                    AppendGlitch(Identifier, EDiagnostic::ErrSemantic_InvalidQualifier, "Not possible to use (super:) when overriding function has no implementation.");
                    return ReplaceMapping(Identifier, TSRef<CExprError>::New());
                }
                else
                {
                    Definitions.Add(const_cast<CFunction*>(_Context._Function));
                }
            }
            else
            {
                Definitions = _Context._Scope->ResolveDefinition(Identifier._Symbol, Qualifier, _Context._Package);

                // Create context for `ResolveIdentifierToDefinitions()` if matched with local using
                if (_Context._Self) // Check if there are any extension methods.
                {
                    const CSymbol ExtensionName = VerifyAddSymbol(Identifier, _Program->_IntrinsicSymbols.MakeExtensionFieldOpName(Identifier._Symbol));
                    SResolvedDefinitionArray ExtensionDefinitions = _Context._Scope->ResolveDefinition(ExtensionName, Qualifier, _Context._Package);
                    Definitions += ExtensionDefinitions;
                }

                FilterByPackageVisibility(Definitions);
            }

            if (!Definitions.Num())
            {
                // No definition was found; try to find the definition in another module that could be imported to solve the problem.
                _Program->IterateRecurseLogicalScopes([&Definitions, &Identifier](const CLogicalScope& LogicalScope)
                {
                    if (LogicalScope.GetKind() != CScope::EKind::Module)
                    {
                        return EVisitResult::Continue;
                    }
                    else
                    {
                        CScope::ResolvedDefnsAppend(&Definitions, LogicalScope.FindDefinitions(Identifier._Symbol));
                        return Definitions.Num() ? EVisitResult::Stop : EVisitResult::Continue;
                    }
                });

                if (Definitions.Num())
                {
                    // Test if the definition comes from the same Verse path as the current context. In this case we need to provide a relative path to the definition and not a using statement.
                    if (Definitions[0]._Definition->_EnclosingScope.GetPackage() && _Context._Scope->GetPackage() &&
                        Definitions[0]._Definition->_EnclosingScope.GetPackage()->_VersePath == _Context._Scope->GetPackage()->_VersePath)
                    {
                        CUTF8String ScopePath = _Context._Scope->GetScopePath('/', CScope::EPathMode::PrefixSeparator);
                        CUTF8String DefinitionPath = Definitions[0]._Definition->_EnclosingScope.GetScopePath('/', CScope::EPathMode::PrefixSeparator);
                        CUTF8String RelativeVersePath = ConvertFullVersePathToRelativeDotSyntax(DefinitionPath, ScopePath);

                        if (!RelativeVersePath.IsEmpty())
                        {
                            RelativeVersePath += ".";
                        }

                        RelativeVersePath += Definitions[0]._Definition->AsNameCString();

                        AppendGlitch(
                            VstNode,
                            EDiagnostic::ErrSemantic_UnknownIdentifier,
                            CUTF8String("Unknown identifier `%s`. Did you mean `%s`?", Identifier._Symbol.AsCString(), RelativeVersePath.AsCString()));
                    }
                    else
                    {
                        AppendGlitch(
                            VstNode,
                            EDiagnostic::ErrSemantic_UnknownIdentifier,
                        CUTF8String("Unknown identifier `%s`. Did you forget to specify using { %s }?", Identifier._Symbol.AsCString(), Definitions[0]._Definition->_EnclosingScope.GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString()));
                    }
                }
                else
                {
                    AppendGlitch(VstNode, EDiagnostic::ErrSemantic_UnknownIdentifier, CUTF8String("Unknown identifier `%s`.", Identifier._Symbol.AsCString()));
                }
                return ReplaceMapping(Identifier, TSRef<CExprError>::New(TURef<CUnknownType>::New(Identifier._Symbol, *_Context._Scope)));
            }
            else
            {
                // If there are multiple possible definitions, and this identifier isn't being resolved
                // before access level attributes are processed:

                // Some or all of the returned Definitions might be declared internal to their module and
                // inaccessible for lookup from here. First we check to see whether -all- returned definitions
                // are internal and inaccessible. If that's the case, we emit an error mentioning which
                // internal definitions were found. If there are some non-internal definitions returned
                // here, then we silently remove the internal ones for further consideration

                // We handle this a little bit differently than the normal public/protected/private access
                // checking, because in the case where you find two definitions, where one is internal
                // and inaccessible, and the other is perfectly usable, we would like to carry on as if
                // the internal one was simply removed from consideration, so that it doesn't cause ambiguous
                // errors, but also doesn't require us to put special handling into a lot of downstream
                // code to handle multiple definitions coming in and needing to filter out the internal
                // ones in a different way for every kind of definition.
                if (Definitions.Num() > 1 && _CurrentTaskPhase >= Deferred_ValidateAttributes)
                {
                    bool bAllDefinitionsInaccessible = true;

                    // build a filtered list of definitions that are not internal and inaccessible
                    SResolvedDefinitionArray FilteredDefinitions;
                    FilteredDefinitions.Reserve(Definitions.Num());

                    for (int32_t DefinitionIndex = 0; DefinitionIndex < Definitions.Num(); DefinitionIndex++)
                    {
                        CDefinition* Definition = Definitions[DefinitionIndex]._Definition;
                        ULANG_ASSERTF(_CurrentTaskPhase >= Deferred_ValidateAttributes, "Should not reach here until attributes have been analyzed.");
                        if (Definition->IsAccessibleFrom(*_Context._Scope) || Identifier._bAllowUnrestrictedAccess)
                        {
                            FilteredDefinitions.Add(Definition);
                            bAllDefinitionsInaccessible = false;
                        }
                    }

                    if (bAllDefinitionsInaccessible)
                    {
                        const CModule* CallingModule = _Context._Scope->GetModule();

                        AppendGlitch(
                            VstNode,
                            EDiagnostic::ErrSemantic_Inaccessible,
                                CUTF8String("All references to `%s` are inaccessible from context `%s` in module `%s`.",
                                Identifier._Symbol.AsCString(),
                                _Context._Scope->GetScopePath().AsCString(),
                                CallingModule ? CallingModule->AsNameCString() : "<none>"));

                        return ReplaceMapping(Identifier, TSRef<CExprError>::New(TURef<CUnknownType>::New(Identifier._Symbol, *_Context._Scope)));
                    }
                    else
                    {
                        // swap the valid definitions for the ones we filtered as still relevant
                        Definitions = Move(FilteredDefinitions);
                        ULANG_ASSERTF(Definitions.Num(), "Expected at least one definition after filtering out internal inaccessible ones");
                    }
                }

                for (const SResolvedDefinition& ResolvedDefn : Definitions)
                {
                    if (ResolvedDefn._Definition->IsInstanceMember() && uLang::AnyOf(_Context._DataMembers, &CDefinition::IsInstanceMember))
                    {
                        AppendGlitch(
                            VstNode,
                            EDiagnostic::ErrSemantic_Unimplemented,
                            CUTF8String(
                                "Accessing instance member `%s` from this scope is not yet implemented.",
                                ResolvedDefn._Definition->AsNameCString()));
                    }
                }

                if (!Qualifier.IsUnspecified() && Definitions.Num() == 1)
                {
                    if (const CFunction* CallTargetDefinition = Definitions[0]._Definition->AsNullable<CFunction>())
                    {
                        DetectFunctionOverrideQualifierWarnings(Identifier, Qualifier._Definition, CallTargetDefinition);
                    }
                }

                return ResolveIdentifierToDefinitions(Identifier, false, Definitions, nullptr, Identifier.TakeQualifier(), ExprCtx, ExprArgs);
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeOptionTypeFormer(CExprOptionTypeFormer& OptionTypeFormer, const SExprCtx& ExprCtx)
    {
        // Analyze the inner type expression.
        if (TSPtr<CExpressionBase> NewInnerTypeAst = AnalyzeExpressionAst(OptionTypeFormer.GetInnerTypeAst(), ExprCtx.WithResultIsUsedAsType()))
        {
            OptionTypeFormer.SetInnerTypeAst(Move(NewInnerTypeAst.AsRef()));
        }

        const TSRef<CExpressionBase>& ValueExpr = OptionTypeFormer.GetInnerTypeAst();
        STypeTypes ValueTypes = MaybeTypeTypes(*ValueExpr);

        if (ValueTypes._Tag == ETypeTypeTag::NotType)
        {
            // Could have tried to specify a named parameter - which is also an error since `?` is not needed
            if (ValueExpr->GetNodeType() == EAstNodeType::Identifier_Data)
            {
                AppendGlitch(
                    *ValueExpr,
                    EDiagnostic::ErrSemantic_NamedOrOptNonType,
                    CUTF8String(
                        "Either `%s` should be a type or it is mistakenly a `?named` argument without a `:= Value`. Also note that parameters variables do not need to be named with a `?` in their function body.",
                        static_cast<const CExprIdentifierData&>(*ValueExpr)._DataDefinition.AsNameCString()));

                return ValueExpr;
            }

            AppendGlitch(
                *ValueExpr,
                EDiagnostic::ErrSemantic_ExpectedType,
                CUTF8String("Expected a type, got %s instead.", ValueExpr->GetErrorDesc().AsCString()));
        }

        ValidateNonAttributeType(ValueTypes._NegativeType, ValueExpr->GetMappedVstNode());
        ValidateNonAttributeType(ValueTypes._PositiveType, ValueExpr->GetMappedVstNode());
        const COptionType& NegativeOptionType = _Program->GetOrCreateOptionType(ValueTypes._NegativeType);
        const COptionType& PositiveOptionType = _Program->GetOrCreateOptionType(ValueTypes._PositiveType);
        OptionTypeFormer._TypeType = &_Program->GetOrCreateTypeType(&NegativeOptionType, &PositiveOptionType);

        return nullptr;
    }
    
    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeArrayTypeFormer(CExprArrayTypeFormer& ArrayTypeFormer, const SExprCtx& ExprCtx)
    {
        // Analyze the inner type expression.
        if (TSPtr<CExpressionBase> NewInnerTypeAst = AnalyzeExpressionAst(ArrayTypeFormer.GetInnerTypeAst(), ExprCtx.WithResultIsUsedAsType()))
        {
            ArrayTypeFormer.SetInnerTypeAst(Move(NewInnerTypeAst.AsRef()));
        }

        STypeTypes ElementTypes = GetTypeTypes(*ArrayTypeFormer.GetInnerTypeAst());
        ValidateNonAttributeType(ElementTypes._NegativeType, ArrayTypeFormer.GetInnerTypeAst()->GetMappedVstNode());
        ValidateNonAttributeType(ElementTypes._PositiveType, ArrayTypeFormer.GetInnerTypeAst()->GetMappedVstNode());
        const CArrayType& NegativeArrayType = _Program->GetOrCreateArrayType(ElementTypes._NegativeType);
        const CArrayType& PositiveArrayType = _Program->GetOrCreateArrayType(ElementTypes._PositiveType);
        ArrayTypeFormer._TypeType = &_Program->GetOrCreateTypeType(&NegativeArrayType, &PositiveArrayType);

        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    // Transform a CExprInvocation into a CExprGeneratorTypeFormer.
    TSPtr<CExpressionBase> AnalyzeGeneratorTypeFormer(CExprInvocation& Invocation, const SExprCtx& ExprCtx)
    {
        if (Invocation._CallsiteBracketStyle == CExprInvocation::EBracketingStyle::SquareBrackets)
        {
            AppendGlitch(
                Invocation,
                EDiagnostic::ErrSemantic_IncompatibleFailure,
                CUTF8String("`generator` uses round brackets / parentheses `generator(..)` rather than square or curly brackets."));
            // Continue as generator to give additional context...
        }

        // Convert the CExprInvocation to a CExprGeneratorTypeFormer.
        TSRef<CExprGeneratorTypeFormer> GeneratorAstRef = TSRef<CExprGeneratorTypeFormer>::New(Invocation.TakeArgument().AsRef());
        CExprGeneratorTypeFormer& GeneratorAst = *GeneratorAstRef;

        Invocation.GetMappedVstNode()->AddMapping(&GeneratorAst);

        // Analyze the inner type expression.
        if (TSPtr<CExpressionBase> NewInnerTypeAst = AnalyzeExpressionAst(GeneratorAst.GetInnerTypeAst(), ExprCtx.WithResultIsUsedAsType()))
        {
            GeneratorAst.SetInnerTypeAst(Move(NewInnerTypeAst.AsRef()));
        }

        STypeTypes ElementTypes = GetTypeTypes(*GeneratorAst.GetInnerTypeAst());
        ValidateNonAttributeType(ElementTypes._NegativeType, GeneratorAst.GetInnerTypeAst()->GetMappedVstNode());
        ValidateNonAttributeType(ElementTypes._PositiveType, GeneratorAst.GetInnerTypeAst()->GetMappedVstNode());
        const CGeneratorType& NegativeGeneratorType = _Program->GetOrCreateGeneratorType(ElementTypes._NegativeType);
        const CGeneratorType& PositiveGeneratorType = _Program->GetOrCreateGeneratorType(ElementTypes._PositiveType);
        GeneratorAst._TypeType = &_Program->GetOrCreateTypeType(&NegativeGeneratorType, &PositiveGeneratorType);

        return GeneratorAstRef;
    }

    //-------------------------------------------------------------------------------------------------
    void ValidateMapKeyType(const CTypeBase* KeyType, const CAstNode& ErrorNode, bool bIsInferred)
    {
        const Vst::Node* ErrorNodeVst = ErrorNode.GetMappedVstNode();
        ULANG_ASSERT(ErrorNodeVst);

        // Validate that the key type is comparable for equality.
        // Comparability of classes is dependent on unique attribute, and so these checks must be deferred until after
        // it is analyzed.
        EnqueueDeferredTask(Deferred_ValidateAttributes, [this, KeyType, ErrorNodeVst, bIsInferred]
        {
            const CNormalType& KeyNormalType = KeyType->GetNormalType();
            EComparability Comparability = KeyNormalType.GetComparability();

            // Prior to 31.00, there was a bug that option types said that if their value type was comparable, the option type was hashable.
            // Instead of threading the UploadedAtFNVersion through CNormalType::GetComparability, simply do the backwards compatibility check here.
            if (!VerseFN::UploadedAtFNVersion::OptionTypeDoesntIgnoreValueHashability(_Context._Package->_UploadedAtFNVersion))
            {
                if (const COptionType* OptionType = KeyNormalType.AsNullable<COptionType>())
                {
                    Comparability = OptionType->GetValueType()->GetNormalType().GetComparability() == EComparability::Incomparable
                        ? EComparability::Incomparable
                        : EComparability::ComparableAndHashable;
                }
            }

            switch (Comparability)
            {
            case EComparability::Incomparable:
                AppendGlitch(
                    ErrorNodeVst,
                    EDiagnostic::ErrSemantic_IncompatibleArgument,
                    CUTF8String("%s'%s' cannot be used as the type of map keys because it is not comparable for equality.",
                        bIsInferred ? "Inferred key type " : "",
                        KeyType->AsCode().AsCString()));
                break;
            case EComparability::Comparable:
                // Temporarily, some types are comparable but not hashable and can't be used as a key.
                AppendGlitch(
                    ErrorNodeVst,
                    EDiagnostic::ErrSemantic_Unimplemented,
                    CUTF8String("Use of %s'%s' as a map key is not yet implemented.",
                        bIsInferred ? "inferred key type " : "",
                        KeyType->AsCode().AsCString()));
                break;
            case EComparability::ComparableAndHashable:
                break;
            default:
                ULANG_UNREACHABLE();
            }
        });
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeMapTypeFormer(CExprMapTypeFormer& MapTypeFormer, const SExprCtx& ExprCtx)
    {
        // Produce an error if there is not exactly one key type expression.
        // The desugarer should only produce a CExprMapTypeFormer if there's a non-zero number of
        // key type expressions, so we don't need to handle that case.
        ULANG_ASSERTF(MapTypeFormer.KeyTypeAsts().Num() > 0, "Expected at least one key type subexpression in CExprMapTypeFormer");
        if (MapTypeFormer.KeyTypeAsts().Num() != 1)
        {
            AppendGlitch(MapTypeFormer, EDiagnostic::ErrSemantic_IncompatibleArgument, "Map type constructor expects exactly one key type argument");
            return ReplaceMapping(MapTypeFormer, TSRef<CExprError>::New());
        }

        // Analyze the key and value type expressions.
        if (TSPtr<CExpressionBase> KeyTypeAst = AnalyzeExpressionAst(MapTypeFormer.KeyTypeAsts()[0], ExprCtx.WithResultIsUsedAsType()))
        {
            MapTypeFormer.SetKeyTypeAst(Move(KeyTypeAst.AsRef()), 0);
        }
        if (TSPtr<CExpressionBase> ValueTypeAst = AnalyzeExpressionAst(MapTypeFormer.ValueTypeAst(), ExprCtx.WithResultIsUsedAsType()))
        {
            MapTypeFormer.SetValueTypeAst(Move(ValueTypeAst.AsRef()));
        }

        STypeTypes KeyTypes = GetTypeTypes(*MapTypeFormer.KeyTypeAsts()[0]);
        STypeTypes ValueTypes = GetTypeTypes(*MapTypeFormer.ValueTypeAst());
        ValidateNonAttributeType(KeyTypes._NegativeType, MapTypeFormer.KeyTypeAsts()[0]->GetMappedVstNode());
        ValidateNonAttributeType(KeyTypes._PositiveType, MapTypeFormer.KeyTypeAsts()[0]->GetMappedVstNode());
        ValidateNonAttributeType(ValueTypes._NegativeType, MapTypeFormer.ValueTypeAst()->GetMappedVstNode());
        ValidateNonAttributeType(ValueTypes._PositiveType, MapTypeFormer.ValueTypeAst()->GetMappedVstNode());

        ValidateMapKeyType(KeyTypes._NegativeType, *MapTypeFormer.KeyTypeAsts()[0], false);

        // Create the map type.
        const CMapType& NegativeMapType = _Program->GetOrCreateMapType(KeyTypes._NegativeType, ValueTypes._NegativeType);
        const CMapType& PositiveMapType = _Program->GetOrCreateMapType(KeyTypes._PositiveType, ValueTypes._PositiveType);
        MapTypeFormer._TypeType = &_Program->GetOrCreateTypeType(&NegativeMapType, &PositiveMapType);

        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    // Transform an invocation into a tuple type
    TSPtr<CExpressionBase> AnalyzeTupleType(CExprInvocation& Invocation, const SExprCtx& ExprCtx)
    {
        if (Invocation._CallsiteBracketStyle == CExprInvocation::EBracketingStyle::SquareBrackets)
        {
            AppendGlitch(
                Invocation,
                EDiagnostic::ErrSemantic_IncompatibleFailure,
                CUTF8String("Tuple type uses round brackets / parentheses `tuple(..)` rather than square `[]` or curly `{}` brackets."));

            // Continue as tuple type to give additional context...
        }

        TSPtr<CExprTupleType> TupleTypeExprPtr;
        CExprTupleType* TupleTypeExpr;
        TSPtrArray<CExpressionBase>* TypeExprs;
        TSPtr<CExpressionBase> Argument = Invocation.TakeArgument();
        if (Argument->GetNodeType() == EAstNodeType::Invoke_MakeTuple)
        {
            const CExprMakeTuple& Arguments = static_cast<const CExprMakeTuple&>(*Argument);
            const TSPtrArray<CExpressionBase>& SubExprs = Arguments.GetSubExprs();
            TupleTypeExprPtr = TSRef<CExprTupleType>::New(SubExprs.Num());
            TupleTypeExpr = TupleTypeExprPtr.Get();
            TypeExprs = &TupleTypeExpr->GetElementTypeExprs();
            for (const auto& TypeExpr : SubExprs)
            {
                // Reference counted so okay just to add
                TypeExprs->Add(TypeExpr);
            }
        }
        else
        {
            TupleTypeExprPtr = TSRef<CExprTupleType>::New(1);
            TupleTypeExpr = TupleTypeExprPtr.Get();
            TypeExprs = &TupleTypeExpr->GetElementTypeExprs();
            TypeExprs->Add(Argument);
        }
        int32_t ArgNum = TypeExprs->Num();
        ReplaceMapping(Invocation, *TupleTypeExpr);

        // Analyze the CExprTupleType.
        CTupleType::ElementArray NegativeTypes;
        NegativeTypes.Reserve(ArgNum);
        CTupleType::ElementArray PositiveTypes;
        PositiveTypes.Reserve(ArgNum);

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Iterate through elements and replace with any more fully analyzed versions
        for (int32_t Idx = 0; Idx < ArgNum; Idx++)
        {
            if (TSPtr<CExpressionBase> NewSubExpr = AnalyzeExpressionAst((*TypeExprs)[Idx].AsRef(), ExprCtx.WithResultIsUsedAsType()))
            {
                TupleTypeExpr->ReplaceElementTypeExpr(Move(NewSubExpr), Idx);
            }

            STypeTypes ElementTypes = GetTypeTypes(*(*TypeExprs)[Idx]);
            ValidateNonAttributeType(ElementTypes._NegativeType, (*TypeExprs)[Idx]->GetMappedVstNode());
            ValidateNonAttributeType(ElementTypes._PositiveType, (*TypeExprs)[Idx]->GetMappedVstNode());
            NegativeTypes.Add(ElementTypes._NegativeType);
            PositiveTypes.Add(ElementTypes._PositiveType);
        }

        const CTupleType& NegativeTupleType = _Program->GetOrCreateTupleType(Move(NegativeTypes));
        const CTupleType& PositiveTupleType = _Program->GetOrCreateTupleType(Move(PositiveTypes));
        TupleTypeExpr->_TypeType = &_Program->GetOrCreateTypeType(&NegativeTupleType, &PositiveTupleType);

        // Replace Invocation with TupleType
        return TupleTypeExprPtr;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeArrow(CExprArrow& Arrow, const SExprCtx& ExprCtx)
    {
        // Analyze the domain and range type expressions.
        if (TSPtr<CExpressionBase> DomainTypeAst = AnalyzeExpressionAst(Arrow.Domain(), ExprCtx.WithResultIsUsedAsType()))
        {
            Arrow.SetDomain(Move(DomainTypeAst.AsRef()));
        }
        if (TSPtr<CExpressionBase> RangeTypeAst = AnalyzeExpressionAst(Arrow.Range(), ExprCtx.WithResultIsUsedAsType()))
        {
            Arrow.SetRange(Move(RangeTypeAst.AsRef()));
        }

        STypeTypes DomainTypes = GetTypeTypes(*Arrow.Domain());
        STypeTypes RangeTypes = GetTypeTypes(*Arrow.Range());
        ValidateNonAttributeType(DomainTypes._NegativeType, Arrow.Domain()->GetMappedVstNode());
        ValidateNonAttributeType(DomainTypes._PositiveType, Arrow.Domain()->GetMappedVstNode());
        ValidateNonAttributeType(RangeTypes._NegativeType, Arrow.Range()->GetMappedVstNode());
        ValidateNonAttributeType(RangeTypes._PositiveType, Arrow.Range()->GetMappedVstNode());

        // Create the function type.
        const CFunctionType& NegativeFunctionType = _Program->GetOrCreateFunctionType(
            *DomainTypes._PositiveType,
            *RangeTypes._NegativeType,
            EffectSets::FunctionDefault);
        const CFunctionType& PositiveFunctionType = _Program->GetOrCreateFunctionType(
            *DomainTypes._NegativeType,
            *RangeTypes._PositiveType,
            EffectSets::FunctionDefault);
        Arrow._TypeType = &_Program->GetOrCreateTypeType(&NegativeFunctionType, &PositiveFunctionType);

        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    // Transform a CExprInvocation into a CExprSubtype.
    TSPtr<CExpressionBase> AnalyzeSubtype(CExprInvocation& Invocation, const SExprCtx& ExprCtx)
    {
        if (Invocation._CallsiteBracketStyle == CExprInvocation::EBracketingStyle::SquareBrackets)
        {
            AppendGlitch(
                Invocation,
                EDiagnostic::ErrSemantic_IncompatibleFailure,
                CUTF8String("`subtype` uses round brackets / parentheses `subtype(..)` rather than square or curly brackets."));
            // Continue as subtype to give additional context...
        }

        // Convert the CExprInvocation to a CExprSubtype.
        TSRef<CExprSubtype> SubtypeAstRef = TSRef<CExprSubtype>::New(Invocation.TakeArgument().AsRef());
        CExprSubtype&       SubtypeAst    = *SubtypeAstRef;

        Invocation.GetMappedVstNode()->AddMapping(&SubtypeAst);

        // Analyze the inner type expression.
        if (TSPtr<CExpressionBase> NewInnerTypeAst = AnalyzeExpressionAst(SubtypeAst.GetInnerTypeAst(), ExprCtx.WithResultIsUsedAsType()))
        {
            SubtypeAst.SetInnerTypeAst(Move(NewInnerTypeAst.AsRef()));
        }

        // Check that the argument is a class.
        STypeTypes InnerTypes = GetTypeTypes(*SubtypeAst.GetInnerTypeAst());
        if (InnerTypes._Tag == ETypeTypeTag::Type)
        {
            if (const CClass* Superclass = InnerTypes._NegativeType->GetNormalType().AsNullable<CClass>())
            {
                const Vst::Node* InnerTypeVst = SubtypeAst.GetInnerTypeAst()->GetMappedVstNode();
                // After the class hierarchy has been constructed, validate that the class isn't a struct and doesn't inherit from attribute.
                EnqueueDeferredTask(Deferred_ValidateType, [this, InnerTypeVst, Superclass]()
                {
                    if (SemanticTypeUtils::IsAttributeType(Superclass))
                    {
                        AppendGlitch(
                            InnerTypeVst,
                            EDiagnostic::ErrSemantic_IncompatibleArgument,
                            "`subtype` expects its argument to be a class.");
                    }
                });
            }
        }

        const CTypeBase& NegativeSubtypeType = _Program->GetOrCreateTypeType(
            &_Program->_falseType,
            InnerTypes._NegativeType);
        const CTypeBase& PositiveSubtypeType = _Program->GetOrCreateTypeType(
            &_Program->_falseType,
            InnerTypes._PositiveType);

        SubtypeAst._TypeType = &_Program->GetOrCreateTypeType(&NegativeSubtypeType, &PositiveSubtypeType);

        return SubtypeAstRef;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeQueryValue(const TSRef<CExprQueryValue>& QueryValue, const SExprCtx& ExprCtx)
    {
        // Analyze the query operator as a call to the appropriate overloaded operator function.
        QueryValue->SetCallee(TSRef<CExprIdentifierUnresolved>::New(_Program->_IntrinsicSymbols._OpNameQuery));
        return AnalyzeInvocation(QueryValue, ExprCtx);
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzePointerToReference(const TSRef<CExprPointerToReference>& PointerToReference, const SExprCtx& ExprCtx)
    {
        if (_CurrentTaskPhase < Deferred_NonFunctionExpressions)
        {
            AppendGlitch(
                *PointerToReference,
                EDiagnostic::ErrSemantic_Unimplemented,
                "Support for the '^' postfix operator in this context is not yet implemented.");
            return ReplaceNodeWithError(PointerToReference);
        }

        if (auto NewOperand = AnalyzeExpressionAst(PointerToReference->Operand().AsRef(), ExprCtx.WithResultIsUsed(nullptr)))
        {
            PointerToReference->SetOperand(Move(NewOperand));
        }

        // Produce an error if the operand result is not a pointer.
        const CNormalType& OperandType = PointerToReference->Operand()->GetResultType(*_Program)->GetNormalType();
        const CTypeBase* NegativeValueType;
        const CTypeBase* PositiveValueType;
        if (const CPointerType* PointerType = OperandType.AsNullable<CPointerType>())
        {
            NegativeValueType = PointerType->NegativeValueType();
            PositiveValueType = PointerType->PositiveValueType();
        }
        else
        {
            NegativeValueType = _Program->GetDefaultUnknownType();
            PositiveValueType = _Program->GetDefaultUnknownType();
            if (!SemanticTypeUtils::IsUnknownType(&OperandType))
            {
                AppendGlitch(*PointerToReference, EDiagnostic::ErrSemantic_ExpectedPointerType);
            }
        }

        const CReferenceType& ReferenceType = _Program->GetOrCreateReferenceType(NegativeValueType, PositiveValueType);
        PointerToReference->SetResultType(&ReferenceType);
        
        if (ExprCtx.ReferenceableContext != InReferenceableContext)
        {
            // convert from ref to value
            RequireEffects(*PointerToReference, EffectSets::Reads, ExprCtx.AllowedEffects, "pointer read");

            TSRef<CExprReferenceToValue> ReferenceToValue = TSRef<CExprReferenceToValue>::New(PointerToReference);
            ReferenceToValue->SetResultType(ReferenceType.PositiveValueType());
            return ReplaceMapping(*PointerToReference, ReferenceToValue);
        }

        return nullptr;
    }

    const CReferenceType* ValidateReferenceType(const CExpressionBase& Expression)
    {
        const CTypeBase* Type = Expression.GetResultType(*_Program);
        if (SemanticTypeUtils::IsUnknownType(Type))
        {
            return nullptr;
        }
        const CNormalType& NormalType = Type->GetNormalType();
        if (!NormalType.IsA<CReferenceType>())
        {
            AppendGlitch(
                Expression,
                EDiagnostic::ErrSemantic_IncompatibleArgument,
                CUTF8String(
                    "The assignment's left hand expression type `%s` cannot be assigned to",
                    NormalType.AsCode().AsCString()));
            return nullptr;
        }
        return &NormalType.AsChecked<CReferenceType>();
    }

    TSPtr<CExpressionBase> AnalyzeSet(const TSRef<CExprSet>& Set, const SExprCtx& ExprCtx)
    {
        if (auto NewOperand = AnalyzeExpressionAst(Set->Operand().AsRef(), ExprCtx.With(InReferenceableContext)))
        {
            Set->SetOperand(Move(NewOperand));
        }
        const CTypeBase* OperandType = ValidateReferenceType(*Set->Operand());
        if (!OperandType)
        {
            OperandType = _Program->GetDefaultUnknownType();
        }
        Set->SetResultType(OperandType);
        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    static bool IsIdentifierSymbol(const CExpressionBase& MaybeIdentifier, const CSymbol Symbol)
    {
        if (MaybeIdentifier.GetNodeType() != EAstNodeType::Identifier_Unresolved) { return false; }
        const CExprIdentifierUnresolved& Identifier = static_cast<const CExprIdentifierUnresolved&>(MaybeIdentifier);
        return !Identifier.Context()
            && !Identifier.Qualifier()
            && Identifier._Symbol == Symbol;
    }
    
    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeInvokeType(
        CExprInvocation& Invocation,
        const CTypeBase* NegativeType,
        const CTypeBase* PositiveType,
        const SExprCtx& ExprCtx)
    {
        // Analyze the argument subexpressions.
        TSPtr<CExpressionBase> Argument = Invocation.TakeArgument();
        if (TSPtr<CExpressionBase> NewArgumentAst = AnalyzeExpressionAst(Argument.AsRef(), ExprCtx.WithResultIsUsed(nullptr)))
        {
            Argument = Move(NewArgumentAst);
        }
        return AnalyzeInvokeType(Invocation, Move(Argument), NegativeType, PositiveType, ExprCtx);
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeInvokeType(
        CExprInvocation& Invocation,
        TSPtr<CExpressionBase> Argument,
        const CTypeBase* NegativeType,
        const CTypeBase* PositiveType,
        const SExprCtx& ExprCtx)
    {
        const bool bIsFallible = Invocation._CallsiteBracketStyle == CExprInvocation::EBracketingStyle::SquareBrackets;
        if (!bIsFallible)
        {
            // void's domain is any, other types have a domain of themselves.
            const CTypeBase* TypeDomain = &GetFunctorDomain(*NegativeType);

            // If the invocation is infallible, require that the argument is a subtype of invoked type's domain.
            const CTypeBase* ArgumentType = Argument->GetResultType(*_Program);
            if (!Constrain(ArgumentType, TypeDomain))
            {
                AppendGlitch(
                    Invocation,
                    EDiagnostic::ErrSemantic_IncompatibleArgument,
                    CUTF8String("This type predicate expects a value of type %s, but this argument is an incompatible value of type %s.",
                        TypeDomain->AsCode().AsCString(),
                        ArgumentType->AsCode().AsCString()));
            }
        }
        else
        {
            // Don't allow fallible casts outside failure contexts.
            RequireEffects(Invocation, EEffect::decides, ExprCtx.AllowedEffects, "type invocation");

            const CTypeBase* ArgType = Argument->GetResultType(*_Program);
            const CNormalType& ArgNormalType = ArgType->GetNormalType();

            if (SemanticTypeUtils::IsUnknownType(NegativeType))
            {
                AppendGlitch(Invocation,
                    EDiagnostic::ErrSemantic_IncompatibleArgument,
                    CUTF8String("Dynamic cast must be to a type, instead got: %s.", NegativeType->AsCode().AsCString()));
                return nullptr;
            }

            const CNormalType& NormalType = NegativeType->GetNormalType();
            if (NormalType.IsA<CClass>() || NormalType.IsA<CInterface>())
            {
                // Check that the argument is a non-attribute class instance.
                if ((!ArgNormalType.IsA<CClass>()
                        || ArgNormalType.AsChecked<CClass>().IsStruct()
                        || SemanticTypeUtils::IsAttributeType(&ArgNormalType))
                    && !ArgNormalType.IsA<CInterface>())
                {
                    const CUTF8String ArgTypeString = ArgType->AsCode();
                    AppendGlitch(
                        *Argument,
                        EDiagnostic::ErrSemantic_IncompatibleArgument,
                        CUTF8String("Dynamic cast %s to `%s`: argument type `%s` must be a class.",
                                    ArgTypeString.AsCString(), NegativeType->AsCode().AsCString(), ArgTypeString.AsCString()));
                }

                // Check that the cast type is either an interface or a non-attribute-derived class.
                if ((!NormalType.IsA<CClass>()
                        || NormalType.AsChecked<CClass>().IsStruct()
                        || SemanticTypeUtils::IsAttributeType(&NormalType))
                    && !NormalType.IsA<CInterface>())
                {
                    AppendGlitch(
                        *Argument,
                        EDiagnostic::ErrSemantic_Unsupported,
                        CUTF8String("Cast target `%s` must be an interface or a class.", NegativeType->AsCode().AsCString()));
                }

                bool bIsParametric = NormalType.IsA<CClass>() ? 
                    NormalType.AsChecked<CClass>().IsParametric()
                    : NormalType.AsChecked<CInterface>().IsParametric();

                if (bIsParametric)
                {
                    AppendGlitch(
                        *Argument,
                        EDiagnostic::ErrSemantic_Unimplemented,
                        CUTF8String("Dynamic casting to a parametric type is not yet supported. In cast to `%s`.", NegativeType->AsCode().AsCString()));
                }

            }
            else if (NormalType.IsA<CIntType>())
            {
                if (!ArgNormalType.IsA<CIntType>())
                {
                    AppendGlitch(*Argument,
                        EDiagnostic::ErrSemantic_IncompatibleArgument,
                        CUTF8String("Dynamic cast to `%s` takes an int as its parameter, instead got %s", NegativeType->AsCode().AsCString(), ArgType->AsCode().AsCString()));
                }
            }
            else if (NormalType.IsA<CFloatType>())
            {
                if (!ArgNormalType.IsA<CFloatType>())
                {
                    AppendGlitch(*Argument,
                        EDiagnostic::ErrSemantic_IncompatibleArgument,
                        CUTF8String("Dynamic cast to `%s` takes an float as its parameter, instead got %s", NegativeType->AsCode().AsCString(), ArgType->AsCode().AsCString()));
                }
            }
            else
            {
                AppendGlitch(
                    *Argument,
                    EDiagnostic::ErrSemantic_Unsupported,
                    CUTF8String("Cast target `%s` must be an interface, class, int, or float.", NegativeType->AsCode().AsCString()));
            }
        }

        return ReplaceMapping(Invocation, TSRef<CExprInvokeType>::New(
            NegativeType,
            PositiveType,
            bIsFallible,
            Invocation.TakeCallee(),
            Move(Argument.AsRef())));
    }

    //-------------------------------------------------------------------------------------------------
    void ResolveOverloads(
        const TArray<const CFunction*>& FunctionOverloads,
        const CTypeBase& ArgumentsType,
        const CTypeBase* ExtensionArgumentsType,
        TArrayG<SOverload, TInlineElementAllocator<4>>& ResolvedOverloads)
    {
        // Gather the list of overloads that match the provided arguments.
        for (const CFunction* OverloadedFunction : FunctionOverloads)
        {
            auto [OverloadedInstTypeVariables, OverloadedFunctionType, OverloadedNegativeReturnType] = Instantiate(*OverloadedFunction);
            if (!ExtensionArgumentsType || OverloadedFunction->_ExtensionFieldAccessorKind == EExtensionFieldAccessorKind::Function)
            {
                if (OverloadedFunctionType && Matches(&ArgumentsType, &OverloadedFunctionType->GetParamsType()))
                {
                    ResolvedOverloads.Add(SOverload{
                        OverloadedFunction,
                        OverloadedInstTypeVariables,
                        OverloadedFunctionType,
                        OverloadedNegativeReturnType });
                }
            }
            else
            {
                if (OverloadedFunctionType && Matches(ExtensionArgumentsType, &OverloadedFunctionType->GetParamsType()))
                {
                    ResolvedOverloads.Add(SOverload{
                        OverloadedFunction,
                        OverloadedInstTypeVariables,
                        OverloadedFunctionType,
                        OverloadedNegativeReturnType });
                }
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    const CFunctionType* ResolveOverloadedCallee(
        CExprInvocation& Invocation,
        CExprIdentifierOverloadedFunction& OverloadedCallee,
        const CTypeBase& ArgumentsType,
        const CTypeBase* ExtensionArgumentsType,
        TSPtr<CExpressionBase>&& ExtensionArgument)
    {
        if (OverloadedCallee.Qualifier() && IsQualifierNamed(OverloadedCallee.Qualifier().AsRef(), _SuperName))
        {
            CUTF8String ErrorMessage("Qualifier (super:) cannot be used on overloaded functions");
            AppendGlitch(Invocation, EDiagnostic::ErrSemantic_Unsupported, Move(ErrorMessage));
        }

        // Gather the list of overloads that match the provided arguments.
        TArrayG<SOverload, TInlineElementAllocator<4>> ResolvedOverloads;
        ResolveOverloads(OverloadedCallee._FunctionOverloads, ArgumentsType, ExtensionArgumentsType, ResolvedOverloads);

        if (ResolvedOverloads.Num() == 1)
        {
            const SOverload& ResolvedOverload = ResolvedOverloads[0];
            const CFunction* Function = static_cast<const CFunction*>(ResolvedOverload.Definition);

            if (!OverloadedCallee._bAllowUnrestrictedAccess)
            {
                // Validate access permissions
                DeferredRequireAccessible(Invocation.GetMappedVstNode(), *_Context._Scope, *Function);
            }

            ValidateDefinitionUse(*Function, Invocation.GetMappedVstNode());

            // If exactly one overload matched the provided arguments, replace the callee with the resolved function.
            if (Function->_ExtensionFieldAccessorKind == EExtensionFieldAccessorKind::Function)
            {
                Invocation.SetCallee(ReplaceMapping(OverloadedCallee, TSRef<CExprIdentifierFunction>::New(
                    *Function,
                    ResolvedOverload.InstantiatedTypeVariables,
                    ResolvedOverload.FunctionType,
                    OverloadedCallee._bConstructor ? ResolvedOverload.NegativeReturnType : nullptr,
                    OverloadedCallee.TakeContext(),
                    OverloadedCallee.TakeQualifier(),
                    false)));
            }
            else
            {
                Invocation.SetCallee(ReplaceMapping(OverloadedCallee, TSRef<CExprIdentifierFunction>::New(
                    *Function,
                    ResolvedOverload.InstantiatedTypeVariables,
                    ResolvedOverload.FunctionType,
                    OverloadedCallee._bConstructor ? ResolvedOverload.NegativeReturnType : nullptr,
                    TSPtr<CExpressionBase>(), // Context is used as first argument
                    OverloadedCallee.TakeQualifier(),
                    false)));
                Invocation.SetArgument(Move(ExtensionArgument));
            }
            return ResolvedOverload.FunctionType;
        }
        else if (SemanticTypeUtils::IsUnknownType(&ArgumentsType))
        {
            // If an error in the arguments produces an unknown arguments type, it will match all overloads.
            // To avoid a spurious error, just produce an error node.
            return nullptr;
        }
        else if (ResolvedOverloads.IsEmpty())
        {
            if (Invocation.GetCallee())
            {
                SResolvedDefinitionArray OutOfScopeDefinitions;

                // No overload matched. Try to find overloaded definitions in another module that could be imported to solve the problem.
                // We rely on the overloaded calls which did not match to tell us if we are extension method as we don't have the SExprCtx here...
                if (OverloadedCallee._FunctionOverloads.Num() && OverloadedCallee._FunctionOverloads[0]->_ExtensionFieldAccessorKind == EExtensionFieldAccessorKind::ExtensionMethod)
                {
                    TOptional<CSymbol> ExtensionName = _Program->GetSymbols()->Find(_Program->_IntrinsicSymbols.MakeExtensionFieldOpName(OverloadedCallee._Symbol));
                    _Program->IterateRecurseLogicalScopes([&OutOfScopeDefinitions, ExtensionName](const CLogicalScope& LogicalScope)
                        {
                            if (LogicalScope.GetKind() == CScope::EKind::Module)
                            {
                                CScope::ResolvedDefnsAppend(&OutOfScopeDefinitions, LogicalScope.FindDefinitions(*ExtensionName));
                            }
                            return EVisitResult::Continue;
                        });
                }
                else
                {
                    _Program->IterateRecurseLogicalScopes([&OutOfScopeDefinitions, &OverloadedCallee](const CLogicalScope& LogicalScope)
                        {
                            if (LogicalScope.GetKind() == CScope::EKind::Module)
                            {
                                CScope::ResolvedDefnsAppend(&OutOfScopeDefinitions, LogicalScope.FindDefinitions(OverloadedCallee._Symbol));
                            }
                            return EVisitResult::Continue;
                        });
                }

                TArray<const CFunction*> OutOfScopeFunctionOverloads;
                for (const SResolvedDefinition& ResolvedDefn : OutOfScopeDefinitions)
                {
                    if (const CFunction* Function = ResolvedDefn._Definition->AsNullable<CFunction>())
                    {
                        // only add new functions found
                        if (!OverloadedCallee._FunctionOverloads.Contains(Function))
                        {
                            OutOfScopeFunctionOverloads.Add(Function);
                        }
                    }
                }

                TArrayG<SOverload, TInlineElementAllocator<4>> OutOfScopeResolvedOverloads;
                ResolveOverloads(OutOfScopeFunctionOverloads, ArgumentsType, ExtensionArgumentsType, OutOfScopeResolvedOverloads);

                if (OutOfScopeResolvedOverloads.Num() == 1)
                {
                    AppendGlitch(
                        Invocation,
                        EDiagnostic::ErrSemantic_IncompatibleArgument,
                        CUTF8String("No overload of the function `%s` matches the provided arguments (%s). Did you forget to specify using { %s }?",
                            OverloadedCallee._Symbol.AsCString(),
                            FormatParameterList(CFunctionType::AsParamTypes(&ArgumentsType)).AsCString(),
                            OutOfScopeResolvedOverloads[0].Definition->_EnclosingScope.GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString()));
                    return nullptr;
                }
                else if (OutOfScopeResolvedOverloads.Num() > 1)
                {
                    // make a set of unique `using` statements, any of which could resolve the overload.
                    TSet<CUTF8String> UsingStatements;
                    for (const SOverload& Overload : OutOfScopeResolvedOverloads)
                    {
                        UsingStatements.Insert(Overload.Definition->_EnclosingScope.GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString());
                    }
                    CUTF8StringBuilder UsingStatementsString;
                    for (const CUTF8String& Using : UsingStatements)
                    {
                        UsingStatementsString.Append(Using);
                        UsingStatementsString.Append("\n");
                    }

                    AppendGlitch(
                        Invocation,
                        EDiagnostic::ErrSemantic_IncompatibleArgument,
                        CUTF8String("No overload of the function `%s` matches the provided arguments (%s). Did you forget to specify one of the following?\n%s",
                            OverloadedCallee._Symbol.AsCString(),
                            FormatParameterList(CFunctionType::AsParamTypes(&ArgumentsType)).AsCString(),
                            UsingStatementsString.AsCString()));
                    return nullptr;
                }
            }

            if (!OverloadedCallee._TypeOverload)
            {
                // Produce an error if none of the overloads matched the provided arguments.
                AppendGlitch(
                    Invocation,
                    EDiagnostic::ErrSemantic_IncompatibleArgument,
                    CUTF8String(
                        "No overload of the function `%s` matches the provided arguments (%s). Could be any of:%s",
                        (*OverloadedCallee._FunctionOverloads.GetData())->AsNameCString(),
                        FormatParameterList(CFunctionType::AsParamTypes(&ArgumentsType)).AsCString(),
                        FormatOverloadList(OverloadedCallee._FunctionOverloads).AsCString()));
                if (ExtensionArgumentsType)
                {
                    AppendGlitch(
                        Invocation,
                        EDiagnostic::ErrSemantic_IncompatibleArgument,
                        CUTF8String(
                            "(Also tried with extension function arguments (%s))",
                            FormatParameterList(CFunctionType::AsParamTypes(ExtensionArgumentsType)).AsCString()));
                }
            }
            else
            {   
                // This is not good, returning a nullptr here will signal to the caller that there were no suitable function available,
                // and the caller will try the dynamic cast function if it's available.
                // If that function fails then the error message will be incomplete.
                // Not possible to do something better here, since a dynamic cast isn't the same as a callee.
            }
            return nullptr;
        }
        else
        {
            // Produce an error if more than one of the overloads matched the provided arguments.
            AppendGlitch(
                Invocation,
                EDiagnostic::ErrSemantic_AmbiguousOverload,
                CUTF8String(
                    "Multiple overloads of the function match the provided arguments (%s). Could be any of:%s",
                    FormatParameterList(CFunctionType::AsParamTypes(&ArgumentsType)).AsCString(),
                    FormatOverloadList(ResolvedOverloads).AsCString()));
            return nullptr;
        }
    }

    //-------------------------------------------------------------------------------------------------
    // Transform an invocation into a tuple element access
    TSPtr<CExpressionBase> AnalyzeTupleElement(CExprInvocation& Invocation, const SExprCtx& ExprCtx)
    {
        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Start replacing invocation with tuple element access
        TSRef<CExprTupleElement> TupleElemExpr = TSRef<CExprTupleElement>::New(Invocation);

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Ensure round brackets used
        if (Invocation._CallsiteBracketStyle != CExprInvocation::EBracketingStyle::Parentheses)
        {
            AppendGlitch(
                *TupleElemExpr,
                EDiagnostic::ErrSemantic_IncompatibleFailure,
                CUTF8String("Tuple element access uses round brackets / parentheses `MyTuple(Idx)` rather than square `[]` or curly `{}` brackets."));

            // Continue to give additional context...
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Ensure argument index is integer literal
        TupleElemExpr->_ElemIdxExpr = Invocation.GetArgument();

        // Do further analysis of argument
        if (TSPtr<CExpressionBase> NewSubExpr = AnalyzeExpressionAst(TupleElemExpr->_ElemIdxExpr.AsRef(), ExprCtx.WithResultIsUsed(_Program->_intType)))
        {
            TupleElemExpr->_ElemIdxExpr = NewSubExpr;
        }


        if ((TupleElemExpr->_ElemIdxExpr->GetNodeType() != EAstNodeType::Literal_Number) || (static_cast<CExprNumber&>(*TupleElemExpr->_ElemIdxExpr).IsFloat()))
        {
            AppendGlitch(
                *TupleElemExpr->_ElemIdxExpr,
                EDiagnostic::ErrSemantic_IncompatibleArgument,
                CUTF8String("Tuple element access expected an integer literal and instead got %s.", TupleElemExpr->_ElemIdxExpr->GetErrorDesc().AsCString()));

            TupleElemExpr->SetResultType(_Program->GetDefaultUnknownType());
            return TupleElemExpr;
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Ensure argument index is within range of element indexes
        TupleElemExpr->_ElemIdx = static_cast<CExprNumber&>(*TupleElemExpr->_ElemIdxExpr).GetIntValue();

        const CTypeBase* TupleExprType = TupleElemExpr->_TupleExpr->GetResultType(*_Program);

		// _TupleExpr comes into this function as the invocation callee, already analyzed. if it's
		// a reference type, insert an ReferenceToValue conversion node
        if(const CReferenceType* TupleReferenceType = TupleExprType->GetNormalType().AsNullable<CReferenceType>())
        {
            TupleExprType = TupleReferenceType->PositiveValueType();
            TupleElemExpr->_TupleExpr = TSRef<CExprReferenceToValue>::New(Move(TupleElemExpr->_TupleExpr));
            TupleElemExpr->_TupleExpr->SetResultType(TupleExprType);
        }

        const CTupleType& TupleType = TupleExprType->GetNormalType().AsChecked<CTupleType>();

        int32_t NumElements = TupleType.Num();
        if (TupleElemExpr->_ElemIdx < 0 || TupleElemExpr->_ElemIdx >= NumElements)
        {
            AppendGlitch(
                *TupleElemExpr->_ElemIdxExpr,
                EDiagnostic::ErrSemantic_TupleElementIdxRange,
                CUTF8String("%s element access expected an integer literal within the range 0..%i and got %i.", TupleType.AsCode().AsCString(), NumElements, TupleElemExpr->_ElemIdx));
            TupleElemExpr->SetResultType(_Program->GetDefaultUnknownType());
            return TupleElemExpr;
        }

        // Store the result type since it is readily available
        TupleElemExpr->SetResultType(TupleType[static_cast<int32_t>(TupleElemExpr->_ElemIdx)]);

        // Replace Invocation with tuple element access
        return TupleElemExpr;
    }

    //-------------------------------------------------------------------------------------------------

    TSPtr<CExpressionBase> GetContextOfCallee(const CExpressionBase* Callee)
    {
        if (Callee->GetNodeType() == Cases<EAstNodeType::Identifier_Function, EAstNodeType::Identifier_OverloadedFunction, EAstNodeType::Identifier_Unresolved>)
        {
            return static_cast<const CExprIdentifierBase*>(Callee)->Context();
        }
        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------

    TSPtr<CExpressionBase> CreateExtensionArguments(CExprInvocation& Invocation, TSPtr<CExpressionBase> ExtensionArgument)
    {
        // a0.callee(a1, a2, ..) => callee(a0, (a1, a2, ..))
        TSPtr<CExpressionBase> Argument = Invocation.GetArgument();

        TSPtr<CExprMakeTuple> NewArgument = TSRef<CExprMakeTuple>::New();
        NewArgument->SetNonReciprocalMappedVstNode(Invocation.GetMappedVstNode());
        NewArgument->AppendSubExpr(Move(ExtensionArgument));
        NewArgument->AppendSubExpr(Argument); // this may be an empty CExprMakeTuple... we append this still to accomodate named args - JIRA #SOL-6937
        SetMakeTupleResultType(*NewArgument);
        return NewArgument;
    }

    //-------------------------------------------------------------------------------------------------
    
    // Create extension method argument, if possible, i.e, from a0.callee(a1 ..) create callee(a0, (a1, ...))
    // If not return nullptr
    TSPtr <CExpressionBase> CreateExtensionArgument(CExprInvocation& Invocation)
    {
        if (TSPtr<CExpressionBase> Context = GetContextOfCallee(Invocation.GetCallee().Get()))
        {
            return CreateExtensionArguments(Invocation, Context);
        }
        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------

    // Create extension method argument by prepending Self, if in a context where Self is available.  
    // If not return nullptr
    TSPtr <CExpressionBase> PrependImplicitSelfArgument(CExprInvocation& Invocation)
    {
        if (_Context._Self)
        {
            return CreateExtensionArguments(Invocation, TSRef<CExprSelf>::New(_Context._Self));
        }
        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    void SetMakeTupleResultType(CExprMakeTuple& Tuple)
    {
        CTupleType::ElementArray ElementTypes;
        ElementTypes.Reserve(Tuple.SubExprNum());
        for (const TSPtr<CExpressionBase>& SubExpr : Tuple.GetSubExprs())
        {
            ElementTypes.Add(SubExpr->GetResultType(*_Program));
        }
        int32_t FirstNamedIndex = GetFirstNamedIndex(Tuple.GetSubExprs());
        CTupleType* TupleType = &_Program->GetOrCreateTupleType(Move(ElementTypes), FirstNamedIndex);
        Tuple.SetResultType(TupleType);
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeInvocation(
        const TSRef<CExprInvocation>& Invocation,
        const SExprCtx& ExprCtx,
        const SExprArgs& ExprArgs = SExprArgs())
    {
        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Determine if it is a tuple type `tuple(type1, type2, ...)`
        // - could alternatively be placed in `CSemanticAnalyzerImpl.AnalyzeInvocation()` though this gets it working as a type sooner
        if (IsIdentifierSymbol(*Invocation->GetCallee(), _Symbol_tuple))
        {
            return AnalyzeTupleType(*Invocation, ExprCtx);
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Separate out `subtype(some_type)`
        // @jira SOL-1651 : Could remove CExprSubtype and just keep it a CExprInvocation, though having separate expressions can be advantageous...
        if (IsIdentifierSymbol(*Invocation->GetCallee(), _Symbol_subtype))
        {
            return AnalyzeSubtype(*Invocation, ExprCtx);
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Separate out `generator(some_type)`
        if (IsIdentifierSymbol(*Invocation->GetCallee(), _Symbol_generator) && VerseFN::UploadedAtFNVersion::EnableGenerators(_Context._Package->_UploadedAtFNVersion))
        {
            return AnalyzeGeneratorTypeFormer(*Invocation, ExprCtx);
        }

        // Analyze the invocation's callee subexpression.
        if (ExprArgs.AnalysisContext != EAnalysisContext::CalleeAlreadyAnalyzed)
        {
            SExprArgs CalleeArgs;
            CalleeArgs.ArchetypeInstantiationContext = ExprArgs.ArchetypeInstantiationContext == ArchetypeInstantiationArgument ?
                ConstructorInvocationCallee :
                NotInArchetypeInstantiationContext;
            CalleeArgs.ReadWriteContext = EReadWriteContext::Partial;
            if (ExprArgs.AnalysisContext == EAnalysisContext::ContextAlreadyAnalyzed)
            {
                CalleeArgs.AnalysisContext = ExprArgs.AnalysisContext;
            }
            if (TSPtr<CExpressionBase> NewCallee = AnalyzeExpressionAst(
                Invocation->GetCallee().AsRef(),
                ExprCtx.ResultContext == ResultIsUsedAsAttribute ? ExprCtx : ExprCtx.WithResultIsCalled(),
                CalleeArgs))
            {
                Invocation->SetCallee(Move(NewCallee));
            }
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Handle tuple element access
        const CTypeBase* CalleeType = Invocation->GetCallee()->GetResultType(*_Program);
        const CTypeBase* const OriginalCalleeType = CalleeType;

        if (!CalleeType || SemanticTypeUtils::IsUnknownType(CalleeType))
        {
            // An error has already been emitted.
            return ReplaceNodeWithError(Invocation);
        }

        // Handle accessing a tuple element with Tuple(<int literal>).
        const CNormalType& CalleeNormalType = CalleeType->GetNormalType();
        if (const CTupleType* CalleeTupleType = CalleeNormalType.AsNullable<CTupleType>())
        {
            const CTupleType& TupleType = _Program->GetOrCreateTupleType(CTupleType::ElementArray(
                CalleeTupleType->Num(),
                &_Program->_anyType));
            if (!Constrain(CalleeType, &TupleType))
            {
                AppendGlitch(
                    *Invocation->GetCallee(),
                    EDiagnostic::ErrSemantic_IncompatibleArgument,
                    CUTF8String("Tuple element access expects a value of type %s, but this tuple is an incompatible value of type %s.",
                        TupleType.AsCode().AsCString(),
                        CalleeType->AsCode().AsCString()));
            }
            return AnalyzeTupleElement(*Invocation, ExprCtx);
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Handle invoking a type as a function to cast to it: <type>[<expr>]
        STypeTypes CalleeTypes = MaybeTypeTypes(*Invocation->GetCallee());
        if (CalleeTypes._Tag == ETypeTypeTag::Type)
        {
            return AnalyzeInvokeType(*Invocation, CalleeTypes._NegativeType, CalleeTypes._PositiveType, ExprCtx);
        }

        SExprArgs ArgumentArgs = {};
        if (ExprArgs.AnalysisContext == EAnalysisContext::FirstTupleElementAlreadyAnalyzed)
        {
            ArgumentArgs.AnalysisContext = EAnalysisContext::FirstTupleElementAlreadyAnalyzed;
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Transform non-function callee from <expr>[<args>] to operator'()'[<expr>, (args)]
        if (Invocation->GetCallee()->GetNodeType() != EAstNodeType::Identifier_OverloadedFunction
            && !SemanticTypeUtils::IsUnknownType(CalleeType)
            && !CalleeType->GetNormalType().IsA<CFunctionType>())
        {
            TSPtr<CExpressionBase> Callee = Invocation->TakeCallee();
            TSPtr<CExpressionBase> Argument = Invocation->TakeArgument();
            Invocation->SetArgument(TSPtr<CExprMakeTuple>::New(
                Move(Callee),
                Move(Argument)));
            ArgumentArgs.AnalysisContext = EAnalysisContext::FirstTupleElementAlreadyAnalyzed;

            Invocation->SetCallee(TSRef<CExprIdentifierUnresolved>::New(_Program->_IntrinsicSymbols._OpNameCall, nullptr, nullptr, true));
            if (TSPtr<CExpressionBase> NewCallee = AnalyzeExpressionAst(Invocation->GetCallee().AsRef(), ExprCtx.WithResultIsCalled()))
            {
                Invocation->SetCallee(Move(NewCallee));
            }
            CalleeType = Invocation->GetCallee()->GetResultType(*_Program);
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Determine the type of the function being called.
        const CFunctionType* FunctionType = CalleeType->GetNormalType().AsNullable<CFunctionType>();
        if (!FunctionType
            && !SemanticTypeUtils::IsUnknownType(CalleeType)
            && Invocation->GetCallee()->GetNodeType() != EAstNodeType::Identifier_OverloadedFunction)
        {
            AppendGlitch(*Invocation->GetCallee(), EDiagnostic::ErrSemantic_ExpectedFunction);
        }

        {
            TGuardValue<bool> IsAnalyzingArgument(_Context._bIsAnalyzingArgumentsInInvocation, true);

            const CTypeBase* ParamsType = FunctionType ? &FunctionType->GetParamsType() : nullptr;
            if (TSPtr<CExpressionBase> NewArgument = AnalyzeExpressionAst(
                Invocation->GetArgument().AsRef(),
                ExprCtx
                  .WithResultIsUsed(ParamsType)
                  .WithAllowNonInvokedReferenceToOverloadedFunction(
                      ExprCtx.ResultContext == ResultIsUsedAsAttribute
                      && IsAccessorFunctionAttributeClass(TryGetFunctionReturnTypeClass(Invocation->GetCallee()))
                  ),
                ArgumentArgs))
            {
                Invocation->SetArgument(Move(NewArgument));
            }
        }

        TSPtr<CExpressionBase> ExtensionArgument = CreateExtensionArgument(*Invocation);
        bool bExplicitExtensionArgument = ExtensionArgument;
        if (!ExtensionArgument)
        {   // No explicit extension argument, try with implicit Self
            ExtensionArgument = PrependImplicitSelfArgument(*Invocation);
        }

        // If the function type is overloaded or generic, resolve the overload or the generic type of the function.
        const bool bSquareBracketInvoke = (Invocation->_CallsiteBracketStyle == CExprInvocation::EBracketingStyle::SquareBrackets);
        if (Invocation->GetCallee()->GetNodeType() == EAstNodeType::Identifier_OverloadedFunction)
        {
            const TSPtr<CExpressionBase>& Argument = Invocation->GetArgument();
            const CTypeBase* ArgumentType = Argument->GetResultType(*_Program);
            CExprIdentifierOverloadedFunction& OverloadedFunctionIdentifier = *Invocation->GetCallee().As<CExprIdentifierOverloadedFunction>();
            const CTypeBase* ExtensionArgumentType = ExtensionArgument ? ExtensionArgument->GetResultType(*_Program) : nullptr;
            FunctionType = ResolveOverloadedCallee(*Invocation, OverloadedFunctionIdentifier, *ArgumentType, ExtensionArgumentType, Move(ExtensionArgument));
            if (!FunctionType)
            {
                if (const CTypeBase* TypeOverload = OverloadedFunctionIdentifier._TypeOverload)
                {   // None of overloaded functions matched, one last chance with the type itself.
                    // Depressingly bad error message if this fails.
                    return AnalyzeInvokeType(*Invocation, Argument, TypeOverload, TypeOverload, ExprCtx);
                }
                else
                {
                    return ReplaceNodeWithError(Invocation);
                }
            }
        }
        else if (!FunctionType)
        {
            FunctionType = &_Program->GetOrCreateFunctionType(
                // Most permissive argument type (top of the lattice).
                _Program->_anyType,
                // Most permissive return type (bottom of the lattice) while
                // also indicating an error occurred.
                *_Program->GetDefaultUnknownType(),
                EffectSets::FunctionDefault.With(EEffect::decides, bSquareBracketInvoke));
        }
        Invocation->SetResolvedCalleeType(FunctionType);
        Invocation->SetResultType(&FunctionType->GetReturnType());

        if (ExprCtx.ResultContext != Cases<ResultIsUsedAsType, ResultIsCalledAsMacro> &&
            FunctionType->GetEffects()[EEffect::diverges] &&
            uLang::AnyOf(_Context._DataMembers, EnclosingScopeIsNotControl))
        {
            // We previously only allowed calling pure functions(which could only be defined as
            // intrinsics) in default initializers to ensure that you can't call something that
            // exposes the CDO trickery we do instead of evaluating the default initializer on
            // class instantiation.
            // 
            // This code was previously working around this restriction by using a do clause and
            // mutation, which is evaluated each time the class is instantiated, and so we can
            // allow it to have side effects.
            // 
            // Now, users can define pure functions, but we can't allow calling them from default
            // initializers: any user-written function could create an instance of the class whose
            // CDO is being initialized. In MaxVerse that has well-defined semantics, and might
            // even not be infinite recursion if the instantiation overrides the default that calls
            // the function. But instead of trying to handle this case gracefully, we just prohibit
            // it by saying that default initializers can only call total functions.
            // 
            // Total functions are pure functions that are guaranteed to terminate in a finite
            // amount of time.The trick is that we let you can write the total effect in Verse code,
            // but only allow it to be used on native functions, so users can't write a total
            // function that instantiates a class and call it from the class's default member
            // initializers.
            // 
            // The reason we don't allow it on non-native code is that checking the total effect is
            // a variation of the halting problem, and so we can only conservatively approximate it
            // with clumsy restrictions like "can only call statically dispatched functions non-
            // recursively, and can only write loops with simple termination conditions".
            // 
            // The net effect of all this is that we can define new native total functions(like
            // log_channel_handle) that can be called directly from member default initializers. We
            // previously could only call functions defined intrinsically from member default
            // initializers.
            AppendGlitch(
                *Invocation,
                EDiagnostic::ErrSemantic_CannotInitDataMemberWithSideEffect,
                CUTF8String("Divergent calls (calls that might not complete) cannot be used to define data-members.", FunctionType->AsCode().AsCString()));

            // @HACK: Where we process data-member value expressions (where 
            //        this is being called from) is before we've wholly built
            //        function signatures -- meaning, we cannot reliably analyze 
            //        using the function's signature.
            return ReplaceNodeWithError(Invocation);
        }

        // Does the invocation agree with if the function can fail? If the invocation is "undefined" 
        // then it's likely an operator invocation, and we'll just conform to the function's signature (so skip this error -- 
        // at least until we have different semantics for invocations using square brackets)
        SEffectSet AllowedEffects = ExprCtx.AllowedEffects;
        if (Invocation->_CallsiteBracketStyle != CExprInvocation::EBracketingStyle::Undefined)
        {
            if (bSquareBracketInvoke && !FunctionType->GetEffects()[EEffect::decides])
            {
                AppendGlitch(
                    *Invocation,
                    EDiagnostic::ErrSemantic_IncompatibleFailure,
                    "This call uses square brackets to call a function that does not have the 'decides' effect. "
                    "Functions that may fail, which is indicated by the 'decides' effect, must be called with square brackets: 'Function[]', "
                    "while functions that always succeed must be called with parentheses: 'Function()'.");
            }
            else if (!bSquareBracketInvoke && FunctionType->GetEffects()[EEffect::decides])
            {
                AppendGlitch(
                    *Invocation,
                    EDiagnostic::ErrSemantic_IncompatibleFailure,
                    "This call uses parentheses to call a function that has the 'decides' effect. "
                    "Functions that may fail, which is indicated by the 'decides' effect, must be called with square brackets, "
                    "while functions that always succeed must be called with parentheses.");
                AllowedEffects |= EffectSets::Decides;
            }
        }

        // If the invocation is the immediate child of a spawn, allow the suspends effect.
        if (ExprCtx.ResultContext == ResultIsSpawned)
        {
            AllowedEffects |= EEffect::suspends;
        }

        RequireEffects(*Invocation, FunctionType->GetEffects(), AllowedEffects, "invocation calls a function that");

        // Type check the invocation's argument subexpressions.

        // If this was an overloaded function which resolved to an extension method, then ExtensionArguments was taken earlier and is now a nullptr.
        // But if it wasn't overloaded than we need to fix the arguments.
        // Also need to handle the case where it wasn't overloaded but used in a context with an implicit Self.
        if (ExtensionArgument)
        {
            if (Invocation->GetCallee()->GetNodeType() == EAstNodeType::Identifier_Function)
            {
                CExprIdentifierFunction* ExprFunction = static_cast<CExprIdentifierFunction*>(Invocation->GetCallee().Get());
                if (ExprFunction->_Function._ExtensionFieldAccessorKind == EExtensionFieldAccessorKind::ExtensionMethod)
                {
                    Invocation->SetArgument(Move(ExtensionArgument));
                    if (bExplicitExtensionArgument)
                    {
                        ExprFunction->SetContext(TSPtr<CExpressionBase>()); // Context is the explicit extension argument, and is now in the arguments
                    }
                }
            }
        }

        {
            /* emit warnings for map lookups that typecheck but are guaranteed to fail
               at runtime (eg. map{0 => 1}["not an int"]): */
            const CMapType* MapType = OriginalCalleeType->GetNormalType().AsNullable<CMapType>();
            auto Args = AsNullable<CExprMakeTuple>(Invocation->GetArgument());
            if (MapType && Args && Args->SubExprNum() == 2)
            {
                TSPtr<CExpressionBase> KeyArg = Args->GetSubExprs()[1];
                const CTypeBase* KeyArgType = KeyArg->GetResultType(*_Program);
                const CTypeBase* MapKeyType = MapType->GetKeyType();
                if (ETypeKind::False == Meet(KeyArgType, MapKeyType)->GetNormalType().GetKind())
                {
                    AppendGlitch(*KeyArg, EDiagnostic::WarnSemantic_ContainerLookupAlwaysFails);
                }
            }
        }

        ConstrainExpressionToType(
            Invocation->GetArgument().AsRef(),
            FunctionType->GetParamsType(),
            EDiagnostic::ErrSemantic_IncompatibleArgument,
            "This function parameter expects", "this argument");

        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    void RequireExpressionCanFail(const CExpressionBase& Expression, const char* Context)
    {
        if (!Expression.CanFail()
            && !SemanticTypeUtils::IsUnknownType(Expression.GetResultType(*_Program)))
        {
            AppendGlitch(
                Expression,
                EDiagnostic::ErrSemantic_ExpectedFallibleExpression,
                CUTF8String("Expected an expression that can fail in %s", Context));
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeLogicalNot(CExprLogicalNot& LogicalNot, const SExprCtx& ExprCtx)
    {
        // If the context doesn't allow failure, produce an error.
        RequireEffects(LogicalNot, EEffect::decides, ExprCtx.AllowedEffects, "logical not operation");

        // Evaluate the operand in a failure context, and a local scope. The local scope is needed since a failure inside not
        // might prevent initialization of definitions, and since not "hides" failure evaluation will continue.
        {
            TSRef<CControlScope> ControlScope = _Context._Scope->CreateNestedControlScope();
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, ControlScope.Get());
            if (TSPtr<CExpressionBase> NewOperand = AnalyzeExpressionAst(LogicalNot.Operand().AsRef(), ExprCtx.WithDecides().WithResultIsIgnored()))
            {
                LogicalNot.SetOperand(Move(NewOperand));
            }
        }

        // Require that the operand might fail.
        RequireExpressionCanFail(*LogicalNot.Operand(), "the operand of 'not'");

        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeBinaryOpLogicalAnd(CExprShortCircuitAnd& AndAst, const SExprCtx& ExprCtx)
    {
        // Need a new scope since  a and b ~> if(a) {b} else {false?}
        // Definition in LHS is visible in RHS, but nothing is visible outside of &&
        {
            TSRef<CControlScope> ControlScope = _Context._Scope->CreateNestedControlScope();
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, ControlScope.Get()); 
            auto NewLhs = AnalyzeExpressionAst(AndAst.Lhs().AsRef(), ExprCtx.WithResultIsIgnored());
            if (NewLhs) { AndAst.SetLhs(Move(NewLhs)); }

            RequireExpressionCanFail(*AndAst.Lhs(), "the left operand of 'and' ");

            auto NewRhs = AnalyzeExpressionAst(AndAst.Rhs().AsRef(), ExprCtx);
            if (NewRhs)
            {
                // Ensure defer is called as a statement rather than an expression - generally within a code block: routine, do, if then/else, for, loop, branch, spawn and not as the last expression
                if (NewRhs->GetNodeType() == EAstNodeType::Flow_Defer)
                {
                    AppendGlitch(
                        *NewRhs,
                        EDiagnostic::ErrSemantic_DeferLocation,
                        "A `defer` will not work as intended within an `and` - place the `defer` before or after this expression and place any conditional within the body of the `defer`.");
                }

                AndAst.SetRhs(Move(NewRhs));
            }
        }
        RequireEffects(AndAst, EEffect::decides, ExprCtx.AllowedEffects, "logical and operation");

        AndAst.SetResultType(AndAst.Rhs().AsRef()->GetResultType(*_Program));
        
        return TSPtr<CExpressionBase>();
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeBinaryOpLogicalOr(CExprShortCircuitOr& OrAst, const SExprCtx& ExprCtx)
    {
        // Need separate scopes for LHS and RHS since a or b ~> if(x:=a) {x} else {b}
        // This is also needed since initialization of definitions in LHS might not be evaluated, SOL-1830
        {
            TSRef<CControlScope> LhsControlScope = _Context._Scope->CreateNestedControlScope();
            TGuardValue<CScope*> LhsCurrentScopeGuard(_Context._Scope, LhsControlScope.Get());
            auto NewLhs = AnalyzeExpressionAst(OrAst.Lhs().AsRef(), ExprCtx.WithDecides().WithResultIsUsed(nullptr));
            if (NewLhs) { OrAst.SetLhs(Move(NewLhs)); }
        }

        RequireExpressionCanFail(*OrAst.Lhs(), "the left operand of 'or'");

        {   // Need a new scope since definitions in this part might not be evaluated, SOL-1830
            TSRef<CControlScope> RhsControlScope = _Context._Scope->CreateNestedControlScope();
            TGuardValue<CScope*> RhsCurrentScopeGuard(_Context._Scope, RhsControlScope.Get()); 
            auto NewRhs = AnalyzeExpressionAst(OrAst.Rhs().AsRef(), ExprCtx);
            if (NewRhs)
            {
                // Ensure defer is called as a statement rather than an expression - generally within a code block: routine, do, if then/else, for, loop, branch, spawn and not as the last expression
                if (NewRhs->GetNodeType() == EAstNodeType::Flow_Defer)
                {
                    AppendGlitch(
                        *NewRhs,
                        EDiagnostic::ErrSemantic_DeferLocation,
                        "A `defer` will not work as intended within an `or` - place the `defer` before or after this expression and place any conditional within the body of the `defer`.");
                }

                OrAst.SetRhs(Move(NewRhs));
            }
        }

        const CTypeBase* LhsType = OrAst.Lhs()->GetResultType(*_Program);
        const CTypeBase* RhsType = OrAst.Rhs()->GetResultType(*_Program);
        const CTypeBase* JoinType = Join(LhsType, RhsType);

        OrAst.SetResultType( JoinType );

        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeComparison(const TSRef<CExprComparison>& AstCompare, const SExprCtx& ExprCtx)
    {
        // Resolve the function name for the comparison operator.
        CSymbol OpFunctionName = _Program->_IntrinsicSymbols.GetComparisonOpName(AstCompare->Op());

        // Analyze the comparison as a call to the appropriate overloaded operator function.
        AstCompare->SetCallee(TSRef<CExprIdentifierUnresolved>::New(OpFunctionName, nullptr, nullptr, true));
        return AnalyzeInvocation(AstCompare, ExprCtx);
    }
     
    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeUnaryArithmetic(const TSRef<CExprUnaryArithmetic>& Arithmetic, const SExprCtx& ExprCtx)
    {
        // Analyze Unary Arithmetic of the form '-expr'
        auto AnalyzeHelper_Negation = [&](const TSRef<CExprUnaryArithmetic>& Arithmetic, const SExprCtx& ExprCtx, bool bIsNegative )->TSPtr<CExpressionBase>
        {
            const EAstNodeType OperandExprType = Arithmetic->Operand()->GetNodeType();
            if (OperandExprType == EAstNodeType::Literal_Number)
            {
                // Fold the negation node into the literal.
                bIsNegative = !bIsNegative;
                if (TSPtr<CExpressionBase> Failure = AnalyzeNumberLiteral(static_cast<CExprNumber&>(*Arithmetic->Operand()), ExprCtx, bIsNegative))
                    return Failure;
                Vst::Node::RemoveMapping(Arithmetic.Get());
                return Arithmetic->TakeOperand();
            }
            else if (OperandExprType == EAstNodeType::Invoke_UnaryArithmetic)
            {
                CExprUnaryArithmetic& NegationOperand = static_cast<CExprUnaryArithmetic&>(*Arithmetic->Operand());
                // We have a sequence of two negations. e.g. '- - a' or '-(-a)'
                // Fold both negations and substitute with 'a'
                ULANG_ASSERTF(NegationOperand.Op() == CExprUnaryArithmetic::EOp::Negate, "Only negation curently supported by Unary Arithmetic");
                Vst::Node::RemoveMapping(Arithmetic.Get());
                Vst::Node::RemoveMapping(&NegationOperand);
                TSRef<CExpressionBase> DoublyNegatedOperand = NegationOperand.Operand();
                auto OperandReplacement = AnalyzeExpressionAst(DoublyNegatedOperand, ExprCtx);
                
                // If the analysis did not have any requests to replace the double negated operand
                // then replace the folded nodes with the DoubleNegatedOperand.
                if (OperandReplacement)
                {
                    return Move(OperandReplacement);
                }
                return NegationOperand.TakeOperand();

            }
            else
            {
                // Analyze the negation as a call to the overloaded negation operator.
                Arithmetic->SetCallee(TSRef<CExprIdentifierUnresolved>::New(_Program->_IntrinsicSymbols._OpNameNegate));
                TSPtr<CExpressionBase> NewInvocation = AnalyzeInvocation(Arithmetic, ExprCtx);
                if (!NewInvocation)
                {
                    NewInvocation = Arithmetic;
                }

                if (NewInvocation->GetNodeType() != EAstNodeType::Invoke_UnaryArithmetic)
                {
                    return NewInvocation;
                }

                // If we can prove the type has been constrained we know that the range of possible values is just [-OldMax, -OldMin]
                const CNormalType& OperandType = NewInvocation.As<CExprUnaryArithmetic>()->Operand()->GetResultType(*_Program)->GetNormalType();
                // This should work for any int but we don't have a way to negate the INT64_MIN right now.
                if (const CIntType* OperandIntType = OperandType.AsNullable<CIntType>(); OperandIntType && OperandIntType->GetMin().GetValue() != INT64_MIN)
                {
					const CIntType& RefinedResultType = _Program->GetOrCreateConstrainedIntType(-1 * OperandIntType->GetMax().GetValue(), -1 * OperandIntType->GetMin().GetValue());
                    NewInvocation->RefineResultType(&RefinedResultType);
                }
                if (const CFloatType* OperandFloatType = OperandType.AsNullable<CFloatType>(); OperandFloatType && !OperandFloatType->IsIntrinsicFloatType())
                {
                    const CFloatType& RefinedResultType = _Program->GetOrCreateConstrainedFloatType(-1 * OperandFloatType->GetMax(), -1 * OperandFloatType->GetMin());
                    NewInvocation->RefineResultType(&RefinedResultType);
                }

                return NewInvocation;
            }
        };

        // The only UnaryArithmetic is currently negation; i.e. -a
        return AnalyzeHelper_Negation(Arithmetic, ExprCtx, false);
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeBinaryArithmetic(const TSRef<CExprBinaryArithmetic>& Arithmetic, const SExprCtx& ExprCtx)
    {
        // Resolve the function name for the arithmetic operator.
        CSymbol OpFunctionName = _Program->_IntrinsicSymbols.GetArithmeticOpName(Arithmetic->Op());

        // Analyze the arithmetic as a call to the appropriate overloaded operator function.
        Arithmetic->SetCallee(TSRef<CExprIdentifierUnresolved>::New(OpFunctionName));
        return AnalyzeInvocation(Arithmetic, ExprCtx);
    }

    const CTupleType* GetRequiredTupleType(const CTypeBase& RequiredType, int32_t NumElements)
    {
        CTupleType::ElementArray NegativeElementTypes;
        NegativeElementTypes.Reserve(NumElements);
        CTupleType::ElementArray PositiveElementTypes;
        PositiveElementTypes.Reserve(NumElements);
        for (int32_t I = 0; I != NumElements; ++I)
        {
            const CFlowType& NegativeType = _Program->CreateNegativeFlowType();
            const CFlowType& PositiveType = _Program->CreatePositiveFlowType();
            NegativeType.AddFlowEdge(&PositiveType);
            PositiveType.AddFlowEdge(&NegativeType);
            NegativeElementTypes.Add(&NegativeType);
            PositiveElementTypes.Add(&PositiveType);
        }
        const CTupleType& PositiveType = _Program->GetOrCreateTupleType(Move(PositiveElementTypes));
        if (!Constrain(&PositiveType, &RequiredType))
        {
            return nullptr;
        }
        return &_Program->GetOrCreateTupleType(Move(NegativeElementTypes));
    }

    const CTupleType* GetRequiredTupleType(const CTypeBase& RequiredType, const TSPtrArray<CExpressionBase>& Elements, int32_t FirstNamedIndex)
    {
        int32_t NumElements = Elements.Num();
        CTupleType::ElementArray NegativeElementTypes;
        NegativeElementTypes.Reserve(NumElements);
        CTupleType::ElementArray PositiveElementTypes;
        PositiveElementTypes.Reserve(NumElements);
        for (int32_t I = 0; I != FirstNamedIndex; ++I)
        {
            const CFlowType& NegativeType = _Program->CreateNegativeFlowType();
            const CFlowType& PositiveType = _Program->CreatePositiveFlowType();
            NegativeType.AddFlowEdge(&PositiveType);
            PositiveType.AddFlowEdge(&NegativeType);
            NegativeElementTypes.Add(&NegativeType);
            PositiveElementTypes.Add(&PositiveType);
        }
        for (int32_t I = FirstNamedIndex; I != NumElements; ++I)
        {
            const TSPtr<CExpressionBase>& Element = Elements[I];
            ULANG_ASSERTF(Element->GetNodeType() == EAstNodeType::Invoke_MakeNamed, "Unexpected unnamed element");
            CSymbol Name = static_cast<const CExprMakeNamed&>(*Element).GetName();
            const CFlowType& NegativeType = _Program->CreateNegativeFlowType();
            const CFlowType& PositiveType = _Program->CreatePositiveFlowType();
            NegativeType.AddFlowEdge(&PositiveType);
            PositiveType.AddFlowEdge(&NegativeType);
            NegativeElementTypes.Add(&_Program->GetOrCreateNamedType(Name, &NegativeType, false));
            PositiveElementTypes.Add(&_Program->GetOrCreateNamedType(Name, &PositiveType, false));
        }
        const CTupleType& PositiveType = _Program->GetOrCreateTupleType(
            Move(PositiveElementTypes),
            FirstNamedIndex);
        if (!Constrain(&PositiveType, &RequiredType))
        {
            return nullptr;
        }
        return &_Program->GetOrCreateTupleType(
            Move(NegativeElementTypes),
            FirstNamedIndex);
    }

    int32_t GetFirstNamedIndex(const TSPtrArray<CExpressionBase>& Elements)
    {
        auto First = Elements.begin();
        auto Last = Elements.end();
        auto I = First;
        for (; I != Last; ++I)
        {
            if ((*I)->GetNodeType() == EAstNodeType::Invoke_MakeNamed)
            {
                break;
            }
        }
        int32_t FirstNamedIndex = static_cast<int32_t>(I - First);
        for (; I != Last; ++I)
        {
            if ((*I)->GetNodeType() == EAstNodeType::Invoke_MakeNamed)
            {
                CSymbol Name = static_cast<const CExprMakeNamed&>(**I).GetName();
                auto J = uLang::FindIf(I + 1, Last, [=](const TSPtr<CExpressionBase>& Element)
                {
                    if (Element->GetNodeType() == EAstNodeType::Invoke_MakeNamed)
                    {
                        return Element.As<CExprMakeNamed>()->GetName() == Name;
                    }
                    return false;
                });
                if (J != Last)
                {
                    AppendGlitch(
                        *Elements[static_cast<int32_t>(J - First)],
                        EDiagnostic::ErrSemantic_DuplicateNamedValueName,
                        CUTF8String("Duplicate named value name %s.", Name.AsCString()));
                }
            }
            else
            {
                FirstNamedIndex = static_cast<int32_t>(I - First) + 1;
                AppendGlitch(
                    **I,
                    EDiagnostic::ErrSemantic_NamedMustFollowNamed,
                    CUTF8String(
                        "Tuple element #%d must be named. Once an earlier element is named (prefixed with `?`) any elements that follow must also be named.",
                        FirstNamedIndex));
            }
        }
        return FirstNamedIndex;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeTupleValue(
        CExprMakeTuple& TupleExpr,
        const SExprCtx& ExprCtx,
        const SExprArgs& ExprArgs)
    {
        TSPtrArray<CExpressionBase>& Elements = TupleExpr.GetSubExprs();
        int32_t NumElements = Elements.Num();

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Specific tuple type already desired?
        const CTupleType::ElementArray* RequiredTypes     = nullptr;
        int32_t                         RequiredTypeCount = 0;

        int32_t FirstNamedIndex = GetFirstNamedIndex(Elements);

        if (ExprCtx.RequiredType)
        {
            if (const CTupleType* RequiredTupleType = GetRequiredTupleType(*ExprCtx.RequiredType, Elements, FirstNamedIndex))
            {
                RequiredTypes = &RequiredTupleType->GetElements();
                RequiredTypeCount = RequiredTypes->Num();
            }
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Track types encountered - check against desired or note inferred tuple type
        CTupleType::ElementArray ElementTypes;
        ElementTypes.Reserve(NumElements);

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Iterate through elements and replace with any more fully analyzed versions
        for (int32_t Idx = 0; Idx < NumElements; Idx++)
        {
            // Get desired element type - though don't go past end if greater number of supplied elements
            const CTypeBase* RequiredType = (RequiredTypeCount > Idx) ? (*RequiredTypes)[Idx] : nullptr;
            if ((ExprArgs.AnalysisContext != EAnalysisContext::FirstTupleElementAlreadyAnalyzed) || Idx != 0)
            {
                if (TSPtr<CExpressionBase> NewSubExpr = AnalyzeExpressionAst(Elements[Idx].AsRef(), ExprCtx.WithResultIsUsed(RequiredType)))
                {
                    TupleExpr.ReplaceSubExpr(Move(NewSubExpr), Idx);
                }
            }

            const CTypeBase* ElementType = Elements[Idx]->GetResultType(*_Program);
            ElementTypes.Add(ElementType);
        }

        // Set actual type
        CTupleType* TupleType = &_Program->GetOrCreateTupleType(Move(ElementTypes), FirstNamedIndex);
        TupleExpr.SetResultType(TupleType);

        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeMakeRange(CExprMakeRange& MakeRange, const SExprCtx& ExprCtx)
    {
        if (ExprCtx.ResultContext != ResultIsIterated)
        {
            AppendGlitch(MakeRange, EDiagnostic::ErrSemantic_Unsupported, "Ranges only supported as iterated expression of `for`, `sync`, `rush`, or `race`.");
        }
        if (auto NewLhs = AnalyzeExpressionAst(MakeRange._Lhs, ExprCtx.WithResultIsUsed(_Program->_intType))) { MakeRange.SetLhs(Move(NewLhs.AsRef())); }
        if (auto NewRhs = AnalyzeExpressionAst(MakeRange._Rhs, ExprCtx.WithResultIsUsed(_Program->_intType))) { MakeRange.SetRhs(Move(NewRhs.AsRef())); }

        // Ensure that the range bounds are both integers.
        const CTypeBase* ItemType = Join(
            MakeRange._Lhs->GetResultType(*_Program),
            MakeRange._Rhs->GetResultType(*_Program));
        if (!ItemType || !Constrain(ItemType, _Program->_intType))
        {
            AppendGlitch(MakeRange, EDiagnostic::ErrSemantic_Unsupported, "Non-integer ranges are not supported");
        }

        MakeRange.SetResultType(&_Program->_rangeType);

        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeIf(CExprIf& If, const SExprCtx& ExprCtx)
    {
        if (_CurrentTaskPhase < Deferred_NonFunctionExpressions)
        {
            AppendGlitch(If, EDiagnostic::ErrSemantic_Unimplemented, "Support for 'if' in this context is not yet implemented.");
            return ReplaceMapping(If, TSRef<CExprError>::New());
        }

        // Analyze the condition subexpression.
        AnalyzeCodeBlock(*If.GetCondition(), ExprCtx.WithDecides().WithResultIsIgnored());

        // Verify that condition subexpression can fail.
        RequireExpressionCanFail(*If.GetCondition(), "the 'if' condition clause");

        // Analyze the then clause subexpression.
        const bool bHasBothClauses = If.GetThenClause() && If.GetElseClause();
        const SExprCtx ClauseExprCtx = bHasBothClauses
            ? ExprCtx
            : ExprCtx.AllowReturnFromLeadingStatementsAsSubexpressionOfReturn().WithResultIsIgnored();
        if (If.GetThenClause())
        {
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, If.GetCondition()->_AssociatedScope);

            if (TSPtr<CExpressionBase> NewThenClause = AnalyzeExpressionAst(If.GetThenClause().AsRef(), ClauseExprCtx))
            {
                If.SetThenClause(Move(NewThenClause.AsRef()));
            }
        }

        // Analyze the option else clause subexpression.
        if (If.GetElseClause())
        {
            if (TSPtr<CExpressionBase> NewElseClause = AnalyzeExpressionAst(If.GetElseClause().AsRef(), ClauseExprCtx))
            {
                If.SetElseClause(Move(NewElseClause.AsRef()));
            }
        }

        // If either the then or else clause is not present, the result of the CExprIf is of type void.
        const CTypeBase* ResultType = &_Program->_voidType;
        if (bHasBothClauses)
        {
            // Calculate the result type as the join of the result type of the then and else clauses.
            const CTypeBase* ThenResultType = If.GetThenClause()->GetResultType(*_Program);
            const CTypeBase* ElseResultType = If.GetElseClause()->GetResultType(*_Program);
            ResultType = Join(ThenResultType, ElseResultType);
        }

        If.SetResultType(ResultType);

        return nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    // Iterated collection form - e.g. sync(itemName:collection) {expr1 expr2}
    TSRef<CExpressionBase> AnalyzeAnyIterated(const TSRef<CExprMacroCall>& MacroCallAst, const TSRef<CExprIteration>& Iteration, const char* MacroName, const CSymbol& ScopeName, const SExprCtx& ExprCtx)
    {
        ULANG_ASSERTF(MacroCallAst->Clauses().Num() == 2, "Expected caller to validate macro form");

        if (_CurrentTaskPhase < Deferred_NonFunctionExpressions)
        {
            AppendGlitch(
                *MacroCallAst,
                EDiagnostic::ErrSemantic_Unimplemented,
                CUTF8String("Support for %s in this context is not yet implemented.", MacroName));
            return ReplaceNodeWithError(MacroCallAst);
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Analyze the of clause.
        CExprMacroCall::CClause& OfClause = MacroCallAst->Clauses()[0]; //-V758

        if (OfClause.Exprs().Num() == 0)
        {
            AppendGlitch(
                *MacroCallAst,
                EDiagnostic::ErrSemantic_MalformedMacro,
                CUTF8String("The %s cannot have zero arguments - it expects iterator mapping such as %s(Value:Iterable).", MacroName, ScopeName.AsCString()));
        }

        Iteration->_AssociatedScope = _Context._Scope->CreateNestedControlScope(ScopeName);
        TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Iteration->_AssociatedScope);

        for (int32_t Index = 0, Last = OfClause.Exprs().Num(); Index < Last; ++Index)
        {
            Iteration->AddFilter(AnalyzeFilterExpressionAst(Iteration, ScopeName, Move(OfClause.Exprs()[Index]), ExprCtx.WithDecides().WithResultIsIgnored(), Index == 0));
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Analyze body

        // Propagate the required type into the body subexpression.
        const CTypeBase* RequiredBodyType = nullptr;
        if (Iteration->GetNodeType() == EAstNodeType::Concurrent_SyncIterated || Iteration->GetNodeType() == EAstNodeType::Flow_Iteration)
        {
            if (ExprCtx.RequiredType)
            {
                if (const CArrayType* ArrayType = ExprCtx.RequiredType->GetNormalType().AsNullable<CArrayType>())
                {
                    RequiredBodyType = ArrayType->GetElementType();
                }
            }
        }
        else
        {
            RequiredBodyType = ExprCtx.RequiredType;
        }

        // Analyze the body clause as a code block with the iteration variable.
        CExprMacroCall::CClause& BodyClause = MacroCallAst->Clauses()[1]; //-V758
        const SExprCtx BodyExprContext = ExprCtx.ResultContext == ResultIsIgnored ? ExprCtx.WithResultIsIgnored() : ExprCtx.WithResultIsUsed(RequiredBodyType);
        Iteration->SetBody(AnalyzeMacroClauseAsCodeBlock(BodyClause, MacroCallAst->GetMappedVstNode(), BodyExprContext));

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Resolve result type - not always resulting in an array
        ULANG_ASSERTF(Iteration->_Body, "Expected non-null body");
        const CTypeBase* ElemType = Iteration->_Body->GetResultType(*_Program);
        ULANG_ASSERTF(ElemType, "Iteration body result type is null");
        const CTypeBase* ResultType = 
            (Iteration->GetNodeType() == EAstNodeType::Concurrent_SyncIterated || Iteration->GetNodeType() == EAstNodeType::Flow_Iteration)
            // If 'for' or iterating `sync` - then the result is an array of element type
            ? &_Program->GetOrCreateArrayType(ElemType)
            // Other iterating forms result in one task
            : ElemType;

        Iteration->SetResultType(ResultType);
    
        return ReplaceMapping(*MacroCallAst, TSRef<CExpressionBase>(Iteration));
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_External(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        // Verify that macro is of the form 'm1{}'
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        if (_Context._Scope->GetPackage()->_Role != ExternalPackageRole
            && !ExprCtx.bAllowExternalMacroCallInNonExternalRole)
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_ExternalNotAllowed);
        }

        return ReplaceMapping(MacroCallAst, TSRef<CExprExternal>::New(*_Program));
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Case(const TSRef<CExprMacroCall>& MacroCallAst, const SExprCtx& ExprCtx)
    {
        if (_CurrentTaskPhase < Deferred_NonFunctionExpressions)
        {
            AppendGlitch(*MacroCallAst, EDiagnostic::ErrSemantic_Unimplemented, "Support for 'case' in this context is not yet implemented.");
            return ReplaceMapping(*MacroCallAst, TSRef<CExprError>::New());
        }

        // Verify that macro is of the form 'case(){}' or 'case() in {}'
        if (!ValidateMacroForm<ESimpleMacroForm::m2, EMacroClauseTag::None>(*MacroCallAst))
        {
            return ReplaceNodeWithError(MacroCallAst);
        }
        else if (!MacroCallAst->Clauses()[1].Exprs().Num())
        {
            AppendGlitch(*MacroCallAst, EDiagnostic::ErrSemantic_NoCasePatterns);
            return ReplaceNodeWithError(MacroCallAst);
        }
        else
        {
            // This transforms:
            //
            //     case (e):
            //         a => x
            //         b => y
            //         _ => z
            //
            // Into:
            //
            //     tmp = e
            //     if (tmp = a):
            //         x
            //     else:
            //         if (tmp = b):
            //             y
            //         else:
            //             z
            //
            // Is this optimal? Almost:
            //
            // - It's trivial to detect the chain-of-ifs idiom in any IR, so if we had the ability to codegen this
            //   switch in any better way, we could add that without changing this code. As a bonus, that optimization
            //   would then also trigger for user-written chains-of-ifs. Hence, the ideal canonical form for
            //   switch/case is chain-of-ifs.
            // - Right now anyone (like the IRGenerator) that encounters a chain-of-ifs will descend down the stack to
            //   a height proportional to the number of cases. That's avoidable by chaining the else case handling
            //   non-recursively, but we don't have to worry about that now.

            CExprMacroCall::CClause& ValueClause = MacroCallAst->Clauses()[0]; //-V758
            if (ValueClause.Exprs().Num() == 0)
            {
                AppendGlitch(*MacroCallAst, EDiagnostic::ErrSemantic_EmptyValueClause);
                return ReplaceNodeWithError(MacroCallAst);
            }
            CExprMacroCall::CClause& PatternsClause = MacroCallAst->Clauses()[1]; //-V758
            TSRef<CExprCodeBlock> ResultAst = TSRef<CExprCodeBlock>::New(2);
            
            ResultAst->_AssociatedScope = _Context._Scope->CreateNestedControlScope();
            TGuardValue<CScope*> OuterScopeGuard(_Context._Scope, ResultAst->_AssociatedScope);
            
            TSRef<CDataDefinition> ValueVar = _Context._Scope->CreateDataDefinition(CSymbol());
            TSRef<CExprCodeBlock> ValueValue = AnalyzeMacroClauseAsCodeBlock(ValueClause, MacroCallAst->GetMappedVstNode(), ExprCtx);
            const CTypeBase* ValueType = ValueValue->GetResultType(*_Program);
            ValueVar->SetType(ValueType);
            ResultAst->AppendSubExpr(TSRef<CExprDataDefinition>::New(ValueVar, nullptr, nullptr, Move(ValueValue)));
            TSPtr<CExprIf> OwnerOfDanglingElse;
            
            TArray<const CEnumerator*> RemainingEnumerators;
            const CNormalType& ValueNormalType = ValueType->GetNormalType();
            if (ValueNormalType.IsA<CEnumeration>())
            {
                for (const CEnumerator* Enumerator : ValueNormalType.AsChecked<CEnumeration>().GetDefinitionsOfKind<CEnumerator>())
                {
                    RemainingEnumerators.Push(Enumerator);
                }
            }

            // Collect the least-ubber-bound (LUB) type of all of the pattern ranges (i.e. the code that executes for the
            // case patterns) first. This is important. We want to produce a single LUB type that all of the Ifs use, so
            // that the else case of an If that has an If doesn't have to do its own type conversion.
            const CTypeBase* LubType = nullptr;
            for (int32_t Index = 0; Index < PatternsClause.Exprs().Num(); ++Index)
            {
                TSRef<CExpressionBase> Expression = PatternsClause.Exprs()[Index];
                if (Expression->GetNodeType() != EAstNodeType::Literal_Function)
                {
                    AppendGlitch(*Expression, EDiagnostic::ErrSemantic_BadCasePattern);
                }
                else
                {
                    TSRef<CExprFunctionLiteral> Callback = Expression.As<CExprFunctionLiteral>();
                    TSRef<CExpressionBase> Range = Callback->Range();
                    if (TSPtr<CExpressionBase> NewRange = AnalyzeExpressionAst(Range, ExprCtx))
                    {
                        Range = Move(NewRange).AsRef();
                        Callback->SetRange(TSRef<CExpressionBase>(Range));
                    }
                    if (LubType)
                    {
                        LubType = Join(LubType, Range->GetResultType(*_Program));
                    }
                    else
                    {
                        LubType = Range->GetResultType(*_Program);
                    }
                }
            }
            
            auto FillDanglingElse = [&OwnerOfDanglingElse, &ResultAst] (TSPtr<CExpressionBase>&& Expr)
            {
                if (OwnerOfDanglingElse)
                {
                    OwnerOfDanglingElse->SetElseClause(Move(Expr));
                }
                else
                {
                    ResultAst->AppendSubExpr(Move(Expr));
                }
            };

            for (int32_t Index = 0; Index < PatternsClause.Exprs().Num(); ++Index)
            {
                TSRef<CExpressionBase> Expression = PatternsClause.Exprs()[Index];
                if (Expression->GetNodeType() != EAstNodeType::Literal_Function)
                {
                    return ReplaceNodeWithError(MacroCallAst);
                }

                TSRef<CExprFunctionLiteral> Callback = Expression.As<CExprFunctionLiteral>();
                TSPtr<CExpressionBase> PatternRange = Callback->Range();

                if (Callback->Domain()->GetNodeType() == EAstNodeType::Identifier_Unresolved)
                {
                    TSRef<CExprIdentifierUnresolved> Identifier = Callback->Domain().As<CExprIdentifierUnresolved>();
                    if (Identifier->_Symbol == _Program->_IntrinsicSymbols._Wildcard)
                    {
                        if (Identifier->Qualifier())
                        {
                            AppendGlitch(*PatternsClause.Exprs()[Index], EDiagnostic::ErrSemantic_UnexpectedQualifier,
                                CUTF8String("You do not need to explicitly qualify the built-in wildcard (`%s`) identifier since there isn't any disambiguation.", _Program->_IntrinsicSymbols._Wildcard.AsCString()));
                        }
                        // i.e. `A._`
                        else if (Identifier->Context())
                        {
                            AppendGlitch(*PatternsClause.Exprs()[Index], EDiagnostic::ErrSemantic_BadCasePattern, CUTF8String("Wildcard (`%s`) identifiers cannot have a prefix.", _Program->_IntrinsicSymbols._Wildcard.AsCString()));
                        }
                        else
                        {
                            if (Index != PatternsClause.Exprs().Num() - 1)
                            {
                                AppendGlitch(*PatternsClause.Exprs()[Index + 1], EDiagnostic::ErrSemantic_UnreachableCases);
                            }
                            FillDanglingElse(Move(PatternRange));
                            break;
                        }
                    }
                }

                TSRef<CExpressionBase> CaseDomain = Callback->Domain();
                if (TSPtr<CExpressionBase> NewCaseDomain = AnalyzeExpressionAst(CaseDomain, ExprCtx))
                {
                    CaseDomain = Move(NewCaseDomain).AsRef();
                }

                const CTypeBase* CaseType = CaseDomain->GetResultType(*_Program);
                // We're currently opting in only certain literals. In the future we can dramatically relax this
                // filter.
                if (CaseDomain->GetNodeType() != EAstNodeType::Literal_Logic  &&
                    CaseDomain->GetNodeType() != EAstNodeType::Literal_Number &&
                    CaseDomain->GetNodeType() != EAstNodeType::Literal_Char   &&
                    CaseDomain->GetNodeType() != EAstNodeType::Literal_String &&
                    CaseDomain->GetNodeType() != EAstNodeType::Literal_Enum)
                {
                    AppendGlitch(*Callback, EDiagnostic::ErrSemantic_InvalidCasePattern, CUTF8String("The case pattern type: %s was not valid. Currently, only logic/number/char/string/enum literals are supported.", CaseType->AsCode().AsCString()));
                }
                // Even if the case domain is an unresolved identifier, we still want to process its sub-expressions.
                if (!SemanticTypeUtils::IsUnknownType(ValueType) &&
                    !SemanticTypeUtils::IsUnknownType(CaseType) &&  // We check here as well so that we don't double up on adding this glitch.
                    !IsSubtype(CaseType, ValueType))
                {
                    AppendGlitch(
                        *Callback, EDiagnostic::ErrSemantic_CaseTypeMismatch,
                        CUTF8String("The case condition value has type `%s`, but this case has an incompatible type `%s`.", ValueType->AsCode().AsCString(), CaseType->AsCode().AsCString()));
                    // NOTE: (yiliang.siew) We specifically use the `ResultAst` node here, because the earlier call to `AnalyzeMacroClauseAsCodeBlock`
                    // indirectly enqueues a deferred task that contains references to the AST nodes that would otherwise be de-allocated by the time
                    // that enqueued task runs. We append the `MacroCallAst` as well since there are also other AST nodes in that tree
                    // that need to have references kept.
                    ResultAst->AppendSubExpr(MacroCallAst);
                    return ReplaceNodeWithError(ResultAst);
                }

                // Here we explicitly opt-in types we're fine with casing on for now. This _could_ just be
                // an overload resolution on the = operator (and should be changed to that if the list gets
                // much longer).
                CFunction* EqOp = _Program->_ComparableEqualOp;
                CFunction* NeqOp = _Program->_ComparableNotEqualOp;
                const CFunctionType* FunctionType = nullptr;
                const CNormalType& CaseNormalType = CaseType->GetNormalType();
                if (CaseNormalType.IsA<CLogicType>() ||
                    CaseNormalType.IsA<CIntType>() ||
                    CaseNormalType.IsA<CChar8Type>() ||
                    CaseNormalType.IsA<CChar32Type>() ||
                    SemanticTypeUtils::IsStringType(CaseNormalType))
                {
                    AssertConstrain(CaseType, &CaseNormalType);
                }
                else if (CaseNormalType.IsA<CEnumeration>())
                {
                    AssertConstrain(CaseType, &CaseNormalType);
                    CTupleType& ArgumentType = _Program->GetOrCreateTupleType({CaseType, CaseType});
                    FunctionType = &_Program->GetOrCreateFunctionType(
                        ArgumentType,
                        *CaseType,
                        EqOp->_Signature.GetFunctionType()->GetEffects());
                    ULANG_VERIFYF(CaseDomain.As<CExprEnumLiteral>()->_Enumerator, "Enumeration literal should have an enumerator");
                    
                    if (!RemainingEnumerators.RemoveSingle(CaseDomain.As<CExprEnumLiteral>()->_Enumerator))
                    {
                        AppendGlitch(*Callback, EDiagnostic::ErrSemantic_UnreachableCases, CUTF8String("Duplicate (and unreachable) enum case"));
                    }

                    if (!RemainingEnumerators.Num())
                    {
                        FillDanglingElse(Move(PatternRange));
                        
                        if (Index != PatternsClause.Exprs().Num() - 1)
                        {
                            AppendGlitch(*PatternsClause.Exprs()[Index + 1], EDiagnostic::ErrSemantic_UnreachableCases);
                            return ReplaceNodeWithError(MacroCallAst);
                        }
                        
                        break;
                    }
                }
                else if (CaseNormalType.IsA<CFloatType>())
                {
                    AppendGlitch(*Callback, EDiagnostic::ErrSemantic_InvalidCasePattern, CUTF8String("Floating-point literals are not currently supported in case patterns.", CaseType->AsCode().AsCString()));
                }
                
                // Given a final case pattern like:
                //
                //     42 => foo
                //
                // we emit:
                //
                //     if (value <> 42):
                //         false?
                //     foo
                //
                // instead of the normal:
                //
                //     if (value = 42):
                //         foo
                //     else
                //         ...
                //     end
                //
                // This cleverly avoids having to give a type to the bottom value returned by `false?`. It also avoids having
                // to teach reachability analysis about `false?`.
                bool bIsLast = Index == PatternsClause.Exprs().Num() - 1;

                if (!FunctionType)
                {
                    FunctionType = EqOp->_Signature.GetFunctionType();

                    // string literals use the array ops which are generic, so resolve the signature here
                    FunctionType = Instantiate(*EqOp)._Type;
                }
                
                CFunctionType::ParamTypes ParamTypes = FunctionType->GetParamTypes();
                ULANG_VERIFYF(ParamTypes.Num() == 2, "EqOp should take two parameters");
                const CTypeBase* ExpectedType = ParamTypes[0];
                
                TSRef<CExprCodeBlock> CaseConditionBlock = TSRef<CExprCodeBlock>::New(1);
                CaseConditionBlock->_AssociatedScope = ResultAst->_AssociatedScope->CreateNestedControlScope();
                TGuardValue<CScope*> AssociatedScopeGuard(_Context._Scope, CaseConditionBlock->_AssociatedScope);
                TSRef<CExpressionBase> ValueUse = TSRef<CExprIdentifierData>::New(*_Program, *ValueVar);
                if (TSPtr<CExpressionBase> NewValueUse = ApplyTypeToExpression(
                        *ExpectedType, ValueUse, EDiagnostic::ErrSemantic_CaseTypeMismatch,
                        "This case expects", "the case value"))
                {
                    ValueUse = Move(NewValueUse).AsRef();
                }
                if (TSPtr<CExpressionBase> NewCaseDomain = ApplyTypeToExpression(
                        *ExpectedType, CaseDomain, EDiagnostic::ErrSemantic_CaseTypeMismatch,
                        "This case expects", "the case pattern value"))
                {
                    CaseDomain = Move(NewCaseDomain).AsRef();
                }
                CTupleType& ArgumentType = _Program->GetOrCreateTupleType({
                    ValueUse->GetResultType(*_Program),
                    CaseDomain->GetResultType(*_Program)});
                TSRef<CExprMakeTuple> Argument = TSRef<CExprMakeTuple>::New(
                    Move(ValueUse),
                    Move(CaseDomain));
                Argument->SetResultType(&ArgumentType);
                TSRef<CExprComparison> CaseComparison = TSRef<CExprComparison>::New(
                    bIsLast ? CExprComparison::EOp::noteq : CExprComparison::EOp::eq,
                    Move(Argument));
                const CFunction& CaseComparisionFunction = *(bIsLast ? NeqOp : EqOp);
                CaseComparison->SetCallee(TSRef<CExprIdentifierFunction>::New(
                    CaseComparisionFunction,
                    CaseComparisionFunction._Signature.GetFunctionType()));
                CaseComparison->SetResolvedCalleeType(FunctionType);
                CaseComparison->SetResultType(&FunctionType->GetReturnType());
                CaseConditionBlock->AppendSubExpr(Move(CaseComparison));
                if (bIsLast)
                {
                    TSRef<CExprCodeBlock> CaseBlock = TSRef<CExprCodeBlock>::New(1);

                    CaseBlock->_AssociatedScope = _Context._Scope->CreateNestedControlScope();
                    TGuardValue<CScope*> LastCaseScopeGuard(_Context._Scope, CaseBlock->_AssociatedScope);

                    // We could let QueryValue to this check for us but then we'd have to map it to a VST node.
                    if (!ExprCtx.AllowedEffects[EEffect::decides])
                    {
                        AppendGlitch(
                            *MacroCallAst, EDiagnostic::ErrSemantic_EffectNotAllowed,
                            CUTF8String("Case may fail due to lack of default case, but failure is not allowed here."));
                    }
                    else
                    {
                        TSRef<CExpressionBase> QueryValue = TSRef<CExprQueryValue>::New(TSRef<CExprLogic>::New(*_Program, false));
                        QueryValue->SetNonReciprocalMappedVstNode(Expression->GetMappedVstNode());
                        if (TSPtr<CExpressionBase> NewQueryValue = AnalyzeExpressionAst(QueryValue, ExprCtx))
                        {
                            QueryValue = Move(NewQueryValue).AsRef();
                        }
                        TSRef<CExprIf> CaseIf = TSRef<CExprIf>::New(Move(CaseConditionBlock), Move(QueryValue));
                        CaseIf->SetResultType(&_Program->_voidType);
                        CaseBlock->AppendSubExpr(CaseIf);
                    }
                    
                    CaseBlock->AppendSubExpr(Move(PatternRange));
                    
                    FillDanglingElse(ReplaceMapping(*Callback, Move(CaseBlock)));
                }
                else
                {
                    TSRef<CExprIf> CaseIf = TSRef<CExprIf>::New(Move(CaseConditionBlock), Move(PatternRange));
                    CaseIf->SetResultType(LubType);
                    TSRef<CExprIf> CaseIfCopy = CaseIf;
                    FillDanglingElse(ReplaceMapping(*Callback, Move(CaseIf)));
                    OwnerOfDanglingElse = Move(CaseIfCopy);
                }
            }
            return ReplaceMapping(*MacroCallAst, Move(ResultAst));
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_For(const TSRef<CExprMacroCall>& MacroCallAst, const SExprCtx& ExprCtx)
    {
        // Verify that macro is of the form 'for(){}' or 'for() do {}'
        if (!ValidateMacroForm<ESimpleMacroForm::m2, EMacroClauseTag::Do|EMacroClauseTag::None>(*MacroCallAst))
        {
            return ReplaceMapping(*MacroCallAst, TSRef<CExprError>::New());
        }
        else
        {
            TSRef<CExprIteration> ResultAst = TSRef<CExprIteration>::New();
            TGuardValue<const CExpressionBase*> LoopGuard(_Context._Loop, ResultAst.Get());
            return AnalyzeAnyIterated(MacroCallAst, ResultAst, "'for' macro", _ForClauseScopeName, ExprCtx);
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeFilterExpressionAst(CExprIteration* Iteration, const CSymbol& ScopeName, TSRef<CExpressionBase>&& AstNode, const SExprCtx& ExprCtx, bool bExpectGenerator)
    {
        if (AstNode->GetNodeType() == EAstNodeType::Definition)
        {
            CExprDefinition& DefinitionAst = *AstNode.As<CExprDefinition>();

            if (!DefinitionAst.Element())
            {
                AppendExpectedDefinitionError(DefinitionAst);
                return ReplaceMapping(DefinitionAst, TSRef<CExprError>::New());
            }

            // If the definition node is in the form x^ :??= ??, then it's something we don't want in an iteration.
            if (DefinitionAst.Element()->GetNodeType() == EAstNodeType::Invoke_PointerToReference)
            {
                AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_IncompatibleArgument, CUTF8String("No mutable variables inside %s", ScopeName.AsCString()));
                return ReplaceMapping(DefinitionAst, TSRef<CExprError>::New());
            }

            // Analyze the RHS of the definition (either :<expr> or :=<expr>).
            const CTypeBase* IterationKeyType = nullptr;
            const CTypeBase* IterationValueType = _Program->GetDefaultUnknownType();
            if (DefinitionAst.Value())
            {
                // We have a value, special case for range since type should be an item type in that case.
                if (TSPtr<CExpressionBase> NewRhsAst = AnalyzeExpressionAst(DefinitionAst.Value().AsRef(), ExprCtx.WithDecides().WithResultIsIterated()))
                {
                    DefinitionAst.SetValue(Move(NewRhsAst.AsRef()));
                }

                const CTypeBase* ValueType = DefinitionAst.Value()->GetResultType(*_Program);
                if (!ValueType)
                {
                    AppendGlitch(FindMappedVstNode(DefinitionAst), EDiagnostic::ErrSemantic_ExpectedIterationIterable);
                }
                else if (ValueType->GetNormalType().IsA<CRangeType>())
                {
                    IterationValueType = _Program->_intType;
                }
                else
                {
                    if (bExpectGenerator)
                    {
                        AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_ExpectIterable, CUTF8String("First argument to %s must be a generator, X:=range, X:array, or X:map", ScopeName.AsCString()));
                    }
                    IterationValueType = ValueType;
                }

                if (IterationValueType && DefinitionAst.ValueDomain())
                {
                    // We have a legal range and ValueDomain is set, check that they agree on type.
                    auto AnalyzeType = [this, &DefinitionAst, IterationValueType]()
                    {
                        if (TSPtr<CExpressionBase> NewTypeAst = AnalyzeExpressionAst(DefinitionAst.ValueDomain().AsRef(), SExprCtx::Default().WithResultIsUsedAsType()))
                        {
                            DefinitionAst.SetValueDomain(MoveIfPossible(NewTypeAst.AsRef()));
                        }

                        const TSPtr<CExpressionBase>& TypeAst = DefinitionAst.ValueDomain();
                        const CTypeBase* DesiredValueType = GetTypeNegativeType(*TypeAst)._Type;
                        ValidateNonAttributeType(DesiredValueType, TypeAst->GetMappedVstNode());

                        if (!Constrain(IterationValueType, DesiredValueType))
                        {
                            AppendGlitch(
                                DefinitionAst,
                                EDiagnostic::ErrSemantic_IncompatibleArgument,
                                CUTF8String("The definition's right hand type `%s` is not compatible with the expected type `%s`"
                                    , IterationValueType->AsCode().AsCString()
                                    , DesiredValueType->AsCode().AsCString())
                            );
                        }
                    };
                    EnqueueDeferredTask(Deferred_ValidateType, Move(AnalyzeType));
                }
            }
            else
            {
                // We only have a ValueDomain, it must be an array, map, or generator
                if (TSPtr<CExpressionBase> NewRhsAst = AnalyzeExpressionAst(DefinitionAst.ValueDomain().AsRef(), ExprCtx.WithDecides().WithResultIsUsed(nullptr)))
                {
                    DefinitionAst.SetValueDomain(Move(NewRhsAst.AsRef()));
                }
                const CTypeBase* DomainType = DefinitionAst.ValueDomain()->GetResultType(*_Program);

                if (!DomainType)
                {
                    AppendGlitch(FindMappedVstNode(DefinitionAst), EDiagnostic::ErrSemantic_ExpectedIterationIterable);
                }
                else if (const CArrayType* DomainArrayType = DomainType->GetNormalType().AsNullable<CArrayType>())
                {
                    IterationKeyType = _Program->_intType;
                    IterationValueType = DomainArrayType->GetElementType();
                }
                else if (const CGeneratorType* DomainGeneratorType = DomainType->GetNormalType().AsNullable<CGeneratorType>())
                {
                    IterationKeyType = DomainGeneratorType->GetElementType();
                    IterationValueType = DomainGeneratorType->GetElementType();
                }
                else if (const CMapType* DomainMapType = DomainType->GetNormalType().AsNullable<CMapType>(); DomainMapType && !DomainMapType->IsWeak())
                {
                    IterationKeyType = DomainMapType->GetKeyType();
                    IterationValueType = DomainMapType->GetValueType();
                }
                else
                {
                    AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_ExpectIterable, CUTF8String("Must be an array, map, or generator after ':' inside %s", ScopeName.AsCString()));
                }
            }

            // Analyze the LHS of the definition.
            if (DefinitionAst.Element()->GetNodeType() == EAstNodeType::Identifier_Unresolved)
            {
                // If the LHS is an identifier, use it as the name of the iteration value.
                CExprIdentifierUnresolved& Identifier = static_cast<CExprIdentifierUnresolved&>(*DefinitionAst.Element());
                ValidateDefinitionIdentifier(Identifier, *_Context._Scope);

                // Create the iteration value definition.
                TSRef<CDataDefinition> IterationValueDefinition = Iteration->_AssociatedScope->CreateDataDefinition(Identifier._Symbol);
                IterationValueDefinition->SetType(IterationValueType);

                // Analyze the qualifier of the definition, if any.
                EnqueueDeferredTask(Deferred_Type,
                    [this, IterationValueDefinition, &DefinitionAst, Qualifier = Identifier.Qualifier(), ExprCtx]()
                    {
                        TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &IterationValueDefinition->_EnclosingScope);
                        AnalyzeDefinitionQualifier(Qualifier, *IterationValueDefinition, DefinitionAst, ExprCtx);
                    });

                // Require that the iteration value definition is unambiguous.
                RequireUnambiguousDefinition(*IterationValueDefinition, "iteration variable");

                // Replace the unresolved identifier node with a resolved identifier that references the definition.
                DefinitionAst.SetElement(ReplaceMapping(Identifier, TSRef<CExprIdentifierData>::New(*_Program, *IterationValueDefinition)));

                // Transform the CExprDefinition to a CExprDataDefinition.
                return ReplaceMapping(DefinitionAst, TSRef<CExprDataDefinition>::New(
                    Move(IterationValueDefinition),
                    DefinitionAst.TakeElement(),
                    DefinitionAst.TakeValueDomain(),
                    MoveIfPossible(DefinitionAst.TakeValue())));
            }
            else if (DefinitionAst.Element()->GetNodeType() == EAstNodeType::Invoke_Arrow)
            {
                TSPtr<CDataDefinition> IterationKeyDefinition;
                TSPtr<CDataDefinition> IterationValueDefinition;

                CExprArrow& ArrowAst = static_cast<CExprArrow&>(*DefinitionAst.Element());
                if (!IterationKeyType)
                {
                    AppendGlitch(ArrowAst, EDiagnostic::ErrSemantic_ExpectedIdentifier, "Expected identifier: definition pattern does not match value");
                    IterationKeyType = _Program->GetDefaultUnknownType();
                }

                // If the LHS is in the form identifier->identifier, use the first identifier as the
                // name of iteration key, and the second as the name of the iteration value.
                if (ArrowAst.Domain()->GetNodeType() == EAstNodeType::Identifier_Unresolved)
                {
                    CExprIdentifierUnresolved& DomainIdentifier = static_cast<CExprIdentifierUnresolved&>(*ArrowAst.Domain());
                    ValidateDefinitionIdentifier(DomainIdentifier, *_Context._Scope);

                    // Create the iteration key definition.
                    IterationKeyDefinition = Iteration->_AssociatedScope->CreateDataDefinition(DomainIdentifier._Symbol);
                    IterationKeyDefinition->SetType(IterationKeyType);

                    // Replace the unresolved identifier node with a resolved identifier that references the definition.
                    ArrowAst.SetDomain(ReplaceMapping(DomainIdentifier, TSRef<CExprIdentifierData>::New(*_Program, *IterationKeyDefinition)));
                }
                else
                {
                    AppendGlitch(FindMappedVstNode(*ArrowAst.Domain()), EDiagnostic::ErrSemantic_ExpectedIdentifier);
                }
                if (ArrowAst.Range()->GetNodeType() == EAstNodeType::Identifier_Unresolved)
                {
                    CExprIdentifierUnresolved& RangeIdentifier = static_cast<CExprIdentifierUnresolved&>(*ArrowAst.Range());
                    ValidateDefinitionIdentifier(RangeIdentifier, *_Context._Scope);

                    // Create the iteration key definition.
                    IterationValueDefinition = Iteration->_AssociatedScope->CreateDataDefinition(RangeIdentifier._Symbol);
                    IterationValueDefinition->SetType(IterationValueType);

                    // Replace the unresolved identifier node with a resolved identifier that references the definition.
                    ArrowAst.SetRange(ReplaceMapping(RangeIdentifier, TSRef<CExprIdentifierData>::New(*_Program, *IterationValueDefinition)));
                }
                else
                {
                    AppendGlitch(FindMappedVstNode(*ArrowAst.Range()), EDiagnostic::ErrSemantic_ExpectedIdentifier);
                }

                if (IterationKeyDefinition && IterationValueDefinition)
                {
                    // Require that the iteration key and value definitions are unambiguous.
                    // This is deferred until both definitions are successfully analyzed to ensure
                    // the CExprIterationPairDefinition will eventually be constructed and set the
                    // AST node link which is used by RequireUnambiguousDefinition to report an
                    // error from a deferred task.
                    RequireUnambiguousDefinition(*IterationKeyDefinition, "iteration key variable");
                    RequireUnambiguousDefinition(*IterationValueDefinition, "iteration value variable");

                    // Transform the CExprDefinition to a CExprIterationPairDefinition.
                    TSRef<CExprIterationPairDefinition> PairDefinition = TSRef<CExprIterationPairDefinition>::New(
                        Move(IterationKeyDefinition.AsRef()),
                        Move(IterationValueDefinition.AsRef()),
                        DefinitionAst.TakeElement(),
                        DefinitionAst.TakeValueDomain(),
                        MoveIfPossible(DefinitionAst.TakeValue()));

                    const CFunctionType& IterationPairType = GetOrCreatePairType(*IterationKeyType, *IterationValueType);
                    ArrowAst._TypeType = &_Program->GetOrCreateTypeType(&IterationPairType, &IterationPairType);
                    PairDefinition->SetResultType(&IterationPairType);

                    return ReplaceMapping(DefinitionAst, Move(PairDefinition));
                }
            }
            else
            {
                AppendGlitch(FindMappedVstNode(*DefinitionAst.Element()), EDiagnostic::ErrSemantic_ExpectedIdentifier);
            }

            return Move(AstNode);
        }
        else
        {
            TSRef<CExpressionBase> Condition = Move(AstNode);
            if (TSPtr<CExpressionBase> NewCondition = AnalyzeExpressionAst(Condition, ExprCtx))
            {
                Condition = Move(NewCondition);
            }

            /*
             * We check that the first filter passed is not an invalid expression.
             * i.e.
             *
             * ```
             * for (foo[]):  # `foo[]` is not a range/map/iterable, so it cannot be iterated over.
             *     bar()
             * ```
             */
            if (bExpectGenerator)
            {
                AppendGlitch(Condition->GetMappedVstNode(), EDiagnostic::ErrSemantic_ExpectIterable, CUTF8String("First argument to %s must be a generator, X:=range, X:array, or X:map", ScopeName.AsCString()));
            }
            else
            {
                RequireExpressionCanFail(*Condition, "the 'for' filter expression");
            }

            return Move(Condition);
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Loop(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        // Validate that the macro is the right form.
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        // Create the loop AST node.
        TSRef<CExprLoop> LoopAst = TSRef<CExprLoop>::New();
        MacroCallAst.GetMappedVstNode()->AddMapping(LoopAst);
        LoopAst->SetResultType(&_Program->_trueType);

        TGuardValue<const CExpressionBase*> BreakableGuard(_Context._Breakable, LoopAst.Get());
        TGuardValue<const CExpressionBase*> LoopGuard(_Context._Loop, LoopAst.Get());

        // Analyze the macro clause as a code block, and set it as the loop body.
        const SExprCtx BodyExprCtx = ExprCtx
            .AllowReturnFromLeadingStatementsAsSubexpressionOfReturn()
            .WithResultIsIgnored()
            .With(ExprCtx.AllowedEffects.With(EEffect::decides, false));
        LoopAst->SetExpr(AnalyzeMacroClauseAsCodeBlock(MacroCallAst.Clauses()[0], LoopAst->GetMappedVstNode(), BodyExprCtx));

        return Move(LoopAst);
    }

    // Helper struct for indexing instance data-members across a class inheritance hierarchy.
    struct SDataMemberInfo
    {
        const CDataDefinition* Member;
        const CClass* MemberClass;
        bool bNeedsToBeInitialized;
        bool bHasInitializer{false};
    };


    struct SDataMemberIndex
    {
        SDataMemberInfo* FindByName(const CSymbol& MemberName)
        {
            for (SDataMemberInfo& DataMemberInfo : _DataMemberInfos)
            {
                if (DataMemberInfo.Member->GetName() == MemberName)
                {
                    return &DataMemberInfo;
                }
            }
            return nullptr;
        }

        TArray<SDataMemberInfo> _DataMemberInfos;
    };

    SDataMemberIndex GetArchetypeInstantiationMemberIndex(const CClass& Class)
    {
        SDataMemberIndex DataMemberIndex;

        VisitStampType VisitStamp = CScope::GenerateNewVisitStamp();

        for (const CClass* IndexingClass = &Class; IndexingClass; IndexingClass = IndexingClass->_Superclass)
        {
            for (const TSRef<CDataDefinition>& DataMember : IndexingClass->GetDefinitionsOfKind<CDataDefinition>())
            {
                // if we are already marked as visited, skip further processing
                if (!DataMember->TryMarkOverriddenAndConstrainedDefinitionsVisited(VisitStamp))
                {
                    // already marked as visited (by an overriding member)
                    continue;
                }
                DataMemberIndex._DataMemberInfos.Add({
                    DataMember,
                    IndexingClass,
                    !DataMember->HasInitializer()});
            }
        }

        return DataMemberIndex;
    }

    void AnalyzeArchetypeDefinitionArgument(
        CExprArchetypeInstantiation& InstantiationAst,
        const CClass& Class,
        SDataMemberIndex& DataMemberIndex,
        const TSRef<CExprDefinition>& Definition,
        const SExprCtx& ExprCtx)
    {
        if (Definition->Element()->GetNodeType() != EAstNodeType::Identifier_Unresolved)
        {
            AppendGlitch(*Definition->Element(), EDiagnostic::ErrSemantic_ExpectedIdentifier);
            return;
        }

        if (Definition->ValueDomain())
        {
            AppendGlitch(*Definition->ValueDomain(), EDiagnostic::ErrSemantic_UnexpectedExpression);
        }

        SDataMemberInfo* MemberInfo;
        {
            ULANG_ASSERT(Definition->Element()->GetNodeType() == EAstNodeType::Identifier_Unresolved);
            CExprIdentifierUnresolved& Identifier = static_cast<CExprIdentifierUnresolved&>(*Definition->Element());

            if (Identifier.Context())
            {
                AppendGlitch(Identifier, EDiagnostic::ErrSemantic_LhsNotDefineable);
            }

            RequireUnqualifiedIdentifier(Identifier);

            // Find the data member named by the LHS of the definition.
            MemberInfo = DataMemberIndex.FindByName(Identifier._Symbol);
            if (!MemberInfo)
            {
                AppendGlitch(
                    *Definition,
                    EDiagnostic::ErrSemantic_UnknownIdentifier,
                    CUTF8String("`%s` is not an instance data-member of `%s`.", Identifier._Symbol.AsCString(), Class.Definition()->AsNameCString()));
                return;
            }
        }

        Definition->SetElement(TSRef<CExprIdentifierData>::New(*_Program, *MemberInfo->Member));

        // Don't allow the archetype to define the same member multiple times.
        if (MemberInfo->bHasInitializer)
        {
            AppendGlitch(
                *Definition,
                EDiagnostic::ErrSemantic_AmbiguousDefinition,
                CUTF8String("`%s.%s` already defined.", Class.Definition()->AsNameCString(), MemberInfo->Member->AsNameCString()));
        }
        MemberInfo->bHasInitializer = true;

        // Check access level of the member we are initializing
        DeferredRequireAccessible(InstantiationAst.GetMappedVstNode(), *_Context._Scope, *MemberInfo->Member);

        // Check that the member isn't final.
        DeferredRequireOverridableByArchetype(InstantiationAst.GetMappedVstNode(), *MemberInfo->Member);

        // If the data member is a unique pointer, the initializer should be of the pointer's value type.
        const CTypeBase* NegativeMemberValueType = MemberInfo->Member->_NegativeType;
        const CTypeBase* PositiveMemberValueType = MemberInfo->Member->GetType();
        if (MemberInfo->Member->IsVar())
        {
            const CPointerType& PositiveMemberPointerType = PositiveMemberValueType->GetNormalType().AsChecked<CPointerType>();
            NegativeMemberValueType = PositiveMemberPointerType.NegativeValueType();
            PositiveMemberValueType = PositiveMemberPointerType.PositiveValueType();
        }

        // Analyze the definition value.
        if (!Definition->Value())
        {
            AppendExpectedDefinitionError(*Definition);
        }
        else
        {
            SExprArgs DefinitionArgs;
            DefinitionArgs.ArchetypeInstantiationContext = ArchetypeInstantiationArgument;
            if (TSPtr<CExpressionBase> NewValue = AnalyzeExpressionAst(
                Definition->Value().AsRef(),
                ExprCtx.WithResultIsUsed(NegativeMemberValueType).With(ExprCtx.AllowedEffects.With(EEffect::suspends, false)),
                DefinitionArgs))
            {
                Definition->SetValue(Move(NewValue.AsRef()));
            }

            // Check the type of the value against the data member's type.
            if (TSPtr<CExpressionBase> NewValue = ApplyTypeToExpression(
                *NegativeMemberValueType,
                Definition->Value().AsRef(),
                EDiagnostic::ErrSemantic_IncompatibleArgument,
                "This variable expects to be initialized with", "this initializer"))
            {
                Definition->SetValue(Move(NewValue.AsRef()));
            }
        }

        // Set the analyzed type for the definition expression.
        Definition->SetResultType(PositiveMemberValueType);
    }

    void MaybeAppendUnsupportedAttributeValueErrors(const TSPtr<CExpressionBase>& Value)
    {
        switch (Value->GetNodeType())
        {
            case EAstNodeType::Literal_String:
            case EAstNodeType::Literal_Number:
            case EAstNodeType::Literal_Char:
            case EAstNodeType::Literal_Logic:
                return;

            case EAstNodeType::Invoke_MakeArray:
                for (const TSPtr<CExpressionBase>& Element : Value.As<CExprMakeArray>()->GetSubExprs())
                {
                    MaybeAppendUnsupportedAttributeValueErrors(Element);
                }
                return;

            case EAstNodeType::Invoke_ArchetypeInstantiation:
                for (const TSRef<CExpressionBase>& Argument : Value.As<CExprArchetypeInstantiation>()->Arguments())
                {
                    MaybeAppendUnsupportedAttributeValueErrors(Argument.As<CExprDefinition>()->Value().As<CExprInvokeType>()->_Argument);
                }
                return;

            case EAstNodeType::Invoke_MakeOption:
                if (const TSPtr<CExpressionBase>& Operand = Value.As<CExprMakeOption>()->Operand())
                {
                    MaybeAppendUnsupportedAttributeValueErrors(Operand);
                }
                return;

            case EAstNodeType::Identifier_Data:
            {
                const TSPtr<CExprIdentifierData>& Identifier = Value.As<CExprIdentifierData>();

                const CExprDataDefinition* Definition = static_cast<const CExprDataDefinition*>(Identifier->_DataDefinition.GetAstNode());
                if (Definition == nullptr || Definition->Value()->GetNodeType() != EAstNodeType::Invoke_Type)
                {
                    break;
                }
                MaybeAppendUnsupportedAttributeValueErrors(Definition->Value());
                return;
            }

            case EAstNodeType::Invoke_Type:
            {
                MaybeAppendUnsupportedAttributeValueErrors(Value.As<CExprInvokeType>()->_Argument);
                return;
            }

            case EAstNodeType::Invoke_Invocation:
            {
                const TSPtr<CExprInvocation>& Invocation = Value.As<CExprInvocation>();
                if (Invocation->GetCallee()->GetNodeType() != EAstNodeType::Identifier_Function)
                {
                    break;
                }

                const TSPtr<CExprIdentifierFunction>& Function = Invocation->GetCallee().As<CExprIdentifierFunction>();
                CFunction* MakeMessageInternal = _Program->FindDefinitionByVersePath<CFunction>("/Verse.org/Verse/MakeMessageInternal");
                if (&Function->_Function != MakeMessageInternal)
                {
                    break;
                }

                return;
            }

            default:
                break;
        }

        AppendGlitch(
            Value->GetMappedVstNode(),
            EDiagnostic::ErrAssembler_AttributeError,
            CUTF8String("Unsupported attribute value expression: %s", Value->GetErrorDesc().AsCString()));
    }

    void AnalyzeArchetypeInstantiation(CExprArchetypeInstantiation& InstantiationAst, const CClass& Class, SDataMemberIndex& DataMemberIndex, const SExprCtx& ExprCtx)
    {
        // Archetype instantiations are not really breakable, but the code
        // generator requires the code generated in archetype instantiations to
        // form a SESE region due to use of `InstantiateObject`.
        TGuardValue<const CExpressionBase*> BreakableGuard(_Context._Breakable, &InstantiationAst);

        // Scope used by any argument `let`s.
        TSRef<CControlScope> ControlScope = _Context._Scope->CreateNestedControlScope();
        TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, ControlScope.Get());

        const CTypeBase* ConstructorPositiveReturnType = nullptr;
        // Analyze each expression in the body of the archetype.
        for (TSRef<CExpressionBase>& Expr : InstantiationAst._BodyAst.Exprs())
        {
            // Check that the expression is a definition: i.e. id := ...
            if (Expr->GetNodeType() == EAstNodeType::Definition)
            {
                TSRef<CExprDefinition> &Definition = Expr.As<CExprDefinition>();

                AnalyzeArchetypeDefinitionArgument(
                    InstantiationAst,
                    Class,
                    DataMemberIndex,
                    Definition,
                    ExprCtx);

                if (ExprCtx.ResultContext == ResultIsUsedAsAttribute)
                {
                    MaybeAppendUnsupportedAttributeValueErrors(Definition->Value().As<CExprInvokeType>()->_Argument);
                }
            }
            else if (ExprCtx.ResultContext == ResultIsUsedAsAttribute)
            {
                AppendGlitch(
                    *Expr,
                    uLang::EDiagnostic::ErrAssembler_AttributeError,
                    uLang::CUTF8String("Unsupported attribute value expression: %s", Expr->GetErrorDesc().AsCString()));
                Expr = ReplaceNodeWithError(Expr);
            }
            else
            {
                SExprArgs ExprArgs;
                ExprArgs.ArchetypeInstantiationContext = ArchetypeInstantiationArgument;
                if (TSPtr<CExpressionBase> NewExpr = AnalyzeExpressionAst(
                    Expr,
                    ExprCtx.WithResultIsIgnored().With(ExprCtx.AllowedEffects.With(EEffect::suspends, false)),
                    ExprArgs))
                {
                    Expr = NewExpr.AsRef();
                }

                if (Expr->GetNodeType() == Cases<EAstNodeType::Flow_CodeBlock, EAstNodeType::Flow_Let>)
                {
                    // Allow but do nothing
                }
                else if (const CExprIdentifierFunction* ConstructorIdentifier = GetConstructorInvocationCallee(*Expr))
                {
                    if (ConstructorPositiveReturnType)
                    {
                        AppendGlitch(*Expr, EDiagnostic::ErrSemantic_MultipleConstructorInvocations);
                    }
                    if (!Constrain(&Class, ConstructorIdentifier->_ConstructorNegativeReturnType))
                    {
                        AppendGlitch(*Expr, EDiagnostic::ErrSemantic_ConstructorInvocationResultType);
                    }
                    ConstructorPositiveReturnType = &static_cast<const CFunctionType*>(ConstructorIdentifier->GetResultType(*_Program))->GetReturnType();
                }
                // NOTE: (yiliang.siew) This preserves the mistake we shipped in `28.20` where mixed use of separators in
                // archetype instantiations wrapped the sub-expressions into an implicit `block`, but in other places, it
                // did not.
                else if (Expr->GetNodeType() == EAstNodeType::Invoke_MakeTuple
                    && VerseFN::UploadedAtFNVersion::EnforceDontMixCommaAndSemicolonInBlocks(_Context._Package->_UploadedAtFNVersion))
                {
                    AppendGlitch(*Expr, EDiagnostic::WarnSemantic_StricterErrorCheck, "Mixing comma and semicolon/newline in an instantiation is incorrect. In a future version of Verse this will be an error, now the parts deliminated by comma will not be used when instaniating.");
                    // Wrap the MakeTuple in a code block to mimic the old behavior and keep
                    // the later compiler stages from asserting on the unexpected node type.
                    TSPtr<CExprCodeBlock> Block = MakeCodeBlock();
                    Block->SetNonReciprocalMappedVstNode(Expr->GetMappedVstNode());
                    Block->SetSubExprs({ Move(Expr) });
                    Expr = Move(Block);
                }
                else
                {
                    AppendGlitch(
                        *Expr,
                        EDiagnostic::ErrSemantic_Unsupported,
                        "Unsupported argument to archetype instantiation.");
                    Expr = ReplaceNodeWithError(Expr);
                }
            }
            InstantiationAst.AppendArgument(Move(Expr));
        }

        InstantiationAst._BodyAst.Exprs().Empty();

        // Check for members that needed to be initialized but were not.
        for (SDataMemberInfo& MemberInfo : DataMemberIndex._DataMemberInfos)
        {
            if (!MemberInfo.bNeedsToBeInitialized)
            {
                continue;
            }
            if (MemberInfo.bHasInitializer)
            {
                continue;
            }
            if (ConstructorPositiveReturnType && Constrain(ConstructorPositiveReturnType, MemberInfo.MemberClass->_NegativeClass))
            {
                continue;
            }

            AppendGlitch(
                InstantiationAst,
                EDiagnostic::ErrSemantic_MissingDataMemberInitializer,
                CUTF8String(
                    "Object archetype must initialize data member `%s`.",
                    MemberInfo.Member->AsNameCString()));
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_InstantiateClass(const TSRef<CExprMacroCall>& MacroCallAst, const CClass& Class, const SExprCtx& ExprCtx)
    {
        // Validate that the macro is the right form.
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(*MacroCallAst))
        {
            return ReplaceNodeWithError(MacroCallAst);
        }

        // Take the clause from the macro node and allocate a new archetype data object to hold it.
        // The archetype data object has shared ownership via TSRef so a reference to it can be captured by the below lambda.
        TSRef<CExpressionBase> ClassAst = MacroCallAst->TakeName();
        TArray<CExprMacroCall::CClause> MacroClauses = MacroCallAst->TakeClauses();
        ULANG_ASSERTF(MacroClauses.Num() == 1, "Expected a single macro clause after calling ValidateMacroForm");
        TSRef<CExprArchetypeInstantiation> InstantiationAst = TSRef<CExprArchetypeInstantiation>::New(
            Move(ClassAst),
            Move(MacroClauses[0]),
            &Class);
        
        if (const Verse::Vst::Node* MappedVstNode = MacroCallAst->GetMappedVstNode())
        {
            MappedVstNode->AddMapping(InstantiationAst);
        }

        // Available attribute archetype-instances need to go a little earlier than other archetype instances because 
        // instances of @available need to be processed already when we start doing symbol lookups. This presents a chicken
        // and egg problem unless we force available attributes to process in an earlier phase. This isn't generally applicable 
        // to all attribute types because they may be compositions of other unresolved types. We can ignore this problem in 
        // this very limited case, however, if we can accept that @available will only be composed from intrinsic -data types.
        EDeferredPri DeferredPriority = &Class == _Program->_availableClass ? Deferred_ValidateAttributes : Deferred_OpenFunctionBodyExpressions;

        // Defer the processing of the archetype body until after all types and data members have been analyzed.
        EnqueueDeferredTask(DeferredPriority, [this, InstantiationAst, ExprCtx]()
        {
            const CClass* Class = InstantiationAst->GetClass(*_Program);
            if (!Class)
            {
                AppendGlitch(
                    *InstantiationAst,
                    EDiagnostic::ErrSemantic_Unsupported,
                    CUTF8String("Archetype constructors are only supported for classes and structs."));
            }
            else if (Class->_Definition->_EffectAttributable.HasAttributeClass(_Program->_abstractClass, *_Program))
            {
                AppendGlitch(
                    *InstantiationAst,
                    EDiagnostic::ErrSemantic_UnexpectedAbstractClass,
                    CUTF8String(
                        "Cannot instantiate class `%s` because it has the `abstract` attribute. Use a subclass of it.",
                        Class->Definition()->AsNameCString()));
            }
            // Don't allow instantiating a class while initializing its defaults.
            else if (uLang::AnyOf(
                _Context._DataMembers,
                [=](const CDefinition* DataMember) { return ClassIsEnclosingScope(DataMember, *Class); }))
            {
                AppendGlitch(
                    *InstantiationAst,
                    EDiagnostic::ErrSemantic_Unimplemented,
                    CUTF8String("Constructing an instance of a class while initializing its defaults is not implemented."));
            }
            else
            {
                if (ExprCtx.ResultContext != ResultIsUsedAsAttribute &&
                    SemanticTypeUtils::IsAttributeType(Class))
                {
                    EnqueueDeferredTask(Deferred_ValidateAttributes, [this, ExprCtx, InstantiationAst]
                    {
                        if (ExprCtx.ResultContext != ResultIsReturned ||
                            !_Context._Function->HasAttributeClass(_Program->_constructorClass, *_Program))
                        {
                            AppendGlitch(
                                *InstantiationAst,
                                EDiagnostic::ErrSemantic_IncorrectUseOfAttributeType,
                                CUTF8String("Attribute class types can only be used as attributes."));
                        }
                    });
                }

                // Check that the class and its constructor are accessible.
                RequireConstructorAccessible(InstantiationAst->GetMappedVstNode(), *_Context._Scope, *Class->_Definition);

                // Attributes previously allowed <transacts> and accepted some expressions which the custom attribute processor ignored.
                if (!VerseFN::UploadedAtFNVersion::AttributesRequireComputes(_Context._Package->_UploadedAtFNVersion) &&
                    SemanticTypeUtils::IsAttributeType(Class))
                {
                    SDataMemberIndex DataMemberIndex = GetArchetypeInstantiationMemberIndex(*Class);
                    SExprCtx NewExprCtx = ExprCtx.With(EffectSets::Transacts);
                    AnalyzeArchetypeInstantiation(*InstantiationAst, *Class, DataMemberIndex, NewExprCtx);
                }
                else
                {
                    // Require that the class's constructor effects are allowed in the current context.
                    RequireEffects(*InstantiationAst, Class->_ConstructorEffects, ExprCtx.AllowedEffects, "archetype instantiation constructs a class that");

                    SDataMemberIndex DataMemberIndex = GetArchetypeInstantiationMemberIndex(*Class);
                    AnalyzeArchetypeInstantiation(*InstantiationAst, *Class, DataMemberIndex, ExprCtx);
                }
            }
        });
        
        return Move(InstantiationAst);
    }

    //-------------------------------------------------------------------------------------------------
    // Non-iterating form - e.g. sync {expr1 expr2}
    void AnalyzeConcurrentBlock(CExprMacroCall& MacroCallAst, CExprConcurrentBlockBase* CoPrimitiveAst, const char* AsyncCStr, const SExprCtx& ExprCtx)
    {
        ULANG_ASSERTF(MacroCallAst.Clauses().Num() == 1, "Expected caller to validate macro form");

        TGuardValue<const CExpressionBase*> BreakableGuard(_Context._Breakable, CoPrimitiveAst);

        // Analyze each subexpression in the body clause.
        const CTypeBase* JoinedResultType = &_Program->_falseType;
        CExprMacroCall::CClause& BodyClause = MacroCallAst.Clauses()[0];
        int TopExprNum = BodyClause.Exprs().Num();

        // Must have at least two top-level expressions
        if (TopExprNum < 2)
        {
            AppendGlitch(
                MacroCallAst,
                EDiagnostic::ErrSemantic_ExpectedAsyncExprNumber,
                CUTF8String("The %s must have two or more top-level expressions to run concurrently and this has %s.", AsyncCStr, TopExprNum == 1 ? "only one expression" : "no expressions"));
        }

        const bool bSyncExpr = CoPrimitiveAst->GetNodeType() == EAstNodeType::Concurrent_Sync;
        const CTypeBase* RequiredType = ExprCtx.RequiredType;
        const bool bRequiredVoid = ExprCtx.RequiredType && ExprCtx.RequiredType->GetNormalType().IsA<CVoidType>();
        const CTupleType* SyncRequiredType = nullptr;

        CTupleType::ElementArray SyncElementTypes;
        if (!bRequiredVoid && bSyncExpr)
        {  
            SyncElementTypes.Reserve(TopExprNum);
            if (ExprCtx.RequiredType)
            {
                SyncRequiredType = GetRequiredTupleType(*ExprCtx.RequiredType, TopExprNum);
                if (!SyncRequiredType)
                {
                    RequiredType = nullptr;   // Drop required type to get rid of extra errors.
                }
            }
        }

        int32_t Idx = 0;
        int32_t AsyncCount = 0;  // Determine number of async top-level subexpressions for `sync`
        CExpressionBase* FirstImmediateExpr = nullptr;

        for (TSRef<CExpressionBase>& SubExprAst : BodyClause.Exprs())
        {
            // Analyze the subexpression, in a local scope.
            TSRef<CControlScope> Scope(_Context._Scope->CreateNestedControlScope());
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope,
                VerseFN::UploadedAtFNVersion::ConcurrencyAddScope(_Context._Package->_UploadedAtFNVersion)
                ? Scope
                : _Context._Scope);

            if (TSPtr<CExpressionBase> NewSubExprAst = AnalyzeExpressionAst(SubExprAst, ExprCtx.WithResultIsSpawned(SyncRequiredType ? (*SyncRequiredType)[Idx] : RequiredType)))
            {
                SubExprAst = Move(NewSubExprAst.AsRef());
            }

            // Determine if it is async
            // @TODO: SOL-1423, DetermineInvokeTime() re-traverses the expression tree, which could add up time wise 
            //        (approaching on n^2) -- there should be a beter way to check this on the initial ProcessExpression()
            bool bImmediate = SubExprAst->DetermineInvokeTime(*_Program) == EInvokeTime::Immediate
                && !SemanticTypeUtils::IsUnknownType(SubExprAst->GetResultType(*_Program));

            if (bSyncExpr)
            {
                SyncElementTypes.Add(SubExprAst->GetResultType(*_Program));
                ++Idx;
                if (bImmediate)
                {
                    if (!FirstImmediateExpr)
                    {
                        FirstImmediateExpr = SubExprAst;
                    }
                }
                else
                {
                    AsyncCount++;
                }
            }
            else
            {
                if (bImmediate)
                {
                    AppendGlitch(
                        *SubExprAst,
                        EDiagnostic::ErrSemantic_ExpectedAsyncExprs,
                        CUTF8String("All the top level expressions in a `%s` must be async (such as a coroutine call) and not immediate.", AsyncCStr));
                }

                // Join the result type of the subexpression with the result type of the previous subexpressions in the body.
                JoinedResultType = Join(
                    JoinedResultType,
                    SubExprAst->GetResultType(*_Program));
            }

            // Append the subexpression to the AST node.
            CoPrimitiveAst->AppendSubExpr(Move(SubExprAst));
        }

        if (bSyncExpr && (TopExprNum >= 2) && (AsyncCount < 2))
        {
            ULANG_ASSERT(FirstImmediateExpr);
            AppendGlitch(
                *FirstImmediateExpr, // Use first immediate expression encountered
                EDiagnostic::ErrSemantic_ExpectedAsyncExprs,
                "At least two top level expressions in a `sync` must be async (such as a coroutine call) and not immediate. Have more async expressions or do not use a `sync`.");
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Resolve result type
        const CTypeBase* ResultType = 
            bRequiredVoid
            ? &_Program->_voidType
            : bSyncExpr
                // If non-iterating `sync` - adjust to result in tuple of task results
                ? &_Program->GetOrCreateTupleType(Move(SyncElementTypes))
                // Other iterating forms result of one task
                : JoinedResultType;

        CoPrimitiveAst->SetResultType(ResultType);
    }


    //-------------------------------------------------------------------------------------------------
    void AnalyzeConcurrentExpr(CExprMacroCall& MacroCallAst, CExprSubBlockBase* CoPrimitiveExpr, const SExprCtx& ExprCtx)
    {
        ULANG_ASSERTF(MacroCallAst.Clauses().Num() == 1, "Expected caller to validate macro form");

        TGuardValue<const CExpressionBase*> BreakableGuard(_Context._Breakable, CoPrimitiveExpr);

        SExprCtx BodyExprCtx = ExprCtx.With(ExprCtx.AllowedEffects | EffectSets::Suspends).WithResultIsSpawned(nullptr);

        CExprMacroCall::CClause& BodyClause = MacroCallAst.Clauses()[0];
        if (BodyClause.Exprs().Num() == 0)
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_ExpectedAsyncExprNumber, "Expected one or more async expressions and found none.");
        }
        else if (BodyClause.Exprs().Num() == 1)
        {
            // Analyze the subexpression, in a local scope.
            TSRef<CControlScope> Scope(_Context._Scope->CreateNestedControlScope());
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope,
                VerseFN::UploadedAtFNVersion::ConcurrencyAddScope(_Context._Package->_UploadedAtFNVersion)
                ? Scope
                : _Context._Scope);
            TSRef<CExpressionBase> ExprAst = Move(BodyClause.Exprs()[0]);
            if (TSPtr<CExpressionBase> NewExprAst = AnalyzeExpressionAst(ExprAst, BodyExprCtx))
            {
                ExprAst = Move(NewExprAst.AsRef());
            }

            // Determine if it is async
            // @TODO: SOL-1423, DetermineInvokeTime() re-traverses the expression tree, which could add up time wise 
            //        (approaching on n^2) -- there should be a better way to check this on the initial ProcessExpression()
            if (ExprAst->DetermineInvokeTime(*_Program) != EInvokeTime::Async
                && !SemanticTypeUtils::IsUnknownType(ExprAst->GetResultType(*_Program)))
            {
                AppendGlitch(
                    *ExprAst,
                    EDiagnostic::ErrSemantic_ExpectedAsyncExprs,
                    "Found immediate expression (such as an immediate function call) when an async expression (such as a coroutine call) was desired.");
            }
            
            CoPrimitiveExpr->SetExpr(Move(ExprAst));
        }
        else
        {
            // Analyze the body clause as an async code block.
            CoPrimitiveExpr->SetExpr(AnalyzeMacroClauseAsCodeBlock(BodyClause, MacroCallAst.GetMappedVstNode(), BodyExprCtx));
        }
    }


    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Option(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        // Verify that macro is of the form 'm1{}'
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        else
        {
            const CTypeBase* DesiredValueType = nullptr;
            if (ExprCtx.RequiredType)
            {
                if (const COptionType* DesiredOptionType = ExprCtx.RequiredType->GetNormalType().AsNullable<COptionType>())
                {
                    DesiredValueType = DesiredOptionType->GetValueType();
                }
            }

            if (MacroCallAst.Clauses()[0].Exprs().Num())
            {
                TSRef<CExpressionBase> ValueAst = InterpretMacroClauseAsExpression(MacroCallAst.Clauses()[0], MacroCallAst.GetMappedVstNode());

                // Need a new scope since initialization of definitions in this part might not be evaluated, SOL-1830
                {
                    TSRef<CControlScope> ControlScope = _Context._Scope->CreateNestedControlScope();
                    TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, ControlScope.Get());

                    // Process the value subexpressions in a failure context.
                    if (TSPtr<CExpressionBase> NewValueAst = AnalyzeExpressionAst(ValueAst, ExprCtx.WithDecides().WithResultIsUsed(DesiredValueType)))
                    {
                        ValueAst = Move(NewValueAst.AsRef());
                    }
                }

                const CTypeBase* ValueType = ValueAst->GetResultType(*_Program);
                const COptionType* OptionType = &_Program->GetOrCreateOptionType(ValueType);
                return ReplaceMapping(MacroCallAst, TSRef<CExprMakeOption>::New(OptionType, Move(ValueAst)));
            }
            else
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_EmptyOption);
                return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
            }
        }
    }


    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Logic(const TSRef<CExprMacroCall>& MacroCallAst, const SExprCtx& ExprCtx)
    {
        // Verify that macro is of the form 'm1{}'
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(*MacroCallAst))
        {
            return ReplaceNodeWithError(MacroCallAst);
        }
        else
        {
            CExprMacroCall::CClause& PredicateClause = MacroCallAst->Clauses()[0];

            if (!PredicateClause.Exprs().Num())
            {
                AppendGlitch(*MacroCallAst, EDiagnostic::ErrSemantic_LogicWithoutExpression);
                return ReplaceNodeWithError(MacroCallAst);
            }

            TSRef<CExpressionBase> PredicateAst = InterpretMacroClauseAsExpression(PredicateClause, MacroCallAst->GetMappedVstNode());

            // Analyze the predicate
            // Need a new scope since initialization of definitions in this part might not be evaluated, SOL-1830
            {
                TSRef<CControlScope> ControlScope = _Context._Scope->CreateNestedControlScope();
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, ControlScope.Get());
                if (TSPtr<CExpressionBase> NewPredicateAst = AnalyzeExpressionAst(PredicateAst, ExprCtx.WithDecides().WithResultIsIgnored()))
                {
                    PredicateAst = Move(NewPredicateAst.AsRef());
                }
            }

            // Require that the predicate can fail.
            RequireExpressionCanFail(*PredicateAst, "the 'logic' clause");

            // Translate logic{<predicate>} to (<predicate> && true) || false
            return ReplaceMapping(
                *MacroCallAst,
                TSRef<CExprShortCircuitOr>::New(
                    TSRef<CExprShortCircuitAnd>::New(
                        Move(PredicateAst),
                        TSRef<CExprLogic>::New(*_Program, true)),
                    TSRef<CExprLogic>::New(*_Program, false),
                    &_Program->_logicType));
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Array(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        // Verify that macro is of the form 'm1{}'
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        else
        {
            using namespace Verse;

            TArray<TSRef<CExpressionBase>>& Args = MacroCallAst.Clauses()[0].Exprs();
            const int32_t NumArgs = Args.Num();

            const CTypeBase* RequiredElementType = nullptr;
            if (ExprCtx.RequiredType)
            {
                if (const CArrayType* RequiredArrayType = ExprCtx.RequiredType->GetNormalType().AsNullable<CArrayType>())
                {
                    RequiredElementType = RequiredArrayType->GetElementType();
                }
            }

            TSRef<CExprMakeArray> MakeArrayAst = TSRef<CExprMakeArray>::New(NumArgs);

            // Analyze arguments as we transfer them from the generic MacroCall to the `MakeArrayAst` structure.
            const CTypeBase* ElementType = &_Program->_falseType;
            for (int32_t i = 0; i < NumArgs; i += 1)
            {
                TSRef<CExpressionBase> CurArgAst = Move(Args[i]);
                if (TSPtr<CExpressionBase> NewArgAst = AnalyzeExpressionAst(CurArgAst, ExprCtx.WithResultIsUsed(RequiredElementType)))
                {
                    //  NewArgAst->GetResultType(*_Program)
                    MakeArrayAst->AppendSubExpr(Move(NewArgAst.AsRef()));
                }
                else
                {
                    MakeArrayAst->AppendSubExpr(Move(CurArgAst));
                }

                const CTypeBase* ExprType = MakeArrayAst->GetLastSubExpr()->GetResultType(*_Program);
                ElementType = Join(ElementType, ExprType);
            }

            MakeArrayAst->SetResultType(&_Program->GetOrCreateArrayType(ElementType));
            return ReplaceMapping(MacroCallAst, Move(MakeArrayAst));
        }

    }

    //-------------------------------------------------------------------------------------------------
    const CFunctionType& GetOrCreatePairType(const CTypeBase& KeyType, const CTypeBase& ValueType)
    {
        // The pair lambdas are pure functions that have a single value in their domain
        // (the key), and a single value in the range (the value). We can't represent
        // such single-element types right now, so we encode the pair's type as a pure
        // PARTIAL function from the key TYPE to the value TYPE.
        return _Program->GetOrCreateFunctionType(KeyType, ValueType, EffectSets::Computes | EffectSets::Decides);
    }
    
    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Map(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        // Verify that macro is of the form 'm1{}'
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        else
        {
            TArray<TSRef<CExpressionBase>>& Pairs = MacroCallAst.Clauses()[0].Exprs();
            const int32_t NumPairs = Pairs.Num();

            const CMapType* RequiredMapType = nullptr;
            const CTypeBase* RequiredKeyType = nullptr;
            const CTypeBase* RequiredValueType = nullptr;
            if (ExprCtx.RequiredType)
            {
                RequiredMapType = ExprCtx.RequiredType->GetNormalType().AsNullable<CMapType>();
                if (RequiredMapType)
                {
                    RequiredKeyType = RequiredMapType->GetKeyType();
                    RequiredValueType = RequiredMapType->GetValueType();
                }
            }

            TSRef<CExprMakeMap> MakeMapAst = TSRef<CExprMakeMap>::New(NumPairs);

            const CTypeBase* KeyType = &_Program->_falseType;
            const CTypeBase* ValueType = &_Program->_falseType;
            for (int32_t PairIndex = 0; PairIndex < NumPairs; ++PairIndex)
            {
                TSRef<CExpressionBase> PairAst = Move(Pairs[PairIndex]);
                if (PairAst->GetNodeType() != EAstNodeType::Literal_Function)
                {
                    AppendGlitch(
                        *PairAst,
                        EDiagnostic::ErrSemantic_IncompatibleArgument,
                        CUTF8String("Expected map pair literal (key=>value), but found %s", PairAst->GetErrorDesc().AsCString()));
                }
                else
                {
                    CExprFunctionLiteral& PairLiteralAst = static_cast<CExprFunctionLiteral&>(*PairAst);

                    if (TSPtr<CExpressionBase> NewKeyAst = AnalyzeExpressionAst(PairLiteralAst.Domain(), ExprCtx.WithResultIsUsed(RequiredKeyType)))
                    {
                        PairLiteralAst.SetDomain(Move(NewKeyAst.AsRef()));
                    }
                    if (TSPtr<CExpressionBase> NewValueAst = AnalyzeExpressionAst(PairLiteralAst.Range(), ExprCtx.WithResultIsUsed(RequiredValueType)))
                    {
                        PairLiteralAst.SetRange(Move(NewValueAst.AsRef()));
                    }

                    if (PairLiteralAst.Domain()->CanFail())
                    {
                        if (_Context._Package->_EffectiveVerseVersion < Verse::Version::MapLiteralKeysHandleIterationAndFailure)
                        {
                            AppendGlitch(*PairLiteralAst.Domain(), EDiagnostic::WarnSemantic_DeprecatedFailureInMapLiteralKey);
                        }
                        else
                        {
                            AppendGlitch(*PairLiteralAst.Domain(), EDiagnostic::ErrSemantic_Unimplemented, "Failure in map literal keys is not yet implemented.");
                        }
                    }

                    const CTypeBase* PairKeyType = PairLiteralAst.Domain()->GetResultType(*_Program);
                    const CTypeBase* PairValueType = PairLiteralAst.Range()->GetResultType(*_Program);

                    PairLiteralAst.SetResultType(&GetOrCreatePairType(*PairKeyType, *PairValueType));

                    KeyType = Join(KeyType, PairKeyType);
                    ValueType = Join(ValueType, PairValueType);

                    MakeMapAst->AppendSubExpr(Move(PairAst));
                }
            }

            ValidateMapKeyType(KeyType, MacroCallAst, true);

            MakeMapAst->SetResultType(&_Program->GetOrCreateMapType(KeyType, ValueType));
            return ReplaceMapping(MacroCallAst, Move(MakeMapAst));
        }
    }


    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Spawn(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        else
        {
            TSRef<CExprSpawn> SpawnExpr = TSRef<CExprSpawn>::New();
            TGuardValue<const CExpressionBase*> BreakableGuard(_Context._Breakable, SpawnExpr.Get());

            const int32_t NumValues = MacroCallAst.Clauses()[0].Exprs().Num();

            if (!ExprCtx.AllowedEffects[EEffect::no_rollback])
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_EffectNotAllowed,
                    "spawn cannot be used when rollback is needed.");
            }
            else
            {
                // Require the transacts effect to spawn.
                RequireEffects(MacroCallAst, EffectSets::Transacts, ExprCtx.AllowedEffects, "'spawn' macro");
            }

            // Allow the transacts/no_rollback effects in the body.
            const SEffectSet BodyAllowedEffects = EffectSets::Transacts | EffectSets::NoRollback;

            const CTypeBase* ExprResultType = nullptr;
            if (NumValues >= 1)
            {
                if (NumValues > 1)
                {
                    // Non-fatal error.
                    AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_UnexpectedNumberOfArguments,
                        "Too many arguments. `spawn` will ignore everything except the first argument.");
                }

                // Analyze the subexpression, in a local scope.
                TSRef<CControlScope> Scope(_Context._Scope->CreateNestedControlScope());
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, 
                    VerseFN::UploadedAtFNVersion::ConcurrencyAddScope(_Context._Package->_UploadedAtFNVersion)
                        ? Scope
                        :_Context._Scope);

                SpawnExpr->SetExpr(Move(MacroCallAst.Clauses()[0].Exprs()[0]));
                if (TSPtr<CExpressionBase> NewSpawnArgAst = AnalyzeExpressionAst(SpawnExpr->Expr().AsRef(), SExprCtx::Default().With(BodyAllowedEffects).WithResultIsSpawned(nullptr)))
                {
                    SpawnExpr->SetExpr(Move(NewSpawnArgAst));
                }
                const CExpressionBase& AsyncCallAst = *SpawnExpr->Expr();

                // @TODO: SOL-1423, DetermineInvokeTime() re-traverses the expression tree, which could add up time wise 
                //        (approaching on n^2) -- there should be a better way to check this on the initial ProcessExpression()
                if (!SemanticTypeUtils::IsUnknownType(AsyncCallAst.GetResultType(*_Program)))
                {
                    if (AsyncCallAst.DetermineInvokeTime(*_Program) != EInvokeTime::Async)
                    {
                        AppendGlitch(AsyncCallAst, EDiagnostic::ErrSemantic_ExpectedAsyncExprs,
                            "Non-async argument. `spawn` expects an async argument (currently must be a single coroutine call) to run concurrently.");
                    }
                    // @TODO: SOL-1108, currently the backend only accepts direct coroutine calls as `spawn{}` arguments.
                    //        Even though this is not a limitation of the semantic analysis here, we're emitting an error so 
                    //        that it can be communicated through language services (error underlining, etc.)
                    else if (AsyncCallAst.GetNodeType() != EAstNodeType::Invoke_Invocation)
                    {
                        AppendGlitch(AsyncCallAst, EDiagnostic::ErrSemantic_Unimplemented,
                            "Non-Coroutine argument. Currently, `spawn` expects a single coroutine call as an argument.");
                    }
                }

                ExprResultType = SpawnExpr->Expr()->GetResultType(*_Program);
            }
            else
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_UnexpectedNumberOfArguments,
                    "Missing argument. `spawn` requires an async argument.");
                ExprResultType = _Program->GetDefaultUnknownType();
            }

            const CTypeBase* ResultType = _Program->InstantiateTaskType(ExprResultType);
            if (!ResultType)
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_AsyncRequiresTaskClass);
                ResultType = _Program->GetDefaultUnknownType();
            }
            SpawnExpr->SetResultType(ResultType);

            return ReplaceMapping(MacroCallAst, Move(SpawnExpr));

        }
    }


    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeMacroCall(const TSRef<CExprMacroCall>& MacroCallAst, const SExprCtx& ExprCtx, const SExprArgs& ExprArgs)
    {
        // Analyze the macro name.
        if (TSPtr<CExpressionBase> NewMacroName = AnalyzeExpressionAst(MacroCallAst->Name(), ExprCtx.WithResultIsCalledAsMacro()))
        {
            MacroCallAst->SetName(Move(NewMacroName.AsRef()));
        }

        STypeTypes NameTypeTypes = MaybeTypeTypes(*MacroCallAst->Name());
        if (NameTypeTypes._Tag == ETypeTypeTag::Type)
        {
            MaybeAppendAttributesNotAllowedError(*MacroCallAst->Name());
            return AnalyzeTypeMacroCall(MacroCallAst, NameTypeTypes._NegativeType, NameTypeTypes._PositiveType, ExprCtx);
        }
        else if (MacroCallAst->Name()->GetNodeType() == Cases<EAstNodeType::Identifier_Function, EAstNodeType::Identifier_OverloadedFunction>)
        {
            // Interpret a macro in the form parametric_type(...){...} as (parametric_type(...)){...}
            if (!ValidateMacroForm<ESimpleMacroForm::m2>(*MacroCallAst))
            {
                return ReplaceNodeWithError(MacroCallAst);
            }

            // Interpret the first, parenthesized clause as the parametric type arguments.
            TArray<CExprMacroCall::CClause> Clauses = MacroCallAst->TakeClauses();
            CExprMacroCall::CClause& ArgumentClause = Clauses[0];
            TSRef<CExpressionBase> Argument = InterpretMacroClauseAsExpression(ArgumentClause, MacroCallAst->GetMappedVstNode());

            TSRef<CExpressionBase> NewMacroName = TSRef<CExprInvocation>::New(
                CExprInvocation::EBracketingStyle::Parentheses,
                MacroCallAst->TakeName(),
                Move(Argument));
            NewMacroName->SetNonReciprocalMappedVstNode(MacroCallAst->GetMappedVstNode());

            SExprArgs MacroNameArgs;
            MacroNameArgs.AnalysisContext = EAnalysisContext::CalleeAlreadyAnalyzed;
            if (TSPtr<CExpressionBase> NewInvocation = AnalyzeInvocation(
                NewMacroName.As<CExprInvocation>(),
                ExprCtx.WithResultIsUsedAsType(),
                MacroNameArgs))
            {
                NewMacroName = NewInvocation.AsRef();
            }
            
            STypeTypes ResultTypes = GetTypeTypes(*NewMacroName);

            MacroCallAst->SetName(Move(NewMacroName));
            MacroCallAst->AppendClause(Move(Clauses[1]));
            return AnalyzeTypeMacroCall(MacroCallAst, ResultTypes._NegativeType, ResultTypes._PositiveType, ExprCtx);
        }
        else if (MacroCallAst->Name()->GetNodeType() == EAstNodeType::Identifier_BuiltInMacro)
        {
            const CSymbol MacroName = static_cast<CExprIdentifierBuiltInMacro&>(*MacroCallAst->Name())._Symbol;
            // The class/struct/interface macros can have attributes on the macro name: class<abstract><transacts>(...){...}
            if      (MacroName == _InnateMacros._class)  { return AnalyzeMacroCall_Class(*MacroCallAst, ExprCtx, ExprArgs, EStructOrClass::Class); }
            else if (MacroName == _InnateMacros._struct) { return AnalyzeMacroCall_Class(*MacroCallAst, ExprCtx, ExprArgs, EStructOrClass::Struct); }
            else if (MacroName == _InnateMacros._interface) { return AnalyzeMacroCall_Interface(*MacroCallAst, ExprCtx, ExprArgs); }
            else if (MacroName == _InnateMacros._enum)   { return AnalyzeMacroCall_Enum(*MacroCallAst, ExprCtx, ExprArgs); }
            else
            {
                // The rest of these macros cannot have attributes on the macro name.
                MaybeAppendAttributesNotAllowedError(*MacroCallAst->Name());

                if      (MacroName == _InnateMacros._array    ) { return AnalyzeMacroCall_Array(*MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._block    ) { return AnalyzeMacroCall_Block(*MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._let      ) { return AnalyzeMacroCall_Let(*MacroCallAst, ExprCtx, ExprArgs); }
                else if (MacroName == _InnateMacros._branch   ) { return AnalyzeMacroCall_Branch(*MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._case     ) { return AnalyzeMacroCall_Case(MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._defer    ) { return AnalyzeMacroCall_Defer(*MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._external ) { return AnalyzeMacroCall_External(*MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._for      ) { return AnalyzeMacroCall_For(MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._loop     ) { return AnalyzeMacroCall_Loop(*MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._map      ) { return AnalyzeMacroCall_Map(*MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._module   ) { return AnalyzeMacroCall_Module(*MacroCallAst, ExprCtx, ExprArgs); }
                else if (MacroName == _InnateMacros._option   ) { return AnalyzeMacroCall_Option(*MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._spawn    ) { return AnalyzeMacroCall_Spawn(*MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._sync     ) { return AnalyzeMacroCall_Sync(MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._rush     ) { return AnalyzeMacroCall_Rush(MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._race     ) { return AnalyzeMacroCall_Race(MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._scoped   ) { return AnalyzeMacroCall_Scoped(*MacroCallAst, ExprCtx, ExprArgs); }
                else if (MacroName == _InnateMacros._type     ) { return AnalyzeMacroCall_Type(*MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._using    ) { return AnalyzeMacroCall_Using(*MacroCallAst, ExprCtx); }
                else if (MacroName == _InnateMacros._profile  ) { return AnalyzeMacroCall_Profile(*MacroCallAst, ExprCtx); }
                else
                {
                    ULANG_ERRORF("Unhandled built-in macro: %s", MacroName.AsCString());
                    ULANG_UNREACHABLE();
                }
            }
        }
        else
        {
            if (!SemanticTypeUtils::IsUnknownType(MacroCallAst->Name()->GetResultType(*_Program)))
            {
                AppendGlitch(*MacroCallAst->Name(), EDiagnostic::ErrSemantic_UnrecognizedMacro, "Macro name must be an identifier");
            }
            return ReplaceNodeWithError(MacroCallAst);
        }
    }

    TSPtr<CExpressionBase> AnalyzeTypeMacroCall(const TSRef<CExprMacroCall>& MacroCallAst, const CTypeBase* NegativeType, const CTypeBase* PositiveType, const SExprCtx& ExprCtx)
    {
        const CNormalType& NegativeNormalType = NegativeType->GetNormalType();
        const CNormalType& PositiveNormalType = PositiveType->GetNormalType();
        if (NegativeNormalType.IsA<CLogicType>() && PositiveNormalType.IsA<CLogicType>())
        {
            return AnalyzeMacroCall_Logic(MacroCallAst, ExprCtx);
        }
        else if (NegativeNormalType.IsA<CClass>() && PositiveNormalType.IsA<CClass>())
        {
            const CClass& NegativeClass = NegativeNormalType.AsChecked<CClass>();
            const CClass& PositiveClass = PositiveNormalType.AsChecked<CClass>();
            if (NegativeClass._NegativeClass == &PositiveClass)
            {
                return AnalyzeMacroCall_InstantiateClass(MacroCallAst, PositiveClass, ExprCtx);
            }
            else
            {
                AppendGlitch(
                    *MacroCallAst->Name(),
                    EDiagnostic::ErrSemantic_Unimplemented,
                    CUTF8String(
                        "Cannot instantiate unknown class bounded below by %s and above by %s.",
                        NegativeType->AsCode().AsCString(),
                        PositiveType->AsCode().AsCString()));
                return ReplaceNodeWithError(MacroCallAst);
            }
        }
        else
        {
            if (!SemanticTypeUtils::IsUnknownType(NegativeType))
            {
                AppendGlitch(
                    *MacroCallAst->Name(),
                    EDiagnostic::ErrSemantic_UnrecognizedMacro,
                    CUTF8String("%s is not a macro.", NegativeType->AsCode().AsCString()));
            }
            else if (!SemanticTypeUtils::IsUnknownType(PositiveType))
            {
                AppendGlitch(
                    *MacroCallAst->Name(),
                    EDiagnostic::ErrSemantic_UnrecognizedMacro,
                    CUTF8String("%s is not a macro.", PositiveType->AsCode().AsCString()));
            }
            return ReplaceNodeWithError(MacroCallAst);
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Block(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        else if (!_Context._Scope->IsControlScope() && _Context._Scope->GetKind() != CScope::EKind::Class)
        {
            AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_InvalidContextForBlock);
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        else
        {
            const bool bIsClassBlockClause = _Context._Scope->GetKind() == CScope::EKind::Class;

            // Analyze the macro clause as a code block.
            // If this is in a class 'block' clause, we need to defer the analysis until analyzing all
            // the non-local function, type, and module and member variable definitions.
            TSPtr<CExprCodeBlock> CodeBlockAst;
            {
                TGuardValue<const CExprCodeBlock*> ClassBlockClauseGuard(_Context._ClassBlockClause, bIsClassBlockClause ? CodeBlockAst.Get() : nullptr);
                CodeBlockAst = AnalyzeMacroClauseAsCodeBlock(MacroCallAst.Clauses()[0], MacroCallAst.GetMappedVstNode(), ExprCtx);
            }

            // If this 'block' macro is directly in a class body, add it to the class's list of 'block' clauses.
            if (bIsClassBlockClause)
            {
                CClass* Class = static_cast<CClass*>(_Context._Scope);

                if(Class->IsStruct())
                {
                    // 'block' clauses are disallowed on structs
                    AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_InvalidContextForBlock);
                    return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
                }
            }

            return ReplaceMapping(MacroCallAst, Move(CodeBlockAst.AsRef()));
        }
    }

    TSRef<CExpressionBase> AnalyzeMacroCall_Let(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx, const SExprArgs& ExprArgs)
    {
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        if (ExprArgs.ArchetypeInstantiationContext != ArchetypeInstantiationArgument)
        {
            AppendGlitch(
                MacroCallAst,
                EDiagnostic::ErrSemantic_Unimplemented,
                "`let` is currently only supported as argument to an archetype instantiation.");
        }
        CExprMacroCall::CClause& Clause = MacroCallAst.Clauses()[0];
        int32_t NumExprs = Clause.Exprs().Num();
        if (NumExprs > 1 && Clause.Form() == Vst::Clause::EForm::NoSemicolonOrNewline)
        {
            AppendGlitch(
                MacroCallAst,
                EDiagnostic::ErrSemantic_Unsupported,
                "Definitions inside `let` should be separated by semicolons or newlines.");
        }
        TSRef<CExprLet> Result = TSRef<CExprLet>::New(NumExprs);
        // Note, no control scope is added.  The archetype instantiation will
        // add a control scope.  Any `let`-bound names are available within the
        // archetype instantiation.
        for (TSRef<CExpressionBase>& Expr : Clause.Exprs())
        {
            if (Expr->GetNodeType() == EAstNodeType::Definition)
            {
                CExprDefinition& Definition = static_cast<CExprDefinition&>(*Expr);
                SDefinitionElementAnalysis ElementAnalysis = TryAnalyzeDefinitionLhs(Definition, /*bNameCanHaveAttributes=*/true);
                if (TSPtr<CExpressionBase> NewExpr = AnalyzeDefinition(Definition, ElementAnalysis, ExprCtx.WithResultIsIgnored()))
                {
                    Expr = Move(NewExpr.AsRef());
                }
                Result->AppendSubExpr(Move(Expr));
            }
            else
            {
                AppendExpectedDefinitionError(*Expr);
            }
        }
        return ReplaceMapping(MacroCallAst, Move(Result));
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Defer(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        // Validate that the macro is the right form.
        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        // Create the defer AST node.
        TSRef<CExprDefer> DeferAst = TSRef<CExprDefer>::New();
        MacroCallAst.GetMappedVstNode()->AddMapping(DeferAst);
        DeferAst->SetResultType(&_Program->_trueType);

        TGuardValue<const CExprDefer*> DeferGuard(_Context._Defer, DeferAst.Get());
        TGuardValue<const CExpressionBase*> BreakableGuard(_Context._Breakable, DeferAst.Get());

        // Analyze the macro clause as a code block, and set it as the defer body.
        // Do not pass on failure context since a fail may not cross the `defer` boundary.
        // Also do not allow the async effect in defer.
        SEffectSet BodyAllowedEffects = ExprCtx.AllowedEffects;
        BodyAllowedEffects &= ~(EEffect::decides | EEffect::suspends);
        DeferAst->SetExpr(AnalyzeMacroClauseAsCodeBlock(MacroCallAst.Clauses()[0], DeferAst->GetMappedVstNode(), ExprCtx.With(BodyAllowedEffects).WithResultIsIgnored()));

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Ensure correct defer semantics

        if (static_cast<CExprCodeBlock*>(DeferAst->Expr().Get())->IsEmpty())
        {
            AppendGlitch(*DeferAst, EDiagnostic::WarnSemantic_EmptyBlock, "Expected one or more expressions in the `defer` block but it is empty.");
        }

        // Ensure defer is called as a statement rather than an expression - generally within a code block: routine, do, if then/else, for, loop, branch, spawn and not as the last expression
        if (ExprCtx.ResultContext != ResultIsIgnored)
        {
            AppendGlitch(*DeferAst, EDiagnostic::ErrSemantic_DeferLocation);
        }

        return Move(DeferAst);
    }

    
    //-------------------------------------------------------------------------------------------------
    template<typename NodeType, typename IteratedNodeType>
    TSRef<CExpressionBase> AnalyzePossiblyIteratedConcurrentMacroCall(const TSRef<CExprMacroCall>& MacroCallAst, const char* MacroName, const CSymbol& ScopeName, const SExprCtx& ExprCtx)
    {
        if (!ValidateMacroForm<ESimpleMacroForm::m1m2, EMacroClauseTag::None>(*MacroCallAst))
        {
            return ReplaceNodeWithError(MacroCallAst);
        }
        else
        {
            RequireEffects(*MacroCallAst, EEffect::suspends, ExprCtx.AllowedEffects, MacroName);

            if (MacroCallAst->Clauses().Num() == 1)
            {
                // 1 clause block indicates non-iterating form - e.g. sync {_expr()}
                TSRef<NodeType> ResultAst = TSRef<NodeType>::New();
                AnalyzeConcurrentBlock(*MacroCallAst, ResultAst, MacroName, ExprCtx);
                return ReplaceMapping(*MacroCallAst, Move(ResultAst));
            }
            else
            {
                // 2 clause blocks indicates iterating form - e.g. sync(item:container) {item._expr()}
                // This is not yet supported by the code generator, SOL-5706.
                AppendGlitch(
                    *MacroCallAst,
                    EDiagnostic::ErrSemantic_Unsupported,
                    "Concurrent macro with iterator is not currently supported.");
                return ReplaceNodeWithError(MacroCallAst);
            }
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Sync(const TSRef<CExprMacroCall>& MacroCallAst, const SExprCtx& ExprCtx)
    {
        return AnalyzePossiblyIteratedConcurrentMacroCall<CExprSync, CExprSyncIterated>(MacroCallAst, "'sync' macro", _InnateMacros._sync, ExprCtx);
    }
    
    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Rush(const TSRef<CExprMacroCall>& MacroCallAst, const SExprCtx& ExprCtx)
    {
        if (_Context._Loop != nullptr)
        {
            AppendGlitch(
                *MacroCallAst,
                EDiagnostic::ErrSemantic_Unsupported,
                "'rush' is not currently supported in loops.");

            return ReplaceMapping(*MacroCallAst, TSRef<CExprError>::New());
        }

        return AnalyzePossiblyIteratedConcurrentMacroCall<CExprRush, CExprRushIterated>(MacroCallAst, "'rush' macro", _InnateMacros._rush, ExprCtx);
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Race(const TSRef<CExprMacroCall>& MacroCallAst, const SExprCtx& ExprCtx)
    {
        return AnalyzePossiblyIteratedConcurrentMacroCall<CExprRace, CExprRaceIterated>(MacroCallAst, "'race' macro", _InnateMacros._race, ExprCtx);
    }

    //-------------------------------------------------------------------------------------------------
    TSRef<CExpressionBase> AnalyzeMacroCall_Branch(CExprMacroCall& MacroCallAst, const SExprCtx& ExprCtx)
    {
        if (_Context._Loop != nullptr)
        {
            AppendGlitch(
                MacroCallAst,
                EDiagnostic::ErrSemantic_Unsupported,
                "'branch' is not currently supported in loops.");

            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }

        if (!ValidateMacroForm<ESimpleMacroForm::m1>(MacroCallAst))
        {
            return ReplaceMapping(MacroCallAst, TSRef<CExprError>::New());
        }
        else
        {
            TSRef<CExprBranch> ResultAst = TSRef<CExprBranch>::New();
            ResultAst->SetResultType(&_Program->_anyType);

            // Ensure `branch` is used within an async context - it can still be immediate though it must be within a coroutine (or in a `spawn` body when that is supported)
            if ((_Context._Function == nullptr) || !_Context._Function->_Signature.GetEffects()[EEffect::suspends])
            {
                AppendGlitch(MacroCallAst, EDiagnostic::ErrSemantic_ExpectedCoroutine);
            }

            AnalyzeConcurrentExpr(MacroCallAst, ResultAst, ExprCtx);
            return ReplaceMapping(MacroCallAst, Move(ResultAst));
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeDefinitionExpression(const TSRef<CExpressionBase>& ExpressionAst, const SExprCtx& ExprCtx)
    {
        TGuardValue<const Vst::Node*> VstNodeGuard(_Context._VstNode, ExpressionAst->GetMappedVstNode() ? ExpressionAst->GetMappedVstNode() : _Context._VstNode);

        // Handle definitions.
        if (ExpressionAst->GetNodeType() == EAstNodeType::Definition)
        {
            CExprDefinition& DefinitionAst = static_cast<CExprDefinition&>(*ExpressionAst);
            SDefinitionElementAnalysis ElementAnalysis = TryAnalyzeDefinitionLhs(DefinitionAst, /*bNameCanHaveAttributes=*/true);
            return AnalyzeDefinition(DefinitionAst, ElementAnalysis, ExprCtx);
        }

        // Handle using and block macros.
        if (ExpressionAst->GetNodeType() == EAstNodeType::MacroCall)
        {
            CExprMacroCall& MacroCallAst = static_cast<CExprMacroCall&>(*ExpressionAst);
            if (MacroCallAst.Name()->GetNodeType() == EAstNodeType::Identifier_Unresolved)
            {
                const CSymbol MacroName = static_cast<CExprIdentifierUnresolved&>(*MacroCallAst.Name())._Symbol;
                if (MacroName == _InnateMacros._using
                ||  MacroName == _InnateMacros._block) { return AnalyzeExpressionAst(ExpressionAst, ExprCtx); }
            }
        }

        // If it wasn't a definition or a using, produce an error.
        AppendExpectedDefinitionError(*ExpressionAst);
        return ReplaceMapping(*ExpressionAst, TSRef<CExprError>::New());
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeDefinition(CExprDefinition& DefinitionAst, const SExprCtx& ExprCtx)
    {
        SDefinitionElementAnalysis ElementAnalysis = TryAnalyzeDefinitionLhs(DefinitionAst, /*bNameCanHaveAttributes=*/true);

        if (ElementAnalysis.AnalysisResult == EDefinitionElementAnalysisResult::Definition && ExprCtx.ResultContext != EResultContext::ResultIsUsedAsQualifier)
        {
            return AnalyzeDefinition(DefinitionAst, ElementAnalysis, ExprCtx);
        }
        else
        {
            if (ExprCtx.ResultContext == EResultContext::ResultIsUsedAsQualifier)
            {
                AppendGlitch(
                    DefinitionAst,
                    EDiagnostic::ErrSemantic_InvalidQualifier,
                    CUTF8String("Illegal qualifier, if the intention is to qualify with a module, use (M.c:) instead of (M:c:)"));
            }
            else
            {
                AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_LhsNotDefineable);
            }
            TSRef<CExprError> ErrorNode = TSRef<CExprError>::New();
            ErrorNode->AppendChild(DefinitionAst.TakeElement());
            if (DefinitionAst.ValueDomain()) { ErrorNode->AppendChild(DefinitionAst.TakeValueDomain()); }
            if (DefinitionAst.Value()) { ErrorNode->AppendChild(DefinitionAst.TakeValue()); }
            return ReplaceMapping(DefinitionAst, Move(ErrorNode));
        }
    }

    SQualifier AnalyzeQualifier(TSPtr<CExpressionBase> Qualifier, CExpressionBase& ExpressionForGlitch, const SExprCtx& ExprCtx, const SExprArgs& ExprArgs = SExprArgs())
    {
        if (Qualifier)
        {
            // We cannot analyze qualified identifiers before we've completed analysis of type definitions since we cannot determine if the type being referred to exists.
            // If we are in a `using` macro, the type definitions would not have been analyzed yet at this point, so we relax this requirement.
            if (_CurrentTaskPhase < Deferred_Type && ExprArgs.AnalysisContext != EAnalysisContext::IsInUsingExpression)
            {
                AppendGlitch(ExpressionForGlitch, EDiagnostic::ErrSemantic_InvalidQualifier, "A qualified identifier was unable to be resolved.");
                return SQualifier::Unknown();
            }

            if (TSPtr<CExpressionBase> AnalyzedQualifier = AnalyzeExpressionAst(Qualifier.AsRef(), ExprCtx.WithResultIsUsedAsQualifier(), ExprArgs))
            {
                Qualifier = AnalyzedQualifier;
            }

            // if the qualifier evaluated to a type, try to interpret it as a CNominalType
            const STypeType QualifierTypeType = MaybeTypePositiveType(*Qualifier);
            if (QualifierTypeType._Tag == ETypeTypesTag::Type)
            {
                ULANG_ENSUREF(QualifierTypeType._Type, "Invalid qualifier type!");
                const CNominalType* QualifierNominalType = QualifierTypeType._Type->GetNormalType().AsNominalType();
                if (QualifierNominalType)
                {
                    return SQualifier::NominalType(*QualifierNominalType);;
                }
                else
                {
                    AppendGlitch(ExpressionForGlitch, EDiagnostic::ErrSemantic_InvalidQualifier, "Qualifier does not refer to a scope.");
                }
            }
            else if (QualifierTypeType._Tag == ETypeTypesTag::NotType)
            {
                if (Qualifier->GetNodeType() == EAstNodeType::Identifier_Module)
                {
                    const CModule* Module = static_cast<const CExprIdentifierModule&>(*Qualifier).GetModule(*_Program);
                    if (ULANG_ENSUREF(Module, "Could not determine the module referred to by this qualifier."))
                    {
                        return SQualifier::NominalType(*Module);
                    }
                }
                else if (Qualifier->GetNodeType() == EAstNodeType::Identifier_ModuleAlias)
                {
                    const CModule* Module = static_cast<const CExprIdentifierModuleAlias&>(*Qualifier)._ModuleAlias.Module();
                    if (Module)
                    {
                        return SQualifier::NominalType(*Module);
                    }
                    else
                    {
                        AppendGlitch(ExpressionForGlitch, EDiagnostic::ErrSemantic_InvalidQualifier, "Could not determine the aliased module referred to by this qualifier.");
                    }
                }
                else if (Qualifier->GetNodeType() == EAstNodeType::Literal_Path)
                {
                    CExprPath& Path = static_cast<CExprPath&>(*Qualifier);
                    if (const CLogicalScope* LogicalScope = ResolvePathToLogicalScope(Path._Path, Path))
                    {
                        if (const CTypeBase* ScopeAsType = LogicalScope->ScopeAsType())
                        {
                            if (const CNominalType* ResultType = ScopeAsType->GetNormalType().AsNominalType())
                            {
                                return SQualifier::NominalType(*ResultType);
                            }
                        }
                    }
                }
                else if (Qualifier->GetNodeType() == EAstNodeType::Identifier_Local)
                {
                    return SQualifier::Local();
                }
                else
                {
                    AppendGlitch(ExpressionForGlitch, EDiagnostic::ErrSemantic_InvalidQualifier, "Could not determine the type referred to by this qualifier.");
                }
            }
        }
        return SQualifier::Unknown();
    }

    //-------------------------------------------------------------------------------------------------
    void AnalyzeDefinitionQualifier(const TSPtr<CExpressionBase>& Qualifier, CDefinition& Definition, CExprDefinition& DefinitionAst, const SExprCtx& ExprCtx)
    {
        if (!Qualifier)
        {
            return;
        }
        const SQualifier QualifierType = AnalyzeQualifier(Qualifier, DefinitionAst, ExprCtx);
        Definition._Qualifier = QualifierType;
        switch (QualifierType._Type)
        {
            case SQualifier::EType::NominalType:
                EnqueueDeferredTask(Deferred_ValidateType,
                    [this, &Definition, &DefinitionAst]()
                    {
                        const CNominalType* QualifiedType = Definition._Qualifier._Definition;
                        const CScope* ParentScope = &Definition._EnclosingScope;
                        if (ParentScope->IsModuleOrSnippet())
                        {
                            if (QualifiedType != ParentScope->GetModule()->ScopeAsType())
                            {
                                AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_InvalidQualifier, CUTF8String("Qualifier on definition in module must be the module itself."));
                            }
                        }
                        else if (const CTypeBase* EnclosingType = ParentScope->ScopeAsType(); EnclosingType && !IsSubtype(EnclosingType, QualifiedType))
                        {
                            AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_InvalidQualifier,
                                CUTF8String("Qualifier on definition is invalid, `%s` doesn't inherit from or implement `%s`", EnclosingType->AsCode().AsCString(), Definition._Qualifier._Definition->AsCode().AsCString()));
                        }
                        // NOTE: (yiliang.siew) For qualifiers that refer to a class, we check if that class actually has a definition that matches.
                        else if (QualifiedType->IsA<CClassDefinition>())
                        {
                            const CClassDefinition& ClassDefinition = QualifiedType->AsChecked<CClassDefinition>();
                            const SmallDefinitionArray DefinitionsFound = ClassDefinition.FindDefinitions(Definition.GetName());
                            if (DefinitionsFound.Num() == 0)
                            {
                                AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_InvalidQualifier,
                                    CUTF8String("Qualifier on definition is invalid; the class `%s` doesn't define `%s`.", ClassDefinition.GetName().AsCString(), Definition.GetName().AsCString()));
                            }
                        }
                    });
                break;
            case SQualifier::EType::Local:
                EnqueueDeferredTask(Deferred_ValidateType,
                    [this, &Definition, &DefinitionAst]()
                    {
                        if (!Definition._EnclosingScope.GetScopeOfKind(CScope::EKind::Function))
                        {
                            AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_InvalidQualifier, CUTF8String("The (%s:) qualifier should only be used on identifiers within functions.", _LocalName.AsCString()));
                        }
                    });
                break;
            case SQualifier::EType::Unknown:
                // Not a qualifier, nothing to do.
                return;
            default:
                ULANG_UNREACHABLE();
                break;
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeDefinition(CExprDefinition& DefinitionAst, SDefinitionElementAnalysis ElementAnalysis, const SExprCtx& ExprCtx)
    {
        if (!DefinitionAst.Element())
        {
            return ReplaceMapping(DefinitionAst, TSRef<CExprError>::New());
        }

        if (ElementAnalysis.AnalysisResult != EDefinitionElementAnalysisResult::Definition)
        {
            AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_LhsNotDefineable);
            TSRef<CExprError> ErrorNode = TSRef<CExprError>::New();
            ErrorNode->AppendChild(DefinitionAst.TakeElement());
            if (DefinitionAst.ValueDomain()) { ErrorNode->AppendChild(DefinitionAst.TakeValueDomain()); }
            if (DefinitionAst.Value()) { ErrorNode->AppendChild(DefinitionAst.TakeValue()); }
            return ReplaceMapping(DefinitionAst, Move(ErrorNode));
        }

        if (ElementAnalysis.InvocationAst)
        {
            return AnalyzeFunctionDefinition(DefinitionAst, ElementAnalysis, ExprCtx);
        }

        if (DefinitionAst.Value())
        {
            if (DefinitionAst.Value()->GetNodeType() == Cases<
                EAstNodeType::Invoke_ArrayFormer,
                EAstNodeType::Invoke_GeneratorFormer,
                EAstNodeType::Invoke_MapFormer,
                EAstNodeType::Invoke_OptionFormer,
                EAstNodeType::Invoke_Subtype,
                EAstNodeType::Invoke_TupleType,
                EAstNodeType::Invoke_Arrow>)
            {
                return AnalyzeTypeDefinition(DefinitionAst, ElementAnalysis, ExprCtx);
            }
            else if (DefinitionAst.Value()->GetNodeType() == EAstNodeType::Invoke_Invocation)
            {
                // @jira SOL-1651 : We should probably remove CExprSubtype
                const CExprInvocation& Invocation = static_cast<const CExprInvocation&>(*DefinitionAst.Value());
                if (IsIdentifierSymbol(*Invocation.GetCallee(), _Symbol_subtype)
                    || IsIdentifierSymbol(*Invocation.GetCallee(), _Symbol_tuple)
                    || (IsIdentifierSymbol(*Invocation.GetCallee(), _Symbol_generator) && VerseFN::UploadedAtFNVersion::EnableGenerators(_Context._Package->_UploadedAtFNVersion)))
                {
                    return AnalyzeTypeDefinition(DefinitionAst, ElementAnalysis, ExprCtx);
                }
                if (IsIdentifierSymbol(*Invocation.GetCallee(), _Symbol_import))
                {
                    return AnalyzeImport(DefinitionAst, ElementAnalysis, ExprCtx);
                }
            }
            else if (DefinitionAst.Value()->GetNodeType() == EAstNodeType::MacroCall)
            {
                const CExprMacroCall& MacroCallAst = *DefinitionAst.Value().As<CExprMacroCall>();;
                if (MacroCallAst.Name()->GetNodeType() == EAstNodeType::Identifier_Unresolved)
                {
                    const CExprIdentifierUnresolved& MacroNameId = *MacroCallAst.Name().As<CExprIdentifierUnresolved>();
                    if (!MacroNameId.Context() && !MacroNameId.Qualifier())
                    {
                        if(MacroNameId._Symbol == _InnateMacros._enum
                        || MacroNameId._Symbol == _InnateMacros._interface
                        || MacroNameId._Symbol == _InnateMacros._class
                        || MacroNameId._Symbol == _InnateMacros._module
                        || MacroNameId._Symbol == _InnateMacros._struct
                        || MacroNameId._Symbol == _InnateMacros._type
                        || MacroNameId._Symbol == _InnateMacros._scoped)
                        {
                            return AnalyzeTypeDefinition(DefinitionAst, ElementAnalysis, ExprCtx);
                        }
                    }
                }
            }
        }

        return AnalyzeDataDefinition(DefinitionAst, ElementAnalysis, ExprCtx);
    }

    const CClass* AsModuleScopedVarWeakMapKeyType(const CTypeBase& Type)
    {
        const CClass* Class = Type.GetNormalType().AsNullable<CClass>();
        if (!Class)
        {
            return nullptr;
        }
        if (!Class->_Definition->_EffectAttributable.HasAttributeClass(_Program->_moduleScopedVarWeakMapKeyClass, *_Program))
        {
            return nullptr;
        }
        return Class;
    }

    void ValidateModuleScopedVarDef(const TSRef<CExprDataDefinition>& DataDefAst)
    {
        const TSRef<CDataDefinition>& DataDefinition = DataDefAst->_DataMember;
        const CFlowType& KeyType = _Program->CreatePositiveFlowType();
        const CFlowType& NegativeKeyType = _Program->CreateNegativeFlowType();
        KeyType.AddFlowEdge(&NegativeKeyType);
        NegativeKeyType.AddFlowEdge(&KeyType);
        const CFlowType& ValueType = _Program->CreatePositiveFlowType();
        const CFlowType& NegativeValueType = _Program->CreateNegativeFlowType();
        ValueType.AddFlowEdge(&NegativeValueType);
        NegativeValueType.AddFlowEdge(&ValueType);
        const CMapType& WeakMapType = _Program->GetOrCreateWeakMapType(
            KeyType,
            ValueType);
        const CMapType& NegativeWeakMapType = _Program->GetOrCreateWeakMapType(
            NegativeKeyType,
            NegativeValueType);
        const CPointerType& PointerType = _Program->GetOrCreatePointerType(
            &NegativeWeakMapType,
            &WeakMapType);
        const CPointerType& NegativePointerType = _Program->GetOrCreatePointerType(
            &WeakMapType,
            &NegativeWeakMapType);
        if (!Constrain(DataDefinition->GetType(), &NegativePointerType)
         || !Constrain(&PointerType, DataDefinition->_NegativeType))
        {
            AppendGlitch(
                *DataDefAst,
                EDiagnostic::ErrSemantic_Unimplemented,
                "Module-scoped `var` must have `weak_map` type.");
        }
        else if (const CClass* KeyClass = AsModuleScopedVarWeakMapKeyType(KeyType))
        {
            if (KeyClass->IsPersistent())
            {
                if (!Constrain(&ValueType, &_Program->_persistableType))
                {
                    AppendGlitch(
                        *DataDefAst,
                        EDiagnostic::ErrSemantic_Unimplemented,
                        "Persistent `var` `weak_map` type must have `persistable` value type.  "
                        "`persistable` types include primitive types; array, map, and option types "
                        "made up of `persistable` types;  `class`es defined as `class<persistable>`; "
                        "and `struct`s defined as `struct<persistable>`.");
                }
                CAstPackage* Package = GetPackage(*DataDefinition);
                ULANG_ASSERTF(Package != nullptr, "Package was null for data definition");

                if (const CClass* Class = ValueType.GetChild()->GetNormalType().AsNullable<CClass>())
                {
                    AssertConstrain(&ValueType, Class->_NegativeClass);
                    if (Package->_Role == EPackageRole::Source && !Class->IsStruct())
                    {
                        _HasPersistentClass = true;
                    }
                }

                if (Package->_Role == EPackageRole::Source)
                {
                    Package->_NumPersistentVars++;
                }

                EnqueueDeferredTask(Deferred_NonFunctionExpressions, [this, DataDefAst, Package]
                {
                    if (!_HasPersistentClass && Package->_NumPersistentVars == _BuildParams._MaxNumPersistentVars)
                    {
                        AppendGlitch(
                            *DataDefAst,
                            EDiagnostic::ErrSemantic_Unimplemented,
                            CUTF8String("If at the limit of allowed persistent `var`s, at least one persistent `var`'s `weak_map` value must be a `class`."));
                    }

                    if (Package->_NumPersistentVars > _BuildParams._MaxNumPersistentVars)
                    {
                        AppendGlitch(
                            *DataDefAst,
                            EDiagnostic::ErrSemantic_Unimplemented,
                            CUTF8String("Only %i persistent `var`s allowed.", _BuildParams._MaxNumPersistentVars));
                    }
                });
                DataDefinition->MarkPersistenceCompatConstraint();
                _Diagnostics->AppendPersistentWeakMap();
            }
        }
        else
        {
            AppendGlitch(
                *DataDefAst,
                EDiagnostic::ErrSemantic_Unimplemented,
                // Too specific (just anything with `module_scoped_var_weak_map_key`
                // specifier is sufficient), but it improves the error message for
                // all current uses.
                "Module-scoped `var` `weak_map` type must have `session` or `player` key type.");
        }
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeDataDefinition(CExprDefinition& DefinitionAst, const SDefinitionElementAnalysis& ElementAnalysis, const SExprCtx& ExprCtx)
    {
        ULANG_ASSERTF(!ElementAnalysis.InvocationAst, "Expected function definition to be handled by caller");

        if (_Context._Scope->GetKind() == CScope::EKind::Interface)
        {
            // Don't allow data definitions in interfaces.
            AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_OnlyFunctionsInInterfaceBody);
            return TSRef<CExprError>::New();
        }
        else if (ElementAnalysis.IdentifierAst->Context())
        {
            // Don't allow extension data members.
            AppendGlitch(
                *ElementAnalysis.IdentifierAst,
                EDiagnostic::ErrSemantic_Unimplemented,
                "Data member extension fields are not yet implemented.");
            return TSRef<CExprError>::New();
        }

        ValidateDefinitionIdentifier(*ElementAnalysis.IdentifierAst, *_Context._Scope);

        const CSymbol VarName = ElementAnalysis.IdentifierAst->_Symbol;

        TSRef<CDataDefinition> DataDefinition = _Context._Scope->CreateDataDefinition(VarName);

        if (ElementAnalysis.VarAst != nullptr)
        {
            DataDefinition->SetIsVar();
        }

        TGuardValue<TArray<const CDefinition*>> DefinitionsGuard(_Context._EnclosingDefinitions);
        _Context._EnclosingDefinitions.Add(DataDefinition.Get());

        _Context._Scope->CreateNegativeDataDefinition(*DataDefinition);

        TArray<SAttribute> NameAttributes = Move(ElementAnalysis.IdentifierAst->_Attributes);
        TArray<SAttribute> DefAttributes = Move(DefinitionAst._Attributes);

        // Transform the CExprDefinition to a CExprDataDefinition.
        TSRef<CExprDataDefinition> DataDefAst = TSRef<CExprDataDefinition>::New(
            DataDefinition,
            DefinitionAst.TakeElement(),
            DefinitionAst.TakeValueDomain(),
            MoveIfPossible(DefinitionAst.TakeValue()));
        if (const Verse::Vst::Node* DefinitionVst = DefinitionAst.GetMappedVstNode())
        {
            DefinitionVst->AddMapping(DataDefAst.Get());
        }

        if (DataDefinition->IsVar())
        {
            if (_Context._Scope->GetLogicalScope().GetKind() == CScope::EKind::Module)
            {
                EnqueueDeferredTask(Deferred_ValidateAttributes, [this, DataDefAst]
                {
                    ValidateModuleScopedVarDef(DataDefAst);
                });
            }
            else
            {
                // <varies> implies <allocates> now, so we don't need to version the use of <allocates> here to cover
                //  Verse version V0 -> V1
                RequireEffects(*DataDefAst, EEffect::allocates, ExprCtx.AllowedEffects, "mutable data definition");
                if (_Context._Scope->GetKind() == CScope::EKind::Class)
                {
                    const CClass* Class = static_cast<const CClass*>(_Context._Scope);
                    if (Class->GetParentScope()->GetKind() == CScope::EKind::Function)
                    {
                        AppendGlitch(
                            *DataDefAst,
                            EDiagnostic::ErrSemantic_Unimplemented,
                            "Mutable members in parametric classes are not yet implemented.");
                    }
                }
            }
        }

        DeferredRequireOverrideDoesntChangeAccessLevel(DataDefAst.As<CExpressionBase>(), *DataDefinition);

        // Analyze the explicit qualifier
        EnqueueDeferredTask(Deferred_Type, [this, DataDefinition, DataDefAst, ElementAnalysis, ExprCtx]()
        {
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &DataDefinition->_EnclosingScope);

            AnalyzeDefinitionQualifier(
                ElementAnalysis.IdentifierAst->Qualifier(),
                *DataDefinition,
                *DataDefAst,
                ExprCtx);
        });

        // Require that the data definition doesn't shadow any other definitions.
        RequireUnambiguousDefinition(*DataDefinition, "data definition");
        
        // Analyze the data's type expression in a deferred task (if it has one).
        if (DataDefAst->ValueDomain())
        {
            auto AnalyzeType = [this, DataDefAst, ExprCtx]
            {
                TGuardValue<CScope*> ScopeGuard(_Context._Scope, &DataDefAst->_DataMember->_EnclosingScope);
                if (TSPtr<CExpressionBase> NewTypeAst = AnalyzeExpressionAst(DataDefAst->ValueDomain().AsRef(), ExprCtx.WithResultIsUsedAsType()))
                {
                    DataDefAst->SetValueDomain(MoveIfPossible(NewTypeAst.AsRef()));
                }

                const TSPtr<CExpressionBase>& TypeAst = DataDefAst->ValueDomain();
                STypeTypes DataTypes = GetTypeTypes(*TypeAst);
                const CTypeBase* NegativeDataType = DataTypes._NegativeType;
                const CTypeBase* PositiveDataType = DataTypes._PositiveType;
                ValidateNonAttributeType(PositiveDataType, TypeAst->GetMappedVstNode());
                if (DataDefAst->_DataMember->IsVar())
                {
                    const CPointerType& NegativePointerType = _Program->GetOrCreatePointerType(PositiveDataType, NegativeDataType);
                    const CPointerType& PositivePointerType = _Program->GetOrCreatePointerType(NegativeDataType, PositiveDataType);
                    const CReferenceType& PositiveReferenceType = _Program->GetOrCreateReferenceType(NegativeDataType, PositiveDataType);
                    DataDefAst->_DataMember->_NegativeType = &NegativePointerType;
                    DataDefAst->_DataMember->SetType(&PositivePointerType);
                    DataDefAst->SetResultType(PositiveDataType);
                    DataDefAst->Element()->SetResultType(&PositiveReferenceType);
                    ULANG_ASSERTF(
                        DataDefAst->Element()->GetNodeType() == EAstNodeType::Definition_Var,
                        "When defining mutable variable data, the left-hand side must be a variable definition node.");
                    DataDefAst->Element().As<CExprVar>()->Operand()->SetResultType(&PositivePointerType);
                }
                else
                {
                    // When a value domain is present, it determines the type of the definition.
                    // e.g. x:int       # this is an int
                    //      y:float=10  # this is a float
                    DataDefAst->_DataMember->_NegativeType = NegativeDataType;
                    DataDefAst->_DataMember->SetType(PositiveDataType);
                    DataDefAst->SetResultType(PositiveDataType);
                    DataDefAst->Element()->SetResultType(PositiveDataType);
                }
            };
            EnqueueDeferredTask(Deferred_Type, Move(AnalyzeType));
        }
        else if (ElementAnalysis.VarAst)
        {
            AppendGlitch(*DataDefAst, EDiagnostic::ErrSemantic_MutableMissingType);
            DataDefAst->_DataMember->_NegativeType = &_Program->GetOrCreatePointerType(
                &_Program->_falseType,
                &_Program->_anyType);
            DataDefAst->_DataMember->SetType(&_Program->GetOrCreatePointerType(
                &_Program->_anyType,
                &_Program->_falseType));
        }
        else
        {
            // only locals may use type inference
            if (!_Context._Scope->IsControlScope())
            {
                AppendGlitch(*DataDefAst,
                    EDiagnostic::ErrSemantic_ExpectedType,
                    CUTF8String("Data definition %s at %s scope must specify a value domain.", VarName.AsCString(), CScope::KindToCString(_Context._Scope->GetKind())));
                const CTypeBase* DataType = _Program->GetDefaultUnknownType();
                DataDefAst->_DataMember->_NegativeType = DataType;
                DataDefAst->_DataMember->SetType(DataType);
                DataDefAst->SetResultType(DataType);
                DataDefAst->Element()->SetResultType(DataType);
                DataDefAst->SetValueDomain(TSRef<CExprError>::New());
            }
        }

        //
        // Analyze the data's (right-hand-side) value expression (if it has one).
        if (DataDefAst->Value())
        {
            DataDefinition->SetHasInitializer();

            SContext SavedContext = _Context;
            auto AnalyzeValueExpression = [this, DataDefAst, ExprCtx]
            {
                TGuardValue<CScope*> ScopeGuard(_Context._Scope, &DataDefAst->_DataMember->_EnclosingScope);
                _Context._DataMembers.Push(DataDefAst->_DataMember.Get());
                TGuard DataMembersGuard([this] { _Context._DataMembers.Pop(); });

                const CTypeBase* DesiredValueType = DataDefAst->_DataMember->_NegativeType;
                if (DataDefAst->_DataMember->IsVar())
                {
                    DesiredValueType = DataDefAst->_DataMember->GetType()->GetNormalType().AsChecked<CPointerType>().NegativeValueType();
                }

                {
                    SExprCtx Ctx{ExprCtx};
                    if (DataDefAst->_DataMember->_OptionalAccessors)
                    {
                        // optional accessors must be initialized with `= external{}` regardless
                        // of package role
                        Ctx.bAllowExternalMacroCallInNonExternalRole = true;
                    }
                    // Analyze the value expression.
                    if (TSPtr<CExpressionBase> NewValueAst = AnalyzeExpressionAst(DataDefAst->Value().AsRef(), Ctx.WithResultIsUsed(DesiredValueType)))
                    {
                        DataDefAst->SetValue(Move(NewValueAst.AsRef()));
                    }
                }

                if (_Context._Scope->GetPackage()->_Role == EPackageRole::External)
                {
                    // If this package is external the value of the definition must be a single external{} macro
                    if (DataDefAst->Value()->GetNodeType() != EAstNodeType::External)
                    {
                        AppendGlitch(*DataDefAst, EDiagnostic::ErrSemantic_ExpectedExternal);
                    }
                }
                else
                {
                    const CTypeBase* ValueType = DataDefAst->Value()->GetResultType(*_Program);

                    // If the data member is a unique pointer, wrap the value in a CExprNewPointer.
                    if (DataDefAst->_DataMember->IsVar())
                    {
                        DataDefAst->SetValue(TSRef<CExprNewPointer>::New(
                            static_cast<const CPointerType*>(ValueType),
                            TSRef<CExpressionBase>(DataDefAst->Value().AsRef())));
                    }

                    if (DesiredValueType == nullptr)
                    {
                        // When no desired type is specified, the definition's type is the rhs's type.
                        // e.g. x=5
                        //      msg="Hello, World"
                        CTypeBase const* RhsType = DataDefAst->Value()->GetResultType(*_Program);
                        DataDefAst->_DataMember->_NegativeType = &_Program->_anyType;
                        DataDefAst->_DataMember->SetType(RhsType);
                        DataDefAst->SetResultType(RhsType);
                        DataDefAst->Element()->SetResultType(RhsType);
                    }
                    else if (TSPtr<CExpressionBase> NewValue = ApplyTypeToExpression(
                        *DesiredValueType,
                        DataDefAst->Value().AsRef(),
                        EDiagnostic::ErrSemantic_IncompatibleArgument,
                        "This variable expects to be initialized with", "this initializer"))
                    {
                        DataDefAst->SetValue(Move(NewValue.AsRef()));
                    }

                    // If this data definition is outside of a function, validate the control flow in
                    // its default value expression.
                    if (!_Context._Function)
                    {
                        ValidateControlFlow(DataDefAst->Value());
                    }
                }
            };
            EnqueueDeferredTask(Deferred_NonFunctionExpressions, Move(AnalyzeValueExpression));
        }
        else if ((_Context._Scope->IsControlScope() || _Context._Scope->IsModuleOrSnippet())
              && !_Context._Scope->IsInsideTypeScope()) // Initiation not needed inside type scope
        {
            AppendGlitch(*DataDefAst,
                EDiagnostic::ErrSemantic_MissingValueInitializer,
                "Data definitions at this scope must be initialized with a value.");
        }
        else
        {
            EnqueueDeferredTask(Deferred_ValidateAttributes, [this, DataDefAst, DataDefinition]
            {
                if (DataDefinition->IsFinal())
                {
                    if (VerseFN::UploadedAtFNVersion::EnableFinalSpecifierFixes(_Context._Package->_UploadedAtFNVersion))
                    {
                        AppendGlitch(*DataDefAst,
                            EDiagnostic::ErrSemantic_MissingFinalFieldInitializer,
                            CUTF8String("Final data member '%s' is not initialized. Since it cannot be overridden, it must be initialized here.",
                                DataDefinition->AsNameCString()));
                    }
                }
            });
        }

        //
        // Analyze the data member's attributes in a deferred task.
        CExprVar* Var = ElementAnalysis.VarAst;
        EnqueueDeferredTask(Deferred_Attributes, [this, DataDefAst, NameAttributes, DefAttributes, Var, DataDefinition, ExprCtx, VarName]
        {
            TGuardValue<CScope*> ScopeGuard(_Context._Scope, &DataDefAst->_DataMember->_EnclosingScope);

            DataDefAst->_DataMember->_Attributes = AnalyzeNameAndDefAttributes(NameAttributes, DefAttributes, CAttributable::EAttributableScope::Data);
            DataDefAst->_DataMember->SetAccessLevel(GetAccessLevelFromAttributes(*DataDefAst->GetMappedVstNode(), *DataDefAst->_DataMember));
            ValidateExperimentalAttribute(*DataDefAst->_DataMember);
            AnalyzeFinalAttribute(*DataDefAst, *DataDefAst->_DataMember);

            if (Var)
            {
                if (Var->_Attributes.Num())
                {
                    CAttributable::EAttributableScope AttributableScope;
                    switch (DataDefAst->_DataMember->_EnclosingScope.GetKind())
                    {
                    case CScope::EKind::Module:
                    case CScope::EKind::ModulePart:
                    case CScope::EKind::Snippet:
                        AttributableScope = CAttributable::EAttributableScope::Module;
                        break;
                    case CScope::EKind::Class:
                        AttributableScope = CAttributable::EAttributableScope::Class;
                        break;
                    case CScope::EKind::Program:
                    case CScope::EKind::CompatConstraintRoot:
                    case CScope::EKind::Function:
                    case CScope::EKind::ControlScope:
                    case CScope::EKind::Interface:
                    case CScope::EKind::Type:
                    case CScope::EKind::Enumeration:
                        AppendGlitch(
                            *DataDefAst,
                            EDiagnostic::ErrSemantic_VarAttributeMustBeInClassOrModule);
                        AttributableScope = CAttributable::EAttributableScope::Module;
                        break;
                    default:
                        ULANG_UNREACHABLE();
                    }

                    AnalyzeAttributes(Var->_Attributes, AttributableScope, EAttributeSource::Var);
                }
                DataDefAst->_DataMember->SetVarAccessLevel(GetAccessLevelFromAttributes(*Var->GetMappedVstNode(), *Var));
            }

            if (SClassVarAccessorFunctions Accessors; FindAccessorFunctions(DataDefAst, NameAttributes, Accessors))
            {
                if (!AnalyzeAccessorFunctions(DataDefAst, Var, VarName, Accessors, ExprCtx))
                {
                    return;
                }
                DataDefAst->_DataMember->_OptionalAccessors = Move(Accessors);
            }

            // Fetch @editable class, if it's available
            const CClass* EditableAttrClass = _Program->_editable.Get();

            // This class is not available in all packages as it is <epic_internal>, so make sure not to block the normal @editable checks on it
            const CClass* EditableNonConcreteAttrClass = _Program->_editable_non_concrete.Get();

            // If we are processing editable and the data member has the editable attribute, check that it's an approved type
            // Must defer until after all attributes have been processed
            if (EditableAttrClass)
            {
                const bool bHasEditableAttrClass = DataDefAst->_DataMember->HasAttributeClass(EditableAttrClass, *_Program);
                const bool bHasEditableNonConcreteAttrClass = EditableNonConcreteAttrClass && DataDefAst->_DataMember->HasAttributeClass(EditableNonConcreteAttrClass, *_Program);
                if (bHasEditableAttrClass && bHasEditableNonConcreteAttrClass)
                {
                    AppendGlitch(*DataDefAst, EDiagnostic::ErrSemantic_AttributeNotAllowed, CUTF8String("@editable and @editable_non_concrete are mutually exclusive and not both allowed on the same data definition."));
                }

                if (bHasEditableAttrClass || bHasEditableNonConcreteAttrClass)
                {
                    EnqueueDeferredTask(Deferred_ValidateAttributes, [this, DataDefAst, bHasEditableNonConcreteAttrClass]()
                        {
                            SemanticTypeUtils::EIsEditable IsEditable = SemanticTypeUtils::IsEditableType(DataDefAst->_DataMember->GetType(), bHasEditableNonConcreteAttrClass);
                            if (IsEditable != SemanticTypeUtils::EIsEditable::Yes)
                            {
                                AppendGlitch(*DataDefAst, EDiagnostic::ErrSemantic_AttributeNotAllowed, SemanticTypeUtils::IsEditableToCMessage(IsEditable));
                            }
                        });
                }

                if (VerseFN::UploadedAtFNVersion::StricterEditableOverrideCheck(_Context._Package->_UploadedAtFNVersion))
                {
                    // Check that overrides of @editable types are of the same type...
                    // Otherwise throw an error as UEFN does not yet support this functionality.
                    EnqueueDeferredTask(Deferred_FinalValidation, [this, DataDefAst, EditableAttrClass, EditableNonConcreteAttrClass]()
                        {
                            if (EditableAttrClass)
                            {
                                const uLang::CDataDefinition* OverriddenMember = DataDefAst->_DataMember->GetOverriddenDefinition();
                                while (OverriddenMember != nullptr)
                                {
                                    if ((OverriddenMember->HasAttributeClass(EditableAttrClass, *_Program)
                                        || (EditableNonConcreteAttrClass && OverriddenMember->HasAttributeClass(EditableNonConcreteAttrClass, *_Program)))
                                        && !uLang::SemanticTypeUtils::Matches(OverriddenMember->GetType(), DataDefAst->_DataMember->GetType()))
                                    {
                                        AppendGlitch(
                                            *DataDefAst,
                                            EDiagnostic::ErrSemantic_AttributeNotAllowed,
                                            CUTF8String("Overriding an @editable member with a different type is not supported (editable-type: %s, overriding-type: %s).",
                                                OverriddenMember->GetType()->AsCode(ETypeSyntaxPrecedence::Definition).AsCString(),
                                                DataDefAst->_DataMember->GetType()->AsCode(ETypeSyntaxPrecedence::Definition).AsCString()));
                                        break;
                                    }
                                    OverriddenMember = OverriddenMember->GetOverriddenDefinition();
                                }
                            }
                        });
                }
            }

            // If the data member has the native attribute, defer a task until after all attributes have been processed to
            // verify that the parent class also has the native attribute.
            if (DataDefAst->_DataMember->IsNative())
            {
                if (_Context._Scope->GetKind() == CScope::EKind::Class)
                {
                    EnqueueDeferredTask(Deferred_ValidateAttributes, [this, DataDefAst]()
                        {
                            const CClass* ScopeAsClass = &static_cast<const CClass&>(DataDefAst->_DataMember->_EnclosingScope);
                            if (!ScopeAsClass->IsNative())
                            {
                                AppendGlitch(*DataDefAst, EDiagnostic::ErrSemantic_NativeMemberOfNonNativeClass);
                            }
                        });
                }
                else if (_Context._Scope->IsModuleOrSnippet())
                {
                    AppendGlitch(*DataDefAst, EDiagnostic::ErrSemantic_Unimplemented, "Module data definitions cannot be marked as `<native>`.");
                }
                else
                {
                    AppendGlitch(*DataDefAst, EDiagnostic::ErrSemantic_AttributeNotAllowed, "Data definitions at this scope cannot be marked as `<native>`.");
                }
            }

            // Disallow access level attributes on function local variables.
            if (DataDefAst->_DataMember->HasAttributes() && _Context._Scope->GetKind() == CScope::EKind::ControlScope)
            {
                EnqueueDeferredTask(Deferred_ValidateAttributes, [this, DataDefAst]()
                    {
                        if (DataDefAst->_DataMember.IsValid() && HasAccessLevelAttribute(*(DataDefAst->_DataMember)))
                        {
                            AppendGlitch(*DataDefAst, EDiagnostic::ErrSemantic_AccessSpecifierNotAllowedOnLocal, 
                                CUTF8String("Function local data definition '%s' is not allowed to use access level attributes(e.g. <public>, <internal>)",
                                    DataDefAst->_DataMember->AsNameCString()));
                        }
                    });
            }
        });

        if (_Context._Scope->GetKind() == CScope::EKind::Class)
        {
            EnqueueDeferredTask(Deferred_ValidateAttributes, [this, DataDefAst]
            {
                const CClassDefinition* ClassDefinition = &static_cast<const CClassDefinition&>(DataDefAst->_DataMember->_EnclosingScope);

                CDataDefinition& DataDefinition = *DataDefAst->_DataMember;
                if (!DataDefAst->Value())
                {
                    // if the data definition doesn't have a default value specified, then we
                    // validate that the data member isn't less accessible than the constructor,
                    // otherwise there can exist code that has access to the constructor but
                    // can't see all of the necessary data members to provide them a value
                    SAccessibilityScope ClassAccessibilityScope = GetAccessibilityScope(*ClassDefinition, ClassDefinition->DerivedConstructorAccessLevel());
                    SAccessibilityScope DataMemberAccessibilityScope = GetAccessibilityScope(DataDefinition);

                    if (ClassAccessibilityScope.IsMoreAccessibleThan(DataMemberAccessibilityScope)
                        && !DataDefinition._OptionalAccessors)
                    {
                        AppendGlitch(
                            *DataDefAst,
                            EDiagnostic::ErrSemantic_Inaccessible,
                            CUTF8String("Data member '%s' in class '%s', is less accessible (%s) than the class constructor (%s). This is not allowed for data members that have no default value, since some code could have access to construct the class but unable to provide a value for this member.",
                            DataDefinition.AsNameCString(),
                            ClassDefinition->GetName().AsCString(),
                            DataDefinition.DerivedAccessLevel().AsCode().AsCString(),
                            ClassDefinition->DerivedConstructorAccessLevel().AsCode().AsCString()));
                    }
                }

                if (DataDefinition.IsVar() && DataDefinition.GetOverriddenDefinition() && DataDefinition.SelfVarAccessLevel().IsSet())
                {
                    AppendGlitch(*DataDefAst, 
                        EDiagnostic::ErrSemantic_OverrideCantChangeAccessLevel,
                        CUTF8String("Overridden var %s cannot specify an access level. Access levels are inherited from the parent definition.", DataDefinition.AsNameCString()));
                }

                // If data definition is a struct, it must be native if the containing class or struct is native
                {
                    TGuardValue<CScope*> ScopeGuard(_Context._Scope, &DataDefinition._EnclosingScope);

                    const CClass& ScopeClass = static_cast<const CClass&>(*_Context._Scope);
                    if (ScopeClass.IsNative())
                    {
                        const CTypeBase* DataMemberType = DataDefinition.GetType();
                        ULANG_ASSERTF(DataMemberType, "`DataMemberType` is `nullptr`");
                        ValidateTypeIsNative(DataMemberType, EValidateTypeIsNativeContext::Member, *DataDefAst);
                    }
                }
            });
        }

        // Check accessibility of the type
        EnqueueDeferredTask(Deferred_ValidateAttributes, [this, DataDefAst]()
            {
                TGuardValue<CScope*> ScopeGuard(_Context._Scope, &DataDefAst->_DataMember->_EnclosingScope);

                DetectInaccessibleTypeDependencies(*DataDefAst->_DataMember, DataDefAst->_DataMember->GetType(), DataDefAst->GetMappedVstNode());
            });

        return Move(DataDefAst);
    }

    //-------------------------------------------------------------------------------------------------
    bool RequireTypeIsNotRecursive(const CTypeBase* Type, const CAstNode* AstNode)
    {
        TArray<const CTypeBase*> PathStack;
        TArray<const CTypeBase*> NonRecursiveTypes;
        return RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, Type, AstNode);
    }
    bool RequireTypeIsNotRecursive(
        TArray<const CTypeBase*>& PathStack,
        TArray<const CTypeBase*>& NonRecursiveTypes,
        const CTypeBase* Type,
        const CAstNode* AstNode)
    {
        if (PathStack.Contains(Type))
        {
            AppendGlitch(*AstNode, EDiagnostic::ErrSemantic_Unimplemented, "Recursive types are not yet implemented.");
            return false;
        }
        if (NonRecursiveTypes.Contains(Type))
        {
            return true;
        }

        PathStack.Push(Type);

        const CNormalType& NormalType = Type->GetNormalType();
        switch (NormalType.GetKind())
        {
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
        case ETypeKind::Class:
        case ETypeKind::Module:
        case ETypeKind::Enumeration:
        case ETypeKind::Interface:
        case ETypeKind::Named:
            break;

        case ETypeKind::Array:
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, NormalType.AsChecked<CArrayType>().GetElementType(), AstNode))
            {
                return false;
            }
            break;

        case ETypeKind::Generator:
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, NormalType.AsChecked<CGeneratorType>().GetElementType(), AstNode))
            {
                return false;
            }
            break;

        case ETypeKind::Map:
        {
            const CMapType& MapType = NormalType.AsChecked<CMapType>();
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, MapType.GetKeyType(), AstNode))
            {
                return false;
            }
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, MapType.GetValueType(), AstNode))
            {
                return false;
            }
            break;
        }

        case ETypeKind::Pointer:
        {
            const CPointerType& PointerType = NormalType.AsChecked<CPointerType>();
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, PointerType.NegativeValueType(), AstNode))
            {
                return false;
            }
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, PointerType.PositiveValueType(), AstNode))
            {
                return false;
            }
            break;
        }

        case ETypeKind::Reference:
        {
            const CReferenceType& ReferenceType = NormalType.AsChecked<CReferenceType>();
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, ReferenceType.NegativeValueType(), AstNode))
            {
                return false;
            }
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, ReferenceType.PositiveValueType(), AstNode))
            {
                return false;
            }
            break;
        }

        case ETypeKind::Option:
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, NormalType.AsChecked<COptionType>().GetValueType(), AstNode))
            {
                return false;
            }
            break;

        case ETypeKind::Type:
        {
            const CTypeType& TypeType = NormalType.AsChecked<CTypeType>();
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, TypeType.NegativeType(), AstNode))
            {
                return false;
            }
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, TypeType.PositiveType(), AstNode))
            {
                return false;
            }
            break;
        }

        case ETypeKind::Tuple:
        {
            const CTupleType& TupleType = NormalType.AsChecked<CTupleType>();
            for (const CTypeBase* ElementType : TupleType.GetElements())
            {
                if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, ElementType, AstNode))
                {
                    return false;
                }
            }
            break;
        }

        case ETypeKind::Function:
        {
            const CFunctionType& FunctionType = NormalType.AsChecked<CFunctionType>();
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, &FunctionType.GetParamsType(), AstNode))
            {
                return false;
            }
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, &FunctionType.GetReturnType(), AstNode))
            {
                return false;
            }
            break;
        }

        case ETypeKind::Variable:
        {
            const CTypeVariable& TypeVariable = NormalType.AsChecked<CTypeVariable>();
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, TypeVariable._NegativeType, AstNode))
            {
                return false;
            }
            if (!RequireTypeIsNotRecursive(PathStack, NonRecursiveTypes, TypeVariable.GetType(), AstNode))
            {
                return false;
            }
            break;
        }

        default:
            ULANG_UNREACHABLE();
        };

        PathStack.Pop();
        NonRecursiveTypes.Add(Type);
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeTypeDefinition(CExprDefinition& DefinitionAst, const SDefinitionElementAnalysis& ElementAnalysis, const SExprCtx& ExprCtx)
    {
        // Require that the definition is of the form 'id := ...' or 'id(...) := ...'
        CExprIdentifierUnresolved& Identifier = *ElementAnalysis.IdentifierAst;
        if (ElementAnalysis.VarAst
            || Identifier.Context()
            || DefinitionAst.ValueDomain())
        {
            AppendGlitch(*DefinitionAst.Element(), EDiagnostic::ErrSemantic_LhsNotDefineable);
            return TSRef<CExprError>::New();
        }

        // Require that the identifier isn't a reserved symbol.
        RequireNonReservedSymbol(Identifier);

        // For now, only allow type definitions at module scope.
        if (!_Context._Scope->IsModuleOrSnippet())
        {
            AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_Unimplemented, "Type definitions are not yet implemented outside of a module scope.");
            return ReplaceMapping(DefinitionAst, TSRef<CExprError>::New());
        }

        if (DefinitionAst.Value()->GetNodeType() == EAstNodeType::MacroCall)
        {
            const TSRef<CExprMacroCall>& MacroCallAst = DefinitionAst.Value().As<CExprMacroCall>().AsRef();
            CExprIdentifierUnresolved& MacroNameId = static_cast<CExprIdentifierUnresolved&>(*MacroCallAst->Name());
            if (MacroNameId._Symbol == _InnateMacros._type)
            {
                return AnalyzeTypeAlias(DefinitionAst, ElementAnalysis, ExprCtx);
            }

            // Only allow qualified identifiers for modules, enums, classes, structs, and interfaces.
            if (MacroNameId._Symbol != _InnateMacros._module
                && MacroNameId._Symbol != _InnateMacros._enum
                && MacroNameId._Symbol != _InnateMacros._class
                && MacroNameId._Symbol != _InnateMacros._struct
                && MacroNameId._Symbol != _InnateMacros._interface)
            {
                RequireUnqualifiedIdentifier(Identifier);
            }

            // Make sure there aren't any attributes on the macro. This would normally be enforced
            // by AnalyzeExpressionAst, but this skips it and directly calls AnalyzeMacroCall.
            MaybeAppendAttributesNotAllowedError(*MacroCallAst);

            // Analyze type definitions in the parent scope.
            SMacroCallDefinitionContext MacroCallDefinitionContext(Identifier._Symbol, Identifier.Qualifier(), Move(Identifier._Attributes), Move(DefinitionAst._Attributes));
            SExprArgs ExprArgs;
            ExprArgs.MacroCallDefinitionContext = &MacroCallDefinitionContext;
            if (TSPtr<CExpressionBase> NewValue = AnalyzeMacroCall(MacroCallAst, ExprCtx.WithResultIsUsedAsType(), ExprArgs))
            {
                DefinitionAst.SetValue(Move(NewValue.AsRef()));
            }

            const CTypeBase* Type = DefinitionAst.Value()->GetResultType(*_Program);
            Identifier.SetResultType(Type);
            DefinitionAst.SetResultType(Type);
            return nullptr;
        }

        return AnalyzeTypeAlias(DefinitionAst, ElementAnalysis, ExprCtx);
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeTypeAlias(CExprDefinition& DefinitionAst, const SDefinitionElementAnalysis& ElementAnalysis, const SExprCtx& ExprCtx)
    {
        CExprIdentifierUnresolved& Identifier = *ElementAnalysis.IdentifierAst;

        RequireUnqualifiedIdentifier(Identifier);

        // Create the type alias
        TSRef<CTypeAlias> TypeAlias = _Context._Scope->CreateTypeAlias(Identifier._Symbol);

        TGuardValue<TArray<const CDefinition*>> DefinitionsGuard(_Context._EnclosingDefinitions);
        _Context._EnclosingDefinitions.Add(TypeAlias.Get());

        TArray<SAttribute> NameAttributes = Move(Identifier._Attributes);
        TArray<SAttribute> DefAttributes = Move(DefinitionAst._Attributes);

        const Vst::Node* DefinitionVst = DefinitionAst.GetMappedVstNode();

        EnqueueDeferredTask(Deferred_Attributes, [this, TypeAlias, DefinitionVst, NameAttributes, DefAttributes]()
        {
            // Not inside the function yet
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &TypeAlias->_EnclosingScope);
            TypeAlias->_Attributes = AnalyzeNameAndDefAttributes(NameAttributes, DefAttributes, CAttributable::EAttributableScope::TypeDefinition);
            TypeAlias->SetAccessLevel(GetAccessLevelFromAttributes(*DefinitionVst, *TypeAlias));
            ValidateExperimentalAttribute(*TypeAlias);
        });

        EnqueueDeferredTask(Deferred_ValidateAttributes, [this, TypeAlias, DefinitionVst]()
        {
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &TypeAlias->_EnclosingScope);

            // Check accessibility of the type
            DetectInaccessibleTypeDependencies(*TypeAlias, TypeAlias->GetTypeType(), DefinitionVst);
        });

        // Create a CExprTypeAliasDefinition expression.
        TSRef<CExprTypeAliasDefinition> TypeAliasDefinitionAst = TSRef<CExprTypeAliasDefinition>::New(TypeAlias, DefinitionAst.TakeElement(), DefinitionAst.TakeValueDomain(), DefinitionAst.TakeValue());

        // Analyze the RHS subexpression in a Deferred_Type task.
        EnqueueDeferredTask(Deferred_Type, [this, TypeAliasDefinitionAst, &Identifier, TypeAlias, ExprCtx]()
        {
            TGuardValue<CScope*> ScopeGuard(_Context._Scope, &TypeAlias->_EnclosingScope);
            if (TSPtr<CExpressionBase> NewValue = AnalyzeExpressionAst(TypeAliasDefinitionAst->Value().AsRef(), ExprCtx.WithResultIsUsedAsType()))
            {
                TypeAliasDefinitionAst->SetValue(Move(NewValue.AsRef()));
            }

            STypeTypes TypeAliasTypes = GetTypeTypes(*TypeAliasDefinitionAst->Value());
            if (TypeAliasTypes._NegativeType->GetNormalType().IsA<CClass>() || TypeAliasTypes._PositiveType->GetNormalType().IsA<CClass>())
            {
                AppendGlitch(*TypeAliasDefinitionAst, EDiagnostic::ErrSemantic_Unimplemented, "Type aliases for classes are not yet implemented.");
                TypeAliasTypes._NegativeType = _Program->GetDefaultUnknownType();
                TypeAliasTypes._PositiveType = _Program->GetDefaultUnknownType();
            }

            TypeAlias->InitType(TypeAliasTypes._NegativeType, TypeAliasTypes._PositiveType);
            TypeAliasDefinitionAst->SetResultType(TypeAlias->GetTypeType());
            Identifier.SetResultType(TypeAlias->GetTypeType());
        });
        
        // Require that the type alias definition is unambiguous.
        RequireUnambiguousDefinition(*TypeAlias, "type alias");

        // Replace the CExprDefinition with the CExprTypeAliasDefinition.
        return ReplaceMapping(DefinitionAst, TSRef<CExprTypeAliasDefinition>(TypeAliasDefinitionAst));
    }

    //-------------------------------------------------------------------------------------------------
    TSPtr<CExpressionBase> AnalyzeImport(CExprDefinition& DefinitionAst, const SDefinitionElementAnalysis& ElementAnalysis, const SExprCtx& ExprCtx)
    {
        // Require that the LHS is a simple identifier.
        if (DefinitionAst.Element()->GetNodeType() != EAstNodeType::Identifier_Unresolved
            || DefinitionAst.ValueDomain())
        {
            if (DefinitionAst.Element()->GetNodeType() != EAstNodeType::Error_)
            {
                AppendGlitch(*DefinitionAst.Element(), EDiagnostic::ErrSemantic_LhsNotDefineable);
            }
            return ReplaceMapping(DefinitionAst, TSRef<CExprError>::New());
        }
        TSPtr<CExprIdentifierUnresolved> LhsIdentifier = DefinitionAst.Element().As<CExprIdentifierUnresolved>();

        // Don't allow identifiers in the form `context.id`
        if (LhsIdentifier->Context())
        {
            AppendGlitch(*LhsIdentifier, EDiagnostic::ErrSemantic_LhsNotDefineable);
            return ReplaceMapping(DefinitionAst, TSRef<CExprError>::New());
        }

        // For now, only allow imports at module scope.
        if (!_Context._Scope->IsModuleOrSnippet())
        {
            AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_Unimplemented, "`import` may currently only occur in module scope.");
            return ReplaceMapping(DefinitionAst, TSRef<CExprError>::New());
        }

        // Check for redefinition of reserved symbols or symbols already defined in the same scope.
        ValidateDefinitionIdentifier(*LhsIdentifier, *_Context._Scope);

        // Analyze RHS function call
        const CExprInvocation& Invocation = static_cast<const CExprInvocation&>(*DefinitionAst.Value());
        ULANG_ASSERTF(Invocation.GetNodeType() == EAstNodeType::Invoke_Invocation, "Caller must ensure this!");

        // Disallow failure semantics
        if (Invocation.CanFail())
        {
            AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_IncompatibleFailure, "`import` must be invoked with parentheses rather than square brackets.");
            return ReplaceMapping(DefinitionAst, TSRef<CExprError>::New());
        }

        // Make sure the argument is a path literal
        if (Invocation.GetArgument()->GetNodeType() != EAstNodeType::Literal_Path)
        {
            AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_IncompatibleArgument, "`import` must take a single path as argument.");
            return ReplaceMapping(DefinitionAst, TSRef<CExprError>::New());
        }

        TSRef<CModuleAlias> ModuleAlias = _Context._Scope->CreateModuleAlias(LhsIdentifier->_Symbol);

        TGuardValue<TArray<const CDefinition*>> DefinitionsGuard(_Context._EnclosingDefinitions);
        _Context._EnclosingDefinitions.Add(ModuleAlias.Get());

        // Defer analysis of the path to a deferred task
        TSRef<CExpressionBase> Argument = Invocation.GetArgument().AsRef();
        CExprPath& Path = static_cast<CExprPath&>(*Argument);
        EnqueueDeferredTask(Deferred_Import, [this, LhsIdentifier, &Path, ExprCtx, ModuleAlias]()
            {
                AnalyzePathLiteral(Path, ExprCtx.WithResultIsImported(_Program->_pathType));

                if (const CModule* Module = ResolvePathToModule(Path._Path, Path))
                {
                    ModuleAlias->SetModule(Module);
                    LhsIdentifier->SetResultType(Module);
                }
            });

        // Require that the module alias is unambiguous.
        RequireUnambiguousDefinition(*ModuleAlias, "module alias");

        // Analyze the module alias's attributes.
        TArray<SAttribute> NameAttributes = Move(LhsIdentifier->_Attributes);
        TArray<SAttribute> DefAttributes = Move(DefinitionAst._Attributes);
        EnqueueDeferredTask(Deferred_Attributes, [this, ModuleAlias, NameAttributes, DefAttributes]()
        {
            TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, &ModuleAlias->_EnclosingScope);
            ModuleAlias->_Attributes = AnalyzeNameAndDefAttributes(NameAttributes, DefAttributes, CAttributable::EAttributableScope::Module);
            ModuleAlias->SetAccessLevel(GetAccessLevelFromAttributes(*ModuleAlias->GetAstNode()->GetMappedVstNode(), *ModuleAlias));
            ValidateExperimentalAttribute(*ModuleAlias);
        });

        // Replace the CExprDefinition with a CExprImport.
        return ReplaceMapping(DefinitionAst, TSRef<CExprImport>::New(Move(ModuleAlias), Move(Argument)));
    }

    //-------------------------------------------------------------------------------------------------

    const CExpressionBase* StripWhere(const CExpressionBase* Lhs)
    {
        while (Lhs->GetNodeType() == EAstNodeType::Definition_Where)
        {
            Lhs = static_cast<const CExprWhere*>(Lhs)->Lhs().Get();
        }
        return Lhs;
    }
        
    //-------------------------------------------------------------------------------------------------
    SDefinitionElementAnalysis TryAnalyzeDefinitionLhs(CExprDefinition& DefinitionAst, bool bNameCanHaveAttributes)
    {
        // Recognize a few different forms of definition element:
        // id
        // id^
        // id(..)
        // id^(..)
        // (..).id()  => operator'.id'(..)

        EDefinitionElementAnalysisResult Result = EDefinitionElementAnalysisResult::Definition;

        CExprIdentifierUnresolved* ResidualIdentifier = nullptr;
        CExprVar* Var = nullptr;
        CExprInvocation* Invocation = nullptr;
        CSymbol IdentifierSymbol;
        bool bSawSquareBrackets = false;

        // Attributes on Definition get analyzed by the caller.

        if (DefinitionAst.Element())
        {
            CExpressionBase* ResidualElement = DefinitionAst.Element().Get();

            if (ResidualElement->GetNodeType() == EAstNodeType::Definition_Var)
            {
                Var = static_cast<CExprVar*>(ResidualElement);
                ResidualElement = Var->Operand().Get();
            }

            if (ResidualElement->GetNodeType() == EAstNodeType::Invoke_Invocation)
            {
                Invocation = static_cast<CExprInvocation*>(ResidualElement);
                ULANG_ASSERTF(Invocation->_CallsiteBracketStyle != CExprInvocation::EBracketingStyle::Undefined, "Invocation bracketing style must be defined here");
                bSawSquareBrackets |= Invocation->_CallsiteBracketStyle == CExprInvocation::EBracketingStyle::SquareBrackets;
                ResidualElement = Invocation->GetCallee().Get();
            }

            if (ResidualElement->GetNodeType() != EAstNodeType::Identifier_Unresolved)
            {
                Result = EDefinitionElementAnalysisResult::Failure;
            }
            else
            {
                ResidualIdentifier = static_cast<CExprIdentifierUnresolved*>(ResidualElement);
                IdentifierSymbol = ResidualIdentifier->_Symbol;

                if (ResidualIdentifier->Context())
                {
                    if (Invocation)
                    {
                        const CExpressionBase* ContextCore = StripWhere(ResidualIdentifier->Context().Get());
                        if (ContextCore->GetNodeType() == EAstNodeType::Definition)
                        {
                            CUTF8String NewName(_Program->_IntrinsicSymbols.MakeExtensionFieldOpName(ResidualIdentifier->_Symbol));
                            const CSymbol OperatorName = VerifyAddSymbol(*ResidualIdentifier, NewName);
                            const_cast<CSymbol&>(ResidualIdentifier->_Symbol) = OperatorName;

                            // Change node from
                            // (lhs).Name(rhs) : T = E
                            // to
                            // operator'.Name'(lhs, (...rhs)) : T = E 

                            TSPtr<CExpressionBase> Context = ResidualIdentifier->TakeContext();
                            TSPtr<CExpressionBase> Argument = Invocation->TakeArgument();

                            ULANG_ASSERTF(DefinitionAst.GetMappedVstNode(), "Cannot process extension method due to missing VstNode (will fail later).");

                            TSPtr<CExprMakeTuple> NewArgument = TSRef<CExprMakeTuple>::New();
                            NewArgument->SetNonReciprocalMappedVstNode(Argument->GetMappedVstNode());
                            NewArgument->AppendSubExpr(Move(Context));
                            NewArgument->AppendSubExpr(Move(Argument)); // this may be an empty CExprMakeTuple... we append this still to accomodate named args - JIRA #SOL-6937
                            Invocation->SetArgument(Move(NewArgument));
                        }
                        else if (ContextCore->GetNodeType() == EAstNodeType::Invoke_MakeTuple)
                        {
                            CExprMakeTuple* TupleContext = static_cast<CExprMakeTuple*>(ResidualIdentifier->Context().Get());
                            AppendGlitch(DefinitionAst,
                                EDiagnostic::ErrSyntax_Unimplemented,
                                TupleContext->IsEmpty() 
                                ? "For now write context as (:tuple()) instead of ()."
                                : "For now write context with explicit tuple type (Arg:tuple(Type1, Type2, ..)) instead of (Arg1:Type1, Arg2:Type2, ..)."
                            );                            
                        }
                    }
                }
            }
        }
        else
        {
            Result = EDefinitionElementAnalysisResult::Failure;
            AppendExpectedDefinitionError(DefinitionAst);
        }

        if (Result == EDefinitionElementAnalysisResult::Definition && bSawSquareBrackets)
        {
            AppendGlitch(DefinitionAst, EDiagnostic::ErrSemantic_SquareBracketFuncDefsDisallowed);
        }

        if (Invocation && Invocation->HasAttributes())
        {
            // The caller won't process these attributes.
            EnqueueDeferredTask(Deferred_Attributes, [this, Invocation, Scope = _Context._Scope]
            {
                TGuardValue<CScope*> CurrentScopeGuard(_Context._Scope, Scope);
                AnalyzeAttributes(Invocation->_Attributes, CAttributable::EAttributableScope::Function, EAttributeSource::Effect);
            });
        }

        return SDefinitionElementAnalysis{
            Result,
            ResidualIdentifier,
            Var,
            Invocation,
            IdentifierSymbol};
    }

    //-------------------------------------------------------------------------------------------
    struct SCustomAccessorClassVarLhs
    {
        TSPtr<CExprPointerToReference> PointerToReference;
        TSPtr<CExprIdentifierData> IdentifierData;
    };

    TOptional<SCustomAccessorClassVarLhs> CustomAccessorClassVarFromSetLhs(TSPtr<CExpressionBase> Lhs)
    {
        auto SetExpr = AsNullable<CExprSet>(Lhs);
        if (!SetExpr)
        {
            return {};
        }
        
        auto InvokePtrToRef = AsNullable<CExprPointerToReference>(SetExpr->Operand());
        if (!InvokePtrToRef)
        {
            return {};
        }

        auto IdentifierData = AsNullable<CExprIdentifierData>(InvokePtrToRef->Operand());
        if (!IdentifierData)
        {
            return {};
        }

        if (!IdentifierData->_DataDefinition.CanHaveCustomAccessors())
        {
            return {};
        }
        
        return {{InvokePtrToRef, IdentifierData}};
    }

    // Convert an expression written in a referenceable context to one that can be used elsewhere.
    TSRef<CExpressionBase> ToValue(const TSRef<CExpressionBase>& ReferenceValue)
    {
        const CNormalType& Type = ReferenceValue->GetResultType(*_Program)->GetNormalType();
        if (const CReferenceType* ReferenceType = Type.AsNullable<CReferenceType>())
        {
            TSRef<CExprReferenceToValue> Value = TSRef<CExprReferenceToValue>::New(ReferenceValue);
            Value->SetResultType(ReferenceType->PositiveValueType());
            return Value;
        }
        else
        {
            return ReferenceValue;
        }
    }

    TSRef<CExprIdentifierData> MakeFreshLocal(CExprCodeBlock& Block, TSRef<CExpressionBase> Value)
    {
        CSymbol VarName = GenerateUniqueName("fresh", *Block._AssociatedScope);
        TSRef<CExprIdentifierUnresolved> Element = TSRef<CExprIdentifierUnresolved>::New(VarName);

        TSRef<CDataDefinition> Definition = Block._AssociatedScope->CreateDataDefinition(VarName);

        TSRef<CExprDataDefinition> DefinitionAst = TSRef<CExprDataDefinition>::New(Definition, Element, nullptr, Value);

        // Infer the type like AnalyzeDataDefinition without a ValueDomain.
        const CTypeBase* DataType = Value->GetResultType(*_Program);
        Definition->_NegativeType = &_Program->_anyType;
        Definition->SetType(DataType);
        DefinitionAst->SetResultType(DataType);
        Element->SetResultType(DataType);

        Block.AppendSubExpr(DefinitionAst);

        // Return a resolved identifier like ResolveIdentifierToDefinition
        return TSRef<CExprIdentifierData>::New(*_Program, *Definition);
    }

    TSRef<CExprPointerToReference> MakeFreshLocalVar(CExprCodeBlock& Block, const CTypeBase* NegativeDataType, const CTypeBase* PositiveDataType, TSRef<CExpressionBase> Value)
    {
        CSymbol VarName = GenerateUniqueName("fresh", *Block._AssociatedScope);
        TSRef<CExprVar> Element = TSRef<CExprVar>::New(TSRef<CExprIdentifierUnresolved>::New(VarName));

        TSRef<CDataDefinition> Definition = Block._AssociatedScope->CreateDataDefinition(VarName);
        Definition->SetIsVar();

        TSRef<CExprDataDefinition> DefinitionAst = TSRef<CExprDataDefinition>::New(Definition, Element, nullptr, nullptr);

        // Record the types involved like AnalyzeDataDefinition with a ValueDomain.
        const CPointerType& NegativePointerType = _Program->GetOrCreatePointerType(PositiveDataType, NegativeDataType);
        const CPointerType& PositivePointerType = _Program->GetOrCreatePointerType(NegativeDataType, PositiveDataType);
        const CReferenceType& PositiveReferenceType = _Program->GetOrCreateReferenceType(NegativeDataType, PositiveDataType);
        Definition->_NegativeType = &NegativePointerType;
        Definition->SetType(&PositivePointerType);
        DefinitionAst->SetResultType(PositiveDataType);
        Element->SetResultType(&PositiveReferenceType);
        Element->Operand()->SetResultType(&PositivePointerType);

        // Wrap the value in a CExprNewPointer like AnalyzeDataDefinition.
        const CTypeBase* ValueType = Value->GetResultType(*_Program);
        TSRef<CExprNewPointer> Pointer = TSRef<CExprNewPointer>::New(
            static_cast<const CPointerType*>(ValueType),
            Move(Value));
        DefinitionAst->SetValue(Move(Pointer));

        Block.AppendSubExpr(DefinitionAst);

        // Return a reference like ResolveIdentifierToDefinition in a referenceable context.
        // The caller may further wrap this in a CExprReferenceToValue when appropriate.
        TSRef<CExprIdentifierData> Identifier = TSRef<CExprIdentifierData>::New(*_Program, *Definition);
        return TSRef<CExprPointerToReference>::New(Identifier);
    }

    TSPtr<CExprCodeBlock> MakeCodeBlock()
    {
        TSRef<CExprCodeBlock> Block = TSRef<CExprCodeBlock>::New(2);
        Block->_AssociatedScope = _Context._Scope->CreateNestedControlScope();
        return Block;
    }

    template <typename>
    static constexpr bool TAnalyzeInPlaceBadArgs = false;

    template <typename TValue, typename TAnalyze, typename... TArgs>
    TSPtr<CExpressionBase> AnalyzeInPlace(TSPtr<TValue> Expr, TAnalyze Analyze, TArgs&&... Args)
    {
        return AnalyzeInPlace(Expr.AsRef(), Analyze, std::forward<TArgs>(Args)...);
    }

    template <typename TValue, typename TAnalyze, typename... TArgs>
    TSPtr<CExpressionBase> AnalyzeInPlace(TSRef<TValue> Expr, TAnalyze Analyze, TArgs&&... Args)
    {
        if constexpr (std::is_invocable_v<TAnalyze, CSemanticAnalyzerImpl*, TSRef<TValue>, TArgs...>)
        {
            auto R = (this->*Analyze)(Expr, std::forward<TArgs>(Args)...);
            return R ? R : Expr;
        }
        else if constexpr (std::is_invocable_v<TAnalyze, CSemanticAnalyzerImpl*, TValue&, TArgs...>)
        {
            auto R = (this->*Analyze)(*Expr, std::forward<TArgs>(Args)...);
            return R ? R : Expr;
        }
        else
        {
            static_assert(TAnalyzeInPlaceBadArgs<TAnalyze>, "AnalyzeInPlace called with invalid analysis function/args.");
            ULANG_UNREACHABLE();
        }
    }
  
    //-------------------------------------------------------------------------------------------------
    TSPtr<CExprIdentifierData> FindParameterCollectionVarLhs(TSPtr<CExprAssignment> Assignment)
    {
        auto ExprSet = AsNullable<CExprSet>(Assignment->Lhs());
        if (!ExprSet)
        {
            return {};
        }
        auto PtrToRef = AsNullable<CExprPointerToReference>(ExprSet->Operand());
        if (!PtrToRef)
        {
            return {};
        }

        auto IdentifierData = AsNullable<CExprIdentifierData>(PtrToRef->Operand());
        if (!IdentifierData || !IsParameterCollectionClassVar(IdentifierData->_DataDefinition))
        {
            return {};
        }

        const CScope& Scope = IdentifierData->_DataDefinition._EnclosingScope;
        if (Scope.GetKind() != CScope::EKind::Class)
        {
            return {};
        }

        const CClassDefinition* Class = static_cast<const CClassDefinition*>(Scope.ScopeAsDefinition());
        if (!IsParameterCollectionSubclass(Class))
        {
            return {};
        }

        return IdentifierData;
    }

    TSPtr<CExpressionBase> TryAnalyzeParameterCollectionVarAssignment(TSPtr<CExprAssignment> Assignment, const SExprCtx& ExprCtx)
    {
        TSPtr<CExprIdentifierData> ParameterCollectionVarLhs = FindParameterCollectionVarLhs(Assignment);
        if (!ParameterCollectionVarLhs)
        {
            return nullptr;
        }

        // when we encounter an expression of the form set [Obj.]X = Y, where Obj (or implicit Self)
        // is an instance of a subclass of parameter_collection, we emit an additional
        // "On_X_Changed" after:
        //
        //   set [Obj.]X = Y
        // =>
        //   block:
        //     Context := Obj (or, := implicit Self)
        //     R := set Context.X = Y
        //     Context.OnPropertyChangedFromVerse("X")
        //     R

        const Verse::Vst::Node* AssignmentVSTNode = Assignment->GetMappedVstNode();
        auto&& MapAssignmentVSTNodeTo = [AssignmentVSTNode](TSPtr<CExpressionBase> Expr)
        {
            if (!AssignmentVSTNode)
            {
                return;
            }
            Expr->SetNonReciprocalMappedVstNode(AssignmentVSTNode);
            AssignmentVSTNode->AddMapping(Expr);
        };

        TSPtr<CExpressionBase> MaybeObj = ParameterCollectionVarLhs->Context();
        TSPtr<CExprCodeBlock> Block = MakeCodeBlock();                                     // block:
        MapAssignmentVSTNodeTo(Block);
        {
            // Note: Obj may have a reference type, if it is itself (projected
            // from) a var.  The local Context should store the object (pointer)
            // itself, not a reference to it.  This is okay because
            // FindParameterCollectionVarLhs only accepts classes, not structs.
            TSPtr<CExprIdentifierData> MaybeContext;
            if (MaybeObj)
            {
                MaybeContext = MakeFreshLocal(*Block, ToValue(MaybeObj.AsRef()));          //    Context := Obj
                ParameterCollectionVarLhs->SetContext(MaybeContext);
            }

            TSPtr<CExprIdentifierData> Result;                                             //    R := set Context.X = Y
            {
                TSPtr<CExpressionBase> AssignmentResult = AnalyzeInPlace(
                    Assignment, &CSemanticAnalyzerImpl::AnalyzeAssignment_Internal, ExprCtx
                );

                Result = MakeFreshLocal(*Block, AssignmentResult.AsRef());
            }

            TSPtr<CExpressionBase> HookCall;                                               //    Context.OnPropertyChangedFromVerse("X")
            {
                HookCall = MakeParameterCollectionOnChangeFuncInvocation(
                    ParameterCollectionVarLhs->_DataDefinition.GetName().AsString(), MaybeContext
                );
                MapAssignmentVSTNodeTo(HookCall);

                SExprArgs ExprArgs = {};
                ExprArgs.AnalysisContext = EAnalysisContext::ContextAlreadyAnalyzed;
                HookCall = AnalyzeInPlace(HookCall, &CSemanticAnalyzerImpl::AnalyzeExpressionAst, ExprCtx, ExprArgs);
            }
            Block->AppendSubExpr(HookCall);

            Block->AppendSubExpr(Result);                                                 //     R
        }

        return Block;
    }

    TSPtr<CExpressionBase> AnalyzeAssignment(TSPtr<CExprAssignment> Assignment, const SExprCtx& ExprCtx)
    {
        Assignment->SetLhs(AnalyzeInPlace(Assignment->Lhs(), &CSemanticAnalyzerImpl::AnalyzeExpressionAst, ExprCtx, SExprArgs{}));

        if (TSPtr<CExpressionBase> R = TryAnalyzeParameterCollectionVarAssignment(Assignment, ExprCtx))
        {
            return R;
        }

        return AnalyzeAssignment_Internal(*Assignment, ExprCtx);
    }

    TSPtr<CExpressionBase> AnalyzeAssignment_Internal(CExprAssignment& Assignment, const SExprCtx& ExprCtx)
    {
        // we assume the LHS subexpression has already been analyzed

        // Is the Lhs type a reference to mutable? if not, it can't be assigned to
        const CReferenceType* LhsReferenceType = ValidateReferenceType(*Assignment.Lhs());
        if (!LhsReferenceType)
        {
            Assignment.SetResultType(_Program->GetDefaultUnknownType());
            return nullptr;
        }

        // Ensure that these effectful assignment operators aren't used in a
        // non-control-scope data member initializer.
        if (uLang::AnyOf(_Context._DataMembers, EnclosingScopeIsNotControl))
        {
            AppendGlitch(
                Assignment,
                EDiagnostic::ErrSemantic_CannotInitDataMemberWithSideEffect,
                "Expressions with side effects cannot be used when defining data-members.");
        }

        // If the op is =, just set the CExprAssignment's result type and use it as is.
        if (Assignment.Op() == CExprAssignment::EOp::assign)
        {
            // Analyze the RHS subexpression.
            if (TSPtr<CExpressionBase> NewRhs = AnalyzeExpressionAst(Assignment.Rhs().AsRef(), ExprCtx.WithResultIsUsed(LhsReferenceType->NegativeValueType())))
            {
                Assignment.SetRhs(Move(NewRhs));
            }

            // If the RHS might fail, warn that the current behavior is deprecated and will eventually change.
            if (Assignment.Rhs()->CanFail())
            {
                if (ULANG_ENSURE(_Context._Package) && _Context._Package->_EffectiveVerseVersion < Verse::Version::SetMutatesFallibility)
                {
                    AppendGlitch(*Assignment.Rhs(), EDiagnostic::WarnSemantic_DeprecatedFailureOnSetRhs);
                }
                else
                {
                    // Note that in the future, when implementing this, bumping the language version is not required.
                    AppendGlitch(*Assignment.Rhs(), EDiagnostic::ErrSemantic_Unimplemented, "Failure in the right operand of 'set ... = ...' is not yet implemented.");
                }
            }

            // Apply the LHS pointer's value type to the RHS value.
            if (TSPtr<CExpressionBase> NewRhs = ApplyTypeToExpression(
                *LhsReferenceType->NegativeValueType(),
                Assignment.Rhs().AsRef(),
                EDiagnostic::ErrSemantic_IncompatibleArgument,
                "This assignment expects", "the assigned value"))
            {
                Assignment.SetRhs(Move(NewRhs));
            }

            // Require the transacts effect for assignment.
            RequireEffects(Assignment, EffectSets::Transacts, ExprCtx.AllowedEffects, "assignment");

            Assignment.SetResultType(LhsReferenceType->PositiveValueType());
            return nullptr;
        }
        else
        {
            auto&& SynthesizeAssignOpCall = [this, &Assignment, &ExprCtx](TSPtr<CExpressionBase> Lhs, TSPtr<CExpressionBase> Rhs) -> TSPtr<CExpressionBase> {
                // Determine the name of the function to call for this assignment op.
                CSymbol OpFunctionName = _Program->_IntrinsicSymbols.GetAssignmentOpName(Assignment.Op());

                // Convert the assignment node into an invocation node.
                TSRef<CExprMakeTuple> Argument = TSRef<CExprMakeTuple>::New(Move(Lhs), Move(Rhs));
                TSRef<CExprInvocation> InvocationAst = TSRef<CExprInvocation>::New(
                  CExprInvocation::EBracketingStyle::Undefined,
                  TSRef<CExprIdentifierUnresolved>::New(OpFunctionName, nullptr, nullptr, true),
                  Argument);
                if (Assignment.GetMappedVstNode())
                {
                    Argument->SetNonReciprocalMappedVstNode(Assignment.GetMappedVstNode());
                    Assignment.GetMappedVstNode()->AddMapping(InvocationAst.Get());
                }

                // Analyze the invocation node, the Lhs has already been analyzed.
                SExprArgs InvocationArgs;
                InvocationArgs.AnalysisContext = EAnalysisContext::FirstTupleElementAlreadyAnalyzed;
                if (TSPtr<CExpressionBase> Result = AnalyzeInvocation(InvocationAst, ExprCtx, InvocationArgs))
                {
                    return Result;
                }
                else
                {
                    return InvocationAst;
                }
            };

            // VerseVM performs a similar lowering for all AssignOp calls in its code generator.
#if WITH_VERSE_BPVM
            TOptional<SCustomAccessorClassVarLhs> ClassVarLhs = CustomAccessorClassVarFromSetLhs(Assignment.Lhs());
            if (!ClassVarLhs)
#endif
            {
                return SynthesizeAssignOpCall(Assignment.Lhs(), Assignment.Rhs());
            }

#if WITH_VERSE_BPVM
            // whenever the lhs of the assign op is a class var that could have custom accessors:
            //   set Context.X += Y
            // =>
            //   block:
            //     C := Context
            //     var Fresh:t = C.X        # where t is the class var type, "Fresh" is a unique name
            //     set Fresh += Y           # "+=" in this example, but really any assign-op (eg. *=, -=, etc.)
            //                              # Y may contain declarations that are still scoped outside the block
            //     set C.X = Fresh          # the codegen will convert this statement to a runtime-assignment
            //                              # ... to handle a potentially required setter call

            TSPtr<CExprCodeBlock> Block = MakeCodeBlock();
            {
                // Evaluate the context once and cache the result in a local.
                if (TSPtr<CExpressionBase> LhsContext = ClassVarLhs->IdentifierData->Context())
                {
                    // Note: The context may have a reference type, if it is itself (projected from) a var.
                    // The local should store the object (pointer) itself, not a reference to it.
                    // This is okay because CustomAccessorClassVarFromSetLhs only accepts classes, not structs (which cannot contain vars).
                    TSRef<CExprIdentifierData> Context = MakeFreshLocal(*Block, ToValue(LhsContext.AsRef()));
                    ClassVarLhs->IdentifierData->SetContext(Context);
                }

                TSRef<CExprPointerToReference> AssignOpResult = MakeFreshLocalVar(
                    *Block,
                    LhsReferenceType->NegativeValueType(),
                    LhsReferenceType->PositiveValueType(),
                    ToValue(ClassVarLhs->PointerToReference.AsRef()));

                TSPtr<CExprSet> OpLhs = TSPtr<CExprSet>::New(AssignOpResult);
                OpLhs->SetResultType(LhsReferenceType);
                Block->AppendSubExpr(SynthesizeAssignOpCall(OpLhs, Assignment.Rhs()));

                {
                    TSPtr<CExpressionBase> Lhs = Assignment.Lhs();
                    TSPtr<CExpressionBase> Rhs = TSRef<CExprReferenceToValue>::New(AssignOpResult);
                    Rhs->SetResultType(LhsReferenceType->PositiveValueType());
                    auto AssignmentExpr = TSPtr<CExprAssignment>::New(CExprAssignment::EOp::assign, Move(Lhs), Move(Rhs));
                    AssignmentExpr->SetResultType(LhsReferenceType->PositiveValueType());
                    Block->AppendSubExpr(AssignmentExpr);
                }
            }

            if (Assignment.GetMappedVstNode())
            {
                Block->SetNonReciprocalMappedVstNode(Assignment.GetMappedVstNode());
                Assignment.GetMappedVstNode()->AddMapping(Block);
            }

            return Block;
#endif
        }
    }

    //-------------------------------------------------------------------------------------------------
    // Validate control after all expressions have been analyzed.
    SConditionalSkipFlags ValidateControlFlow(CExpressionBase* RootExpression)
    {
        // Iterate recursively across all expressions in routine
        SReachabilityAnalysisVisitor Visitor(*_Program, *_Diagnostics);
        Visitor.Visit(*RootExpression);
        return Visitor.GetDominatingSkips();
    }

    //-------------------------------------------------------------------------------------------------
    enum class ETypeTypesTag
    {
        Type,
        NotType,
        Error
    };

    struct STypeTypes
    {
        ETypeTypesTag _Tag;
        const CTypeBase* _NegativeType;
        const CTypeBase* _PositiveType;
    };

    void AppendExpectedTypeGlitch(const CExpressionBase& Expression)
    {
        AppendGlitch(
            Expression,
            EDiagnostic::ErrSemantic_ExpectedType,
            CUTF8String("Expected a type, got %s instead.", Expression.GetErrorDesc().AsCString()));
    }

    STypeTypes MaybeTypeTypes(const CTypeBase* Type)
    {
        // Check for an immediate `CTypeType`.
        if (const CTypeType* TypeType = Type->GetNormalType().AsNullable<CTypeType>())
        {
            AssertConstrain(Type, _Program->_typeType);
            return {
                ETypeTypesTag::Type,
                TypeType->NegativeType(),
                TypeType->PositiveType() };
        }
        // Otherwise, use more expensive subtyping check.
        if (!IsSubtype(Type, _Program->_typeType))
        {
            const CUnknownType* UnknownType = _Program->GetDefaultUnknownType();
            return {ETypeTypesTag::NotType, UnknownType, UnknownType};
        }
        CFlowType& NegativeFlowType1 = _Program->CreateNegativeFlowType();
        CFlowType& PositiveFlowType1 = _Program->CreatePositiveFlowType();
        NegativeFlowType1.AddFlowEdge(&PositiveFlowType1);
        PositiveFlowType1.AddFlowEdge(&NegativeFlowType1);
        CFlowType& NegativeFlowType2 = _Program->CreateNegativeFlowType();
        CFlowType& PositiveFlowType2 = _Program->CreatePositiveFlowType();
        NegativeFlowType2.AddFlowEdge(&PositiveFlowType2);
        PositiveFlowType2.AddFlowEdge(&NegativeFlowType2);
        AssertConstrain(Type, &_Program->GetOrCreateTypeType(&PositiveFlowType1, &NegativeFlowType2));
        return {
            ETypeTypesTag::Type,
            &NegativeFlowType1,
            &PositiveFlowType2};
    }

    STypeTypes MaybeTypeTypes(const CExpressionBase& Expression)
    {
        if (Expression.GetNodeType() == EAstNodeType::Error_)
        {
            const CTypeBase* Type = _Program->GetDefaultUnknownType();
            return {ETypeTypesTag::Error, Type, Type};
        }
        const CTypeBase* TypeType = Expression.GetResultType(*_Program);
        if (!TypeType)
        {
            const CTypeBase* Type = _Program->GetDefaultUnknownType();
            return {ETypeTypesTag::NotType, Type, Type};
        }
        else if (SemanticTypeUtils::IsUnknownType(TypeType))
        {
            return {ETypeTypesTag::Error, TypeType, TypeType};
        }
        return MaybeTypeTypes(TypeType);
    }

    STypeTypes GetTypeTypes(const CExpressionBase& Expression)
    {
        STypeTypes Result = MaybeTypeTypes(Expression);
        if (Result._Tag == ETypeTypesTag::NotType)
        {
            AppendExpectedTypeGlitch(Expression);
        }
        return Result;
    }

    using ETypeTypeTag = ETypeTypesTag;

    struct STypeType
    {
        ETypeTypeTag _Tag;
        const CTypeBase* _Type;
    };


    STypeType MaybeTypeNegativeType(const CExpressionBase& Expression)
    {
        STypeTypes Result = MaybeTypeTypes(Expression);
        return {Result._Tag, Result._NegativeType};
    }

    STypeType GetTypeNegativeType(const CExpressionBase& Expression)
    {
        STypeType Result = MaybeTypeNegativeType(Expression);
        if (Result._Tag == ETypeTypesTag::NotType)
        {
            AppendExpectedTypeGlitch(Expression);
        }
        return Result;
    }

    STypeType MaybeTypePositiveType(const CExpressionBase& Expression)
    {
        STypeTypes Result = MaybeTypeTypes(Expression);
        return {Result._Tag, Result._PositiveType};
    }

    STypeType GetTypePositiveType(const CExpressionBase& Expression)
    {
        STypeType Result = MaybeTypePositiveType(Expression);
        if (Result._Tag == ETypeTypesTag::NotType)
        {
            AppendExpectedTypeGlitch(Expression);
        }
        return Result;
    }

    //-------------------------------------------------------------------------------------------------
    void RequireNonReservedSymbol(const CExprIdentifierUnresolved& IdentifierAst, const CSymbol DefinitionSymbol)
    {
        const EIsReservedSymbolResult ReservedResult = IsReservedSymbol(DefinitionSymbol);
        if (ReservedResult == EIsReservedSymbolResult::Reserved)
        {
            // If the identifier is a reserved symbol, produce an error and generate a deduped name.
            AppendGlitch(IdentifierAst, EDiagnostic::ErrSemantic_RedefinitionOfReservedIdentifier, CUTF8String("Cannot use reserved identifier `%s` as definition name.", DefinitionSymbol.AsCString()));
        }
        else if (ReservedResult == EIsReservedSymbolResult::ReservedFuture)
        {
            AppendGlitch(IdentifierAst, EDiagnostic::WarnSemantic_ReservedFutureIdentifier, CUTF8String("The identifier: `%s` has been reserved in a future version of Verse. You should rename this identifier.", DefinitionSymbol.AsCString()));
        }
    }

    void RequireNonReservedSymbol(const CExprIdentifierUnresolved& IdentifierAst)
    {
        RequireNonReservedSymbol(IdentifierAst, IdentifierAst._Symbol);
    }

    //-------------------------------------------------------------------------------------------------
    void ValidateDefinitionIdentifier(const CExprIdentifierUnresolved& IdentifierAst, const CScope& /*EnclosingScope*/)
    {
        RequireNonReservedSymbol(IdentifierAst);
    }

    //-------------------------------------------------------------------------------------------------
    CSymbol GenerateUniqueName(const CUTF8StringView& BaseName, const CScope& EnclosingScope)
    {
        CSymbol Result;
        // Generate a unique symbol using a prefix ("__dupe_"), the original identifier, and a number suffix.
        do
        {
            Result = _Program->GetSymbols()->AddChecked(CUTF8String(
                "__dupe_%s_%zu",
                CUTF8String(BaseName.SubViewBegin(CSymbolTable::MaxSymbolLength-32)).AsCString(),
                _NextUniqueSymbolId++),
                true);
        } while (EnclosingScope.ResolveDefinition(Result).Num());

        return Result;
    }

    //-------------------------------------------------------------------------------------------------
    bool Constrain(const CTypeBase* Type1, const CTypeBase* Type2)
    {
        return SemanticTypeUtils::Constrain(Type1, Type2);
    }

    void AssertConstrain(const CTypeBase* Type1, const CTypeBase* Type2)
    {
        bool bConstrained = Constrain(Type1, Type2);
        ULANG_ASSERTF(bConstrained, "`Type1` must be a subtype of `Type2`");
    }

    bool IsSubtype(const CTypeBase* Type1, const CTypeBase* Type2)
    {
        return SemanticTypeUtils::IsSubtype(Type1, Type2);
    }

    bool Matches(const CTypeBase* Type1, const CTypeBase* Type2)
    {
        return SemanticTypeUtils::Matches(Type1, Type2);
    }

    const CTypeBase* Meet(const CTypeBase* Type1, const CTypeBase* Type2)
    {
        return SemanticTypeUtils::Meet(Type1, Type2);
    }

    const CTypeBase* Join(const CTypeBase* Type1, const CTypeBase* Type2)
    {
        return SemanticTypeUtils::Join(Type1, Type2);
    }

    struct SInstantiatedFunction
    {
        TArray<SInstantiatedTypeVariable> _InstantiatedTypeVariables;
        const CFunctionType* _Type;
        const CTypeBase* _NegativeReturnType;
    };

    SInstantiatedFunction Instantiate(const CFunction& Function)
    {
        const CFunctionType* FunctionType = Function._Signature.GetFunctionType();
        if (!Function._Signature.GetFunctionType())
        {
            AppendGlitch(
                *Function.GetDefineeAst(),
                EDiagnostic::ErrSemantic_Unimplemented,
                "Can't access a function from a preceding type.");
            return {{}, nullptr, nullptr};
        }

        const CTypeBase* NegativeReturnType = &Function._NegativeType->GetReturnType();

        int32_t FunctionIndex = Function.Index();
        SFunctionVertex* FunctionVertex = &GetFunctionVertex(FunctionIndex);
        // Corresponds to a successor iteration in Tarjan's SCC algorithm.
        if (FunctionVertex->_Number == -1)
        {
            FunctionVertex = &StrongConnectFunctionVertex(FunctionIndex, FunctionVertex);
            if (const CFunction* ContextFunction = _Context._Function)
            {
                SFunctionVertex& ContextFunctionVertex = GetFunctionVertex(ContextFunction->Index());
                FunctionVertex = &GetFunctionVertex(Function); // The FunctionVertex pointer might be invalidated by the above GetFunctionVertex.
                AssignMin(ContextFunctionVertex._LowLink, FunctionVertex->_LowLink);
            }
        }
        else if (FunctionVertex->_OnStack)
        {
            if (const CFunction* ContextFunction = _Context._Function)
            {
                SFunctionVertex& ContextFunctionVertex = GetFunctionVertex(ContextFunction->Index());
                FunctionVertex = &GetFunctionVertex(Function); // The FunctionVertex pointer might be invalidated by the above GetFunctionVertex.
                AssignMin(ContextFunctionVertex._LowLink, FunctionVertex->_Number);
            }
        }

        TArray<SInstantiatedTypeVariable> InstTypeVariables;
        // Only instantiate functions that have been generalized, i.e. functions
        // referenced from outside their SCC.
        if (Generalized(*FunctionVertex))
        {
            // Ensure a recursive type is not created.
            if (!Function.GetReturnTypeAst())
            {
                const TSPtr<CExpressionBase>& FunctionDefinee = Function.GetDefineeAst();
                if (!RequireTypeIsNotRecursive(Function._NegativeType, FunctionDefinee.Get()) ||
                    !RequireTypeIsNotRecursive(FunctionType, FunctionDefinee.Get()))
                {
                    const CUnknownType* UnknownType = _Program->GetDefaultUnknownType();
                    FunctionType = &_Program->GetOrCreateFunctionType(
                        *UnknownType,
                        *UnknownType,
                        FunctionType->GetEffects(),
                        {},
                        FunctionType->ImplicitlySpecialized());
                    NegativeReturnType = UnknownType;
                    return {Move(InstTypeVariables), FunctionType, NegativeReturnType};
                }
            }
            TArray<STypeVariableSubstitution> Substitutions = SemanticTypeUtils::Instantiate(FunctionType->GetTypeVariables());
            if (!Substitutions.IsEmpty())
            {
                const CTypeBase* ParamsType = &FunctionType->GetParamsType();
                const CTypeBase* ReturnType = &FunctionType->GetReturnType();
                const CTypeBase* InstParamsType = SemanticTypeUtils::Substitute(*ParamsType, ETypePolarity::Negative, Substitutions);
                const CTypeBase* InstReturnType = SemanticTypeUtils::Substitute(*ReturnType, ETypePolarity::Positive, Substitutions);
                FunctionType = &FunctionType->GetProgram().GetOrCreateFunctionType(
                    *InstParamsType,
                    *InstReturnType,
                    FunctionType->GetEffects(),
                    {},
                    FunctionType->ImplicitlySpecialized());
                NegativeReturnType = SemanticTypeUtils::Substitute(*NegativeReturnType, ETypePolarity::Negative, Substitutions);
                InstTypeVariables.Reserve(Substitutions.Num());
                for (auto&& [TypeVariable, NegativeFlowType, PositiveFlowType] : Substitutions)
                {
                    InstTypeVariables.Add({NegativeFlowType, PositiveFlowType });
                }
            }
        }
        return {Move(InstTypeVariables), FunctionType, NegativeReturnType};
    }

    // Replace any functors contained in `Type` with the domain of the functor.
    // @note This currently only handles `void` and `tuple`s of `void`.
    const CTypeBase& GetFunctorDomain(const CTypeBase& Type)
    {
        if (Type.AsFlowType())
        {
            return Type;
        }
        const CNormalType& NormalType = Type.GetNormalType();
        if (NormalType.IsA<CVoidType>())
        {
            return _Program->_anyType;
        }
        if (const CTupleType* TupleType = NormalType.AsNullable<CTupleType>())
        {
            const CTupleType::ElementArray& Elements = TupleType->GetElements();
            CTupleType::ElementArray NewElements;
            NewElements.Reserve(Elements.Num());
            for (const CTypeBase* Element : Elements)
            {
                NewElements.Add(&GetFunctorDomain(*Element));
            }
            CTupleType& NewTupleType = _Program->GetOrCreateTupleType(
                Move(NewElements),
                TupleType->GetFirstNamedIndex());
            return NewTupleType;
        }
        return NormalType;
    }

    void ConstrainExpressionToType(
        const TSRef<CExpressionBase>& Expression,
        const CTypeBase& TypeOrFunctor,
        EDiagnostic TypeError,
        const char* TypeOrFunctorDescription,
        const char* ArgumentDescription)
    {
        const CTypeBase* ExpressionType = Expression->GetResultType(*_Program);
        const CNormalType& NormalTypeOrFunctor = TypeOrFunctor.GetNormalType();
        const CTypeBase& TypeOrFunctorDomain = GetFunctorDomain(TypeOrFunctor);
        if (const CUnknownType* UnknownType = NormalTypeOrFunctor.AsNullable<CUnknownType>())
        {
            // Suggest type if the type is unknown
            UnknownType->_SuggestedTypes.Add(ExpressionType);
        }
        else if (!Constrain(ExpressionType, &TypeOrFunctorDomain))
        {
            if (!SemanticTypeUtils::IsUnknownType(&TypeOrFunctorDomain)
                && !SemanticTypeUtils::IsUnknownType(ExpressionType))
            {
                AppendGlitch(
                    FindMappedVstNode(*Expression),
                    TypeError,
                    CUTF8String("%s a value of type %s, but %s is an incompatible value of type %s.",
                        TypeOrFunctorDescription,
                        TypeOrFunctorDomain.AsCode().AsCString(),
                        ArgumentDescription,
                        ExpressionType->AsCode().AsCString()));
            }
        }
    }

    // Applies a "type" to an expression as an infallible call: i.e. type(<expr>)
    // This will produce an error if the expression might have a value outside the domain of the type.
    // Most types are identity functions, so this will either produce an error or a no-op.
    // In the case of void, it is a functor that maps any value to false. This will produce a new
    // expression that encodes the application void(<expr>).
    // If a new expression is produced, it is returned. If the type is an identity function, null will
    // be returned to indicate that the input expression should be used.
    TSPtr<CExpressionBase> ApplyTypeToExpression(
        const CTypeBase& TypeOrFunctor,
        const TSRef<CExpressionBase>& Expression,
        EDiagnostic TypeError,
        const char* TypeOrFunctorDescription,
        const char* ArgumentDescription)
    {
        ConstrainExpressionToType(
            Expression,
            TypeOrFunctor,
            TypeError,
            TypeOrFunctorDescription,
            ArgumentDescription);
        return TSRef<CExprInvokeType>::New(
            &TypeOrFunctor,
            Expression->GetResultType(*_Program),
            false,
            TSPtr<CExpressionBase>(),
            TSRef<CExpressionBase>(Expression));
    }

    //-------------------------------------------------------------------------------------------------
    const Vst::Node* FindMappedVstNode(const CExpressionBase& Expression)
    {
        if (const Vst::Node* MappedVstNode = Expression.GetMappedVstNode())
        {
            return MappedVstNode;
        }
        else
        {
            return _Context._VstNode;
        }
    }

    //-------------------------------------------------------------------------------------------------
    void ReplaceMapping(const CExpressionBase& OldAstNode, CExpressionBase& NewAstNode)
    {
        const Vst::Node* VstNode = OldAstNode.GetMappedVstNode();
        if (VstNode)
        {
            VstNode->AddMapping(&NewAstNode);
        }
    }

    TSRef<CExpressionBase> ReplaceMapping(const CExpressionBase& OldAstNode, TSRef<CExpressionBase>&& NewAstNode)
    {
        ReplaceMapping(OldAstNode, *NewAstNode);
        return Move(NewAstNode);
    }

    //-------------------------------------------------------------------------------------------------
    CSymbol VerifyAddSymbol(const Vst::Node* VstNode, const CUTF8StringView& Text)
    {
        TOptional<CSymbol> OptionalSymbol = _Program->GetSymbols()->Add(Text);
        if (!OptionalSymbol.IsSet())
        {
            AppendGlitch(VstNode, EDiagnostic::ErrSemantic_TooLongIdentifier);
            OptionalSymbol = _Program->GetSymbols()->Add(Text.SubViewBegin(CSymbolTable::MaxSymbolLength - 1), true);
            ULANG_ASSERTF(OptionalSymbol.IsSet(), "Truncated name is too long");
        }
        return OptionalSymbol.GetValue();
    }

    //-------------------------------------------------------------------------------------------------
    CSymbol VerifyAddSymbol(const CAstNode& AstNode, const CUTF8StringView& Text)
    {
        TOptional<CSymbol> OptionalSymbol = _Program->GetSymbols()->Add(Text);
        if (!OptionalSymbol.IsSet())
        {
            AppendGlitch(AstNode, EDiagnostic::ErrSemantic_TooLongIdentifier);
            OptionalSymbol = _Program->GetSymbols()->Add(Text.SubViewBegin(CSymbolTable::MaxSymbolLength - 1), true);
            ULANG_ASSERTF(OptionalSymbol.IsSet(), "Truncated name is too long");
        }
        return OptionalSymbol.GetValue();
    }

    //-------------------------------------------------------------------------------------------------
    template<typename... ResultArgsType>
    void AppendGlitch(const Vst::Node* VstNode, ResultArgsType&&... ResultArgs)
    {
        SGlitchResult Glitch(uLang::ForwardArg<ResultArgsType>(ResultArgs)...);
        ULANG_ASSERTF(
            VstNode && VstNode->Whence().IsValid(),
            "Expected valid whence for node used as glitch locus on %i - %s",
            GetDiagnosticInfo(Glitch._Id).ReferenceCode,
            Glitch._Message.AsCString());
        _Diagnostics->AppendGlitch(Move(Glitch), SGlitchLocus(VstNode));
    }

    //-------------------------------------------------------------------------------------------------
    template<typename... ResultArgsType>
    void AppendGlitch(const CAstNode& AstNode, ResultArgsType&&... ResultArgs)
    {
        SGlitchResult Glitch(uLang::ForwardArg<ResultArgsType>(ResultArgs)...);
        if (AstNode.GetNodeType() == Cases<EAstNodeType::Context_Package, EAstNodeType::Definition_Module>
            && (!AstNode.GetMappedVstNode() || !AstNode.GetMappedVstNode()->Whence().IsValid()))
        {
            _Diagnostics->AppendGlitch(Move(Glitch), SGlitchLocus());
        }
        else
        {
            ULANG_ASSERTF(
                AstNode.GetMappedVstNode() && AstNode.GetMappedVstNode()->Whence().IsValid(),
                "Expected valid whence for node used as glitch locus on %s id:%i - %s",
                AstNode.GetErrorDesc().AsCString(),
                GetDiagnosticInfo(Glitch._Id).ReferenceCode,
                Glitch._Message.AsCString());
            _Diagnostics->AppendGlitch(Move(Glitch), SGlitchLocus(&AstNode));
        }
    }
    
    //-------------------------------------------------------------------------------------------------
    void AppendExpectedDefinitionError(const CExpressionBase& ExpressionAst)
    {
        // Check if the expression is of the form a=b, and provide an error message with the suggestion
        // to change = to :=
        if (ExpressionAst.GetNodeType() == EAstNodeType::Invoke_Comparison
            && static_cast<const CExprComparison&>(ExpressionAst).Op() == Vst::BinaryOpCompare::op::eq)
        {
            AppendGlitch(
                ExpressionAst,
                EDiagnostic::ErrSemantic_ExpectedDefinition,
                CUTF8String("Expected definition but found %s. Did you mean to use ':=' instead of '='?",
                    ExpressionAst.GetErrorDesc().AsCString()));
        }
        // If the expression is an invocation, assume that a function declaration was intended and produce an error
        // that the return type is missing.
        else if (ExpressionAst.GetNodeType() == EAstNodeType::Invoke_Invocation)
        {
            AppendGlitch(ExpressionAst, EDiagnostic::ErrSemantic_FunctionSignatureMustDeclareReturn);
        }
        else if (ExpressionAst.GetNodeType() == EAstNodeType::Definition)
        {   // This is to get rid of the error message "Expected definition but found definition."
            // AnalyzeExpressionAst called on :array{} ends up here.
            AppendGlitch(
                ExpressionAst,
                EDiagnostic::ErrSemantic_ExpectedDefinition,
                CUTF8String("Expected definition but found ':' operator.", ExpressionAst.GetErrorDesc().AsCString()));
        }
        else
        {
            AppendGlitch(
                ExpressionAst,
                EDiagnostic::ErrSemantic_ExpectedDefinition,
                CUTF8String("Expected definition but found %s.", ExpressionAst.GetErrorDesc().AsCString()));
        }
    }

    //-------------------------------------------------------------------------------------------------
    void AppendAttributesNotAllowedError(const CExpressionBase& ExpressionAst)
    {
        AppendGlitch(
            ExpressionAst,
            EDiagnostic::ErrSemantic_AttributeNotAllowed,
            CUTF8String("Attributes are not allowed on %s expressions.", ExpressionAst.GetErrorDesc().AsCString()));
    }

    //-------------------------------------------------------------------------------------------------
    void MaybeAppendAttributesNotAllowedError(const CExpressionBase& ExpressionAst)
    {
        // Produce an error if an expression that doesn't support attributes has attributes.
        if(ExpressionAst.HasAttributes())
        {
            AppendAttributesNotAllowedError(ExpressionAst);
        }
    }

    //-------------------------------------------------------------------------------------------------
    void EnqueueDeferredTask(EDeferredPri Pri, TFunction<void()>&& TaskBlock)
    {
        if (Pri <= _CurrentTaskPhase && _CurrentTaskPhase != Deferred__Invalid)
        {
            TaskBlock();
        }
        else
        {
            SDeferredTask* Task = (SDeferredTask*)_DeferredTaskAllocator.Allocate(sizeof(SDeferredTask));
            new(Task) SDeferredTask;
            Task->NextTask = nullptr;
            if (_DeferredTasks[Pri]._Tail)
            {
                _DeferredTasks[Pri]._Tail->NextTask = Task;
                _DeferredTasks[Pri]._Tail = Task;
            }
            else
            {
                _DeferredTasks[Pri]._Head = _DeferredTasks[Pri]._Tail = Task;
            }
            Task->Run = Move(TaskBlock);
            Task->_Context = _Context;
        }
    }

    //-------------------------------------------------------------------------------------------------
    struct SDeferredTask;
    void DeleteDeferredTask(SDeferredTask* Task)
    {
        Task->~SDeferredTask();
        // Arena allocator never releases memory
    }

    //-------------------------------------------------------------------------------------------------
    bool IsReservedOperatorSymbol(const CSymbol& Symbol)
    {
        auto const& IntrinsicSymbols = _Program->_IntrinsicSymbols;
        if (IntrinsicSymbols.IsOperatorOpName(Symbol))
        {
            return
                Symbol != IntrinsicSymbols._OpNameAdd &&
                Symbol != IntrinsicSymbols._OpNameSub &&
                Symbol != IntrinsicSymbols._OpNameMul &&
                Symbol != IntrinsicSymbols._OpNameDiv &&
                Symbol != IntrinsicSymbols._OpNameQuery;
        }
        if (_Program->_IntrinsicSymbols.IsPrefixOpName(Symbol))
        {
            return Symbol != IntrinsicSymbols._OpNameNegate;
        }
        if (_Program->_IntrinsicSymbols.IsPostfixOpName(Symbol))
        {
            return true;
        }
        return false;
    }

    //-------------------------------------------------------------------------------------------------
    EIsReservedSymbolResult IsReservedSymbol(const CSymbol& Symbol)
    {
        const uint32_t EffectiveVerseVersion = _Context._Package->_EffectiveVerseVersion;
        const uint32_t UploadedAtFNVersion = _Context._Package->_UploadedAtFNVersion;

        // TODO: (yiliang.siew) Migrate this function to just list all the permutations in `ReservedSymbols.inl` as well.
        if (IsReservedOperatorSymbol(Symbol))
        {
            return EIsReservedSymbolResult::Reserved;
        }
        return GetReservationForSymbol(Symbol, EffectiveVerseVersion, UploadedAtFNVersion);
    }

    //-------------------------------------------------------------------------------------------------
    void ValidateNonAttributeType(const CTypeBase* Type, const Vst::Node* Context)
    {
        if (Type)
        {
            // Defer this check until after types are fully processed to avoid triggering recursive
            // parametric type instantiation.
            EnqueueDeferredTask(Deferred_ValidateType,[this, Type, Context]
            {
                if (SemanticTypeUtils::IsAttributeType(Type))
                {
                    // @TODO: SOL-972, need better (fuller) support for attributes
                    AppendGlitch(
                        Context,
                        EDiagnostic::ErrSemantic_Unsupported,
                        CUTF8String("Attributes cannot be used as data types. Attribute types are meant to be used in attribute expressions: @%s or <%s>",
                            Type->GetNormalType().AsNominalType()->Definition()->AsNameCString(),
                            Type->GetNormalType().AsNominalType()->Definition()->AsNameCString())
                    );
                }
            });
        } 
    }

    struct STypeVariablePolarity
    {
        const CTypeVariable* TypeVariable;
        ETypePolarity Polarity;

        friend bool operator<(const STypeVariablePolarity& Left, const STypeVariablePolarity& Right)
        {
            if (Left.TypeVariable < Right.TypeVariable)
            {
                return true;
            }
            if (Right.TypeVariable < Left.TypeVariable)
            {
                return false;
            }
            return Left.Polarity < Right.Polarity;
        }
    };

    using STypeVariablePolarities = TArray<STypeVariablePolarity>;

    struct SPolarNormalType
    {
        const CNormalType* NormalType;
        const ETypePolarity Polarity;

        friend bool operator==(const SPolarNormalType& Left, const SPolarNormalType& Right)
        {
            return Left.NormalType == Right.NormalType && Left.Polarity == Right.Polarity;
        }
    };

    void FillTypeVariablePolaritiesImpl(
        const CTypeBase* Type,
        ETypePolarity Polarity,
        STypeVariablePolarities& TypeVariablePolarities,
        TArray<SPolarNormalType>& Visited)
    {
        const CNormalType& NormalType = Type->GetNormalType();
        if (auto Last = Visited.end(); uLang::Find(Visited.begin(), Last, SPolarNormalType{&NormalType, Polarity}) != Last)
        {
            return;
        }
        Visited.Add({&NormalType, Polarity});
        switch (NormalType.GetKind())
        {
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
            break;
        case ETypeKind::Class:
        {
            const CClass& Class = NormalType.AsChecked<CClass>();
            if (Class.GetParentScope()->GetKind() != CScope::EKind::Function)
            {
                break;
            }
            for (CDataDefinition* DataMember : Class.GetDefinitionsOfKind<CDataDefinition>())
            {
                FillTypeVariablePolaritiesImpl(
                    DataMember->GetType(),
                    Polarity,
                    TypeVariablePolarities,
                    Visited);
            }
            for (CFunction* Function : Class.GetDefinitionsOfKind<CFunction>())
            {
                FillTypeVariablePolaritiesImpl(
                    Function->_Signature.GetFunctionType(),
                    Polarity,
                    TypeVariablePolarities,
                    Visited);
            }
            break;
        }
        case ETypeKind::Array:
            FillTypeVariablePolaritiesImpl(
                NormalType.AsChecked<CArrayType>().GetElementType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
            break;
        case ETypeKind::Generator:
            FillTypeVariablePolaritiesImpl(
                NormalType.AsChecked<CGeneratorType>().GetElementType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
            break;
        case ETypeKind::Map:
        {
            const CMapType& MapType = NormalType.AsChecked<CMapType>();
            FillTypeVariablePolaritiesImpl(
                MapType.GetKeyType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
            FillTypeVariablePolaritiesImpl(
                MapType.GetValueType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
            break;
        }
        case ETypeKind::Pointer:
        {
            const CPointerType& PointerType = NormalType.AsChecked<CPointerType>();
            FillTypeVariablePolaritiesImpl(
                PointerType.NegativeValueType(),
                FlipPolarity(Polarity),
                TypeVariablePolarities,
                Visited);
            FillTypeVariablePolaritiesImpl(
                PointerType.PositiveValueType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
            break;
        }
        case ETypeKind::Reference:
        {
            const CReferenceType& ReferenceType = NormalType.AsChecked<CReferenceType>();
            FillTypeVariablePolaritiesImpl(
                ReferenceType.NegativeValueType(),
                FlipPolarity(Polarity),
                TypeVariablePolarities,
                Visited);
            FillTypeVariablePolaritiesImpl(
                ReferenceType.PositiveValueType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
            break;
        }
        case ETypeKind::Option:
            FillTypeVariablePolaritiesImpl(
                NormalType.AsChecked<COptionType>().GetValueType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
            break;
        case ETypeKind::Type:
        {
            const CTypeType& TypeType = NormalType.AsChecked<CTypeType>();
            FillTypeVariablePolaritiesImpl(
                TypeType.NegativeType(),
                FlipPolarity(Polarity),
                TypeVariablePolarities,
                Visited);
            FillTypeVariablePolaritiesImpl(
                TypeType.PositiveType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
            break;
        }
        case ETypeKind::Interface:
        {
            const CInterface& Interface = NormalType.AsChecked<CInterface>();
            if (Interface.GetParentScope()->GetKind() != CScope::EKind::Function)
            {
                break;
            }
            for (const CFunction* Function : Interface.GetDefinitionsOfKind<CFunction>())
            {
                FillTypeVariablePolaritiesImpl(
                    Function->_Signature.GetFunctionType(),
                    Polarity,
                    TypeVariablePolarities,
                    Visited);
            }
            break;
        }
        case ETypeKind::Tuple:
        {
            const CTupleType& TupleType = NormalType.AsChecked<CTupleType>();
            for (const CTypeBase* ElementType : TupleType.GetElements())
            {
                FillTypeVariablePolaritiesImpl(
                    ElementType,
                    Polarity,
                    TypeVariablePolarities,
                    Visited);
            }
            break;
        }
        case ETypeKind::Function:
        {
            const CFunctionType& FunctionType = NormalType.AsChecked<CFunctionType>();
            FillTypeVariablePolaritiesImpl(
                &FunctionType.GetParamsType(),
                FlipPolarity(Polarity),
                TypeVariablePolarities,
                Visited);
            FillTypeVariablePolaritiesImpl(
                &FunctionType.GetReturnType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
            break;
        }
        case ETypeKind::Variable:
        {
            const CTypeVariable& TypeVariable = NormalType.AsChecked<CTypeVariable>();
            if (TypeVariable._ExplicitParam)
            {
                break;
            }
            TypeVariablePolarities.Add({&TypeVariable, Polarity});
            break;
        }
        case ETypeKind::Named:
            FillTypeVariablePolaritiesImpl(
                NormalType.AsChecked<CNamedType>().GetValueType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
            break;
        default:
            ULANG_UNREACHABLE();
        }
    }

    void FillTypeVariablePolarities(
        const CTypeBase* Type,
        ETypePolarity Polarity,
        STypeVariablePolarities& TypeVariablePolarities)
    {
        TArray<SPolarNormalType> Visited;
        FillTypeVariablePolaritiesImpl(Type, Polarity, TypeVariablePolarities, Visited);
    }

    void AppendUnusedTypeVariableGlitch(const CTypeVariable& TypeVariable, const CAstNode& AstNode)
    {
        AppendGlitch(
            AstNode,
            EDiagnostic::ErrSemantic_AmbiguousTypeVariable,
            CUTF8String(
                "Type variable %s is unused.  To avoid an ambiguous type for %s, ensure it is used in an argument position.",
                TypeVariable.AsNameCString(), TypeVariable.AsNameCString()));
    }

    void AppendAmbiguousTypeVariableGlitch(const CTypeVariable& TypeVariable, const CAstNode& AstNode)
    {
        AppendGlitch(
            AstNode,
            EDiagnostic::ErrSemantic_AmbiguousTypeVariable,
            CUTF8String(
                "Type variable %s used only in a result position.  To avoid an ambiguous type for %s, ensure it is also used in an argument position.",
                TypeVariable.AsNameCString(), TypeVariable.AsNameCString()));
    }

    void ValidateFunctionTypeVariables(const CFunctionType& FunctionType, const CAstNode& AstNode)
    {
        TArray<const CTypeVariable*> TypeVariables = FunctionType.GetTypeVariables();
        TypeVariables.RemoveAll([](const CTypeVariable* Arg) { return Arg->_ExplicitParam; });
        uLang::Algo::Sort(TypeVariables);
        STypeVariablePolarities TypesVariablePolarities;
        FillTypeVariablePolarities(&FunctionType, ETypePolarity::Positive, TypesVariablePolarities);
        uLang::Algo::Sort(TypesVariablePolarities);
        auto CurrentTypeVariablePolarity = TypesVariablePolarities.begin();
        auto LastTypeVariablePolarity = TypesVariablePolarities.end();
        for (auto CurrentTypeVariable = TypeVariables.begin(), LastTypeVariable = TypeVariables.end();
             CurrentTypeVariable != LastTypeVariable;
             ++CurrentTypeVariable)
        {
            for (;;)
            {
                if (CurrentTypeVariablePolarity == LastTypeVariablePolarity)
                {
                    // No further uses of type variable definitions. Remaining
                    // type variable definitions are ambiguous.
                    for (; CurrentTypeVariable != LastTypeVariable; ++CurrentTypeVariable)
                    {
                        AppendUnusedTypeVariableGlitch(**CurrentTypeVariable, AstNode);
                    }
                    return;
                }
                if (*CurrentTypeVariable < CurrentTypeVariablePolarity->TypeVariable)
                {
                    // No uses of the current type variable definition. The
                    // current type variable definition is ambiguous.
                    AppendUnusedTypeVariableGlitch(**CurrentTypeVariable, AstNode);
                    // Move on to the next type variable definition.
                    break;
                }
                if (*CurrentTypeVariable == CurrentTypeVariablePolarity->TypeVariable)
                {
                    // If all type variable uses are positive (where the sorting
                    // ensures negative uses occur before positive uses), the
                    // type variable definition is ambiguous.
                    if (CurrentTypeVariablePolarity->Polarity == ETypePolarity::Positive)
                    {
                        AppendAmbiguousTypeVariableGlitch(**CurrentTypeVariable, AstNode);
                    }
                    // Skip the remaining (positive) uses of the current type
                    // variable definition.
                    ++CurrentTypeVariablePolarity;
                    for (;
                         CurrentTypeVariablePolarity != LastTypeVariablePolarity &&
                         *CurrentTypeVariable == CurrentTypeVariablePolarity->TypeVariable;
                         ++CurrentTypeVariablePolarity);
                    // Move on to the next type variable definition.
                    break;
                }
                // The current type variable use refers to a definition from an
                // outer scope, but later uses may refer to the current
                // definition.  Move on to the next type variable use.
                ++CurrentTypeVariablePolarity;
            }
        }
    }

    static constexpr const char * const _MissingTypeString = "<INDETERMINATE>";

    TSRef<CSemanticProgram> _Program;
    TSRef<CDiagnostics> _Diagnostics;
    SemanticRevision _NextRevision;

    const TArray<CUTF8String> _BuiltInPackageNames;
    const TUPtr<SPackageUsage>& _OutPackageUsage;

    const SBuildParams& _BuildParams;
    bool _HasPersistentClass = false;

    const CSymbol _UnknownTypeName;
    const CSymbol _LogicLitSym_True;
    const CSymbol _LogicLitSym_False;
    const CSymbol _SelfName;
    const CSymbol _SuperName;
    const CSymbol _LocalName;
    const CSymbol _Symbol_subtype;
    const CSymbol _Symbol_tuple;
    const CSymbol _Symbol_break;
    const CSymbol _Symbol_import;
    const CSymbol _Symbol_generator;
    const CSymbol _TaskName;
    const CSymbol _ForClauseScopeName;

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    struct SInnateMacro
    {
        SInnateMacro(const TSPtr<CSemanticProgram>& InProgram)
        : _array(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Array)))
        , _block(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Block)))
        , _let(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Let)))
        , _branch(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Branch)))
        , _break(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Break)))
        , _case(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Case)))
        , _class(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Class)))
        , _defer(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Defer)))
        , _enum(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Enum)))
        , _external(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::External)))
        , _for(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::For)))
        , _interface(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Interface)))
        , _loop(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Loop)))
        , _map(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Map)))
        , _module(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Module)))
        , _option(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Option)))
        , _race(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Race)))
        , _rush(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Rush)))
        , _spawn(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Spawn)))
        , _struct(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Struct)))
        , _sync(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Sync)))
        , _type(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Type)))
        , _using(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Using)))
        , _scoped(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Scoped)))
        , _profile(InProgram->GetSymbols()->AddChecked(GetReservedSymbol(EReservedSymbol::Profile)))
        {}
        
        CSymbol _array;
        CSymbol _block;
        CSymbol _let;
        CSymbol _branch;
        CSymbol _break;
        CSymbol _case;
        CSymbol _class;
        CSymbol _defer;
        CSymbol _enum;
        CSymbol _external;
        CSymbol _for;
        CSymbol _interface;
        CSymbol _loop;
        CSymbol _map;
        CSymbol _module;
        CSymbol _option;
        CSymbol _race;
        CSymbol _rush;
        CSymbol _spawn;
        CSymbol _struct;
        CSymbol _sync;
        CSymbol _type;
        CSymbol _using;
        CSymbol _scoped;
        CSymbol _profile;
    } _InnateMacros;

    TArray<CSymbol> _NamesReservedForFuture;

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Context dependent state maintained by the analyzer through the call hierarchy
    // Must be transferred to deferred tasks to ensure proper context
    struct SContext
    {
        CScope* _Scope = nullptr;
        const CTypeBase* _Self = nullptr;
        const CFunction* _Function = nullptr;
        TArray<const CDataDefinition*> _DataMembers;
        const CExpressionBase* _Breakable = nullptr;
        const CExpressionBase* _Loop = nullptr;
        const CExprDefer* _Defer = nullptr;
        const CExprCodeBlock* _ClassBlockClause = nullptr;
        const Vst::Node* _VstNode{nullptr};
        CAstPackage* _Package{nullptr};
        bool _bIsAnalyzingArgumentsInInvocation{false};
        TArray<const CDefinition*> _EnclosingDefinitions;
    } _Context;

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    struct SDeferredTask
    {
        SDeferredTask* NextTask;
        TFunction<void()> Run;
        SContext _Context;
    };

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    struct SDeferredTaskList
    {
        SDeferredTask* _Head{nullptr};
        SDeferredTask* _Tail{nullptr};
    };

    SDeferredTaskList _DeferredTasks[Deferred__Num]; // Singly linked lists of tasks
    CArenaAllocator _DeferredTaskAllocator; // Fast allocator for tasks

    // Extra information for `CFunction` needed during semantic analysis
    struct SFunctionVertex
    {
        // Information relevant to Tarjan's SCC algorithm.  Functions that do
        // not specify a result type may require processing upon being
        // referenced via an identifier.  In the case of recursive references,
        // such functions are considered to be monomorphic with respect to other
        // functions referenced in the same SCC.
        int32_t _Number = -1;
        int32_t _LowLink = -1;
        bool _OnStack = false;
        int32_t _NextStackIndex = -1;
        TFunction<void()> _ProcessFunctionBody;
    };

    SFunctionVertex& GetFunctionVertex(const CFunction& Function)
    {
        return GetFunctionVertex(Function.Index());
    }

    SFunctionVertex& GetFunctionVertex(int32_t FunctionIndex)
    {
        int32_t NumFunctionVertices = _FunctionVertices.Num();
        if (FunctionIndex >= NumFunctionVertices)
        {
            _FunctionVertices.AddDefaulted(FunctionIndex - NumFunctionVertices + 1);
        }
        return _FunctionVertices[FunctionIndex];
    }

    // Corresponds to `STRONGCONNECT` function from Tarjan's SCC algorithm.
    SFunctionVertex& StrongConnectFunctionVertex(int32_t FunctionIndex, SFunctionVertex& FunctionVertex)
    {
        return StrongConnectFunctionVertex(FunctionIndex, &FunctionVertex);
    }

    SFunctionVertex& StrongConnectFunctionVertex(int32_t FunctionIndex, SFunctionVertex* FunctionVertex)
    {
        FunctionVertex->_Number = _NextFunctionNumber++;
        FunctionVertex->_LowLink = FunctionVertex->_Number;

        PushFunctionVertex(FunctionIndex, *FunctionVertex);

        if (FunctionVertex->_ProcessFunctionBody)
        {
            FunctionVertex->_ProcessFunctionBody();
            FunctionVertex = &_FunctionVertices[FunctionIndex];
        }
        if (FunctionVertex->_LowLink == FunctionVertex->_Number)
        {
            while (&PopFunctionVertex() != FunctionVertex)
            {
            }
        }
        return *FunctionVertex;
    }

    void PushFunctionVertex(int32_t FunctionIndex, SFunctionVertex& FunctionVertex)
    {
        FunctionVertex._OnStack = true;
        FunctionVertex._NextStackIndex = _FunctionStackTop;
        _FunctionStackTop = FunctionIndex;
    }

    SFunctionVertex& PopFunctionVertex()
    {
        SFunctionVertex& TopFunctionVertex = _FunctionVertices[_FunctionStackTop];
        _FunctionStackTop = TopFunctionVertex._NextStackIndex;
        TopFunctionVertex._OnStack = false;
        TopFunctionVertex._NextStackIndex = -1;
        return TopFunctionVertex;
    }

    bool Generalized(const SFunctionVertex& FunctionVertex)
    {
        ULANG_ASSERTF(FunctionVertex._Number != -1, "Expected initialized _Number");
        return !FunctionVertex._OnStack;
    }

    bool Generalized(const CFunction& Function)
    {
        return Generalized(GetFunctionVertex(Function.Index()));
    }

    TArray<SFunctionVertex> _FunctionVertices;

    int32_t _NextFunctionNumber{0};

    int32_t _FunctionStackTop{-1};

    size_t _NextUniqueSymbolId{0};
};


//====================================================================================
// CSemanticAnalyzer Implementation
//====================================================================================

//-------------------------------------------------------------------------------------------------
CSemanticAnalyzer::CSemanticAnalyzer(const TSRef<CSemanticProgram>& Program, const SBuildContext& BuildContext)
    : _SemaImpl(TURef<CSemanticAnalyzerImpl>::New(Program, BuildContext))
{    
}

//-------------------------------------------------------------------------------------------------
CSemanticAnalyzer::~CSemanticAnalyzer()
{
    _SemaImpl->ProcessPackageUsage();
}


//-------------------------------------------------------------------------------------------------
bool CSemanticAnalyzer::ProcessVst(const Vst::Project& VstProject, const ESemanticPass Stage) const
{
    switch (Stage)
    {
        case ESemanticPass::SemanticPass_Invalid:
            ULANG_ERRORF("Invalid semantic pass");
            return false;

        case ESemanticPass::SemanticPass_Types:
        {
            const bool bDesugarResult                       = _SemaImpl->DesugarVstTopLevel(VstProject);
            const bool bAnalyzeResult                       = _SemaImpl->AnalyzeAstTopLevel();
            const bool bModuleResult                        = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_Module);
            _SemaImpl->LinkCompatConstraints();
            const bool bImportResult                        = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_Import);
            const bool bModuleRefsResult                    = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_ModuleReferences);
            const bool bTypeResult                          = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_Type);
            const bool bValidateCyclesResult                = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_ValidateCycles);
            const bool bClosedFunctionBodyExpressionsResult = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_ClosedFunctionBodyExpressions);
            _SemaImpl->LinkOverrides();
            const bool bValidateTypeResult = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_ValidateType);
            return bDesugarResult && bAnalyzeResult && bModuleResult && bImportResult && bModuleRefsResult && bTypeResult && bValidateCyclesResult && bClosedFunctionBodyExpressionsResult && bValidateTypeResult;
        }

        case ESemanticPass::SemanticPass_Attributes:
        {
            const bool bAttributeClassAttributesResult = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_AttributeClassAttributes);
            const bool bAttributesResult               = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_Attributes);
            const bool bPropagateAttributes            = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_PropagateAttributes);
            const bool bValidateAttributesResult       = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_ValidateAttributes);
            return bAttributeClassAttributesResult && bAttributesResult && bPropagateAttributes && bValidateAttributesResult;
        }

        case ESemanticPass::SemanticPass_Code:
        {
            const bool bNonFunctionExpressionsResult = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_NonFunctionExpressions);
            const bool bOpenFunctionBodyExpressionsResult = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_OpenFunctionBodyExpressions);
            const bool bFinalValidationResult = _SemaImpl->ProcessStage(CSemanticAnalyzerImpl::EDeferredPri::Deferred_FinalValidation);
            _SemaImpl->AnalyzeCompatConstraints();
            return bNonFunctionExpressionsResult && bOpenFunctionBodyExpressionsResult && bFinalValidationResult;
        }

        default:
            return false;
    }
}

//-------------------------------------------------------------------------------------------------
const TSRef<uLang::CSemanticProgram>& CSemanticAnalyzer::GetSemanticProgram() const
{
    return _SemaImpl->GetProgram();
}

} // namespace uLang
