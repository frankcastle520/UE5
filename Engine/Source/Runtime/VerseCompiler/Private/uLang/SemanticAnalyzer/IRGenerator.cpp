// Copyright Epic Games, Inc. All Rights Reserved.

// Implements CIrGenerator, a class that generates an IR from an Ast.
// It's possible to turn this class into a nop by modifying the method ProcessAst below.
// This can be done as long AstNodes are used also to represent IrNodes, and is useful 
// while developing the IrNode type.
// The intention is that the generated IrNodes should be easier to use, both for analysis
// and code generation, than the AstNodes.
// However, the initial implementation only copies the AstNodes, with some care since the Ast<->Vst
// must not be broken.

#include "uLang/SemanticAnalyzer/IRGenerator.h"

#include "uLang/Common/Algo/Cases.h"
#include "uLang/Common/Algo/FindIf.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/UniquePointer.h"
#include "uLang/Diagnostics/Glitch.h"
#include "uLang/SemanticAnalyzer/IRGenerator.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/ScopedAccessLevelType.h"
#include "uLang/Semantics/SemanticClass.h"
#include "uLang/Semantics/SemanticEnumeration.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticInterface.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Semantics/UnknownType.h"
#include "uLang/Syntax/VstNode.h"

#include <stdexcept>

namespace uLang
{
namespace Vst = Verse::Vst;

namespace {
    // A simple cache used to ensure that an AstNode is mapped to the same IrNode whenever it's encounted.
    // Only needed for CAstPackage at since they are accessed from CSemanticModule.
    template<typename FromType, typename ToType = FromType>
    class TCache
    {
    private:
        // A Map is probably better.
        TArray<FromType*> _FromNodes;
        TArray<TSRef<ToType>> _ToNodes;

    public:
        // Tries to find the cached value, returns null if it's not there.
        TSPtr<ToType> TryLookup(const FromType* FromNode) const
        {
            for (int ix = 0; ix < _FromNodes.Num(); ++ix)
            {
                if (_FromNodes[ix] == FromNode) { return _ToNodes[ix]; }
            }
            return TSPtr<ToType>();
        }

        // Returns the cached value, or complain if it's not there
        TSPtr<ToType> Lookup(const FromType* FromNode) const
        {
            if (FromNode)
            {
                TSPtr<ToType> ToNode = TryLookup(FromNode);
                ULANG_ASSERTF(ToNode, "Failed to find object translation for AstNode");
                ULANG_ASSERTF(ToNode->IsIrNode(), "Translated node isn't an IrNode");
                return ToNode;
            }
            return TSPtr<ToType>();
        }

        // Add a new mapping, complaining if it's already there.
        void Add(FromType& FromNode, const TSRef<ToType> ToNode)
        {
            if (TSPtr<ToType> oldValue = TryLookup(&FromNode))
            {
                ULANG_ASSERTF(true, oldValue.Get() == ToNode.Get()
                    ? "Add something that is already mapped with same address"
                    : "Add something that is already mapped with different address");
            }
            _FromNodes.Add(&FromNode);
            _ToNodes.Add(ToNode);
        }
    };

    template <typename Iterator>
    Iterator FindNamedType(Iterator First, Iterator Last, CSymbol Name)
    {
        return uLang::FindIf(First, Last, [=](const CTypeBase* Type)
        {
            const CNamedType* NamedType = Type->GetNormalType().AsNullable<CNamedType>();
            return NamedType && NamedType->GetName() == Name;
        });
    }

    template <typename Iterator>
    Iterator FindIndexedType(Iterator First, Iterator Last, int32_t Index)
    {
        int32_t CurrentIndex = 0;
        for (; First != Last; ++First)
        {
            if (!(*First)->GetNormalType().template IsA<CNamedType>())
            {
                if (CurrentIndex == Index)
                {
                    break;
                }
                ++CurrentIndex;
            }
        }
        return First;
    }

    template <typename FirstIterator1, typename LastIterator1, typename FirstIterator2, typename LastIterator2>
    bool ElementOrderMatches(FirstIterator1 First1, LastIterator1 Last1, FirstIterator2 First2, LastIterator2 Last2)
    {
        if (Last1 - First1 != Last2 - First2)
        {
            return false;
        }
        for (; First1 != Last1; ++First1, ++First2)
        {
            if (const CNamedType* NamedType1 = (*First1)->GetNormalType().template AsNullable<CNamedType>())
            {
                if (const CNamedType* NamedType2 = (*First2)->GetNormalType().template AsNullable<CNamedType>())
                {
                    if (NamedType1->GetName() != NamedType2->GetName())
                    {
                        return false;
                    }
                }
                return false;
            }
            if ((*First2)->GetNormalType().template IsA<CNamedType>())
            {
                return false;
            }
        }
        return true;
    }

    bool ElementOrderMatches(const CTupleType& Type1, const CNormalType& Type2)
    {
        const CTupleType::ElementArray& Elements1 = Type1.GetElements();
        if (const CTupleType* TupleType2 = Type2.AsNullable<CTupleType>())
        {
            const CTupleType::ElementArray& Elements2 = TupleType2->GetElements();
            return ElementOrderMatches(Elements1.begin(), Elements1.end(), Elements2.begin(), Elements2.end());
        }
        const CTypeBase* Elements2 = &Type2;
        return ElementOrderMatches(Elements1.begin(), Elements1.end(), &Elements2, &Elements2 + 1);
    }
}

class CIrGeneratorImpl
{
public:

    CIrGeneratorImpl(const TSRef<CSemanticProgram>& Program, const TSRef<CDiagnostics>& Diagnostics, SBuildParams::EWhichVM TargetVM)
        : _SemanticProgram(Program)
        , _Program(*Program)
        , _Diagnostics(*Diagnostics)
        , _Scope(Program.Get())
        , _TargetVM(TargetVM)
    {}

    bool ProcessAst()
    {
        // The minimal change to disable IR is to change ProcessAst to do nothing except returning true.
        // The byte code will be generated from the Ast in this case.
        TSRef<CAstProject> IrProject = Gen(*_Program._AstProject);
        _Program.SetIrProject(IrProject);
        return true;
    }
    
    TSRef<CAstProject> Gen(const CAstProject& AstNode)
    {
        TGuardValue<const Vst::Node*> MappedVstNodeGuard(_MappedVstNode, AstNode.GetMappedVstNode());
        TSRef<CAstProject> IrNode = NewIrNode<CAstProject>(AstNode._Name);
        IrNode->ReserveCompilationUnits(AstNode.OrderedCompilationUnits().Num());

        for (TSRef<CAstCompilationUnit> CompilationUnit : AstNode.OrderedCompilationUnits())
        {
            IrNode->AppendCompilationUnit(GenCompilationUnit(*CompilationUnit));
        }
        return IrNode;
    }

    const TSRef<CSemanticProgram>& GetProgram() const
    {
        return _SemanticProgram;
    }

private:
    void InitIrMemberDefinitions(CMemberDefinitions& IrNode, CMemberDefinitions& AstNode)
    {
        // Iterate `AstNode.Members()` using explicit indices.
        // `GenNode` can call `CreateCoercedOverridingFunctionDefinition` which may add to `AstNode.Members()`,
        // possibly invalidating iterators.  Furthermore, such added functions  do not
        // need to be visited.  Computing `NumMembers` before iterating ensures this.
        for (int32_t Ix = 0, NumMembers = AstNode.Members().Num(); Ix < NumMembers; ++Ix)
        {
            IrNode.AppendMember(GenNode(AstNode.Members()[Ix]));
        }
    }

    // Package nodes must be cached since they are accessed more than once. Both from the project node and from CSemanticModule.
    TCache<CAstPackage, CAstPackage> _PackageCache;
 
    TSRef<CAstCompilationUnit> GenCompilationUnit(CAstCompilationUnit& AstNode)
    {
        TGuardValue<const Vst::Node*> MappedVstNodeGuard(_MappedVstNode, AstNode.GetMappedVstNode());
        TSRef<CAstCompilationUnit> IrNode = NewIrNode<CAstCompilationUnit>();
        IrNode->ReservePackages(AstNode.Packages().Num());

        for (TSRef<CAstPackage> AstPackage : AstNode.Packages())
        {
            IrNode->AppendPackage(GenPackage(*AstPackage, IrNode));
        }

        return IrNode;
    }

    TSRef<CAstPackage> GenPackage(CAstPackage& AstNode, CAstCompilationUnit* IrCompilationUnit)
    {
        TGuardValue<const Vst::Node*> MappedVstNodeGuard(_MappedVstNode, AstNode.GetMappedVstNode());

        if (auto IrNode = _PackageCache.TryLookup(&AstNode)) { return IrNode.AsRef(); }

        TSRef<CAstPackage> IrNode = NewIrNode<CAstPackage>(
            AstNode._Name,
            AstNode._VersePath,
            AstNode._VerseScope,
            AstNode._Role,
            AstNode._EffectiveVerseVersion,
            AstNode._UploadedAtFNVersion,
            AstNode._bAllowNative,
            AstNode._bTreatModulesAsImplicit,
            AstNode._bAllowExperimental);
        _PackageCache.Add(AstNode, IrNode);

        IrNode->_RootModule = AstNode._RootModule;
        IrNode->_CompilationUnit = IrCompilationUnit;

        // Update the IR packages of all parents
        for (CModulePart* IrPart = IrNode->_RootModule;
            IrPart;
            IrPart = IrPart->GetParentScope() ? IrPart->GetParentScope()->GetModulePart() : nullptr)
        {
            ULANG_ASSERTF(IrPart->GetAstPackage() == &AstNode, "All parent module parts of a module part must belong to the same package.");
            IrPart->SetIrPackage(IrNode.Get());
        }

        for (const CAstPackage* Dependency : AstNode._Dependencies)
        {
            const CAstPackage* IrDependency = _SemanticProgram->GetIrProject()->FindPackageByName(Dependency->_Name);
            ULANG_ASSERT(IrDependency);
            IrNode->_Dependencies.Add(IrDependency);
        }

        InitIrMemberDefinitions(*IrNode, AstNode);
        return IrNode;
    }

    // Use GenNode instead. This code uses the tag to call the specific create method,
    // GenNode adds some general stuff that must be done for all IR nodes.
    // Calls ULANG_ERRORF if it detects a node type it doesn't uinderstand.
    TSRef<CExpressionBase> MakeIrNode(CExpressionBase& AstNode)
    {
        switch (AstNode.GetNodeType())
        {
        case EAstNodeType::External:   // An (unknown) external expression - should never reach the code generator
            return GenExternal(static_cast<CExprExternal&>(AstNode));

        // Literals
        case EAstNodeType::Literal_Logic:          // CExprLogic - Logic literal - true/false
            return GenLogic(static_cast<CExprLogic&>(AstNode));
        case EAstNodeType::Literal_Number:         // CExprNumber - Integer literal - 42, 0, -123, 123_456_789, 0x12fe, 0b101010
                                                   //               or Float literal - 42.0, 0.0, -123.0, 123_456.0, 3.14159, .5, -.33, 4.2e1, -1e6, 7.5e-8
            return GenNumber(static_cast<CExprNumber&>(AstNode));
        case EAstNodeType::Literal_Char:           // CExprChar - Character literal - 'a', '\n'
            return GenChar(static_cast<CExprChar&>(AstNode));
        case EAstNodeType::Literal_String:         // CExprString - String literal - "Hello, world!", "Line 1\nLine2"
            return GenString(static_cast<CExprString&>(AstNode));
        case EAstNodeType::Literal_Path:           // CExprPath - Path literal - /Verse.org/Math
            return GenPath(static_cast<CExprPath&>(AstNode));
        case EAstNodeType::Literal_Enum:           // CExprEnumLiteral - Enumerator - Color.Red, Size.XXL
            return GenEnum(static_cast<CExprEnumLiteral&>(AstNode));
        case EAstNodeType::Literal_Type:        // CExprType - Typedef - typedef{<expr>}
            return GenType(static_cast<CExprType&>(AstNode));
        case EAstNodeType::Literal_Function:       // CExprFunctionLiteral - a=>b or function(a){b}
            return GenFunction(static_cast<CExprFunctionLiteral&> (AstNode));

        // Identifiers
        case EAstNodeType::Identifier_Unresolved:         // CExprIdentifierUnresolved - An existing identifier that is unresolved. It is produced by desugaring and consumed by analysis.
            return GenIdentifierUnresolved(static_cast<CExprIdentifierUnresolved&> (AstNode));

        case EAstNodeType::Identifier_Class:              // CExprIdentifierClass - Type identifier - e.g. my_type, int, color, string
            return GenIdentifierClass(static_cast<CExprIdentifierClass&>(AstNode));
        case EAstNodeType::Identifier_Module:             // CExprIdentifierModule - Module name
            return GenIdentifierModule(static_cast<CExprIdentifierModule&>(AstNode));
        case EAstNodeType::Identifier_ModuleAlias:        // CExprIdentifierModuleAlias - Module alias name
            return GenIdentifierModuleAlias(static_cast<CExprIdentifierModuleAlias&>(AstNode));
        case EAstNodeType::Identifier_Enum:               // CExprEnumerationType - Enum name
            return GenIdentifierEnum(static_cast<CExprEnumerationType&>(AstNode));
        case EAstNodeType::Identifier_Interface:              // CExprInterfaceType - Interface name
            return GenIdentifierInterface(static_cast<CExprInterfaceType&>(AstNode));
        case EAstNodeType::Identifier_Data:               // CExprIdentifierData - Scoped data-definition (class member, local, etc.)
            return GenIdentifierData(static_cast<CExprIdentifierData&>(AstNode));
        case EAstNodeType::Identifier_TypeAlias:          // CExprIdentifierTypeAlias - Access to type alias
            return GenIdentifierTypeAlias(static_cast<CExprIdentifierTypeAlias&>(AstNode));
        case EAstNodeType::Identifier_TypeVariable:       // CExprIdentifierTypeVariable - Access to a type variable
            return GenIdentifierTypeVariable(static_cast<CExprIdentifierTypeVariable&>(AstNode));
        case EAstNodeType::Identifier_Function:           // CExprIdentifierFunction - Access to functions
            return GenIdentifierFunction(static_cast<CExprIdentifierFunction&>(AstNode));
        case EAstNodeType::Identifier_OverloadedFunction: // CExprIdentifierOverloadedFunction - An overloaded function identifier that hasn't been resolved to a specific overload.
            return GenIdentifierOverloadedFunction(static_cast<CExprIdentifierOverloadedFunction&>(AstNode));
        case EAstNodeType::Identifier_Self:               // CExprSelf - Access to the instance the current function is being invoked on.
            return GenSelf(static_cast<CExprSelf&>(AstNode));

        // Multi purpose syntax
        case EAstNodeType::Definition:             // CExprDefinition - represents syntactic forms elt:domain=value, elt:domain, elt=value
            return GenExprDefinition(static_cast<CExprDefinition&>(AstNode));

        // Invocations
        case EAstNodeType::Invoke_Invocation:      // CExprInvocation - Routine call - expr1.call(expr2, expr3)
            return GenInvocation(static_cast<CExprInvocation&>(AstNode));
        case EAstNodeType::Invoke_UnaryArithmetic: // CExprUnaryArithmetic - negation
            return GenUnaryArithmetic(static_cast<CExprUnaryArithmetic&>(AstNode));
        case EAstNodeType::Invoke_BinaryArithmetic:// CExprBinaryArithmetic - add, sub, mul, div; two operands only
            return GenBinaryArithmetic(static_cast<CExprBinaryArithmetic&>(AstNode));
        case EAstNodeType::Invoke_ShortCircuitAnd: // CExprShortCircuitAnd - short-circuit evaluation of logic and
            return GenShortCircuitAnd(static_cast<CExprShortCircuitAnd&>(AstNode));
        case EAstNodeType::Invoke_ShortCircuitOr:  // CExprShortCircuitOr - short-circuit evaluation of logic or
            return GenShortCircuitOr(static_cast<CExprShortCircuitOr&>(AstNode));
        case EAstNodeType::Invoke_LogicalNot:      // CExprLogicalNot - logical not operator
            return GenLogicalNot(static_cast<CExprLogicalNot&>(AstNode));
        case EAstNodeType::Invoke_Comparison:      // CExprComparison - comparison operators
            return GenComparison(static_cast<CExprComparison&>(AstNode));
        case EAstNodeType::Invoke_QueryValue:      // CExprQueryValue - Querying the value of a logic or option.
            return GenQueryValue(static_cast<CExprQueryValue&>(AstNode));
        case EAstNodeType::Invoke_MakeOption:    // CExprMakeOption - Making an option value.
            return GenMakeOption(static_cast<CExprMakeOption&>(AstNode));
        case EAstNodeType::Invoke_MakeArray:       // CExprMakeArray - Making an array value.
            return GenMakeArray(static_cast<CExprMakeArray&>(AstNode));
        case EAstNodeType::Invoke_MakeMap:         // CExprMakeMap - Making a map  value.
            return GenMakeMap(static_cast<CExprMakeMap&>(AstNode));
        case EAstNodeType::Invoke_MakeTuple:       // CExprMakeTuple - Making a tuple value - (1, 2.0f, "three")
            return GenMakeTuple(static_cast<CExprMakeTuple&>(AstNode));
        case EAstNodeType::Invoke_TupleElement:    // CExprTupleElement - Tuple element access `TupleExpr(Idx)`
            return GenTupleElement(static_cast<CExprTupleElement&>(AstNode));
        case EAstNodeType::Invoke_MakeRange:       // CExprMakeRange - Making a range value.
            return GenMakeRange(static_cast<CExprMakeRange&>(AstNode));
        case EAstNodeType::Invoke_Type:            // CExprInvokeType - Invoke a type as a function on a value.
            return GenInvokeType(static_cast<CExprInvokeType&>(AstNode));
        case EAstNodeType::Invoke_PointerToReference: // CExprPointerToReference - Access the mutable reference behind the pointer
            return GenPointerToReference(static_cast<CExprPointerToReference&>(AstNode));
        case EAstNodeType::Invoke_Set:             // CExprSet - Evaluate operand to an l-expression.
            return GenSet(static_cast<CExprSet&>(AstNode));
        case EAstNodeType::Invoke_NewPointer:      // CExprNewPointer - Create a new pointer from an initial value.
            return GenNewPointer(static_cast<CExprNewPointer&>(AstNode));
        case EAstNodeType::Invoke_ReferenceToValue:// CExprReferenceToValue - Evaluates the value of an expression yielding a reference type.
            return GenReferenceToValue(static_cast<CExprReferenceToValue&>(AstNode));

        case EAstNodeType::Assignment:             // CExprAssignment - Assignment operation - expr1 = expr2, expr1 := expr2, expr1 += expr2, etc.
            return GenAssignment(static_cast<CExprAssignment&>(AstNode));

        // TypeFormers
        case EAstNodeType::Invoke_ArrayFormer:     // CExprArrayTypeFormer - Invoke (at compile time) a formation of an array of another type
            return GenArrayTypeFormer(static_cast<CExprArrayTypeFormer&>(AstNode));
        case EAstNodeType::Invoke_GeneratorFormer:  // CExprGeneratorTypeFormer - Invoke (at compile time) a formation of an generator type.
            return GenGeneratorTypeFormer(static_cast<CExprGeneratorTypeFormer&>(AstNode));
        case EAstNodeType::Invoke_MapFormer:       // CExprMapTypeFormer - Invoke (at compile time) a formation of a map from a key and value type.
            return GenMapTypeFormer(static_cast<CExprMapTypeFormer&>(AstNode));
        case EAstNodeType::Invoke_OptionFormer:  // CExprOptionTypeFormer - Invoke (at compile time) a formation of an option of some primitive type
            return GenOptionTypeFormer(static_cast<CExprOptionTypeFormer&>(AstNode));
        case EAstNodeType::Invoke_Subtype:         // CExprSubtype - Invoke (at compile time) a formation of a metaclass type.
            return GenSubtype(static_cast<CExprSubtype&>(AstNode));
        case EAstNodeType::Invoke_TupleType:       // CExprTupleType - Get or create a tuple tuple based on `tuple(type1, type2, ...)`
            return GenTupleType(static_cast<CExprTupleType&>(AstNode));
        case EAstNodeType::Invoke_Arrow:           // CExprArrow - Create a function type from a parameter and return type.
            return GenArrow(static_cast<CExprArrow&>(AstNode));

        case EAstNodeType::Invoke_ArchetypeInstantiation: // CExprArchetypeInstantiation - Initializer list style instantiation - Type{expr1, id=expr2, ...}
            return GenArchetypeInstantiation(static_cast<CExprArchetypeInstantiation&>(AstNode));

        case EAstNodeType::Invoke_MakeNamed:       // CExprMakeNamed
            return GenMakeNamed(static_cast<CExprMakeNamed&>(AstNode));

        // Flow Control
        case EAstNodeType::Flow_CodeBlock:         // CExprCodeBlock - Code block - block {expr1; expr2}
            return GenCodeBlock(static_cast<CExprCodeBlock&>(AstNode));
        case EAstNodeType::Flow_Let:               // CExprLet -     - let {definition1; definition2}
            return GenLet(static_cast<CExprLet&>(AstNode));
        case EAstNodeType::Flow_Defer:             // CExprDefer     - defer {expr1; expr2}
            return GenDefer(static_cast<CExprDefer&>(AstNode));
        case EAstNodeType::Flow_If:                // CExprIf        - Conditional with failable tests- if (Test[]) {clause1}, if (Test[]) {clause1} else {else_clause}
            return GenIf(static_cast<CExprIf&>(AstNode));
        case EAstNodeType::Flow_Iteration:         // CExprIteration - Bounded iteration over an iterable type - for(Num:Nums) {DoStuff(Num)}
            return GenIteration(static_cast<CExprIteration&>(AstNode));
        case EAstNodeType::Flow_Loop:              // CExprLoop      - Simple loop - loop {DoStuff()}
            return GenLoop(static_cast<CExprLoop&>(AstNode));
        case EAstNodeType::Flow_Break:             // CExprBreak     - Control flow early exit - loop {if (IsEarlyExit[]) {break}; DoLoopStuff()}
            return GenBreak(static_cast<CExprBreak&>(AstNode));
        case EAstNodeType::Flow_Return:            // CExprReturn    - Return statement - return expr
            return GenReturn(static_cast<CExprReturn&>(AstNode));
        case EAstNodeType::Flow_ProfileBlock:
            return GenProfileBlock(static_cast<CExprProfileBlock&>(AstNode));

        // Concurrency Primitives
        case EAstNodeType::Concurrent_Sync:          // CExprSync         - sync {Coro1(); Coro2()}
            return  GenSync(static_cast<CExprSync&>(AstNode));
        case EAstNodeType::Concurrent_Rush:          // CExprRush         - rush {Coro1(); Coro2()}
            return GenRush(static_cast<CExprRush&>(AstNode));
        case EAstNodeType::Concurrent_Race:          // CExprRace         - race {Coro1(); Coro2()}
            return GenRace(static_cast<CExprRace&>(AstNode));
        case EAstNodeType::Concurrent_SyncIterated:  // CExprSyncIterated - sync(Item:Container) {Item.Coro1(); Coro2(Item)}
            // No versetest trigger this
            return GenSyncIterated(static_cast<CExprSyncIterated&>(AstNode));
        case EAstNodeType::Concurrent_RushIterated:  // CExprRushIterated - rush(Item:Container) {Item.Coro1(); Coro2(Item)}
            // No versetest trigger this
            return GenRushIterated(static_cast<CExprRushIterated&>(AstNode));
        case EAstNodeType::Concurrent_RaceIterated:  // CExprRaceIterated - race(Item:Container) {Item.Coro1(); Coro2(Item)}
            // No versetest trigger this
            return GenRaceIterated(static_cast<CExprRaceIterated&>(AstNode));
        case EAstNodeType::Concurrent_Branch:        // CExprBranch       - branch {Coro1(); Coro2()}
            return GenBranch(static_cast<CExprBranch&>(AstNode));
        case EAstNodeType::Concurrent_Spawn:         // CExprSpawn        - spawn {Coro()}
            return GenSpawn(static_cast<CExprSpawn&>(AstNode));

        // Definitions
        case EAstNodeType::Definition_Module:      // CExprModuleDefinition
            return GenModuleDefinition(static_cast<CExprModuleDefinition&>(AstNode));
        case EAstNodeType::Definition_Enum:        // CExprEnumDefinition
            return GenEnumDefinition(static_cast<CExprEnumDefinition&>(AstNode));
        case EAstNodeType::Definition_Interface:   // CExprInterfaceDefinition
            return GenInterfaceDefinition(static_cast<CExprInterfaceDefinition&>(AstNode));
        case EAstNodeType::Definition_Class:       // CExprClassDefinition
            return GenClassDefinition(static_cast<CExprClassDefinition&>(AstNode));
        case EAstNodeType::Definition_Data:        // CExprDataDefinition
            return GenDataDefinition(static_cast<CExprDataDefinition&>(AstNode));
        case EAstNodeType::Definition_IterationPair: // CExprIterationPairDefinition
            return GenIterationPairDefinition(static_cast<CExprIterationPairDefinition&>(AstNode));
        case EAstNodeType::Definition_Function:    // CExprFunctionDefinition
            return GenFunctionDefinition(static_cast<CExprFunctionDefinition&>(AstNode));
        case EAstNodeType::Definition_TypeAlias:   // CExprTypeAliasDefinition
            return GenTypeAliasDefinition(static_cast<CExprTypeAliasDefinition&>(AstNode));
        case EAstNodeType::Definition_Using:       // CExprUsing
            return GenExprUsing(static_cast<CExprUsing&>(AstNode));
        case EAstNodeType::Definition_Import:      // CExprImport
            return GenExprImport(static_cast<CExprImport&>(AstNode));
        case EAstNodeType::Definition_Where:       // CExprWhere
            return GenExprWhere(static_cast<CExprWhere&>(AstNode));
        case EAstNodeType::Definition_Var:         // CExprVar
            return GenVar(static_cast<CExprVar&>(AstNode));
        case EAstNodeType::Definition_ScopedAccessLevel:       // CScopedAccessLevelDefinition
            return GenAccessLevelDefinition(static_cast<CExprScopedAccessLevelDefinition&>(AstNode));

        case EAstNodeType::Context_Snippet:
            return GenExprSnippet(static_cast<CExprSnippet&>(AstNode));

        case EAstNodeType::Error_:
        case EAstNodeType::Placeholder_:
        case EAstNodeType::PathPlusSymbol:
        case EAstNodeType::Identifier_BuiltInMacro:
        case EAstNodeType::Identifier_Local:
        case EAstNodeType::MacroCall:
        case EAstNodeType::Context_Project:
        case EAstNodeType::Context_CompilationUnit:
        case EAstNodeType::Context_Package:
        case EAstNodeType::Ir_For:
        case EAstNodeType::Ir_ForBody:
        case EAstNodeType::Ir_ArrayAdd:
        case EAstNodeType::Ir_MapAdd:
        case EAstNodeType::Ir_ArrayUnsafeCall:
        case EAstNodeType::Ir_ConvertToDynamic:
        case EAstNodeType::Ir_ConvertFromDynamic:
        default:
            // Use an ensure here to report an error to crash reporter, but (hopefully) not crash the entire process.
            ULANG_ENSUREF(false, "Tried to generate IR for unknown node type: %s", AstNode.GetErrorDesc().AsCString());
            AppendGlitch(AstNode, EDiagnostic::ErrSemantic_Internal);
            return NewIrNode<CExprError>();
        }
    }

    // A wrapper for MakeIrNode
    // There was a cache here before but there seem to be no sharing of AstNodes.
    // Now it does some common work for all IrNodes.
    TSRef<CExpressionBase> GenNode(CExpressionBase& AstNode)
    {
        TGuardValue<const Vst::Node*> MappedVstNodeGuard(
            _MappedVstNode,
            AstNode.GetMappedVstNode() ? AstNode.GetMappedVstNode() : _MappedVstNode);

        TSRef<CExpressionBase> IrNode = MakeIrNode(AstNode);
        if (!IrNode->IrGetResultType())
        {
            IrNode->IrSetResultType(AstNode.GetResultType(*_SemanticProgram));
        }
        return IrNode;
    }

    //-----------------------------------------------------------
    // Some useful utility methods

    TSPtr<CExpressionBase> GenNode(const TSPtr<CExpressionBase>& AstNode)
    {
        return AstNode ? GenNode(*AstNode) : TSPtr<CExpressionBase>();
    }

    TSRef<CExpressionBase> GenNode(const TSRef<CExpressionBase>& AstNode)
    {
        return GenNode(*AstNode);
    }

    TSPtr<CExpressionBase> GenNode(CExpressionBase* AstNode)
    {
        return AstNode ? GenNode(*AstNode) : TSPtr<CExpressionBase>();
    }

    TArray<TSRef<CExpressionBase>> GenNodes(const TArray<TSRef<CExpressionBase>>& AstNodes)
    {
        TArray<TSRef<CExpressionBase>> IrNodes;
        for (TSRef<CExpressionBase> AstNode : AstNodes)
        {
            IrNodes.Add(GenNode(*AstNode));
        }
        return IrNodes;
    }

    TSRef<CExprQueryValue> NewIrQueryValue(TSRef<CExpressionBase>&& Argument)
    {
        const CTypeBase* ArgumentType = Argument->GetResultType(*_SemanticProgram);
        TSRef<CExprQueryValue> QueryValue = NewIrNode<CExprQueryValue>(Move(Argument));
        const CFunction* CalleeFunction = _SemanticProgram->_OptionQueryOp;
        const CFunctionType* CalleeType = SemanticTypeUtils::Instantiate(CalleeFunction->_Signature.GetFunctionType());
        TSPtr<CExpressionBase> Callee = NewIrNode<CExprIdentifierFunction>(
            *CalleeFunction,
            CalleeType);
        QueryValue->SetCallee(Move(Callee));
        bool bConstrained = SemanticTypeUtils::Constrain(ArgumentType, &CalleeType->GetParamsType());
        ULANG_ASSERTF(
            bConstrained,
            "`ArgumentType` must be a subtype of `CalleeType->GetParamsType()`");
        QueryValue->SetResolvedCalleeType(CalleeType);
        QueryValue->SetResultType(&CalleeType->GetReturnType());
        return QueryValue;
    }
    
    // Creates a new code block + scope that begins with a binding of some subexpression to a temporary variable.
    // Used to bind a name to some subexpression with unknown effects and reference it multiple times in the rest of the block.
    struct STempBinding
    {
        TSRef<CDataDefinition> Definition;
        TSRef<CExprCodeBlock> CodeBlock;
    };

    STempBinding BindValueToTemporaryInNewCodeBlock(TSRef<CExpressionBase>&& Value, CSymbol TempName = CSymbol())
    {
        ULANG_ASSERT(_Scope);

        TSRef<CControlScope> CodeBlockScope = _Scope->CreateNestedControlScope();
        TSRef<CExprCodeBlock> CodeBlock = NewIrNode<CExprCodeBlock>(2);
        CodeBlock->_AssociatedScope = CodeBlockScope;
        TSRef<CDataDefinition> Definition = CodeBlockScope->CreateDataDefinition(
            TempName,
            Value->GetResultType(*_SemanticProgram));
        CodeBlock->AppendSubExpr(NewIrNode<CExprDataDefinition>(
            Definition,
            NewIrNode<CExprIdentifierData>(_Program, *Definition),
            nullptr,
            Move(Value),
            EVstMappingType::Ir));
        return STempBinding{Move(Definition), Move(CodeBlock)};
    }

    TSRef<CExprCodeBlock> MoveValueToNewCodeBlock(TSRef<CExpressionBase>&& Value)
    {
        ULANG_ASSERT(_Scope);

        TSRef<CControlScope> CodeBlockScope = _Scope->CreateNestedControlScope();
        TSRef<CExprCodeBlock> CodeBlock = NewIrNode<CExprCodeBlock>(2);
        CodeBlock->_AssociatedScope = CodeBlockScope;
        CodeBlock->AppendSubExpr(Move(Value));
        return CodeBlock;
    }

    template <typename Function>
    TSPtr<CExpressionBase> WithElements(TSRef<CExpressionBase>&& Expr, const CNormalType& Type, bool bBindElementsToTemporary, Function F)
    {
        ULANG_ASSERT(_Scope);

        if (const CTupleType* TupleType = Type.AsNullable<CTupleType>())
        {
            int32_t NumElements = TupleType->Num();
            TArray<TSPtr<CExpressionBase>> Elements;
            Elements.Reserve(NumElements);
            if (!bBindElementsToTemporary && Expr->GetNodeType() == EAstNodeType::Invoke_MakeTuple)
            {
                CExprMakeTuple& MakeTuple = static_cast<CExprMakeTuple&>(*Expr);
                for (const TSPtr<CExpressionBase>& SubExpr : MakeTuple.GetSubExprs())
                {
                    Elements.Add(SubExpr);
                }
                return uLang::Invoke(F, TRangeView(Elements), TRangeView(TupleType->GetElements()));
            }
            else
            {
                auto [Definition, CodeBlock] = BindValueToTemporaryInNewCodeBlock(Move(Expr));
                for (int32_t I = 0; I != NumElements; ++I)
                {
                    TSRef<CExpressionBase> Element = NewIrNode<CExprTupleElement>(
                        NewIrNode<CExprIdentifierData>(_Program, *Definition),
                        I,
                        nullptr);
                    Element->IrSetResultType((*TupleType)[I]);
                    Elements.Add(Move(Element));
                }
                TSPtr<CExpressionBase> Result = uLang::Invoke(F, TRangeView(Elements), TRangeView(TupleType->GetElements()));
                if (!Result)
                {
                    return nullptr;
                }
                CodeBlock->AppendSubExpr(Move(Result));
                return CodeBlock;
            }
        }
        const CTypeBase* ElementTypes = &Type;
        return uLang::Invoke(F, SingletonRangeView(Expr), SingletonRangeView(ElementTypes));
    }

    TSPtr<CExpressionBase> MaybeCoerceArrayToTuple(TSRef<CExpressionBase>&& Value, const CArrayType& SourceArrayType, const CTupleType& ResultTupleType)
    {
        ULANG_ASSERT(_Scope);

        const CTypeBase* SourceElementType = SourceArrayType.GetElementType();
        int32_t ResultNumElements = ResultTupleType.Num();
        TSRef<CExprMakeTuple> MakeTuple = NewIrNode<CExprMakeTuple>(ResultNumElements);
        MakeTuple->IrSetResultType(&ResultTupleType);
        STempBinding SourceBinding = BindValueToTemporaryInNewCodeBlock(Move(Value));
        Integer I = 0;
        for (const CTypeBase* ResultElementType : ResultTupleType.GetElements())
        {
            TSRef<CExpressionBase> SourceElement = NewIrNode<CIrArrayUnsafeCall>(
                NewIrNode<CExprIdentifierData>(_Program, *SourceBinding.Definition),
                NewIrNode<CExprNumber>(_Program, I));
            SourceElement->SetResultType(SourceElementType);
            TSPtr<CExpressionBase> CoercedElement = MaybeCoerceToType(Move(SourceElement), ResultElementType);
            if (!CoercedElement)
            {
                return nullptr;
            }
            MakeTuple->AppendSubExpr(Move(CoercedElement));
            ++I;
        }
        SourceBinding.CodeBlock->AppendSubExpr(Move(MakeTuple));
        return Move(SourceBinding.CodeBlock);
    }

    TSPtr<CExpressionBase> MaybeCoerceElementsToTuple(TSRef<CExpressionBase>&& Value, const CNormalType& SourceNormalType, const CTupleType& ResultTupleType)
    {
        bool bNeedsTemporary = !ElementOrderMatches(ResultTupleType, SourceNormalType);
        return WithElements(Move(Value), SourceNormalType, bNeedsTemporary, [&](auto&& Elements, auto&& SourceElementTypes) -> TSPtr<CExpressionBase>
        {
            TSRef<CExprMakeTuple> MakeTuple = NewIrNode<CExprMakeTuple>(ResultTupleType.Num());
            int32_t ResultIndex = 0;
            for (const CTypeBase* ResultElementType : ResultTupleType.GetElements())
            {
                if (const CNamedType* ResultNamedType = ResultElementType->GetNormalType().AsNullable<CNamedType>())
                {
                    CSymbol ResultName = ResultNamedType->GetName();
                    auto First = SourceElementTypes.begin();
                    auto Last = SourceElementTypes.end();
                    auto I = FindNamedType(First, Last, ResultName);
                    if (I == Last)
                    {
                        TSRef<CExpressionBase> CoercedElement = NewIrNode<CExprMakeNamed>(ResultName);
                        CoercedElement->IrSetResultType(ResultElementType);
                        MakeTuple->AppendSubExpr(Move(CoercedElement));
                    }
                    else
                    {
                        TSPtr<CExpressionBase> Element = Elements[static_cast<int32_t>(I - First)];
                        TSPtr<CExpressionBase> CoercedElement = MaybeCoerceToType(
                            Move(Element.AsRef()),
                            ResultElementType);
                        if (!CoercedElement)
                        {
                            return nullptr;
                        }
                        MakeTuple->AppendSubExpr(Move(CoercedElement));
                    }
                }
                else
                {
                    auto First = SourceElementTypes.begin();
                    auto Last = SourceElementTypes.end();
                    auto I = FindIndexedType(First, Last, ResultIndex);
                    ULANG_ASSERTF(I != Last, "Semantic analyzer should have errored");
                    TSPtr<CExpressionBase> Element = Elements[static_cast<int32_t>(I - First)];
                    TSPtr<CExpressionBase> CoercedElement = MaybeCoerceToType(
                        Move(Element.AsRef()),
                        ResultElementType);
                    if (!CoercedElement)
                    {
                        return nullptr;
                    }
                    MakeTuple->AppendSubExpr(Move(CoercedElement));
                    ++ResultIndex;
                }
            }
            MakeTuple->IrSetResultType(&ResultTupleType);
            return MakeTuple;
        });
    }

    // Fix the case when ResultNormalType is a CNamedType but SourceNormalType isn't, and Value is a definition.
    // This happens in definitions like:
    // F1(X:int, ?Y:int = 1):int = ... 
    // F2(X:int, ?Y:tuple(int, int) = (1, 2)):int = ... 
    const CNormalType& GetResultNormalType(TSRef<CExpressionBase> Value, const CTypeBase* ResultType, const CNormalType& SourceNormalType)
    {
        const CNormalType& ResultNormalType = SemanticTypeUtils::Canonicalize(*ResultType).GetNormalType();
        if (ResultNormalType.IsA<CNamedType>() && !SourceNormalType.IsA<CNamedType>())
        {
            if (Value->GetNodeType() == EAstNodeType::Definition)
            {   // Processing a definition, check if symbols are the same
                CExprDefinition& Definition = static_cast<CExprDefinition&>(*Value);
                if (Definition.Element()->GetNodeType() == EAstNodeType::Identifier_Unresolved)
                {
                    CExprIdentifierUnresolved& Unresolved = static_cast<CExprIdentifierUnresolved&>(*Definition.Element());
                    if (Unresolved._Symbol == ResultNormalType.AsChecked<CNamedType>().GetName())
                    {   // Symbols are the same, use the value type instead of the named type.
                        return SemanticTypeUtils::Canonicalize(*ResultNormalType.AsChecked<CNamedType>().GetValueType()).GetNormalType();
                    }
                }
            }
        }
        return ResultNormalType;
    }

    // Given a result type and an expression yielding a value in the result type's domain, return an expression that
    // yields the value of the provided expression in the representation of the result type.
    TSPtr<CExpressionBase> MaybeCoerceToType(TSRef<CExpressionBase> Value, const CTypeBase* ResultType)
    {
        ULANG_ASSERT(_Scope);

        const CTypeBase* SourceType = Value->GetResultType(*_SemanticProgram);
        ULANG_ENSUREF(SourceType != nullptr,
            "FORT-592189 - Null encountered in type coersion - Value: (0x%X) \"%s\" Result: \"%s\"",
            Value->GetNodeType(),
            Value->GetErrorDesc().AsCString(),
            ResultType->AsCode().AsCString());

        if (SourceType == nullptr)
        {
            // BEGIN HACK added 2023/04/28 by @jason.weiler trying to capture FORT-592189 in the wild and avoid the crash
            AppendGlitch(
                *Value,
                EDiagnostic::ErrSemantic_Internal,
                CUTF8String("Internal Error: null encountered in type coersion"));

            return nullptr;
            // END HACK
        }

        const CNormalType& SourceNormalType = SemanticTypeUtils::Canonicalize(*SourceType).GetNormalType();
        const CNormalType& ResultNormalType = GetResultNormalType(Value, ResultType, SourceNormalType);
        if (NeedsCoercion(*Value, ResultNormalType, SourceNormalType))
        {
            if (ResultNormalType.GetKind() == Cases<ETypeKind::Void, ETypeKind::True>
                || SourceNormalType.GetKind() == ETypeKind::False)
            {
                TSRef<CExprCodeBlock> CodeBlock = MoveValueToNewCodeBlock(Move(Value));
                CodeBlock->AppendSubExpr(NewIrNode<CExprLogic>(_Program, false));
                return CodeBlock;
            }
            else if (ResultNormalType.GetKind() == ETypeKind::False)
            {
                TSRef<CExprCodeBlock> CodeBlock = MoveValueToNewCodeBlock(Move(Value));
                CodeBlock->AppendSubExpr(NewIrNode<CExprLogic>(_Program, false));
                return CodeBlock;
            }
            else if (_TargetVM != SBuildParams::EWhichVM::BPVM)
            {
                return Move(Value);
            }
            else if (ResultNormalType.GetKind() == ETypeKind::Any)
            {
                // CIrConvertToDynamic can convert any type to a dynamically typed value.
                return NewIrNode<CIrConvertToDynamic>(ResultType, Move(Value));
            }
            else if (SourceNormalType.GetKind() == ETypeKind::Any)
            {
                return NewIrNode<CIrConvertFromDynamic>(ResultType, Move(Value));
            }
            else if (ResultNormalType.IsA<CArrayType>() && SourceNormalType.IsA<CTupleType>())
            {
                const CArrayType& ResultArrayType = ResultNormalType.AsChecked<CArrayType>();
                const CTupleType& SourceTupleType = SourceNormalType.AsChecked<CTupleType>();
                int32_t N = SourceTupleType.Num();
                TSRef<CExprMakeArray> MakeArray = NewIrNode<CExprMakeArray>(N);
                MakeArray->IrSetResultType(ResultType);
                STempBinding SourceBinding = BindValueToTemporaryInNewCodeBlock(Move(Value));
                int32_t I = 0;
                for (const CTypeBase* SourceElementType : SourceTupleType.GetElements())
                {
                    TSRef<CExpressionBase> SourceElement = NewIrNode<CExprTupleElement>(
                        NewIrNode<CExprIdentifierData>(_Program, *SourceBinding.Definition),
                        I,
                        nullptr);
                    SourceElement->SetResultType(SourceElementType);
                    TSPtr<CExpressionBase> CoercedElement = MaybeCoerceToType(Move(SourceElement), ResultArrayType.GetElementType());
                    if (!CoercedElement)
                    {
                        return nullptr;
                    }
                    MakeArray->AppendSubExpr(Move(CoercedElement));
                    ++I;
                }
                SourceBinding.CodeBlock->AppendSubExpr(Move(MakeArray));
                return Move(SourceBinding.CodeBlock);
            }
            else if (const CTupleType* ResultTupleType = ResultNormalType.AsNullable<CTupleType>())
            {
                if (const CArrayType* SourceArrayType = SourceNormalType.AsNullable<CArrayType>();
                    SourceArrayType && ResultTupleType->GetFirstNamedIndex() == ResultTupleType->Num())
                {
                    return MaybeCoerceArrayToTuple(Move(Value), *SourceArrayType, *ResultTupleType);
                }
                else
                {
                    return MaybeCoerceElementsToTuple(Move(Value), SourceNormalType, *ResultTupleType);
                }
            }
            else if (ResultNormalType.IsA<CNamedType>() && SourceNormalType.IsA<CTupleType>())
            {
                const CNamedType& ResultNamedType = ResultNormalType.AsChecked<CNamedType>();
                ULANG_ASSERTF(ResultNamedType.HasValue(), "Semantic analyzer should have errored");
                if (SourceNormalType.AsChecked<CTupleType>().Num() != 0)
                {   // Should never happen, but have happend before so might again.
                    AppendGlitch(
                        *Value,
                        EDiagnostic::ErrSemantic_Unimplemented,
                        CUTF8String("Unsupported usage of named type"));
                }
                TSRef<CExpressionBase> CoercedValue = NewIrNode<CExprMakeNamed>(ResultNamedType.GetName());
                CoercedValue->IrSetResultType(ResultType);
                return Move(CoercedValue);
            }
            else if (ResultNormalType.GetKind() == SourceNormalType.GetKind())
            {
                // If coercing from some parametric type to another parametric type of the same kind, the coercion
                // distributes to the type parameter.
                const ETypeKind CommonKind = ResultNormalType.GetKind();
                if (CommonKind == ETypeKind::Option)
                {
                    // If coercing expr:?t to ?u, translate the coercion to:
                    // let(Option:=expr) in option{u(Option?)}.
                    const COptionType& ResultOptionType = ResultNormalType.AsChecked<COptionType>();

                    STempBinding SourceBinding = BindValueToTemporaryInNewCodeBlock(Move(Value));
                    TSRef<CExpressionBase> QueryValue = NewIrQueryValue(
                        NewIrNode<CExprIdentifierData>(_Program, *SourceBinding.Definition));
                    TSPtr<CExpressionBase> CoercedValue = MaybeCoerceToType(Move(QueryValue), ResultOptionType.GetValueType());
                    if (!CoercedValue)
                    {
                        return nullptr;
                    }
                    SourceBinding.CodeBlock->AppendSubExpr(NewIrNode<CExprMakeOption>(
                        &ResultOptionType,
                        Move(CoercedValue)));
                    return Move(SourceBinding.CodeBlock);
                }
                else if (CommonKind == ETypeKind::Array)
                {
                    // If coercing expr:[]t to []u, translate the coercion to:
                    // { Array:=expr; for(Item:Array) in u(Item) }
                    // expr must be evaluated outside of for otherwise all failures will be translated to empty arrays

                    const CArrayType& ResultArrayType = ResultNormalType.AsChecked<CArrayType>();
                    const CArrayType& SourceArrayType = SourceNormalType.AsChecked<CArrayType>();

                    STempBinding SourceBinding = BindValueToTemporaryInNewCodeBlock(Move(Value), _Program.GetSymbols()->AddChecked("Array", true));

                    TSRef<CControlScope> ForScope = SourceBinding.CodeBlock->_AssociatedScope->CreateNestedControlScope();
                    TSRef<CDataDefinition> ElementDefinition = ForScope->CreateDataDefinition(_Program.GetSymbols()->AddChecked("Item", true));
                    ElementDefinition->SetType(SourceArrayType.GetElementType());

                    TSRef<CIrFor> For = NewIrNode<CIrFor>(
                        ElementDefinition,
                        NewIrNode<CExprIdentifierData>(_Program, *ElementDefinition),
                        NewIrNode<CExprIdentifierData>(_Program, *SourceBinding.Definition),
                        nullptr);
                    For->_bGenerateResult = true;
                    For->_AssociatedScope = Move(ForScope);
                    For->IrSetResultType(ResultType);

                    TSRef<CExprIdentifierData> Element = NewIrNode<CExprIdentifierData>(_Program, *ElementDefinition);
                    TSPtr<CExpressionBase> CoercedElement = MaybeCoerceToType(
                        Move(Element.As<CExpressionBase>()),
                        ResultArrayType.GetElementType());
                    if (!CoercedElement)
                    {
                        return nullptr;
                    }
                    For->SetBody(NewIrNode<CIrForBody>(NewIrNode<CIrArrayAdd>(Move(CoercedElement.AsRef()))));

                    SourceBinding.CodeBlock->AppendSubExpr(Move(For));
                    SourceBinding.CodeBlock->IrSetResultType(&ResultArrayType);

                    return Move(SourceBinding.CodeBlock);
                }
                else if (CommonKind == ETypeKind::Generator)
                {
                    // If coercing expr:generator(t) to generator(u), translate the coercion to:
                    // { Array:=expr; for(Item:Array) in u(Item) }
                    // expr must be evaluated outside of for otherwise all failures will be translated to empty arrays

                    const CGeneratorType& ResultGeneratorType = ResultNormalType.AsChecked<CGeneratorType>();
                    const CGeneratorType& SourceGeneratorType = SourceNormalType.AsChecked<CGeneratorType>();

                    STempBinding SourceBinding = BindValueToTemporaryInNewCodeBlock(Move(Value), _Program.GetSymbols()->AddChecked("Generator", true));

                    TSRef<CControlScope> ForScope = SourceBinding.CodeBlock->_AssociatedScope->CreateNestedControlScope();
                    TSRef<CDataDefinition> ElementDefinition = ForScope->CreateDataDefinition(_Program.GetSymbols()->AddChecked("Item", true));
                    ElementDefinition->SetType(SourceGeneratorType.GetElementType());

                    TSRef<CIrFor> For = NewIrNode<CIrFor>(
                        ElementDefinition,
                        NewIrNode<CExprIdentifierData>(_Program, *ElementDefinition),
                        NewIrNode<CExprIdentifierData>(_Program, *SourceBinding.Definition),
                        nullptr);
                    For->_bGenerateResult = true;
                    For->_AssociatedScope = Move(ForScope);
                    For->IrSetResultType(ResultType);

                    TSRef<CExprIdentifierData> Element = NewIrNode<CExprIdentifierData>(_Program, *ElementDefinition);
                    TSPtr<CExpressionBase> CoercedElement = MaybeCoerceToType(
                        Move(Element.As<CExpressionBase>()),
                        ResultGeneratorType.GetElementType());
                    if (!CoercedElement)
                    {
                        return nullptr;
                    }
                    For->SetBody(NewIrNode<CIrForBody>(NewIrNode<CIrArrayAdd>(Move(CoercedElement.AsRef()))));

                    SourceBinding.CodeBlock->AppendSubExpr(Move(For));
                    SourceBinding.CodeBlock->IrSetResultType(&ResultGeneratorType);

                    return Move(SourceBinding.CodeBlock);
                }
                else if (CommonKind == ETypeKind::Map)
                {
                    const CMapType& ResultMapType = ResultNormalType.AsChecked<CMapType>();
                    const CMapType& SourceMapType = SourceNormalType.AsChecked<CMapType>();

                    STempBinding SourceBinding = BindValueToTemporaryInNewCodeBlock(Move(Value), _Program.GetSymbols()->AddChecked("Map", true));

                    TSRef<CControlScope> ForScope = SourceBinding.CodeBlock->_AssociatedScope->CreateNestedControlScope();

                    TSRef<CDataDefinition> MapKeyDefinition = ForScope->CreateDataDefinition(_Program.GetSymbols()->AddChecked("Key", true));
                    MapKeyDefinition->SetType(SourceMapType.GetKeyType());

                    TSRef<CDataDefinition> MapValueDefinition = ForScope->CreateDataDefinition(_Program.GetSymbols()->AddChecked("Value", true));
                    MapValueDefinition->SetType(SourceMapType.GetValueType());

                    TSRef<CIrFor> For = NewIrNode<CIrFor>(
                        MapKeyDefinition,
                        MapValueDefinition,
                        nullptr,
                        NewIrNode<CExprIdentifierData>(_Program, *SourceBinding.Definition),
                        nullptr);
                    For->_bGenerateResult = true;
                    For->_AssociatedScope = Move(ForScope);
                    For->IrSetResultType(&ResultMapType);

                    TSRef<CExprIdentifierData> MapKey = NewIrNode<CExprIdentifierData>(_Program, *MapKeyDefinition);
                    TSPtr<CExpressionBase> CoercedMapKey = MaybeCoerceToType(
                        Move(MapKey.As<CExpressionBase>()),
                        ResultMapType.GetKeyType());
                    if (!CoercedMapKey)
                    {
                        return nullptr;
                    }

                    TSRef<CExprIdentifierData> MapValue = NewIrNode<CExprIdentifierData>(_Program, *MapValueDefinition);
                    TSPtr<CExpressionBase> CoercedMapValue = MaybeCoerceToType(
                        Move(MapValue.As<CExpressionBase>()),
                        ResultMapType.GetValueType());
                    if (!CoercedMapValue)
                    {
                        return nullptr;
                    }

                    For->SetBody(NewIrNode<CIrForBody>(NewIrNode<CIrMapAdd>(Move(CoercedMapKey.AsRef()), Move(CoercedMapValue.AsRef()))));

                    SourceBinding.CodeBlock->AppendSubExpr(Move(For));
                    SourceBinding.CodeBlock->IrSetResultType(&ResultMapType);

                    return Move(SourceBinding.CodeBlock);
                }
                else if (CommonKind == ETypeKind::Function && Value->GetNodeType() == EAstNodeType::Identifier_Function)
                {
                    CExprIdentifierFunction& Identifier = static_cast<CExprIdentifierFunction&>(*Value);
                    const CFunction& Function = Identifier._Function;
                    const CFunctionType& ResultFunctionType = ResultNormalType.AsChecked<CFunctionType>();
                    if (CScope* CoercedFunctionScope = GetScopeForCoercedFunction(Function))
                    {
                        std::size_t CoercedFunctionId = 0;
                        const CFunction* CoercedFunction = FindCoercedFunction(Function, ResultFunctionType, CoercedFunctionId);
                        if (!CoercedFunction)
                        {
                            CSymbol CoercedFunctionName = _Program.GetSymbols()->AddChecked(CUTF8String("%s%zu", Function.GetName().AsCString(), CoercedFunctionId), true);
                            TSPtr<CExprFunctionDefinition> CoercedFunctionDefinition = MaybeCreateCoercedFunctionDefinition(
                                CoercedFunctionName,
                                Function,
                                *CoercedFunctionScope,
                                {},
                                ResultFunctionType);
                            if (!CoercedFunctionDefinition)
                            {
                                return nullptr;
                            }
                            CoercedFunction = CoercedFunctionDefinition->_Function.Get();
                            _CoercedFunctions.Add({&Function, &ResultFunctionType, CoercedFunction});
                            _Scope->GetPackage()->AppendMember(Move(CoercedFunctionDefinition.AsRef()));
                        }
                        return NewIrNode<CExprIdentifierFunction>(
                            *CoercedFunction,
                            &ResultFunctionType,
                            Identifier.TakeContext(),
                            Identifier.TakeQualifier());
                    }
                }
            }
            return nullptr;
        }
        return Move(Value);
    }

    void AppendCoerceToTypeGlitch(const CAstNode& Node, const CTypeBase& SourceType, const CTypeBase& ResultType)
    {
        AppendGlitch(
            Node,
            EDiagnostic::ErrSemantic_Unimplemented,
            CUTF8String(
                "Using a value of type %s as a value of type %s is not yet implemented.",
                SourceType.AsCode().AsCString(),
                ResultType.AsCode().AsCString()));
    }

    TSRef<CExpressionBase> CoerceToType(TSRef<CExpressionBase>&& IrNode, const CTypeBase* ResultType)
    {
        if (TSPtr<CExpressionBase> Result = MaybeCoerceToType(IrNode, ResultType))
        {
            return Move(Result.AsRef());
        }
        const CTypeBase* SourceType = IrNode->GetResultType(*_SemanticProgram);
        AppendCoerceToTypeGlitch(*IrNode, *SourceType, *ResultType);
        // To prevent redundant coercion errors from being produced, replace the subexpression
        // with an error node.
        TSRef<CExprError> Error = NewIrNode<CExprError>();
        Error->AppendChild(IrNode);
        return Error;
    }

    TSPtr<CExpressionBase> CoerceToType(TSPtr<CExpressionBase>&& IrNode, const CTypeBase* ResultType)
    {
        if (IrNode)
        {
            return CoerceToType(Move(IrNode.AsRef()), ResultType);
        }
        else
        {
            return nullptr;
        }
    }

    TSPtr<CExprFunctionDefinition> CreateCoercedOverridingFunctionDefinition(
        CFunction& Function,
        const TArray<const CFunctionType*>& CoercedTypes,
        const CFunctionType& CoercedType)
    {
        if (TSPtr<CExprFunctionDefinition> Result = MaybeCreateCoercedFunctionDefinition(
            Function.GetName(),
            Function,
            Function._EnclosingScope,
            CoercedTypes,
            CoercedType))
        {
            Function.MarkCoercedOverride();
            return Result;
        }
        const CFunctionType* Type = Function._Signature.GetFunctionType();
        AppendCoerceToTypeGlitch(*Function.GetIrNode(), *Type, CoercedType);
        return nullptr;
    }
    
    CSymbol MakeGeneratedName(CSymbol OriginalName)
    {
        return _Program.GetSymbols()->AddChecked(OriginalName.AsStringView(), true);
    }

    TSPtr<CExprFunctionDefinition> MaybeCreateCoercedFunctionDefinition(
        CSymbol CoercedFunctionName,
        const CFunction& Function,
        CScope& Scope,
        const TArray<const CFunctionType*>& CoercedTypes,
        const CFunctionType& CoercedType)
    {
        const CFunctionType* Type = Function._Signature.GetFunctionType();
        TSRef<CFunction> CoercedFunction = Scope.CreateFunction(MakeGeneratedName(CoercedFunctionName));
        CoercedFunction->MarkCoercion();
        CSymbol ArgumentName = _Program.GetSymbols()->AddChecked("Argument", true);
        TSRef<CDataDefinition> ArgumentDefinition = CoercedFunction->CreateDataDefinition(ArgumentName);
        SSignature CoercedSignature(CoercedType, {ArgumentDefinition.Get()});
        CoercedFunction->SetSignature(Move(CoercedSignature), Function.GetSignatureRevision());
        ArgumentDefinition->SetType(&CoercedType.GetParamsType());
        CExprInvocation::EBracketingStyle BracketingStyle = Type->GetEffects()[EEffect::decides]?
            CExprInvocation::EBracketingStyle::SquareBrackets :
            CExprInvocation::EBracketingStyle::Parentheses;
        TGuardValue<const Vst::Node*> MappedVstNodeGuard(_MappedVstNode, Function.GetIrNode()->GetMappedVstNode());
        TSRef<CExpressionBase> ArgumentExpr = NewIrNode<CExprIdentifierData>(
            _Program,
            *ArgumentDefinition);
        TSPtr<CExpressionBase> CoercedArgumentExpr = Move(ArgumentExpr);
        for (auto First = CoercedTypes.begin(), Last = CoercedTypes.end(); First != Last; --Last)
        {
            auto I = Last;
            --I;
            CoercedArgumentExpr = MaybeCoerceToType(
                Move(CoercedArgumentExpr.AsRef()),
                &(*I)->GetParamsType());
            if (!CoercedArgumentExpr)
            {
                return nullptr;
            }
        }
        CoercedArgumentExpr = MaybeCoerceToType(
            Move(CoercedArgumentExpr.AsRef()),
            &Type->GetParamsType());
        if (!CoercedArgumentExpr)
        {
            return nullptr;
        }
        TSRef<CExprInvocation> Invocation = NewIrNode<CExprInvocation>(
            BracketingStyle,
            NewIrNode<CExprIdentifierFunction>(Function, Type),
            Move(CoercedArgumentExpr.AsRef()));
        Invocation->SetResultType(&Type->GetReturnType());
        Invocation->SetResolvedCalleeType(Type);
        TSPtr<CExpressionBase> CoercedFunctionBody = Move(Invocation);
        for (auto First = CoercedTypes.begin(), Last = CoercedTypes.end(); First != Last; ++First)
        {
            CoercedFunctionBody = MaybeCoerceToType(
                Move(CoercedFunctionBody.AsRef()),
                &(*First)->GetReturnType());
            if (!CoercedFunctionBody)
            {
                return nullptr;
            }
        }
        CoercedFunctionBody = MaybeCoerceToType(
            Move(CoercedFunctionBody.AsRef()),
            &CoercedType.GetReturnType());
        if (!CoercedFunctionBody)
        {
            return nullptr;
        }
        TSRef<CExprFunctionDefinition> CoercedFunctionDefinition = NewIrNode<CExprFunctionDefinition>(
            CoercedFunction,
            nullptr,
            nullptr,
            Move(CoercedFunctionBody),
            EVstMappingType::Ir);
        CoercedFunction->SetIrNode(CoercedFunctionDefinition.Get());
        return CoercedFunctionDefinition;
    }

    CScope* GetScopeForCoercedFunction(const CFunction& Function)
    {
        if (Function.IsInstanceMember())
        {
            // The coerced function of an instance member must be added to the
            // same class.  This is only possible for classes in non-external packages.
            if (Function._EnclosingScope.GetPackage()->_Role == EPackageRole::External)
            {
                return nullptr;
            }

            return &Function._EnclosingScope;
        }
        else
        {
            // The coerced function of a non-instance member can be added to the
            // module of the current scope.
            return _Scope->GetModulePart();
        }
    }

    // Determine whether coercion is necessary between the runtime representations of two types.
    bool NeedsCoercion(const CNormalType& ResultType, const CNormalType& SourceType)
    {
        if (ResultType.GetKind() == Cases<ETypeKind::Void, ETypeKind::True>
         && SourceType.GetKind() != Cases<ETypeKind::Void, ETypeKind::True>)
        {
            return true;
        }
        else if (ResultType.GetKind() != Cases<ETypeKind::False, ETypeKind::True, ETypeKind::Void, ETypeKind::Logic>
              && SourceType.GetKind() == ETypeKind::False)
        {
            return true;
        }
        else if (ResultType.GetKind() == ETypeKind::False
              && SourceType.GetKind() != Cases<ETypeKind::False, ETypeKind::True, ETypeKind::Void, ETypeKind::Logic>)
        {
            return true;
        }
        else if (ResultType.IsA<CArrayType>() && SourceType.IsA<CTupleType>())
        {
            return true;
        }
        else if (ResultType.IsA<CTupleType>() && SourceType.IsA<CArrayType>())
        {
            return true;
        }
        else if (ResultType.GetKind() != Cases<ETypeKind::Unknown, ETypeKind::Tuple> && SourceType.IsA<CTupleType>())
        {
            return true;
        }
        else if (ResultType.IsA<CTupleType>() && SourceType.GetKind() != Cases<ETypeKind::Unknown, ETypeKind::Tuple>)
        {
            return true;
        }
        else if (ResultType.GetKind() == SourceType.GetKind())
        {
            const ETypeKind CommonKind = ResultType.GetKind();
            if (CommonKind == ETypeKind::Array)
            {
                const CArrayType& ResultArrayType = ResultType.AsChecked<CArrayType>();
                const CArrayType& SourceArrayType = SourceType.AsChecked<CArrayType>();
                return NeedsCoercion(ResultArrayType.GetElementType(), SourceArrayType.GetElementType());
            }
            else if (CommonKind == ETypeKind::Generator)
            {
                const CGeneratorType& ResultGeneratorType = ResultType.AsChecked<CGeneratorType>();
                const CGeneratorType& SourceGeneratorType = SourceType.AsChecked<CGeneratorType>();
                return NeedsCoercion(ResultGeneratorType.GetElementType(), SourceGeneratorType.GetElementType());
            }
            else if (CommonKind == ETypeKind::Map)
            {
                const CMapType& ResultMapType = ResultType.AsChecked<CMapType>();
                const CMapType& SourceMapType = SourceType.AsChecked<CMapType>();
                return NeedsCoercion(ResultMapType.GetKeyType(), SourceMapType.GetKeyType())
                    || NeedsCoercion(ResultMapType.GetValueType(), SourceMapType.GetValueType());
            }
            else if (CommonKind == ETypeKind::Option)
            {
                const COptionType& ResultOptionType = ResultType.AsChecked<COptionType>();
                const COptionType& SourceOptionType = SourceType.AsChecked<COptionType>();
                return NeedsCoercion(ResultOptionType.GetValueType(), SourceOptionType.GetValueType());
            }
            else if (CommonKind == ETypeKind::Tuple)
            {
                const CTupleType& ResultTupleType = ResultType.AsChecked<CTupleType>();
                const CTupleType& SourceTupleType = SourceType.AsChecked<CTupleType>();
                int32_t ResultNumElements = ResultTupleType.Num();
                if (ResultNumElements != SourceTupleType.Num())
                {
                    return true;
                }
                for (int32_t ElementIndex = 0; ElementIndex != ResultNumElements; ++ElementIndex)
                {
                    const CNormalType& ResultElementType = ResultTupleType[ElementIndex]->GetNormalType();
                    const CNormalType& SourceElementType = SourceTupleType[ElementIndex]->GetNormalType();
                    if (const CNamedType* ResultNamedType = ResultElementType.AsNullable<CNamedType>())
                    {
                        if (const CNamedType* SourceNamedType = SourceElementType.AsNullable<CNamedType>())
                        {
                            if (ResultNamedType->GetName() != SourceNamedType->GetName())
                            {
                                return true;
                            }
                            if (NeedsCoercion(ResultNamedType->GetValueType(), SourceNamedType->GetValueType()))
                            {
                                return true;
                            }
                        }
                        else
                        {
                            return true;
                        }
                    }
                    else if (SourceElementType.IsA<CNamedType>())
                    {
                        return true;
                    }
                    else if (NeedsCoercion(ResultElementType, SourceElementType))
                    {
                        return true;
                    }
                }
                return false;
            }
            else if (CommonKind == ETypeKind::Function)
            {
                const CFunctionType& ResultFunctionType = ResultType.AsChecked<CFunctionType>();
                const CFunctionType& SourceFunctionType = SourceType.AsChecked<CFunctionType>();
                if (SourceFunctionType.ImplicitlySpecialized())
                {
                    return false;
                }
                if (NeedsCoercion(&SourceFunctionType.GetParamsType(), &ResultFunctionType.GetParamsType()))
                {
                    return true;
                }
                if (NeedsCoercion(&ResultFunctionType.GetReturnType(), &SourceFunctionType.GetReturnType()))
                {
                    return true;
                }
                if (ResultFunctionType.GetEffects() != SourceFunctionType.GetEffects())
                {
                    return true;
                }
                return false;
            }
            else if (CommonKind == ETypeKind::Named)
            {
                const CNamedType& ResultNamedType = ResultType.AsChecked<CNamedType>();
                const CNamedType& SourceNamedType = SourceType.AsChecked<CNamedType>();
                if (ResultNamedType.GetName() != SourceNamedType.GetName())
                {
                    return true;
                }
                if (NeedsCoercion(ResultNamedType.GetValueType(), SourceNamedType.GetValueType()))
                {
                    return true;
                }
                return false;
            }
        }
        else
        {
            const bool bResultIsDynamicallyTyped = ResultType.GetKind() == ETypeKind::Any;
            const bool bSourceIsDynamicallyTyped = SourceType.GetKind() == ETypeKind::Any;
            return bResultIsDynamicallyTyped != bSourceIsDynamicallyTyped;
        }

        return false;
    }

    bool NeedsCoercion(const CTypeBase* ResultType, const CTypeBase* SourceType)
    {
        return NeedsCoercion(ResultType->GetNormalType(), SourceType->GetNormalType());
    }

    bool NeedsCoercion(const CExpressionBase& Value, const CNormalType& ResultType, const CNormalType& SourceType)
    {
        if (Value.GetNodeType() == EAstNodeType::Identifier_Function)
        {
            const CExprIdentifierFunction& Identifier = static_cast<const CExprIdentifierFunction&>(Value);
            if (!Identifier.HasAttributeClass(_Program._constructorClass, _Program) && Identifier._Function.HasAttributeClass(_Program._constructorClass, _Program))
            {
                return true;
            }
        }
        if (NeedsCoercion(ResultType, SourceType))
        {
            return true;
        }
        return false;
    }

    CExprMacroCall::CClause CreateClause(const CExprMacroCall::CClause& Clause)
    {
        return CExprMacroCall::CClause(Clause.Tag(), Clause.Form(), GenNodes(Clause.Exprs()));
    }

    //-------------------------------------------------------------
    // The copy code

    TSRef<CExprExternal>GenExternal(CExprExternal& AstNode)
    {
        TSRef<CExprExternal>IrNode = NewIrNode<CExprExternal>(_Program);
        return IrNode;
    }

    TSRef<CExprLogic>GenLogic(CExprLogic& AstNode)
    {
        TSRef<CExprLogic>IrNode = NewIrNode<CExprLogic>(_Program, AstNode._Value);
        return IrNode;
    }

    TSRef<CExprNumber>GenNumber(CExprNumber& AstNode)
    {
        TSRef<CExprNumber> IrNode = AstNode.IsFloat()
            ? NewIrNode<CExprNumber>(_Program, AstNode.GetFloatValue())
            : NewIrNode<CExprNumber>(_Program, AstNode.GetIntValue());
        return IrNode;
    }
    
    TSRef<CExprChar>GenChar(CExprChar& AstNode)
    {
        TSRef<CExprChar> IrNode = NewIrNode<CExprChar>(AstNode._CodePoint, AstNode._Type);
        return IrNode;
    }

    TSRef<CExprString>GenString(CExprString& AstNode)
    {
        TSRef<CExprString> IrNode = NewIrNode<CExprString>(AstNode._String);
        return IrNode;
    }

    TSRef<CExprPath>GenPath(CExprPath& AstNode)
    {
        TSRef<CExprPath> IrNode = NewIrNode<CExprPath>(AstNode._Path);
        return IrNode;
    }

    TSRef<CExprEnumLiteral>GenEnum(CExprEnumLiteral& AstNode)
    {
        TSRef<CExprEnumLiteral> IrNode = NewIrNode<CExprEnumLiteral>(AstNode._Enumerator, GenNode(AstNode.Context()), GenNode(AstNode.Qualifier()));
        return IrNode;
    }

    TSRef<CExprType>GenType(CExprType& AstNode)
    {
        TSRef<CExpressionBase> AbstractValue = AstNode._AbstractValue;
        // We don't want to gen the abstract value because it could be in an incosistent state for `type{ _X:int where ... }` and it's not needed for lowering to BP bytecode.
        TSRef<CExprType> IrNode = NewIrNode<CExprType>(Move(AbstractValue), *AstNode.GetTypeType());
        return IrNode;
    }

    TSRef<CExprFunctionLiteral>GenFunction(CExprFunctionLiteral& AstNode)
    {
        TSRef<CExprFunctionLiteral> IrNode = NewIrNode<CExprFunctionLiteral>(GenNode(*AstNode.Domain()), GenNode(*AstNode.Range()));
        return IrNode;
    }

    TSPtr<CExpressionBase> GenNodeUnlessModule(const TSPtr<CExpressionBase>& Context)
    {
        // Module id was used during analysis, but should not be left for code generation.
        if (!Context || Context->GetNodeType() == Cases<EAstNodeType::Identifier_Module, EAstNodeType::Identifier_ModuleAlias>)
        {
            return TSPtr<CExpressionBase>();
        }
        else
        {
            return GenNode(Context);
        }
    }

    TSRef<CExprIdentifierUnresolved> GenIdentifierUnresolved(CExprIdentifierUnresolved& AstNode)
    {
        TSRef<CExprIdentifierUnresolved> IrNode = NewIrNode<CExprIdentifierUnresolved>(
            AstNode._Symbol,
            GenNodeUnlessModule(AstNode.Context()),
            GenNode(AstNode.Qualifier()));
        return IrNode;
    }

    TSRef<CExprIdentifierClass>GenIdentifierClass(CExprIdentifierClass& AstNode)
    {
        TSRef<CExprIdentifierClass> IrNode = NewIrNode<CExprIdentifierClass>(AstNode.GetTypeType(_Program), GenNodeUnlessModule(AstNode.Context()), GenNode(AstNode.Qualifier()));
        return IrNode;
    }

    TSRef<CExprIdentifierModule>GenIdentifierModule(CExprIdentifierModule& AstNode)
    {
        TSRef<CExprIdentifierModule> IrNode = NewIrNode<CExprIdentifierModule>(AstNode.GetModule(_Program), GenNodeUnlessModule(AstNode.Context()), GenNode(AstNode.Qualifier()));
        return IrNode;
    }

    TSRef<CExprIdentifierModuleAlias>GenIdentifierModuleAlias(CExprIdentifierModuleAlias& AstNode)
    {
        TSRef<CExprIdentifierModuleAlias> IrNode = NewIrNode<CExprIdentifierModuleAlias>(AstNode._ModuleAlias, GenNodeUnlessModule(AstNode.Context()), GenNode(AstNode.Qualifier()));
        return IrNode;
    }

    TSRef<CExprEnumerationType>GenIdentifierEnum(CExprEnumerationType& AstNode)
    {
        TSRef<CExprEnumerationType> IrNode = NewIrNode<CExprEnumerationType>(AstNode.GetTypeType(_Program), GenNodeUnlessModule(AstNode.Context()), GenNode(AstNode.Qualifier()));
        return IrNode;
    }

    TSRef<CExprInterfaceType>GenIdentifierInterface(CExprInterfaceType& AstNode)
    {
        TSRef<CExprInterfaceType> IrNode = NewIrNode<CExprInterfaceType>(AstNode.GetTypeType(_Program), GenNodeUnlessModule(AstNode.Context()), GenNode(AstNode.Qualifier()));
        return IrNode;
    }

    TSRef<CExpressionBase> GenIdentifierData(CExprIdentifierData& AstNode)
    {
        // If a data definition is instantiated as part of a parametric type, lower it to its prototype definition+generic type.
        const CTypeBase* ResultType = AstNode.GetResultType(_Program);
        const CDataDefinition& OverriddenPrototypeDefinition = *AstNode._DataDefinition.GetBaseOverriddenDefinition().GetPrototypeDefinition();
        TSPtr<CExpressionBase> IrContext;
        if (TSPtr<CExpressionBase> Context = AstNode.Context())
        {
            IrContext = GenNodeUnlessModule(Move(Context.AsRef()));
        }
        TSRef<CExprIdentifierData> IrNode = NewIrNode<CExprIdentifierData>(
            _Program,
            OverriddenPrototypeDefinition,
            Move(IrContext),
            GenNode(AstNode.Qualifier()));
        return CoerceToType(Move(IrNode.As<CExpressionBase>()), ResultType);
    }

    TSRef<CExprIdentifierTypeAlias>GenIdentifierTypeAlias(CExprIdentifierTypeAlias& AstNode)
    {
        TSRef<CExprIdentifierTypeAlias> IrNode = NewIrNode<CExprIdentifierTypeAlias>(AstNode._TypeAlias, GenNodeUnlessModule(AstNode.Context()), GenNode(AstNode.Qualifier()));
        return IrNode;
    }

    TSRef<CExprIdentifierTypeVariable> GenIdentifierTypeVariable(CExprIdentifierTypeVariable& AstNode)
    {
        TSRef<CExprIdentifierTypeVariable> IrNode = NewIrNode<CExprIdentifierTypeVariable>(AstNode._TypeVariable, GenNodeUnlessModule(AstNode.Context()), GenNode(AstNode.Qualifier()));
        return IrNode;
    }

    // Get all type variables that were instantiated for this function
    // identifier.  This includes both type variables quantified by the function
    // directly, as well as type variables quantified by any containing class or
    // interface (perhaps implicitly via the rewriting of `:type`).  Note that
    // repeated instantiation, e.g.
    // @code
    // class1(t:type) := class:
    //     Method(:u where u:subtype(t)):void
    // @endcode
    // results in merging the flow types generated for `t` (via `Merge`) into
    // the flow types generated for `u` - i.e., no repeated remapping by callers
    // of `GetInstantiatedTypeVariables` need occur.
    TArray<SInstantiatedTypeVariable> GetInstantiatedTypeVariables(const CExprIdentifierFunction& AstNode)
    {
        TArray<SInstantiatedTypeVariable> InstTypeVariables = AstNode._InstantiatedTypeVariables;
        CScope& EnclosingScope = AstNode._Function._EnclosingScope;
        if (EnclosingScope.GetKind() == CScope::EKind::Class)
        {
            for (const STypeVariableSubstitution Substitution : static_cast<const CClass&>(EnclosingScope)._TypeVariableSubstitutions)
            {
                InstTypeVariables.Emplace(Substitution._NegativeType, Substitution._PositiveType);
            }
        }
        else if (EnclosingScope.GetKind() == CScope::EKind::Interface)
        {
            for (const STypeVariableSubstitution Substitution : static_cast<const CInterface&>(EnclosingScope)._TypeVariableSubstitutions)
            {
                InstTypeVariables.Emplace(Substitution._NegativeType, Substitution._PositiveType);
            }
        }
        return InstTypeVariables;
    }

    TSRef<CExpressionBase> GenIdentifierFunction(CExprIdentifierFunction& AstNode)
    {
        // If the function is instantiated as part of a parametric type, lower it to its prototype definition+generic type.
        TArray<SInstantiatedTypeVariable> InstTypeVariables = GetInstantiatedTypeVariables(AstNode);
        const CTypeBase* ResultType = &SemanticTypeUtils::AsPositive(*AstNode.GetResultType(_Program), InstTypeVariables);
        const CFunction& PrototypeFunction = *AstNode._Function.GetPrototypeDefinition();
        const CFunctionType* SourceType = PrototypeFunction._Signature.GetFunctionType();
        TSPtr<CExpressionBase> IrContext;
        if (TSPtr<CExpressionBase> Context = AstNode.Context())
        {
            IrContext = GenNodeUnlessModule(Move(Context.AsRef()));
        }
        TSRef<CExpressionBase> IrNode = NewIrNode<CExprIdentifierFunction>(
            PrototypeFunction,
            TArray<SInstantiatedTypeVariable>{},
            SourceType,
            AstNode._ConstructorNegativeReturnType,
            Move(IrContext),
            GenNode(AstNode.Qualifier()),
            AstNode._bSuperQualified);
        return CoerceToType(Move(IrNode), ResultType);
    }

    TSRef<CExprIdentifierOverloadedFunction> GenIdentifierOverloadedFunction(CExprIdentifierOverloadedFunction& AstNode)
    {
        TArray<const CFunction*> OverloadedFunctions(AstNode._FunctionOverloads);
        TSRef<CExprIdentifierOverloadedFunction> IrNode = NewIrNode<CExprIdentifierOverloadedFunction>(
            Move(OverloadedFunctions),
            AstNode._bConstructor,
            AstNode._Symbol,
            AstNode._TypeOverload,
            GenNodeUnlessModule(AstNode.Context()),
            GenNode(AstNode.Qualifier()),
            AstNode.GetResultType(_Program));
        return IrNode;
    }

    TSRef<CExprSelf> GenSelf(CExprSelf& AstNode)
    {
        TSRef<CExprSelf> IrNode = NewIrNode<CExprSelf>(AstNode.GetResultType(_Program), GenNode(AstNode.Qualifier()));
        return IrNode;
    }

    TSRef<CExprDefinition> GenExprDefinition(CExprDefinition& AstNode)
    {
        TSRef<CExprDefinition> IrNode = NewIrNode<CExprDefinition>(GenNode(AstNode.Element()), GenNode(AstNode.ValueDomain()), GenNode(AstNode.Value()));
        IrNode->SetName(AstNode.GetName());
        if (CDataDefinition* FunctionParamDefinition = FindFunctionParamDefinition(AstNode))
        {
            FunctionParamDefinition->SetIrNode(IrNode.Get());
        }
        return IrNode;
    }

    TSRef<CExpressionBase> GenInvocation(CExprInvocation& AstNode, const CExprIdentifierFunction& AstIdentifierFunction, TSPtr<CExpressionBase>&& IrContext)
    {
        const CFunction& PrototypeCalleeFunction = *AstIdentifierFunction._Function.GetPrototypeDefinition();
        // Native methods are required to implement the prototype of the base
        // overridden definition - i.e., the type erased form of the root-most
        // method signature.
        const CFunctionType* PrototypeCalleeType = AstIdentifierFunction._Function.IsNative() ?
            AstIdentifierFunction._Function.GetBaseOverriddenDefinition().GetPrototypeDefinition()->_Signature.GetFunctionType() :
            AstIdentifierFunction._Function.GetPrototypeDefinition()->_Signature.GetFunctionType();
        TSPtr<CExpressionBase> IrQualifier = GenNode(AstIdentifierFunction.Qualifier());
        TSRef<CExpressionBase> IrCallee = NewIrNode<CExprIdentifierFunction>(
            PrototypeCalleeFunction,
            TArray<SInstantiatedTypeVariable>{},
            PrototypeCalleeType,
            AstIdentifierFunction._ConstructorNegativeReturnType,
            Move(IrContext),
            Move(IrQualifier),
            AstIdentifierFunction._bSuperQualified);
        TSRef<CExpressionBase> IrArgument = GenNode(*AstNode.GetArgument());
        const CFunctionType& InstCalleeType = AstIdentifierFunction.GetResultType(_Program)->GetNormalType().AsChecked<CFunctionType>();
        const CTypeBase& InstParamsType = SemanticTypeUtils::AsPositive(
            InstCalleeType.GetParamsType(),
            GetInstantiatedTypeVariables(AstIdentifierFunction));
        IrArgument = CoerceToType(Move(IrArgument), &InstParamsType);
        if (!InstCalleeType.ImplicitlySpecialized())
        {
            // Attempt to coerce the argument to the generalized function parameter
            // type, result to expected result type first.  Failing this, coerce the
            // callee to the instantiated callee type.  Values of function type can
            // only be coerced if the value is the function identifier (to avoid
            // generating a closure), so either options may fail - try both to
            // ensure more programs compile.
            if (TSPtr<CExpressionBase> CoercedIrArgument = MaybeCoerceToType(
                IrArgument,
                &PrototypeCalleeType->GetParamsType()))
            {
                TSRef<CExprInvocation> IrNode = NewIrNode<CExprInvocation>(
                    AstNode._CallsiteBracketStyle,
                    Move(IrCallee),
                    Move(CoercedIrArgument.AsRef()));
                IrNode->SetResolvedCalleeType(AstNode.GetResolvedCalleeType());
                IrNode->SetResultType(&PrototypeCalleeType->GetReturnType());
                const CTypeBase* ResultType = AstNode.GetResultType(_Program);
                if (TSPtr<CExpressionBase> CoercedIrNode = MaybeCoerceToType(
                    IrNode,
                    ResultType))
                {
                    // `MaybeCoerceToType` will ensure the low representation of
                    // the types are the same. Explicitly set the type to
                    // `ResultType` to preserve the high representation
                    // (important for digest generation).
                    CoercedIrNode->IrSetResultType(ResultType);
                    return CoercedIrNode.AsRef();
                }
            }
            // Coercion of the argument and return types failed.  Attempt coercion of the callee.
            IrCallee = CoerceToType(Move(IrCallee), &InstCalleeType);
        }
        TSRef<CExprInvocation> IrNode = NewIrNode<CExprInvocation>(
            AstNode._CallsiteBracketStyle,
            Move(IrCallee),
            Move(IrArgument));
        IrNode->SetResolvedCalleeType(AstNode.GetResolvedCalleeType());
        IrNode->SetResultType(AstNode.GetResultType(_Program));
        return IrNode;
    }

    TSRef<CExpressionBase> GenInvocation(CExprInvocation& AstNode, const CExprIdentifierFunction& AstIdentifierFunction)
    {
        ULANG_ASSERT(_Scope);

        if (TSPtr<CExpressionBase> IrContext = GenNodeUnlessModule(AstIdentifierFunction.Context()))
        {
            // Hoist the context to avoid duplicating side effects.
            STempBinding TempBinding = BindValueToTemporaryInNewCodeBlock(Move(IrContext.AsRef()));
            TSRef<CExpressionBase> TempContext = NewIrNode<CExprIdentifierData>(
                _Program,
                *TempBinding.Definition);
            TSRef<CExpressionBase> Result = GenInvocation(AstNode, AstIdentifierFunction, Move(TempContext));
            TempBinding.CodeBlock->AppendSubExpr(Move(Result));
            return Move(TempBinding.CodeBlock);
        }
        return GenInvocation(AstNode, AstIdentifierFunction, nullptr);
    }

    TSRef<CExpressionBase> GenInvocation(CExprInvocation& AstNode)
    {
        const TSPtr<CExpressionBase>& AstCallee = AstNode.GetCallee();
        if (AstCallee->GetNodeType() == EAstNodeType::Identifier_Function)
        {
            const CExprIdentifierFunction& AstIdentifierFunction = static_cast<const CExprIdentifierFunction&>(*AstCallee);
            return GenInvocation(AstNode, AstIdentifierFunction);
        }
        const CFunctionType& CalleeType = AstCallee->GetResultType(_Program)->GetNormalType().AsChecked<CFunctionType>();
        TSRef<CExpressionBase> IrCallee = GenNode(*AstCallee);
        TSRef<CExpressionBase> IrArgument = CoerceToType(GenNode(AstNode.GetArgument().AsRef()), &CalleeType.GetParamsType());
        TSRef<CExprInvocation> IrNode = NewIrNode<CExprInvocation>(
            AstNode._CallsiteBracketStyle,
            Move(IrCallee),
            Move(IrArgument));
        IrNode->SetResolvedCalleeType(AstNode.GetResolvedCalleeType());
        IrNode->SetResultType(AstNode.GetResultType(_Program));
        return IrNode;
    }

    TSRef<CExpressionBase>GenUnaryArithmetic(CExprUnaryArithmetic& AstNode)
    {
        return GenInvocation(AstNode);
    }

    TSRef<CExpressionBase>GenBinaryArithmetic(CExprBinaryArithmetic& AstNode)
    {
        return GenInvocation(AstNode);
    }
    
    TSRef<CExprShortCircuitAnd>GenShortCircuitAnd(CExprShortCircuitAnd& AstNode)
    {
        TSRef<CExprShortCircuitAnd> IrNode = NewIrNode<CExprShortCircuitAnd>(
            GenNode(AstNode.Lhs()),
            GenNode(AstNode.Rhs()));
        return IrNode;
    }

    TSRef<CExprShortCircuitOr>GenShortCircuitOr(CExprShortCircuitOr& AstNode)
    {
        const CTypeBase* JoinType = AstNode.GetResultType(_Program);
        TSRef<CExprShortCircuitOr> IrNode = NewIrNode<CExprShortCircuitOr>(
            CoerceToType(GenNode(*AstNode.Lhs()), JoinType),
            CoerceToType(GenNode(*AstNode.Rhs()), JoinType));
        return IrNode;
    }

    TSRef<CExprLogicalNot> GenLogicalNot(CExprLogicalNot& AstNode)
    {
        TSRef<CExprLogicalNot> IrNode = NewIrNode<CExprLogicalNot>(GenNode(AstNode.Operand()));
        return IrNode;
    }

    TSRef<CExpressionBase> GenComparison(CExprComparison& AstNode)
    {
        return GenInvocation(AstNode);
    }

    TSRef<CExpressionBase> GenQueryValue(CExprQueryValue& AstNode)
    {
        return GenInvocation(AstNode);
    }

    TSRef<CExprMakeOption> GenMakeOption(CExprMakeOption& AstNode)
    {
        const CTypeBase* ValueType = AstNode.GetOptionType(_Program)->GetValueType();
        TSRef<CExprMakeOption> IrNode = NewIrNode<CExprMakeOption>(
            AstNode.GetResultType(_Program),
            AstNode.Operand()
                ? CoerceToType(GenNode(AstNode.Operand().AsRef()), ValueType)
                : TSPtr<CExpressionBase>());
        return IrNode;
    }

    TSRef<CExprMakeArray> GenMakeArray(CExprMakeArray& AstNode)
    {
        const CTypeBase* ElementType = AstNode.GetArrayType(_Program)->GetElementType();
        const TSPtrArray<CExpressionBase>& SubExprs = AstNode.GetSubExprs();
        TSRef<CExprMakeArray> IrNode = NewIrNode<CExprMakeArray>(SubExprs.Num());
        for (const TSPtr<CExpressionBase>& ElementAst : AstNode.GetSubExprs())
        {
            IrNode->AppendSubExpr(CoerceToType(GenNode(*ElementAst), ElementType));
        }
        return IrNode;
    }
 
    TSRef<CExprMakeMap> GenMakeMap(CExprMakeMap& AstNode)
    {
        const CMapType* MapType = AstNode.GetMapType(_Program);
        const CTypeBase* KeyType = MapType->GetKeyType();
        const CTypeBase* ValueType = MapType->GetValueType();
        const TSPtrArray<CExpressionBase>& SubExprs = AstNode.GetSubExprs();
        TSRef<CExprMakeMap> IrNode = NewIrNode<CExprMakeMap>(SubExprs.Num());
        for (const TSPtr<CExpressionBase>& PairAst : AstNode.GetSubExprs())
        {
            ULANG_ASSERTF(PairAst->GetNodeType() == EAstNodeType::Literal_Function, "CExprMakeMap subexpressions must be function literals");
            const CExprFunctionLiteral& PairLiteralAst = static_cast<const CExprFunctionLiteral&>(*PairAst);
            IrNode->AppendSubExpr(NewIrNode<CExprFunctionLiteral>(
                CoerceToType(GenNode(*PairLiteralAst.Domain()), KeyType),
                CoerceToType(GenNode(*PairLiteralAst.Range()), ValueType)));
        }
        return IrNode;
    }

    TSRef<CExprMakeTuple> GenMakeTuple(CExprMakeTuple& AstNode)
    {
        const CTupleType* TupleType = AstNode.GetTupleType(_Program);
        const TSPtrArray<CExpressionBase>& SubExprs = AstNode.GetSubExprs();
        TSRef<CExprMakeTuple> IrNode = NewIrNode<CExprMakeTuple>(SubExprs.Num());
        ULANG_ASSERTF(AstNode.GetSubExprs().Num() == TupleType->GetElements().Num(), "Mismatched number of elements");
        for (int32_t ElementIndex = 0; ElementIndex < AstNode.GetSubExprs().Num(); ++ElementIndex)
        {
            IrNode->AppendSubExpr(CoerceToType(
                GenNode(*AstNode.GetSubExprs()[ElementIndex]),
                (*TupleType)[ElementIndex]));
        }
        return IrNode;
    }

    TSRef<CExprTupleElement> GenTupleElement(CExprTupleElement& AstNode)
    {
        TSRef<CExprTupleElement> IrNode = NewIrNode<CExprTupleElement>(GenNode(*AstNode._TupleExpr), AstNode._ElemIdx, AstNode.GetMappedVstNode());
        return IrNode;
    }

    TSRef<CExprMakeRange> GenMakeRange(CExprMakeRange& AstNode)
    {
        TSRef<CExprMakeRange> IrNode = NewIrNode<CExprMakeRange>(GenNode(*AstNode._Lhs), GenNode(*AstNode._Rhs));
        return IrNode;
    }

    TSRef<CExpressionBase> GenInvokeType(CExprInvokeType& AstNode)
    {
        TSRef<CExpressionBase> IrNode = GenNode(AstNode._Argument);

        // Elide infallible casts unless they are to void.
        if (AstNode._bIsFallible)
        {
            IrNode = NewIrNode<CExprInvokeType>(
                AstNode._NegativeType,
                AstNode.GetResultType(_Program),
                AstNode._bIsFallible,
                GenNode(AstNode._TypeAst),
                Move(IrNode));
        }

        return CoerceToType(
            Move(IrNode),
            AstNode._NegativeType);
    }

    TSRef<CExprPointerToReference> GenPointerToReference(CExprPointerToReference& AstNode)
    {
        TSRef<CExprPointerToReference> IrNode = NewIrNode<CExprPointerToReference>(GenNode(*AstNode.Operand()));
        return IrNode;
    }

    TSRef<CExprSet> GenSet(CExprSet& AstNode)
    {
        return NewIrNode<CExprSet>(GenNode(*AstNode.Operand()));
    }

    TSRef<CExprNewPointer> GenNewPointer(CExprNewPointer& AstNode)
    {
        TSRef<CExprNewPointer> IrNode = NewIrNode<CExprNewPointer>(static_cast<const CPointerType*>(AstNode.GetResultType(_Program)), GenNode(*AstNode._Value));
        return IrNode;
    }

    TSRef<CExprReferenceToValue> GenReferenceToValue(CExprReferenceToValue& AstNode)
    {
        TSRef<CExprReferenceToValue> IrNode = NewIrNode<CExprReferenceToValue>(GenNode(*AstNode.Operand()));
        return IrNode;
    }

    TSRef<CExprAssignment> GenAssignment(CExprAssignment& AstNode)
    {
        TSRef<CExprAssignment> IrNode = NewIrNode<CExprAssignment>(
            AstNode.Op(),
            GenNode(AstNode.Lhs()),
            GenNode(AstNode.Rhs()));
        return IrNode;
    }

    TSRef<CExprArrayTypeFormer> GenArrayTypeFormer(CExprArrayTypeFormer& AstNode)
    {
        TSRef<CExprArrayTypeFormer> IrNode = NewIrNode<CExprArrayTypeFormer>(GenNode(*AstNode.GetInnerTypeAst()));
        IrNode->_TypeType = AstNode._TypeType;
        return IrNode;
    }

    TSRef<CExprGeneratorTypeFormer> GenGeneratorTypeFormer(CExprGeneratorTypeFormer& AstNode)
    {
        TSRef<CExprGeneratorTypeFormer> IrNode = NewIrNode<CExprGeneratorTypeFormer>(GenNode(*AstNode.GetInnerTypeAst()));
        IrNode->_TypeType = AstNode._TypeType;
        return IrNode;
    }

    TSRef<CExprMapTypeFormer> GenMapTypeFormer(CExprMapTypeFormer& AstNode)
    {
        TSRef<CExprMapTypeFormer> IrNode = NewIrNode<CExprMapTypeFormer>(GenNodes(AstNode.KeyTypeAsts()), GenNode(*AstNode.ValueTypeAst()));
        IrNode->_TypeType = AstNode._TypeType;
        return IrNode;
    }

    TSRef<CExprOptionTypeFormer> GenOptionTypeFormer(CExprOptionTypeFormer& AstNode)
    {
        TSRef<CExprOptionTypeFormer> IrNode = NewIrNode<CExprOptionTypeFormer>(GenNode(*AstNode.GetInnerTypeAst()));
        IrNode->_TypeType = AstNode._TypeType;
        return IrNode;
    }

    TSRef<CExprSubtype> GenSubtype(CExprSubtype& AstNode)
    {
        TSRef<CExprSubtype> IrNode = NewIrNode<CExprSubtype>(GenNode(*AstNode.GetInnerTypeAst()));
        IrNode->_TypeType = AstNode._TypeType;
        return IrNode;
    }

    TSRef<CExprTupleType> GenTupleType(CExprTupleType& AstNode)
    {
        const TSPtrArray<CExpressionBase>& ElementTypes = AstNode.GetElementTypeExprs();
        TSRef<CExprTupleType> IrNode = NewIrNode<CExprTupleType>(ElementTypes.Num());
        for (const TSPtr<CExpressionBase>& ElementType : ElementTypes)
        {
            IrNode->GetElementTypeExprs().Add(GenNode(*ElementType));
        }
        IrNode->_TypeType = AstNode._TypeType;
        return IrNode;
    }

    TSRef<CExprArrow> GenArrow(CExprArrow& AstNode)
    {
        TSRef<CExprArrow> IrNode = NewIrNode<CExprArrow>(GenNode(*AstNode.Domain()), GenNode(*AstNode.Range()));
        IrNode->_TypeType = AstNode._TypeType;
        return IrNode;
    }

    TSRef<CExprArchetypeInstantiation> GenArchetypeInstantiation(CExprArchetypeInstantiation& AstNode)
    {
        TSRef<CExprArchetypeInstantiation> IrNode = NewIrNode<CExprArchetypeInstantiation>(
            GenNode(*AstNode._ClassAst),
            CreateClause(AstNode._BodyAst),
            AstNode.GetResultType(*_SemanticProgram));
        for (const TSRef<CExpressionBase>& Argument : AstNode.Arguments())
        {
            if (Argument->GetNodeType() == EAstNodeType::Definition)
            {
                const CExprDefinition& Definition = static_cast<const CExprDefinition&>(*Argument);
                const CExprIdentifierData& Element = static_cast<const CExprIdentifierData&>(*Definition.Element());

                const CDataDefinition& OverriddenPrototypeDefinition = *Element._DataDefinition.GetBaseOverriddenDefinition().GetPrototypeDefinition();

                const CTypeBase* PrototypeInitializerType = OverriddenPrototypeDefinition.IsVar()
                    ? OverriddenPrototypeDefinition.GetType()->GetNormalType().AsChecked<CPointerType>().PositiveValueType()
                    : OverriddenPrototypeDefinition.GetType();

                IrNode->AppendArgument(NewIrNode<CExprDefinition>(
                    NewIrNode<CExprIdentifierData>(*_SemanticProgram, OverriddenPrototypeDefinition),
                    nullptr,
                    CoerceToType(GenNode(Definition.Value()), PrototypeInitializerType)));
            }
            else if (Argument->GetNodeType() == EAstNodeType::Flow_CodeBlock)
            {
                IrNode->AppendArgument(GenNode(*Argument));
            }
            else if (Argument->GetNodeType() == EAstNodeType::Flow_Let)
            {
                IrNode->AppendArgument(GenNode(*Argument));
            }
            else if (Argument->GetNodeType() == EAstNodeType::Invoke_Invocation)
            {
                IrNode->AppendArgument(GenNode(*Argument));
            }
            else
            {
                ULANG_ERRORF("Unexpected node type");
            }
        }
        return IrNode;
    }

    TSRef<CExprCodeBlock> GenCodeBlock(CExprCodeBlock& AstNode)
    {
        TGuardValue<CScope*> ScopeGuard(_Scope, AstNode._AssociatedScope.Get());
        ULANG_ASSERT(_Scope);

        const TSPtrArray<CExpressionBase>& SubExprs = AstNode.GetSubExprs();
        TSRef<CExprCodeBlock> IrNode = NewIrNode<CExprCodeBlock>(SubExprs.Num());
        for (const TSPtr<CExpressionBase>& SubExpr : AstNode.GetSubExprs())
        {
            IrNode->AppendSubExpr(GenNode(*SubExpr));
        }
        return IrNode;
    }

    TSRef<CExprLet> GenLet(CExprLet& AstNode)
    {
        const TSPtrArray<CExpressionBase>& SubExprs = AstNode.GetSubExprs();
        TSRef<CExprLet> IrNode = NewIrNode<CExprLet>(SubExprs.Num());
        for (const TSPtr<CExpressionBase>& SubExpr : AstNode.GetSubExprs())
        {
            IrNode->AppendSubExpr(GenNode(*SubExpr));
        }
        return IrNode;
    }

    TSRef<CExprDefer> GenDefer(CExprDefer& AstNode)
    {
        TSRef<CExprDefer> IrNode = NewIrNode<CExprDefer>();
        IrNode->SetExpr(GenNode(AstNode.Expr()));
        return IrNode;
    }

    TSRef<CExprIf> GenIf(CExprIf& AstNode)
    {
        TSRef<CExprCodeBlock> Condition = GenCodeBlock(*AstNode.GetCondition());

        TGuardValue<CScope*> ScopeGuard(_Scope, AstNode.GetCondition()->_AssociatedScope.Get());
        ULANG_ASSERT(_Scope);

        const CTypeBase* ResultType = AstNode.GetResultType(_Program);
        TSPtr<CExpressionBase> Then = AstNode.GetThenClause()
            ? CoerceToType(GenNode(*AstNode.GetThenClause()), ResultType)
            : TSPtr<CExpressionBase>();
        TSPtr<CExpressionBase> Else = CoerceToType(GenNode(AstNode.GetElseClause()), ResultType);
        TSRef<CExprIf> IrNode = NewIrNode<CExprIf>(Move(Condition), Move(Then), Move(Else));
        return IrNode;
    }

    bool IsGenerator(CExpressionBase& Expr)
    {
        if (Expr.GetNodeType() == EAstNodeType::Definition_Data
         || Expr.GetNodeType() == EAstNodeType::Definition_IterationPair)
        {
            CExprDefinition& Definition = static_cast<CExprDefinition&>(Expr);
            if (Definition.Value())
            {
                const CNormalType& IterableType = Definition.Value()->GetResultType(_Program)->GetNormalType();
                return IterableType.IsA<CRangeType>();
            }
            if (Definition.ValueDomain())
            {
                const CNormalType& IterableType = Definition.ValueDomain()->GetResultType(_Program)->GetNormalType();
                return IterableType.IsA<CArrayType>() || IterableType.IsA<CGeneratorType>() || IterableType.IsA<CMapType>();
            }
        }
        return false;
    }

   /*
    * The CExprIteration type encodes
    * 
    * for(generators, definitions, conditions) { expr }
    * 
    * Where the generators, definitions, and conditions can come in any order as long as the first is a generator.
    * 
    * This is transformed into
    *
    * do 
    * {
    *   ir_for(generator) 
    *   {
    *     definition
    *     if (condition) 
    *     {
    *       resultDestination.add(expr)
    *     }
    *   }
    * }
    * 
    *  Only one each of generator, definition, and condition is show, but they can be nested arbitrarly as long as
    *  the outermost is a generator.
    * 
    * ResultDestination is created by the code generator for now. It will be explicit in the IR in the future.
    */

    TSRef<CIrFor> GenIrFor(CExprDataDefinition& DataDefinition)
    {
        TSRef<CIrFor> For = NewIrNode<CIrFor>(DataDefinition._DataMember, GenNode(DataDefinition.Element()), GenNode(DataDefinition.ValueDomain()), GenNode(DataDefinition.Value()));
        return For;
    }
    
    TSRef<CIrFor> GenIrFor(CExprIterationPairDefinition& DataDefinition)
    {
        TSRef<CIrFor> For = NewIrNode<CIrFor>(DataDefinition._KeyDefinition, DataDefinition._ValueDefinition, GenNode(DataDefinition.Element()), GenNode(DataDefinition.ValueDomain()), GenNode(DataDefinition.Value()));
        return For;
    }


    TSRef<CExprCodeBlock> GenIteration(CExprIteration& AstNode)
    {
        TGuardValue<CScope*> ScopeGuard(_Scope, AstNode._AssociatedScope.Get());
        ULANG_ASSERT(_Scope);

        TSRef<CExprCodeBlock> IrNode = NewIrNode<CExprCodeBlock>(2);
        TSRef<CExprCodeBlock> CurrentBlock = IrNode;
        bool bOutermost = true;
        bool bGenerateResult = true;
        for (const TSRef<CExpressionBase>& Filter : AstNode._Filters) 
        {
            TGuardValue<const Vst::Node*> MappedVstNodeGuard(_MappedVstNode, Filter->GetMappedVstNode());

            if (IsGenerator(*Filter))
            { // Generate CIrFor
                TSRef<CIrFor> For = Filter->GetNodeType() == EAstNodeType::Definition_IterationPair
                    ? GenIrFor(static_cast<CExprIterationPairDefinition&>(*Filter))
                    : GenIrFor(static_cast<CExprDataDefinition&>(*Filter));
                For->_bOutermost = bOutermost;
                For->_bGenerateResult = bGenerateResult;
                if (bOutermost)
                {
                    For->_bCanFail = AstNode.CanFail();
                }
                bOutermost = false;
                bGenerateResult = false;
                TSRef<CExprCodeBlock> ForBody = NewIrNode<CExprCodeBlock>(1);
                For->SetBody(ForBody);
                For->IrSetResultType(AstNode.IrGetResultType());

                CurrentBlock->AppendSubExpr(Move(For));
                CurrentBlock = Move(ForBody);
            }
            else
            {
                CurrentBlock->AppendSubExpr(GenNode(*Filter));
            }
        }

        ULANG_ASSERTF(AstNode._Body.IsValid(), "Missing body in for");
        CurrentBlock->AppendSubExpr(NewIrNode<CIrForBody>(NewIrNode<CIrArrayAdd>(GenNode(*AstNode._Body))));

        return IrNode;
    }

    TSRef<CExprLoop> GenLoop(CExprLoop& AstNode)
    {
        TSRef<CExprLoop> IrNode = NewIrNode<CExprLoop>();
        IrNode->SetExpr(GenNode(AstNode.Expr()));
        return IrNode;
    }

    TSRef<CExprBreak> GenBreak(CExprBreak& AstNode)
    {
        TSRef<CExprBreak> IrNode = NewIrNode<CExprBreak>();
        return IrNode;
    }

    TSRef<CExprReturn> GenReturn(CExprReturn& AstNode)
    {
        return NewIrNode<CExprReturn>(
            GenNode(AstNode.Result()),
            AstNode.Function());
    }

    TSRef<CExprSync> GenSync(CExprSync& AstNode)
    {
        const TSPtrArray<CExpressionBase>& SubExprs = AstNode.GetSubExprs();
        TSRef<CExprSync> IrNode = NewIrNode<CExprSync>();
        for (const TSPtr<CExpressionBase>& SubExpr : SubExprs)
        {
            IrNode->AppendSubExpr(GenNode(*SubExpr));
        }
        return IrNode;
    }

    TSRef<CExprRush> GenRush(CExprRush& AstNode)
    {
        const CTypeBase* ResultType = AstNode.GetResultType(_Program);
        const TSPtrArray<CExpressionBase>& SubExprs = AstNode.GetSubExprs();
        TSRef<CExprRush> IrNode = NewIrNode<CExprRush>();
        for (const TSPtr<CExpressionBase>& SubExpr : SubExprs)
        {
            IrNode->AppendSubExpr(CoerceToType(GenNode(*SubExpr), ResultType));
        }
        return IrNode;
    }

    TSRef<CExprRace> GenRace(CExprRace& AstNode)
    {
        const CTypeBase* ResultType = AstNode.GetResultType(_Program);
        const TSPtrArray<CExpressionBase>& SubExprs = AstNode.GetSubExprs();
        TSRef<CExprRace> IrNode = NewIrNode<CExprRace>();
        for (const TSPtr<CExpressionBase>& SubExpr : SubExprs)
        {
            IrNode->AppendSubExpr(CoerceToType(GenNode(*SubExpr), ResultType));
        }
        return IrNode;
    }

    TSRef<CExprSyncIterated> GenSyncIterated(CExprSyncIterated& AstNode)
    {
        TSRef<CExprSyncIterated> IrNode = NewIrNode<CExprSyncIterated>();
        IrNode->SetBody(GenNode(AstNode._Body));
        for (const TSRef<CExpressionBase>& Filter : AstNode._Filters)
        {
            IrNode->AddFilter(GenNode(*Filter));
        }
        return IrNode;
    }

    TSRef<CExprRushIterated> GenRushIterated(CExprRushIterated& AstNode)
    {
        TSRef<CExprRushIterated> IrNode = NewIrNode<CExprRushIterated>();
        IrNode->SetBody(GenNode(AstNode._Body));
        for (const TSRef<CExpressionBase>& Filter : AstNode._Filters)
        {
            IrNode->AddFilter(GenNode(*Filter));
        }
        return IrNode;
    }

    TSRef<CExprRaceIterated> GenRaceIterated(CExprRaceIterated& AstNode)
    {
        TSRef<CExprRaceIterated> IrNode = NewIrNode<CExprRaceIterated>();
        IrNode->SetBody(GenNode(AstNode._Body));
        for (const TSRef<CExpressionBase>& Filter : AstNode._Filters)
        {
            IrNode->AddFilter(GenNode(*Filter));
        }
        return IrNode;
    }

    TSRef<CExprBranch> GenBranch(CExprBranch& AstNode)
    {
        TSRef<CExprBranch> IrNode = NewIrNode<CExprBranch>();
        IrNode->SetExpr(GenNode(AstNode.Expr()));
        return IrNode;
    }

    TSRef<CExprSpawn> GenSpawn(CExprSpawn& AstNode)
    {
        TSRef<CExprSpawn> IrNode = NewIrNode<CExprSpawn>();
        IrNode->SetExpr(GenNode(AstNode.Expr()));
        return IrNode;
    }

    TSRef<CExprModuleDefinition> GenModuleDefinition(CExprModuleDefinition& AstNode)
    {
        TGuardValue<CScope*> ScopeGuard(_Scope, AstNode._SemanticModule);
        ULANG_ASSERTF(AstNode._SemanticModule->GetAstNode() == &AstNode, "Not this node!");
        TSRef<CExprModuleDefinition> IrNode = NewIrNode<CExprModuleDefinition>(AstNode._Name, EVstMappingType::Ir);

        IrNode->_SemanticModule = AstNode._SemanticModule;
        IrNode->_SemanticModule->SetIrNode(IrNode.Get());
        InitIrMemberDefinitions(*IrNode, AstNode);

        IrNode->_SemanticModule->SetIrPackage(_PackageCache.Lookup(IrNode->_SemanticModule->GetAstPackage()).Get());
        return IrNode;
    }

    TSRef<CExprEnumDefinition> GenEnumDefinition(CExprEnumDefinition& AstNode)
    {
        ULANG_ASSERTF(AstNode._Enum.GetAstNode() == &AstNode, "Not this node!");

        // The BPVM codegen doesn't support enumerator values over byte-size, so flag that as an error 
        if (_TargetVM == SBuildParams::EWhichVM::BPVM)
        {
            for (TSRef<CExpressionBase> Member : AstNode._Members)
            {
                TSPtr<CExprEnumLiteral> EnumValue = Member.As<CExprEnumLiteral>();
                if (EnumValue->_Enumerator->_Value < std::numeric_limits<uint8_t>::min() 
                    || EnumValue->_Enumerator->_Value > std::numeric_limits<uint8_t>::max())
                {
                    AppendGlitch(*EnumValue,
                        EDiagnostic::ErrSemantic_Unsupported,
                        uLang::CUTF8String("Enumerator value `%s` is out of byte-range which is not yet supported", EnumValue->_Enumerator->AsCode().AsCString()));

                    // Avoid spam
                    break;
                }
            }
        }

        TSRef<CExprEnumDefinition> IrNode = NewIrNode<CExprEnumDefinition>(
            AstNode._Enum,
            GenNodes(AstNode._Members),
            EVstMappingType::Ir);
        AstNode._Enum.SetIrNode(IrNode);
        return IrNode;
    }

    TSRef<CExprInterfaceDefinition> GenInterfaceDefinition(CExprInterfaceDefinition& AstNode)
    {
        ULANG_ASSERTF(AstNode._Interface.GetAstNode() == &AstNode, "Not this node!");
        TSRef<CExprInterfaceDefinition> IrNode = NewIrNode<CExprInterfaceDefinition>(
            AstNode._Interface,
            EVstMappingType::Ir);
        TArray<TSRef<CExpressionBase>> SuperInterfaces = GenNodes(AstNode.SuperInterfaces());
        IrNode->SetSuperInterfaces(Move(SuperInterfaces));
        TGuardValue<CScope*> ScopeGuard(_Scope, &AstNode._Interface);
        TArray<TSRef<CExpressionBase>> Members = GenNodes(AstNode.Members());
        IrNode->SetMembers(Move(Members));
        AstNode._Interface.SetIrNode(IrNode);
        return IrNode;
    }

    TSRef<CExprClassDefinition> GenClassDefinition(CExprClassDefinition& AstNode)
    {
        ULANG_ASSERTF(AstNode._Class._Definition->GetAstNode() == &AstNode, "Not this node!");
        TSRef<CExprClassDefinition> IrNode = NewIrNode<CExprClassDefinition>(
            AstNode._Class,
            EVstMappingType::Ir);

        TArray<TSRef<CExpressionBase>> SuperTypes = GenNodes(AstNode.SuperTypes());
        IrNode->SetSuperTypes(Move(SuperTypes));

        TGuardValue<CScope*> ScopeGuard(_Scope, &AstNode._Class);

        TArray<TSRef<CExpressionBase>> IrMembers = GenNodes(AstNode.Members());
        for (const TSRef<CExpressionBase>& Member : IrMembers)
        {
            if (Member->GetNodeType() == EAstNodeType::Flow_CodeBlock)
            {
                IrNode->_Class._IrBlockClauses.Add(Member.As<CExprCodeBlock>().Get());
            }
        }

        const TArray<TSRef<CDefinition>>& Definitions = AstNode._Class.GetDefinitions();
        // Iterate `Definitions` using explicit indices.
        // `CreateCoercedOverridingFunctionDefinition` may add to `Definitions`,
        // possibly invalidating iterators.  Furthermore, such added functions
        // do not need to be visited.  Computing `NumFunctions` before iterating
        // ensures this.
        for (int32_t I = 0, NumDefinitions = Definitions.Num(); I != NumDefinitions; ++I)
        {
            if (CDataDefinition* DataDefinition = Definitions[I]->AsNullable<CDataDefinition>())
            {
                ULANG_ASSERT(DataDefinition->GetIrNode()->GetNodeType() == EAstNodeType::Definition_Data);
                CExprDataDefinition& DefinitionIr = *static_cast<CExprDataDefinition*>(DataDefinition->GetIrNode());

                // Data definitions that override an inherited field must coerce the overridden default value to
                // the overridden field type.
                if (DefinitionIr.Value().IsValid())
                {
                    const CDataDefinition& OverriddenPrototypeDefinition = *DataDefinition->GetBaseOverriddenDefinition().GetPrototypeDefinition();

                    const CTypeBase* PrototypeInitializerType = OverriddenPrototypeDefinition.IsVar()
                        ? OverriddenPrototypeDefinition.GetType()->GetNormalType().AsChecked<CPointerType>().PositiveValueType()
                        : OverriddenPrototypeDefinition.GetType();

                    DefinitionIr.SetValue(CoerceToType(DefinitionIr.TakeValue().AsRef(), PrototypeInitializerType));
                }
            }
            else if (CFunction* Function = Definitions[I]->AsNullable<CFunction>())
            {
                if (Function->HasAttributeClass(_Program._nativeClass, _Program))
                {
                    // Native methods are required to implement the prototype of
                    // the base overridden definition - i.e., the type-erased
                    // form of the root-most method signature.  No coercion
                    // should be generated.
                    continue;
                }
                const CFunctionType* Type = Function->_Signature.GetFunctionType();
                const CFunctionType& CanonicalType = SemanticTypeUtils::Canonicalize(*Type);
                TArray<const CFunctionType*> CanonicalBaseOverriddenTypes;
                const CFunction* Next = Function;
                for (;;)
                {
                    const CFunction* OverriddenFunction = Next->GetOverriddenDefinition();
                    // If there is no overridden function, no coercion is
                    // needed.
                    if (!OverriddenFunction)
                    {
                        break;
                    }

                    const CFunction& BaseOverriddenFunction = OverriddenFunction->GetBaseCoercedOverriddenFunction();
                    const CFunctionType* BaseOverriddenType = BaseOverriddenFunction._Signature.GetFunctionType();
                    const CFunctionType& CanonicalBaseOverridenType = SemanticTypeUtils::Canonicalize(*BaseOverriddenType);

                    const CFunction* PrototypeBaseOverriddenFunction = BaseOverriddenFunction.GetPrototypeDefinition();
                    const CFunctionType* PrototypeBaseOverriddenFunctionType = PrototypeBaseOverriddenFunction->_Signature.GetFunctionType();
                    const CFunctionType& CanonicalPrototypeBaseOverriddenType = SemanticTypeUtils::Canonicalize(*PrototypeBaseOverriddenFunctionType);

                    // If the original function matches the overridden function
                    // for which code will be generated (i.e., the prototype
                    // function), no coercion is needed.
                    if (!NeedsCoercion(CanonicalPrototypeBaseOverriddenType, CanonicalType))
                    {
                        break;
                    }
                    // Add `CanonicalBaseOverridenType` to
                    // `CanonicalBaseOverriddenTypes` before coercing to each of
                    // `CanonicalBaseOverriddenTypes` (and before coercing to
                    // `CanonicalPrototypeBaseOverriddenType`).  An override may
                    // both not match the instantiated base type nor the
                    // prototype base type, and require coercion first to the
                    // instantiated base type, then the prototype base type.
                    CanonicalBaseOverriddenTypes.Add(&CanonicalBaseOverridenType);

                    TSPtr<CExprFunctionDefinition> OverridingFunctionDefinition = CreateCoercedOverridingFunctionDefinition(
                        *Function,
                        CanonicalBaseOverriddenTypes,
                        CanonicalPrototypeBaseOverriddenType);
                    // If a coercion is needed, but cannot be created,
                    // `CreateCoercedOverridingFunctionDefinition` produces a
                    // glitch.
                    if (!OverridingFunctionDefinition)
                    {
                        break;
                    }
                    // Mark the coercion as overriding the function for which
                    // its type matches (i.e., the prototype function).
                    OverridingFunctionDefinition->_Function->SetOverriddenDefinition(PrototypeBaseOverriddenFunction);
                    IrMembers.Add(Move(OverridingFunctionDefinition.AsRef()));
                    // If the overridden function matches the prototype
                    // function, all further needed coercions from ancestors
                    // classes are handled when generating coercions for the
                    // prototype function.
                    if (&CanonicalBaseOverridenType == &CanonicalPrototypeBaseOverriddenType)
                    {
                        break;
                    }
                    Next = &BaseOverriddenFunction;
                }
            }
        }

        IrNode->SetMembers(Move(IrMembers));

        AstNode._Class._Definition->SetIrNode(IrNode);

        return IrNode;
    }

    TSRef<CExprDataDefinition> GenDataDefinition(CExprDataDefinition& AstNode)
    {
        TSRef<CExprDataDefinition> IrNode = NewIrNode<CExprDataDefinition>(
            AstNode._DataMember,
            GenNode(AstNode.Element()),
            GenNode(AstNode.ValueDomain()),
            GenNode(AstNode.Value()),
            EVstMappingType::Ir);
        AstNode._DataMember->SetIrNode(IrNode);
        return IrNode;
    }

    TSRef<CExprIterationPairDefinition> GenIterationPairDefinition(CExprIterationPairDefinition& AstNode)
    {
        TSRef<CExprIterationPairDefinition> IrNode = NewIrNode<CExprIterationPairDefinition>(
            TSRef<CDataDefinition>(AstNode._KeyDefinition),
            TSRef<CDataDefinition>(AstNode._ValueDefinition),
            GenNode(AstNode.Element().Get()),
            GenNode(AstNode.ValueDomain().Get()),
            GenNode(AstNode.Value().Get()),
            EVstMappingType::Ir);
        return IrNode;
    }

    TSRef<CExprFunctionDefinition> GenFunctionDefinition(CExprFunctionDefinition& AstNode)
    {
        CFunction& Function = *AstNode._Function;
        TGuardValue<CScope*> ScopeGuard(_Scope, &Function);
        TGuard FunctionParamDefinitionGuard([this, NumParamDefinitions = _FunctionParamDefinitions.Num()]
        {
            _FunctionParamDefinitions.SetNum(NumParamDefinitions);
        });
        for (CDataDefinition* Param : Function._Signature.GetParams())
        {
            _FunctionParamDefinitions.Add(Param);
        }
        TSRef<CExprFunctionDefinition> IrNode = NewIrNode<CExprFunctionDefinition>(
            AstNode._Function,
            GenNode(AstNode.Element()),
            GenNode(AstNode.ValueDomain()),
            GenNode(AstNode.Value()),
            EVstMappingType::Ir);
        AstNode._Function->SetIrNode(IrNode);
        return IrNode;
    }

    TSRef<CExprTypeAliasDefinition> GenTypeAliasDefinition(CExprTypeAliasDefinition& AstNode)
    {
        TSRef<CExprTypeAliasDefinition> IrNode = NewIrNode<CExprTypeAliasDefinition>(
            AstNode._TypeAlias,
            GenNode(AstNode.Element()),
            GenNode(AstNode.ValueDomain()),
            GenNode(AstNode.Value()),
            EVstMappingType::Ir);
        return IrNode;
    }

    TSRef<CExprScopedAccessLevelDefinition> GenAccessLevelDefinition(CExprScopedAccessLevelDefinition& AstNode)
    {
        ULANG_ASSERTF(AstNode._AccessLevelDefinition->GetAstNode() == &AstNode, "Not this node!");
        TSRef<CExprScopedAccessLevelDefinition> IrNode = NewIrNode<CExprScopedAccessLevelDefinition>(
            AstNode._AccessLevelDefinition,
            EVstMappingType::Ir);
        IrNode->_ScopeReferenceExprs = GenNodes(AstNode._ScopeReferenceExprs);
        AstNode._AccessLevelDefinition->SetIrNode(IrNode);
        return IrNode;
    }

    TSRef<CExprProfileBlock> GenProfileBlock(CExprProfileBlock& AstNode)
    {
        TSRef<CExprProfileBlock> IrNode = NewIrNode<CExprProfileBlock>();
        IrNode->SetExpr(GenNode(AstNode.Expr()));
        IrNode->_UserTag = GenNode(AstNode._UserTag.Get());

        return IrNode;
    }

    TSRef<CExprUsing> GenExprUsing(CExprUsing& AstNode)
    {
        TSRef<CExprUsing> IrNode = NewIrNode<CExprUsing>(GenNode(*AstNode._Context));
        IrNode->_Module = AstNode._Module;
        return IrNode;
    }

    TSRef<CExprImport> GenExprImport(CExprImport& AstNode)
    {
        TSRef<CExprImport> IrNode = NewIrNode<CExprImport>(AstNode._ModuleAlias, GenNode(*AstNode._Path), EVstMappingType::Ir);
        return IrNode;
    }

    TSRef<CExprWhere> GenExprWhere(CExprWhere& AstNode)
    {
        TSRef<CExpressionBase> IrLhs = GenNode(*AstNode.Lhs());
        const TSPtrArray<CExpressionBase>& RhsArray = AstNode.Rhs();
        TSPtrArray<CExpressionBase> IrRhs;
        IrRhs.Reserve(RhsArray.Num());
        for (const TSPtr<CExpressionBase>& Rhs : RhsArray)
        {
            IrRhs.Add(GenNode(*Rhs));
        }
        return NewIrNode<CExprWhere>(
            Move(IrLhs),
            Move(IrRhs));
    }

    TSRef<CExprVar> GenVar(CExprVar& AstNode)
    {
        return NewIrNode<CExprVar>(GenNode(*AstNode.Operand()));
    }

    TSRef<CExpressionBase> GenMakeNamed(CExprMakeNamed& AstNode)
    {
        return NewIrNode<CExprMakeNamed>(
            AstNode.GetName(),
            GenNode(*AstNode.GetValue()));
    }

    TSRef<CExprSnippet> GenExprSnippet(CExprSnippet& AstNode)
    {
        TSRef<CExprSnippet> IrNode = NewIrNode<CExprSnippet>(AstNode._Path);
        IrNode->_SemanticSnippet = AstNode._SemanticSnippet;
        InitIrMemberDefinitions(*IrNode, AstNode);
        return IrNode;
    }

    template<typename... ResultArgsType>
    void AppendGlitch(const CAstNode& AstNode, ResultArgsType&&... ResultArgs)
    {
        SGlitchResult Glitch(uLang::ForwardArg<ResultArgsType>(ResultArgs)...);
        ULANG_ASSERTF(
            AstNode.GetMappedVstNode() && AstNode.GetMappedVstNode()->Whence().IsValid(),
            "Expected valid whence for node used as glitch locus on %s id:%i - %s",
            AstNode.GetErrorDesc().AsCString(),
            GetDiagnosticInfo(Glitch._Id).ReferenceCode,
            Glitch._Message.AsCString());
        _Diagnostics.AppendGlitch(Move(Glitch), SGlitchLocus(&AstNode));
    }

    template<typename NodeType, typename... Parameters>
    TSRef<NodeType> NewIrNode(Parameters&&... Args)
    {
        TSRef<NodeType> IrNode = TSRef<NodeType>::New(uLang::ForwardArg<Parameters>(Args)...);
        IrNode->SetIrMappedVstNode(_MappedVstNode);
        return Move(IrNode);
    }

    // When no matching coerced function is found, write the number of existing coerced versions of `Function` to `OutNumCoerced`.
    const CFunction* FindCoercedFunction(const CFunction& Function, const CFunctionType& CoercedType, std::size_t& OutNumCoerced)
    {
        std::size_t NumCoerced = 0;
        for (auto&& [FunctionPtr, CoercedTypePtr, CoercedFunctionPtr] : _CoercedFunctions)
        {
            if (FunctionPtr == &Function)
            {
                NumCoerced++;
                if (CoercedTypePtr == &CoercedType)
                {
                    return CoercedFunctionPtr;
                }
            }
        }
        OutNumCoerced = NumCoerced;
        return nullptr;
    }

    CDataDefinition* FindFunctionParamDefinition(const CExprDefinition& AstNode)
    {
        auto Last = _FunctionParamDefinitions.end();
        auto I = uLang::FindIf(_FunctionParamDefinitions.begin(), Last, [&](const CDataDefinition* Arg)
        {
            return Arg->GetAstNode() == &AstNode;
        });
        return I == Last? nullptr : *I;
    }

    const TSRef<CSemanticProgram> _SemanticProgram;
    CSemanticProgram& _Program;
    CDiagnostics& _Diagnostics;

    struct SCoercedFunctionDefinition
    {
        const CFunction* Function;
        const CFunctionType* CoercedType;
        const CFunction* CoercedFunction;
    };

    TArray<SCoercedFunctionDefinition> _CoercedFunctions;

    TArray<CDataDefinition*> _FunctionParamDefinitions;

    CScope* _Scope{nullptr};
    SBuildParams::EWhichVM _TargetVM;
    const Vst::Node* _MappedVstNode{nullptr};
};

//====================================================================================
// CIrGenerate Implementation
//====================================================================================

//-------------------------------------------------------------------------------------------------
bool GenerateIr(const TSRef<CSemanticProgram>& Program, const TSRef<CDiagnostics>& Diagnostics, SBuildParams::EWhichVM TargetVM)
{
    TURef<CIrGeneratorImpl> IrGenerator = TURef<CIrGeneratorImpl>::New(Program, Diagnostics, TargetVM);
    return IrGenerator->ProcessAst();
}
}
