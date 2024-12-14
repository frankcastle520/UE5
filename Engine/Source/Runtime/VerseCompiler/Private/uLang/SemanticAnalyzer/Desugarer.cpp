// Copyright Epic Games, Inc. All Rights Reserved.

#include "Desugarer.h"

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/UniquePointer.h"
#include "uLang/Diagnostics/Glitch.h"
#include "uLang/Parser/ParserPass.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/SemanticClass.h"
#include "uLang/Semantics/UnknownType.h"
#include "uLang/SourceProject/UploadedAtFNVersion.h"
#include "uLang/SourceProject/VerseVersion.h"
#include "uLang/Syntax/VstNode.h"
#include "uLang/Syntax/vsyntax_types.h"

namespace
{
namespace Vst = Verse::Vst;
using namespace uLang;

class CDesugarerImpl
{
public:

    CDesugarerImpl(CSymbolTable& Symbols, CDiagnostics& Diagnostics): _Symbols(Symbols), _Diagnostics(Diagnostics) {}

    TSRef<CAstProject> DesugarProject(const Vst::Project& VstProject)
    {
        // Desugar all the project's packages (and build vertex array for Tarjan's algorithm).
        struct SPackageVertex
        {
            TSRef<CAstPackage> Package;
            TArray<int32_t> Dependencies; // Indices of vertices this package depends on
            int32_t DepthIndex = IndexNone;
            int32_t LowLink = IndexNone;
            bool bOnStack = false;            
        };
        TArray<SPackageVertex> Vertices;
        Vertices.Reserve(VstProject.GetChildCount());
        for (const TSRef<Vst::Node>& VstPackage : VstProject.GetChildren())
        {
            ULANG_ASSERTF(VstPackage->GetElementType() == Vst::NodeType::Package, "Toolchain must ensure that a project only ever contains packages.");
            Vertices.Add({ DesugarPackage(static_cast<const Vst::Package&>(*VstPackage)), {} });
        }

        // Populate the dependencies for both the Tarjan vertices and the AST packages
        for (SPackageVertex& Vertex : Vertices)
        {
            const Vst::Package* VstPackage = static_cast<const Vst::Package*>(Vertex.Package->GetMappedVstNode());
            if (ULANG_ENSUREF(VstPackage && VstPackage->GetElementType() == Vst::NodeType::Package, "Node should have been properly mapped by DesugarPackage."))
            {
                Vertex.Dependencies.Reserve(VstPackage->_DependencyPackages.Num());
                Vertex.Package->_Dependencies.Reserve(VstPackage->_DependencyPackages.Num());

                for (const CUTF8String& DependencyName : VstPackage->_DependencyPackages)
                {
                    int32_t DependencyIndex = Vertices.IndexOfByPredicate([&DependencyName](const SPackageVertex& DependencyVertex) { return DependencyVertex.Package->_Name == DependencyName; });
                    if (DependencyIndex != IndexNone)
                    {
                        Vertex.Dependencies.Add(DependencyIndex);
                        Vertex.Package->_Dependencies.Add(Vertices[DependencyIndex].Package);
                    }
                    else if (Vertex.Package->_Role != ConstraintPackageRole)
                    {
                        AppendGlitch(VstPackage, EDiagnostic::ErrSemantic_UnknownPackageDependency,
                            CUTF8String("Package `%s` specifies dependency `%s` which does not exist.", *Vertex.Package->_Name, *DependencyName));
                    }
                }
            }
        }

        // Prepare new AST project
        TSRef<CAstProject> AstProject = TSRef<CAstProject>::New(VstProject._Name);
        AstProject->ReserveCompilationUnits(VstProject.GetChildCount());

        // Run Tarjan's algorithm to generate the compilation units (SCCs)
        TArray<int32_t> Stack;
        Stack.Reserve(Vertices.Num());
        int32_t CurrentDepthIndex = 0;
        auto StrongConnect = [&AstProject, &Vertices, &Stack, &CurrentDepthIndex](int32_t V, const auto& StrongConnectRef) -> void
            {
                // Set the depth index for V to the smallest unused index
                SPackageVertex& Vertex = Vertices[V];
                Vertex.DepthIndex = Vertex.LowLink = CurrentDepthIndex++;
                Stack.Push(V);
                Vertex.bOnStack = true;

                // Consider dependencies of V
                for (int32_t W : Vertex.Dependencies)
                {
                    SPackageVertex& DependencyVertex = Vertices[W];
                    if (DependencyVertex.DepthIndex == IndexNone)
                    {
                        // Dependency W has not yet been visited - recurse on it
                        StrongConnectRef(W, StrongConnectRef);
                        Vertex.LowLink = CMath::Min(Vertex.LowLink, DependencyVertex.LowLink);
                    }
                    else if (DependencyVertex.bOnStack)
                    {
                        // Dependency W is in stack and hence in the current SCC
                        // If W is not on stack, then (V, W) is an edge pointing to an SCC already found and must be ignored
                        // The next line may look odd - but is correct.
                        // It says DependencyVertex.DepthIndex not DependencyVertex.LowLink - that is deliberate and from the original paper
                        Vertex.LowLink = CMath::Min(Vertex.LowLink, DependencyVertex.DepthIndex);
                    }
                }

                // If V is a root node, pop the stack and generate an SCC
                if (Vertex.LowLink == Vertex.DepthIndex)
                {
                    // Since Tarjan's algorithm does a depth-first search, compilation units
                    // will be generated in depth-first order which is the order we desire
                    // therefore no explicit sorting of compilation units is required after this algorithm is done
                    TSRef<CAstCompilationUnit> CompilationUnit = TSRef<CAstCompilationUnit>::New();
                    int32_t W;
                    do
                    {
                        W = Stack.Pop();
                        SPackageVertex& SCCVertex = Vertices[W];
                        SCCVertex.bOnStack = false;
                        SCCVertex.Package->_CompilationUnit = CompilationUnit;
                        CompilationUnit->AppendPackage(Move(SCCVertex.Package));
                    } while (W != V);
                    AstProject->AppendCompilationUnit(Move(CompilationUnit));
                }
            };
        for (int32_t Index = 0; Index < Vertices.Num(); ++Index)
        {
            if (Vertices[Index].DepthIndex == IndexNone)
            {
                StrongConnect(Index, StrongConnect);
            }
        }

        VstProject.AddMapping(&*AstProject);
        return Move(AstProject);
    }

private:

    TSRef<CAstPackage> DesugarPackage(const Vst::Package& VstPackage)
    {
        // Turn the language version override into an effective version.
        uint32_t EffectiveVerseVersion = VstPackage._VerseVersion.Get(Verse::Version::Default);
        if (EffectiveVerseVersion < Verse::Version::Minimum
            || EffectiveVerseVersion > Verse::Version::Maximum)
        {
            AppendGlitch(
                &VstPackage,
                EDiagnostic::ErrSystem_InvalidVerseVersion,
                CUTF8String("Invalid Verse version for package %s: %u", VstPackage._Name.AsCString(), EffectiveVerseVersion));
        }

        TSRef<CAstPackage> AstPackage = TSRef<CAstPackage>::New(
            VstPackage._Name,
            VstPackage._VersePath,
            VstPackage._VerseScope,
            VstPackage._Role,
            EffectiveVerseVersion,
            VstPackage._UploadedAtFNVersion,
            VstPackage._VniDestDir.IsSet(),
            VstPackage._bTreatModulesAsImplicit,
            VstPackage._bAllowExperimental);

        TGuardValue<CAstPackage*> CurrentPackageGuard(_Package, AstPackage.Get());

        // Desugar all the package's modules or snippets.
        for (const TSRef<Vst::Node>& VstNode : VstPackage.GetChildren())
        {
            if (VstNode->GetElementType() == Vst::NodeType::Module)
            {
                AstPackage->AppendMember(DesugarModule(static_cast<const Vst::Module&>(*VstNode)));
            }
            else if (VstNode->GetElementType() == Vst::NodeType::Snippet)
            {
                AstPackage->AppendMember(DesugarSnippet(static_cast<const Vst::Snippet&>(*VstNode)));
            }
            else
            {
                ULANG_ERRORF("Toolchain must ensure that a package only ever contains modules or snippets.");
            }
        }
        
        VstPackage.AddMapping(&*AstPackage);
        return Move(AstPackage);
    }
    
    TSRef<CExprModuleDefinition> DesugarModule(const Vst::Module& VstModule)
    {
        TSRef<CExprModuleDefinition> AstModule = TSRef<CExprModuleDefinition>::New(VstModule._Name);

        // Is a vmodule file present?
        if (VstModule._FilePath.ToStringView().EndsWith(".vmodule"))
        {
            // Yes - mark public to mimic legacy behavior of vmodule files
            AstModule->_bLegacyPublic = true;
        }

        // Desugar the module's children, which may be either submodules or snippets.
        for (const TSRef<Vst::Node>& VstNode : VstModule.GetChildren())
        {
            if (VstNode->GetElementType() == Vst::NodeType::Module)
            {
                AstModule->AppendMember(DesugarModule(static_cast<const Vst::Module&>(*VstNode)));
            }
            else if (VstNode->GetElementType() == Vst::NodeType::Snippet)
            {
                AstModule->AppendMember(DesugarSnippet(static_cast<const Vst::Snippet&>(*VstNode)));
            }
            else
            {
                ULANG_ENSUREF(false, "Toolchain must ensure that a module only ever contains modules or snippets.");
            }
        }
        
        VstModule.AddMapping(&*AstModule);
        return Move(AstModule);
    }
    
    TSRef<CExprSnippet> DesugarSnippet(const Vst::Snippet& VstSnippet)
    {
        TSRef<CExprSnippet> AstSnippet = TSRef<CExprSnippet>::New(VstSnippet._Path);

        // Desugar all the snippet's top-level expressions.
        for (const TSRef<Vst::Node>& VstNode : VstSnippet.GetChildren())
        {
            if (!VstNode->IsA<Vst::Comment>())
            {
                AstSnippet->AppendMember(DesugarExpressionVst(*VstNode));
            }
        }
        
        VstSnippet.AddMapping(&*AstSnippet);
        return Move(AstSnippet);
    }
    
    TSRef<CExpressionBase> DesugarClauseAsExpression(const Vst::Node& MaybeClauseVst)
    {
        if (MaybeClauseVst.GetElementType() != Verse::Vst::NodeType::Clause)
        {
            // If the expression isn't a clause, just desugar it directly.
            return DesugarExpressionVst(MaybeClauseVst);
        }
        else
        {
            const Verse::Vst::Clause& ClauseVst = MaybeClauseVst.As<Verse::Vst::Clause>();

            // Determine if the clause has a single non-comment child expression.
            const Vst::Node* NonCommentChild = nullptr;
            for (const Vst::Node* ChildVst : ClauseVst.GetChildren())
            {
                if (!ChildVst->IsA<Vst::Comment>())
                {
                    if (NonCommentChild == nullptr)
                    {
                        NonCommentChild = ChildVst;
                    }
                    else
                    {
                        NonCommentChild = nullptr;
                        break;
                    }
                }
            }

            if (NonCommentChild)
            {
                // If so, desugar that expression as though it occurred on its own.
                return DesugarExpressionVst(*NonCommentChild);
            }
            else
            {
                // Otherwise, desugar the clause as a code block.
                return DesugarClauseAsCodeBlock(ClauseVst);
            }
        }
    }

    TSRef<CExpressionBase> DesugarWhere(const Vst::Where& WhereVst)
    {
        TSRef<CExpressionBase> LhsAst = DesugarExpressionVst(*WhereVst.GetLhs());
        Vst::Where::RhsView RhsVstView = WhereVst.GetRhs();
        TSPtrArray<CExpressionBase> RhsAst;
        RhsAst.Reserve(RhsVstView.Num());
        for (const TSRef<Vst::Node>& RhsVst : RhsVstView)
        {
            RhsAst.Add(DesugarExpressionVst(*RhsVst));
        }
        return AddMapping(WhereVst, TSRef<CExprWhere>::New(Move(LhsAst), Move(RhsAst)));
    }

    TSRef<CExpressionBase> DesugarMutation(const Vst::Mutation& MutationVst)
    {
        switch (MutationVst._Keyword)
        {
        case Vst::Mutation::EKeyword::Var:
            return AddMapping(MutationVst, TSRef<CExprVar>::New(DesugarExpressionVst(*MutationVst.Child())));
        case Vst::Mutation::EKeyword::Set:
            return AddMapping(MutationVst, TSRef<CExprSet>::New(DesugarExpressionVst(*MutationVst.Child())));
        default:
            ULANG_UNREACHABLE();
        }
    }

    // This is old code that can't handle named parameters.
    // Only kept to compile code published before 20.30, since this code ignores some errors that the new code complains about.

    struct SNameTypeIdentifierPair
    {
        const Vst::Identifier* Name;
        const Vst::Identifier* Type;
    };

    TSRef<CExpressionBase> DesugarLocalizableOld(
        const Vst::Definition& DefinitionVst,
        const Vst::Identifier& MessageKeyVst,
        const CUTF8String& MessageDefaultText,
        const TSRef<Vst::Node>& MessageTypeVst,
        const TArray<SNameTypeIdentifierPair>& NameTypePairs,
        bool bIsFunction)
    {
        const CSymbol MessageKeySymbol = VerifyAddSymbol(&MessageKeyVst, MessageKeyVst._OriginalCode);

        TArray<TSRef<CExpressionBase>> MapClauseExprs;

        const CSymbol MakeLocalizableSymbol = _Symbols.AddChecked("MakeLocalizableValue");

        TSRef<CExprIdentifierUnresolved> MakeLocalizableIdentifier = TSRef<CExprIdentifierUnresolved>::New(MakeLocalizableSymbol);
        MakeLocalizableIdentifier->SetNonReciprocalMappedVstNode(&DefinitionVst);

        // ** special exception here to allow looking up this identifier which is <epic_internal> to 
        // another module, we're doing this as a short term protection against users writing 
        // code that depends on internal details of the message type
        MakeLocalizableIdentifier->GrantUnrestrictedAccess();

        auto CreateArgDefinition = [this, &MapClauseExprs, &DefinitionVst, &MakeLocalizableIdentifier](const SNameTypeIdentifierPair& NameTypePair) -> TSRef<CExprDefinition>
        {
            const CSymbol NameSymbol = VerifyAddSymbol(NameTypePair.Name, NameTypePair.Name->_OriginalCode);
            const CSymbol TypeSymbol = VerifyAddSymbol(NameTypePair.Type, NameTypePair.Type->_OriginalCode);

            TSRef<CExpressionBase> NameIdentifier = AddMapping(*NameTypePair.Name, TSRef<CExprIdentifierUnresolved>::New(NameSymbol));
            TSRef<CExpressionBase> TypeIdentifier = AddMapping(*NameTypePair.Type, TSRef<CExprIdentifierUnresolved>::New(TypeSymbol));

            // create the function parameter definition
            TSRef<CExprDefinition> ArgDefinition =
                TSRef<CExprDefinition>::New(
                    NameIdentifier,
                    TypeIdentifier,
                    nullptr);
            ArgDefinition->SetNonReciprocalMappedVstNode(&DefinitionVst);

            // create an invocation passing the argument to MakeLocalizableValue
            // so it can be added to the Substitutions map
            TSRef<CExprInvocation> MakeLocalizableValueInvocation =
                TSRef<CExprInvocation>::New(
                    CExprInvocation::EBracketingStyle::Parentheses,
                    MakeLocalizableIdentifier,
                    NameIdentifier);
            MakeLocalizableValueInvocation->SetNonReciprocalMappedVstNode(&DefinitionVst);

            TSRef<CExprString> ArgNameString = TSRef<CExprString>::New(NameTypePair.Name->_OriginalCode);
            ArgNameString->SetNonReciprocalMappedVstNode(NameTypePair.Name);

            TSRef<CExprFunctionLiteral> MapClauseExpr =
                TSRef<CExprFunctionLiteral>::New(
                    Move(ArgNameString),
                    MakeLocalizableValueInvocation);
            MapClauseExpr->SetNonReciprocalMappedVstNode(&DefinitionVst);
            MapClauseExprs.Add(Move(MapClauseExpr));

            return ArgDefinition;
        };

        // in the function case, we have to differentiate between a single
        // parameter function and a multi-parameter function where for
        // multi-parameter functions the AST is expected to have the
        // list of definitions wrapped by a CExprMakeTuple node, but
        // single parameter functions should not be wrapped
        TSPtr<CExpressionBase> ElementArguments;

        if (NameTypePairs.Num() == 1)
        {
            ElementArguments = CreateArgDefinition(NameTypePairs[0]);
        }
        else
        {
            // multi-parameter functions need to wrap the 
            // definitions in a CExprMakeTuple node
            TSRef<CExprMakeTuple> ElementArgumentsTuple = TSRef<CExprMakeTuple>::New();
            ElementArgumentsTuple->SetNonReciprocalMappedVstNode(&DefinitionVst);

            for (const SNameTypeIdentifierPair& NameTypePair : NameTypePairs)
            {
                ElementArgumentsTuple->AppendSubExpr(CreateArgDefinition(NameTypePair));
            }

            ElementArguments = Move(ElementArgumentsTuple);
        }

        TSRefArray<CExpressionBase> ArgumentExprs;

        {
            // Key argument

            // for the function case, the current scope will include the function name, so we pass the null symbol here
            TSRef<CExprPathPlusSymbol> KeyPath = TSRef<CExprPathPlusSymbol>::New(bIsFunction ? CSymbol() : MessageKeySymbol);
            KeyPath->SetNonReciprocalMappedVstNode(&MessageKeyVst);

            ArgumentExprs.Add(Move(KeyPath));
        }

        {
            // DefaultText argument
            TSRef<CExpressionBase> DefaultTextString = TSRef<CExprString>::New(MessageDefaultText);

            ArgumentExprs.Add(Move(DefaultTextString));
        }

        {
            // Substitutions argument
            const CSymbol MapSymbol = _Symbols.AddChecked("map");
            TSRef<CExprIdentifierUnresolved> MapIdentifier = TSRef<CExprIdentifierUnresolved>::New(MapSymbol);
            MapIdentifier->SetNonReciprocalMappedVstNode(&DefinitionVst);
            TSRef<CExprMacroCall> MapMacroExpr = TSRef<CExprMacroCall>::New(MapIdentifier);
            MapMacroExpr->SetNonReciprocalMappedVstNode(&DefinitionVst);
            MapMacroExpr->AppendClause(CExprMacroCall::CClause(EMacroClauseTag::None, Verse::Vst::Clause::EForm::Synthetic, Move(MapClauseExprs)));

            ArgumentExprs.Add(Move(MapMacroExpr));
        }

        TSRef<CExprMakeTuple> ArgTuple = WrapExpressionListInTuple(Move(ArgumentExprs), DefinitionVst, false);

        const CSymbol MakeMessageSymbol = _Symbols.AddChecked("MakeMessageInternal");
        TSRef<CExprIdentifierUnresolved> MakeMessageIdentifier = TSRef<CExprIdentifierUnresolved>::New(MakeMessageSymbol);

        // ** special exception here to allow looking up this identifier which is <epic_internal> to 
        // another module, we're doing this as a short term protection against users writing 
        // code that depends on internal details of the message type
        MakeMessageIdentifier->GrantUnrestrictedAccess();

        MakeMessageIdentifier->SetNonReciprocalMappedVstNode(&DefinitionVst);

        TSRef<CExprInvocation> MakeMessageInvocation =
            TSRef<CExprInvocation>::New(
                CExprInvocation::EBracketingStyle::Parentheses,
                Move(MakeMessageIdentifier),
                Move(ArgTuple));
        MakeMessageInvocation->SetNonReciprocalMappedVstNode(&DefinitionVst);

        TSRef<CExpressionBase> MessageKeyIdentifier = AddMapping(MessageKeyVst, TSRef<CExprIdentifierUnresolved>::New(MessageKeySymbol));

        if (MessageKeyVst.HasAttributes())
        {
            MessageKeyIdentifier->_Attributes = DesugarAttributes(MessageKeyVst.GetAux()->GetChildren());
        }

        TSPtr<CExpressionBase> DefinitionElement;

        if (bIsFunction)
        {
            TSRef<CExprInvocation> ElementInvocation =
                TSRef<CExprInvocation>::New(
                    CExprInvocation::EBracketingStyle::Parentheses,
                    Move(MessageKeyIdentifier),
                    Move(ElementArguments.AsRef()));
            ElementInvocation->SetNonReciprocalMappedVstNode(&DefinitionVst);
            DefinitionElement = ElementInvocation;
        }
        else
        {
            DefinitionElement = MessageKeyIdentifier;
        }

        TSRef<CExprDefinition> Definition = TSRef<CExprDefinition>::New(DefinitionElement.AsRef(), DesugarExpressionVst(*MessageTypeVst), Move(MakeMessageInvocation));
        return Definition;
    }

    // This is the new code that improves localization, see SOL-6057

    void FillinClauseExprs(
        const Vst::Definition& DefinitionVst,
        TArray<TSRef<CExpressionBase>>& MapClauseExprs,
        const TSPtrArray<CExpressionBase>& Parameters)
    {
        const CSymbol MakeLocalizableSymbol = _Symbols.AddChecked("MakeLocalizableValue");
        TSRef<CExprIdentifierUnresolved> MakeLocalizableIdentifier = TSRef<CExprIdentifierUnresolved>::New(MakeLocalizableSymbol);
        MakeLocalizableIdentifier->SetNonReciprocalMappedVstNode(&DefinitionVst);

        // ** special exception here to allow looking up this identifier which is <epic_internal> to 
        // another module, we're doing this as a short term protection against users writing 
        // code that depends on internal details of the message type
        MakeLocalizableIdentifier->GrantUnrestrictedAccess();

        for (const TSPtr<CExpressionBase>& Parameter : Parameters)
        {
            if (Parameter->GetNodeType() == EAstNodeType::Definition)
            {
                TSPtr<CExprDefinition> ParamDefinition = Parameter.As<CExprDefinition>();
                TSPtr<CExpressionBase> Element = ParamDefinition->Element();
                if (!Element || Element->GetNodeType() != EAstNodeType::Identifier_Unresolved)
                {
                    continue;
                }

                TSPtr<CExprIdentifierUnresolved> ParamUnresolved = Element.As<CExprIdentifierUnresolved>();
                CSymbol NameSymbol = ParamUnresolved->_Symbol;
                const Vst::Node* NameVstNode = ParamUnresolved->GetMappedVstNode();

                TSRef<CExprIdentifierUnresolved> NameIdentifier = TSRef<CExprIdentifierUnresolved>::New(NameSymbol);
                NameIdentifier->SetNonReciprocalMappedVstNode(ParamUnresolved->GetMappedVstNode());

                // create an invocation passing the argument to MakeLocalizableValue
                // so it can be added to the Substitutions map
                TSRef<CExprInvocation> MakeLocalizableValueInvocation =
                    TSRef<CExprInvocation>::New(
                        CExprInvocation::EBracketingStyle::Parentheses,
                        MakeLocalizableIdentifier,
                        NameIdentifier);
                MakeLocalizableValueInvocation->SetNonReciprocalMappedVstNode(NameVstNode);

                TSRef<CExprString> ArgNameString = TSRef<CExprString>::New(NameSymbol.AsString());
                ArgNameString->SetNonReciprocalMappedVstNode(NameVstNode);

                TSRef<CExprFunctionLiteral> MapClauseExpr =
                    TSRef<CExprFunctionLiteral>::New(
                        Move(ArgNameString),
                        MakeLocalizableValueInvocation);
                MapClauseExpr->SetNonReciprocalMappedVstNode(&DefinitionVst);
                MapClauseExprs.Add(Move(MapClauseExpr));
            }
            else if (Parameter->GetNodeType() == EAstNodeType::Invoke_MakeTuple)
            {
                TSPtr<CExprMakeTuple> ParamTuple = Parameter.As<CExprMakeTuple>();
                FillinClauseExprs(DefinitionVst, MapClauseExprs, ParamTuple->GetSubExprs());
            }
        }
    }

    TSRef<CExpressionBase> DesugarLocalizable(
        const Vst::Definition& DefinitionVst,
        const Vst::Identifier& MessageKeyVst,
        const CUTF8String& MessageDefaultText,
        const TSRef<Vst::Node>& MessageTypeVst,
        TSPtrArray<CExpressionBase>& Parameters,
        bool bIsFunction)
    {
        const CSymbol MessageKeySymbol = VerifyAddSymbol(&MessageKeyVst, MessageKeyVst._OriginalCode);

        TArray<TSRef<CExpressionBase>> MapClauseExprs;
        FillinClauseExprs(DefinitionVst, MapClauseExprs, Parameters);

        TSPtr<CExpressionBase> ElementParameters;

        if (Parameters.Num() == 1)
        {
            ElementParameters = Parameters[0];
        }
        else
        {
            // multi-parameter functions need to wrap the 
            // definitions in a CExprMakeTuple node
            TSRef<CExprMakeTuple> ElementParametersTuple = TSRef<CExprMakeTuple>::New();
            ElementParametersTuple->SetNonReciprocalMappedVstNode(&DefinitionVst);

            for (const TSPtr<CExpressionBase>& Parameter : Parameters)
            {
                ElementParametersTuple->AppendSubExpr(Parameter);
            }

            ElementParameters = Move(ElementParametersTuple);
        }

        TSRefArray<CExpressionBase> ArgumentExprs;

        {
            // Key argument

            // for the function case, the current scope will include the function name, so we pass the null symbol here
            TSRef<CExprPathPlusSymbol> KeyPath = TSRef<CExprPathPlusSymbol>::New(bIsFunction ? CSymbol() : MessageKeySymbol);
            KeyPath->SetNonReciprocalMappedVstNode(&MessageKeyVst);

            ArgumentExprs.Add(Move(KeyPath));
        }

        {
            // DefaultText argument
            TSRef<CExpressionBase> DefaultTextString = TSRef<CExprString>::New(MessageDefaultText);
            DefaultTextString->SetNonReciprocalMappedVstNode(&DefinitionVst);
            ArgumentExprs.Add(Move(DefaultTextString));
        }

        {
            // Substitutions argument
            const CSymbol MapSymbol = _Symbols.AddChecked("map");
            TSRef<CExprIdentifierUnresolved> MapIdentifier = TSRef<CExprIdentifierUnresolved>::New(MapSymbol);
            MapIdentifier->SetNonReciprocalMappedVstNode(&DefinitionVst);
            TSRef<CExprMacroCall> MapMacroExpr = TSRef<CExprMacroCall>::New(MapIdentifier);
            MapMacroExpr->SetNonReciprocalMappedVstNode(&DefinitionVst);
            MapMacroExpr->AppendClause(CExprMacroCall::CClause(EMacroClauseTag::None, Verse::Vst::Clause::EForm::Synthetic, Move(MapClauseExprs)));

            ArgumentExprs.Add(Move(MapMacroExpr));
        }

        TSRef<CExprMakeTuple> ArgTuple = WrapExpressionListInTuple(Move(ArgumentExprs), DefinitionVst, false);

        const CSymbol MakeMessageSymbol = _Symbols.AddChecked("MakeMessageInternal");
        TSRef<CExprIdentifierUnresolved> MakeMessageIdentifier = TSRef<CExprIdentifierUnresolved>::New(MakeMessageSymbol);
        
        // ** special exception here to allow looking up this identifier which is <epic_internal> to 
        // another module, we're doing this as a short term protection against users writing 
        // code that depends on internal details of the message type
        MakeMessageIdentifier->GrantUnrestrictedAccess();

        MakeMessageIdentifier->SetNonReciprocalMappedVstNode(&DefinitionVst);

        TSRef<CExprInvocation> MakeMessageInvocation = 
            TSRef<CExprInvocation>::New(
                CExprInvocation::EBracketingStyle::Parentheses,
                Move(MakeMessageIdentifier),
                Move(ArgTuple));
        MakeMessageInvocation->SetNonReciprocalMappedVstNode(&DefinitionVst);

        TSRef<CExpressionBase> MessageKeyIdentifier = AddMapping(MessageKeyVst, TSRef<CExprIdentifierUnresolved>::New(MessageKeySymbol));

        if (MessageKeyVst.HasAttributes())
        {
            MessageKeyIdentifier->_Attributes = DesugarAttributes(MessageKeyVst.GetAux()->GetChildren());
        }

        TSPtr<CExpressionBase> DefinitionElement;

        if (bIsFunction)
        {
            TSRef<CExprInvocation> ElementInvocation =
                TSRef<CExprInvocation>::New(
                    CExprInvocation::EBracketingStyle::Parentheses,
                    Move(MessageKeyIdentifier),
                    Move(ElementParameters.AsRef()));
            ElementInvocation->SetNonReciprocalMappedVstNode(&DefinitionVst);
            DefinitionElement = ElementInvocation;
        }
        else
        {
            DefinitionElement = MessageKeyIdentifier;
        }

        TSRef<CExprDefinition> Definition = TSRef<CExprDefinition>::New(DefinitionElement.AsRef(), DesugarExpressionVst(*MessageTypeVst), Move(MakeMessageInvocation));
        return Definition;
    }

    // Common code for new and old code for localize.
    // Selects new or old behaviour depending on UploadedAtFNVesrsion.

    TSPtr<CExpressionBase> TryDesugarLocalizable(const Vst::Definition& DefinitionVst)
    {
        TSRef<Vst::Node> LhsVst = DefinitionVst.GetOperandLeft();

        bool bEnableNamedParametersForLocalize = VerseFN::UploadedAtFNVersion::EnableNamedParametersForLocalize(_Package->_UploadedAtFNVersion);

        //
        // there are several valid forms for localized definitions
        // this list is mirrored in Localization.versetest
        //
        // 1) TheMsg<localizes> := "The Message"
        // 2) TheMsg<localizes> : message = "The Message"
        // 3) TheMsg<localizes>(Name:string) := "The Message"
        // 4) TheMsg<localizes>(Name:string) : message = "The Message"
        // 5) TheMsg<localizes>(Name:string) := "The Message to {Name}"
        // 6) TheMsg<localizes>(Name:string) : message = "The Message to {Name}"
        //
        // NOTE that currently we do not support any of the forms(1, 3, 5) that omit the type name,
        // but we still parse every form in order to give better error messages here
        //

        const Vst::Identifier* MaybeLocalizedIdentifier = nullptr;
        TSPtr<Vst::Node> MaybeLocalizedType;
        const Vst::Clause* MaybeLocalizedArgs = nullptr;

        if (const Vst::Identifier* LhsIdentifier = LhsVst->AsNullable<Vst::Identifier>())
        {
            // this is only hit for case 1 where the identifier is the Lhs of the definition
            MaybeLocalizedIdentifier = LhsIdentifier;
        }
        else if (const Vst::TypeSpec* LhsTypeSpec = LhsVst->AsNullable<Vst::TypeSpec>())
        {
            // this is hit for case 2, 4, and 6, where the user explicitly stated a type

            if (LhsTypeSpec->HasLhs())
            {
                const Vst::Node& LhsNode = *LhsTypeSpec->GetLhs();

                // case 2?
                MaybeLocalizedIdentifier = LhsNode.AsNullable<Vst::Identifier>();

                if (!MaybeLocalizedIdentifier)
                {
                    const Vst::PrePostCall* PrePostCallNode = LhsNode.AsNullable<Vst::PrePostCall>();

                    if (PrePostCallNode && (PrePostCallNode->GetChildCount() >= 2))
                    {
                        TSRef<Vst::Node> PrePostCallFirstChild = PrePostCallNode->GetChildren()[0];
                        TSRef<Vst::Node> PrePostCallSecondChild = PrePostCallNode->GetChildren()[1];

                        // this is case 4 and 6
                        MaybeLocalizedIdentifier = PrePostCallFirstChild->AsNullable<Vst::Identifier>();
                        MaybeLocalizedArgs = PrePostCallSecondChild->AsNullable<Vst::Clause>();
                    }
                }
            }

            MaybeLocalizedType = LhsTypeSpec->GetRhs();
        }
        else if (const Vst::PrePostCall* PrePostCallNode = LhsVst->AsNullable<Vst::PrePostCall>())
        {
            // this is hit for case 3 and 5
            if (PrePostCallNode->GetChildCount() >= 2)
            {
                TSRef<Vst::Node> PrePostCallFirstChild = PrePostCallNode->GetChildren()[0];
                TSRef<Vst::Node> PrePostCallSecondChild = PrePostCallNode->GetChildren()[1];

                // this is case 3 and 5
                MaybeLocalizedIdentifier = PrePostCallFirstChild->AsNullable<Vst::Identifier>();
                MaybeLocalizedArgs = PrePostCallSecondChild->AsNullable<Vst::Clause>();
            }
        }

        if (MaybeLocalizedIdentifier != nullptr)
        {
            TArray<SNameTypeIdentifierPair> LocalizedArgumentNameTypePairs;
            if (!bEnableNamedParametersForLocalize)
            {
                if (MaybeLocalizedArgs)
                {
                    // this collects the pairs of function parameter name and type, (Subject,string) and (Rank,int) in the above example
                    for (const TSRef<Vst::Node>& ArgNode : MaybeLocalizedArgs->GetChildren())
                    {
                        if (ArgNode->GetElementType() == Vst::NodeType::TypeSpec)
                        {
                            const Vst::TypeSpec& ArgTypeSpecNode = ArgNode->As<Vst::TypeSpec>();

                            if (ArgTypeSpecNode.HasLhs())
                            {
                                const Vst::Identifier* ArgNameIdentifier = ArgTypeSpecNode.GetLhs()->AsNullable<Vst::Identifier>();
                                const Vst::Identifier* ArgTypeIdentifier = ArgTypeSpecNode.GetRhs()->AsNullable<Vst::Identifier>();

                                if (ArgNameIdentifier && ArgTypeIdentifier)
                                {
                                    LocalizedArgumentNameTypePairs.Add(SNameTypeIdentifierPair{ ArgNameIdentifier, ArgTypeIdentifier });
                                }
                            }
                        }
                    }
                }
            }

            // does this identifier have a 'localizes' attribute attached?
            if (MaybeLocalizedIdentifier->IsAttributePresent("localizes"))
            {
                // first ensure that they've specified a type (we don't currently support omitting the type)
                if (MaybeLocalizedType == nullptr)
                {
                    AppendGlitch(&DefinitionVst, EDiagnostic::ErrSemantic_LocalizesMustSpecifyType);
                    return AddMapping(DefinitionVst, TSRef<CExprError>::New());
                }

                TSPtrArray<CExpressionBase> Arguments;
                if (bEnableNamedParametersForLocalize)
                {
                    if (MaybeLocalizedArgs)
                    {
                        for (const TSRef<Vst::Node>& ParamVst : MaybeLocalizedArgs->GetChildren())
                        {
                            Arguments.Add(DesugarExpressionVst(*ParamVst));
                        }
                    }
                }

                // Now get the RHS value
                TSRef<Vst::Node> RhsVst    = DefinitionVst.GetOperandRight();
                TSPtr<Vst::Node> ValueNode = RhsVst;

                // Unwrap if wrapped in a clause
                if (RhsVst->GetElementType() == Vst::NodeType::Clause)
                {
                    if (RhsVst->GetChildCount() == 1)
                    {
                        ValueNode = RhsVst->GetChildren()[0];
                    }
                    else
                    {
                        // Bad clause - too many children
                        ValueNode.Reset();
                    }
                }

                // Only support the Rhs being a string literal or an interpolated string expression
                const Vst::Identifier* MessageKeyVst = nullptr;
                CUTF8String MessageDefaultText;
                bool bIsExternal = false;

                if (ValueNode)
                {
                    if (const Vst::StringLiteral* RhsStringLiteral = ValueNode->AsNullable<Vst::StringLiteral>())
                    {
                        MessageKeyVst = MaybeLocalizedIdentifier;
                        MessageDefaultText = RhsStringLiteral->GetSourceText();
                    }
                    else if (const Vst::InterpolatedString* RhsInterpolatedString = ValueNode->AsNullable<Vst::InterpolatedString>())
                    {

                        // The code uses GetSourceText() on the interpolated string. This is incorrect since escape characters are not decoded.
                        // Neither is there any check that only the available arguments are used, and no complicated expressions or any other identifiers that are in scope.
                        // A better solution is to create the localization string from the arguments to Concatenate, evaluating any constant expressions, and verify that the rest are one of the arguments.
                        // This is way to much work for SOL-3734. The code here is a minimal fix which interprets any unicode escapes and escape characters.
                        // Better than before but far from perfect.
                        bool bInterpolateOnlyUtf8 = true;
                        CUTF8StringBuilder DecodedString;
                        const CUTF8String& RawString = RhsInterpolatedString->GetSourceText();

                        for (int32_t Ix = 0, IxEnd = RawString.ByteLen(); Ix < IxEnd; ++Ix)
                        {
                            UTF8Char Char = RawString[Ix];
                            if (Char == '\\')
                            {
                                if (Ix + 1 >= IxEnd)
                                {
                                    // If this one triggers then the parser accepted an incorrect escape.
                                    AppendGlitch(&DefinitionVst, EDiagnostic::ErrSemantic_LocalizesEscape, "Incomplete escape character");
                                }
                                else
                                {
                                    // From parser
                                    // Special := '\'|'{'|'}'|'#'|'<'|'>'|'&'|'~'
                                    // CharEsc := '\' ('r'|'n'|'t'|'''|'"'|Special)
                                    ++Ix;
                                    switch (RawString[Ix])
                                    {
                                    case '\\': case '{': case '}': case '#': case '<': case '>': case '&': case '~': case '\'': case '"':
                                        DecodedString.Append(RawString[Ix]);
                                        break;
                                    case 'r':
                                        DecodedString.Append('\r');
                                        break;
                                    case 'n':
                                        DecodedString.Append('\n');
                                        break;
                                    case 't':
                                        DecodedString.Append('\t');
                                        break;
                                    default:
                                        // If this one triggers then the parser understands more escape characters than this code does.
                                        AppendGlitch(&DefinitionVst, EDiagnostic::ErrSemantic_LocalizesEscape, CUTF8String("Escaped character '%c' not recognized in localized message", RawString[Ix]));
                                        DecodedString.Append(RawString[Ix]);
                                    }
                                }
                            }
                            else if (Char == '{')
                            {
                                if (Ix + 2 < IxEnd && RawString[Ix + 1] == '0' && RawString[Ix + 2] == 'u')
                                {   // Decode code point, parser ensures that terminating '}' must be in string.
                                    UniCodePoint CodePoint = 0;
                                    Ix += 3;
                                    for (; Ix < IxEnd; ++Ix)
                                    {
                                        UTF8Char UnicodeChar = RawString[Ix];
                                        if (UnicodeChar == '}')
                                        {
                                            SUTF8CodePoint Utf8 = CUnicode::EncodeUTF8(CodePoint);
                                            DecodedString.Append(CUTF8StringView(Utf8.Units, Utf8.Units + Utf8.NumUnits));
                                            break;
                                        }
                                        else
                                        {
                                            UTF8Char LowerChar = CUnicode::ToLower_ASCII(UnicodeChar);
                                            if (CUnicode::IsDigitASCII(LowerChar))
                                            {
                                                CodePoint = CodePoint * 16 + LowerChar - '0';
                                            }
                                            else if (LowerChar >= 'a' && LowerChar <= 'f')
                                            {
                                                CodePoint = CodePoint * 16 + LowerChar - 'a' + 10;
                                            }
                                            else
                                            {
                                                AppendGlitch(&DefinitionVst, EDiagnostic::ErrSemantic_LocalizesEscape, "Localized string failed to convert unicode escape");
                                                return AddMapping(DefinitionVst, TSRef<CExprError>::New());
                                            }
                                        }
                                    }
                                } 
                                else if (Ix + 1 < IxEnd && CUnicode::IsIdentifierStart(RawString[Ix + 1]))
                                {   // Only accept identifier, parse ensures that terminating '}' must be in string.
                                    bInterpolateOnlyUtf8 = false;
                                    DecodedString.Append(Char);
                                    Ix += 1;
                                    for (; Ix < IxEnd; ++Ix)
                                    {
                                        UTF8Char IdentifierChar = RawString[Ix];
                                        DecodedString.Append(IdentifierChar);
                                        if (IdentifierChar == '}')
                                        {
                                            break;
                                        }
                                        else if (!CUnicode::IsIdentifierTail(IdentifierChar))
                                        {
                                            AppendGlitch(&DefinitionVst, EDiagnostic::ErrSemantic_LocalizesEscape, "Localized string failed to convert parse escaped identifier");
                                            return AddMapping(DefinitionVst, TSRef<CExprError>::New());
                                        }
                                    }
                                }
                                else
                                {
                                    AppendGlitch(&DefinitionVst, EDiagnostic::ErrSemantic_LocalizesEscape, "Localized string only understands unicode escape and argument identifiers");
                                    return AddMapping(DefinitionVst, TSRef<CExprError>::New());
                                }
                            }
                            else
                            {
                                DecodedString.Append(Char);
                            }
                        }

                        if (MaybeLocalizedArgs || bInterpolateOnlyUtf8)
                        {
                            MessageKeyVst = MaybeLocalizedIdentifier;
                            MessageDefaultText = DecodedString.MoveToString();
                        }
                    }
                    else if (const Vst::Macro* RhsMacro = ValueNode->AsNullable<Vst::Macro>())
                    {
                        const Vst::Node* RhsMacroName = RhsMacro->GetName();

                        if (RhsMacroName)
                        {
                            const Vst::Identifier* RhsMacroNameIdentifier = RhsMacroName->AsNullable<Vst::Identifier>();

                            if (RhsMacroNameIdentifier)
                            {
                                bIsExternal = RhsMacroNameIdentifier->GetSourceText() == "external";
                            }
                        }
                    }
                }

                if (MessageKeyVst != nullptr &&
                    MaybeLocalizedType != nullptr)
                {
                    // the success case is here - we gathered the message key, default text, and any function arguments
                    const bool bIsFunction = (MaybeLocalizedArgs != nullptr);
                    if (bEnableNamedParametersForLocalize)
                    {
                        return AddMapping(DefinitionVst, DesugarLocalizable(DefinitionVst, *MessageKeyVst, MessageDefaultText, MaybeLocalizedType.AsRef(), Arguments, bIsFunction));
                    }
                    else
                    {
                        return AddMapping(DefinitionVst, DesugarLocalizableOld(DefinitionVst, *MessageKeyVst, MessageDefaultText, MaybeLocalizedType.AsRef(), LocalizedArgumentNameTypePairs, bIsFunction));
                    }
                }
                else if (bIsExternal)
                {
                    // silently allow this through, no need to desugar
                }
                else
                {
                    AppendGlitch(&DefinitionVst, EDiagnostic::ErrSemantic_LocalizesRhsMustBeString);
                    return AddMapping(DefinitionVst, TSRef<CExprError>::New());
                }
            }
        }

        return nullptr;
    }

    TSRef<CExpressionBase> DesugarDefinition(const Vst::Definition& DefinitionVst)
    {
        TSRef<Vst::Node> LhsVst = DefinitionVst.GetOperandLeft();
        TSRef<Vst::Node> RhsVst = DefinitionVst.GetOperandRight();
        TSPtr<CExpressionBase> Element;
        TSPtr<CExpressionBase> ValueDomain;
        CSymbol                Name;

        TSPtr<CExpressionBase> LocalizableDefinition = TryDesugarLocalizable(DefinitionVst);

        if (LocalizableDefinition != nullptr)
        {
            return LocalizableDefinition.AsRef();
        }

        if (const Vst::TypeSpec* LhsTypeSpec = LhsVst->AsNullable<Vst::TypeSpec>())
        {
            if (LhsTypeSpec->HasLhs())
            {
                // Definition is `x:t = y`
                Element = DesugarMaybeNamed(*LhsTypeSpec->GetLhs(), Name);
            }
            ValueDomain = DesugarExpressionVst(*LhsTypeSpec->GetRhs());
        }
        else
        {
            // Definition is `x := y`
            Element = DesugarMaybeNamed(*LhsVst, Name);
        }
        TSRef<CExpressionBase> Value = DesugarClauseAsExpression(*RhsVst);
        if (!Name.IsNull() && !ValueDomain)
        {
            // Looks like a named argument - matched `_Parameter` will be set later in semantic analysis
            return AddMapping(DefinitionVst, TSRef<CExprMakeNamed>::New(Name, Move(Value)));
        }
        TSRef<CExprDefinition> DefinitionAst = TSRef<CExprDefinition>::New(Move(Element), Move(ValueDomain), Move(Value));
        if (!Name.IsNull())
        {
            // Looks like a named parameter
            DefinitionAst->SetName(Name);
        }
        return AddMapping(DefinitionVst, DefinitionAst);
    }

    TSRef<CExpressionBase> DesugarAssignment(const Vst::Assignment& AssignmentVst)
    {
        TSRef<Vst::Node> LhsVst = AssignmentVst.GetOperandLeft();
        TSRef<Vst::Node> RhsVst = AssignmentVst.GetOperandRight();
        Vst::Assignment::EOp Op = RhsVst->GetTag<Vst::Assignment::EOp>();
        // Desugar the LHS and RHS subexpressions.
        TSRef<CExpressionBase> LhsAst = DesugarExpressionVst(*LhsVst);
        TSRef<CExpressionBase> RhsAst = DesugarClauseAsExpression(*RhsVst);
        return AddMapping(AssignmentVst, TSRef<CExprAssignment>::New(Op, Move(LhsAst), Move(RhsAst)));
    }
    
    TSRef<CExpressionBase> DesugarBinaryOpLogicalAndOr(const Vst::Node& VstNode)
    {
        const Vst::NodeType ThisNodeType = VstNode.GetElementType();
        const bool bIsLogicalOr = ThisNodeType == Vst::NodeType::BinaryOpLogicalOr;
        const bool bIsLogicalAnd = ThisNodeType == Vst::NodeType::BinaryOpLogicalAnd;
        const int32_t NumChildren = VstNode.GetChildCount();
        if (NumChildren == 0)
        {
            AppendGlitch(&VstNode, EDiagnostic::ErrSemantic_BinaryOpNoOperands);
            return AddMapping(VstNode, TSRef<CExprError>::New(nullptr, /*bCanFail=*/true));
        }

        // Convert the flat operand list into a right-recursive binary tree: (0 (1 (2 3)))

        // Start with the rightmost child
        const Vst::Node* RHSNode = VstNode.GetChildren().Last().Get();
        TSRef<CExpressionBase> Result = DesugarExpressionVst(*RHSNode);

        // Then loop back and build expression tree
        for (int i = NumChildren - 2; i >= 0; --i)
        {
            // Evaluate LHS expression
            const Vst::Node* LHSNode = VstNode.GetChildren()[i].Get();
            TSRef<CExpressionBase> LHS = DesugarExpressionVst(*LHSNode);

            // Build expression node
            if (bIsLogicalAnd)
            {
                Result = TSRef<CExprShortCircuitAnd>::New(Move(LHS), Move(Result));
            }
            else if (bIsLogicalOr)
            {
                Result = TSRef<CExprShortCircuitOr>::New(Move(LHS), Move(Result));
            }
            else
            {
                ULANG_UNREACHABLE();
            }
        }

        // RHS contains the final expression tree for this node
        return AddMapping(VstNode, Move(Result));
    }
    
    TSRef<CExpressionBase> DesugarPrefixOpLogicalNot(const Vst::PrefixOpLogicalNot& PrefixOpLogicalNotNode)
    {
        if (PrefixOpLogicalNotNode.GetChildCount() == 0)
        {
            AppendGlitch(&PrefixOpLogicalNotNode, EDiagnostic::ErrSemantic_PrefixOpNoOperand);
            return AddMapping(PrefixOpLogicalNotNode, TSRef<CExprError>::New(nullptr, /*bCanFail=*/true));
        }
        else
        {
            const Vst::Node* OperandVst = PrefixOpLogicalNotNode.GetChildren()[0];
            TSRef<CExpressionBase> OperandAst = DesugarExpressionVst(*OperandVst);
            return AddMapping(PrefixOpLogicalNotNode, TSRef<CExprLogicalNot>::New(Move(OperandAst)));
        }
    }
    
    TSRef<CExpressionBase> DesugarBinaryOpCompare(const Vst::BinaryOpCompare& BinaryOpCompareNode)
    {
        const int32_t NumChildren = BinaryOpCompareNode.GetChildCount();
        if (NumChildren != 2)
        {
            AppendGlitch(&BinaryOpCompareNode, EDiagnostic::ErrSemantic_BinaryOpExpectedTwoOperands);
            return AddMapping(BinaryOpCompareNode, TSRef<CExprError>::New(nullptr, /*bCanFail=*/true));
        }
        else
        {
            const Vst::Node* LhsNode = BinaryOpCompareNode.GetChildren()[0].Get();
            TSRef<CExpressionBase> Lhs = DesugarExpressionVst(*LhsNode);
            
            // Get RHS operand
            const Vst::Node* RhsNode = BinaryOpCompareNode.GetChildren()[1].Get();
            TSRef<CExpressionBase> Rhs = DesugarExpressionVst(*RhsNode);

            TSRef<CExprMakeTuple> Argument = TSRef<CExprMakeTuple>::New(Move(Lhs), Move(Rhs));
            Argument->SetNonReciprocalMappedVstNode(&BinaryOpCompareNode);
            TSRef<CExpressionBase> Result = AddMapping(BinaryOpCompareNode, TSRef<CExprComparison>::New(
                RhsNode->GetTag<Vst::BinaryOpCompare::op>(),
                Argument));
            return Result;
        }
    }

    TSRef<CExpressionBase> DesugarBinaryOp(const Vst::BinaryOp& BinaryOpNode)
    {
        using EOp = Vst::BinaryOp::op;

        const int32_t NumChildren = BinaryOpNode.GetChildCount();
        if (NumChildren == 0)
        {
            AppendGlitch(&BinaryOpNode, EDiagnostic::ErrSemantic_BinaryOpNoOperands);
            return AddMapping(BinaryOpNode, TSRef<CExprError>::New());
        }

        // Get our first LHS operand
        const Vst::Node* LHSNode = BinaryOpNode.GetChildren()[0].Get();
        TSPtr<CExpressionBase> LhsPtr;
        

        bool bHasLeadingOperator = false; 
        if (LHSNode->GetElementType() == Vst::NodeType::Operator && BinaryOpNode.GetChildCount() > 1)
        {
            bHasLeadingOperator = true;

            const Vst::Node* OperatorNode = BinaryOpNode.GetChildren()[0].Get();
            const Vst::Node* OprandNode = BinaryOpNode.GetChildren()[1].Get();

            const CUTF8String& OpString = OperatorNode->As<Vst::Operator>().GetSourceText();
            TSRef<CExpressionBase> Result = DesugarExpressionVst(*OprandNode);
            if (OpString[0] == u'-')
            {
                Result = TSRef<CExprUnaryArithmetic>::New(CExprUnaryArithmetic::EOp::Negate, Move(Result));
                LhsPtr = AddMapping(BinaryOpNode, Move(Result));
            }
            else
            {
                LhsPtr = Move(Result);
            }
        }
        else
        {
            // Get our first LHS operand
            LhsPtr = DesugarExpressionVst(*LHSNode);
        }


        TSRef<CExpressionBase> Lhs = LhsPtr.AsRef();

        auto HandleMalformedVst = [&Lhs](TSRef<CExpressionBase>&& Rhs)
        {
            TSRef<CExprError> ErrorExpr = TSRef<CExprError>::New();
            ErrorExpr->AppendChild(Move(Lhs));
            ErrorExpr->AppendChild(Move(Rhs));
            Lhs = Move(ErrorExpr);
        };

        // Then loop and build expression tree
        for (int i = bHasLeadingOperator ? 2 : 1; i < NumChildren; i += 2)
        {
            const Vst::Node* OperatorNode = BinaryOpNode.GetChildren()[i].Get();

            ULANG_ENSUREF(OperatorNode->GetTag<EOp>() == EOp::Operator, "Malformed binary op node, expecting an operator. ");
            if (ULANG_ENSUREF(i + 1 < NumChildren, "Malformed binary Op node, no trailing operand."))
            {
                const Vst::Node* RhsOperandNode = BinaryOpNode.GetChildren()[i + 1].Get();
                ULANG_ENSUREF(RhsOperandNode->GetTag<EOp>() == EOp::Operand, "Malformed binary op node, expecting an operand.");
                TSRef<CExpressionBase> Rhs = DesugarExpressionVst(*RhsOperandNode);

                if (OperatorNode->GetElementType() == Vst::NodeType::Operator)
                {
                    const CUTF8String& OpString = OperatorNode->As<Vst::Operator>().GetSourceText();
                    if (OpString.ByteLen() == 1)
                    {
                        CExprBinaryArithmetic::EOp ArithmeticOp;
                        if(BinaryOpNode.GetElementType() == Vst::NodeType::BinaryOpAddSub)
                            ArithmeticOp = (OpString[0] == u'+')
                            ? CExprBinaryArithmetic::EOp::Add
                            : CExprBinaryArithmetic::EOp::Sub;
                        else if(BinaryOpNode.GetElementType() == Vst::NodeType::BinaryOpMulDivInfix)
                            ArithmeticOp = (OpString[0] == u'*')
                            ? CExprBinaryArithmetic::EOp::Mul
                            : CExprBinaryArithmetic::EOp::Div;
                        else 
                            ULANG_UNREACHABLE();
                        TSRef<CExprMakeTuple> Argument = TSRef<CExprMakeTuple>::New(Move(Lhs), Move(Rhs));
                        Argument->SetNonReciprocalMappedVstNode(OperatorNode);
                        Lhs = AddMapping(*OperatorNode, TSRef<CExprBinaryArithmetic>::New(
                            ArithmeticOp,
                            Argument));
                    }
                    else
                    {
                        HandleMalformedVst(Move(Rhs));
                    }
                }
                else if (OperatorNode->GetElementType() == Vst::NodeType::Identifier)
                {
                    TSRef<CExprMakeTuple> Argument = TSRef<CExprMakeTuple>::New(Move(Lhs), Move(Rhs));
                    Argument->SetNonReciprocalMappedVstNode(OperatorNode);
                    TSRef<CExprInvocation> Invocation = TSRef<CExprInvocation>::New(Argument);
                    const CSymbol OperatorSymbol = VerifyAddSymbol(OperatorNode, CUTF8String("operator'%s'", OperatorNode->As<Vst::Identifier>().GetSourceCStr()));
                    Invocation->SetCallee(TSRef<CExprIdentifierUnresolved>::New(OperatorSymbol));
                    Lhs = AddMapping(*OperatorNode, Move(Invocation));
                }
                else
                {
                    HandleMalformedVst(Move(Rhs));
                }
            }
        }

        // LHS contains the final expression tree for this node
        return Move(Lhs);
    }

    TSRef<CExpressionBase> DesugarBinaryOpRange(const Vst::BinaryOpRange& BinaryOpRange)
    {
        TSRef<CExpressionBase> Lhs = DesugarExpressionVst(*BinaryOpRange.GetChildren()[0]);
        TSRef<CExpressionBase> Rhs = DesugarExpressionVst(*BinaryOpRange.GetChildren()[1]);
        return AddMapping(BinaryOpRange, TSRef<CExprMakeRange>::New(Move(Lhs), Move(Rhs)));
    }

    TSRef<CExpressionBase> DesugarBinaryOpArrow(const Vst::BinaryOpArrow& BinaryOpArrow)
    {
        TSRef<CExpressionBase> Lhs = DesugarExpressionVst(*BinaryOpArrow.GetChildren()[0]);
        TSRef<CExpressionBase> Rhs = DesugarExpressionVst(*BinaryOpArrow.GetChildren()[1]);
        return AddMapping(BinaryOpArrow, TSRef<CExprArrow>::New(Move(Lhs), Move(Rhs)));
    }

    TSRef<CExpressionBase> DesugarMaybeNamed(Vst::Node& VstNode, CSymbol& Name)
    {
        if ((VstNode.GetElementType() == Vst::NodeType::PrePostCall) && (VstNode.GetChildCount() >= 2))
        {
            TSRef<Vst::Node> VarChild0 = VstNode.GetChildren()[0];
            Vst::Node*       VarChild1 = VstNode.GetChildren()[1];

            if ((VarChild0->GetTag<Vst::PrePostCall::Op>() == Vst::PrePostCall::Op::Option)
                && (VarChild0->GetElementType() == Vst::NodeType::Clause)
                && (VarChild1->GetTag<Vst::PrePostCall::Op>() == Vst::PrePostCall::Op::Expression)
                && (VarChild1->GetElementType() == Vst::NodeType::Identifier)
                && !static_cast<const Vst::Identifier*>(VarChild1)->IsQualified())
            {
                Name = VerifyAddSymbol(VarChild1, VarChild1->As<Vst::Identifier>().GetSourceText());
                // Temporarily remove option clause so option not created and track as explicitly ?named parameter or argument
                VstNode.AccessChildren().RemoveAt(0);
                // Continue processing
                TSRef<CExpressionBase> NamedExpr = DesugarExpressionVst(VstNode);
                // Replace temporarily removed option clause so VST remains as it was originally
                VstNode.AccessChildren().Insert(VarChild0, 0);
                return NamedExpr;
            }
        }

        return DesugarExpressionVst(VstNode);
    }

    TSRef<CExpressionBase> DesugarTypeSpec(const Vst::TypeSpec& TypeSpecVst)
    {
        TSPtr<CExpressionBase> Lhs = nullptr;
        CSymbol Name;

        if (TypeSpecVst.HasLhs())
        {
            Lhs = DesugarMaybeNamed(*TypeSpecVst.GetLhs(), Name);
        }
        TSRef<CExpressionBase> Rhs = DesugarExpressionVst(*TypeSpecVst.GetRhs());

        TSPtr<CExpressionBase> NoDefaultValue = nullptr;

        // Create a CExprDefinition AST node.
        TSRef<CExprDefinition> DefinitionAst = TSRef<CExprDefinition>::New(Move(Lhs), Move(Rhs), Move(NoDefaultValue));

        if (!Name.IsNull())
        {
            DefinitionAst->SetName(Name);
        }
        
        // Desugar the type-spec's attributes.
        if (TypeSpecVst.HasAttributes())
        {
            DefinitionAst->_Attributes = DesugarAttributes(TypeSpecVst.GetAux()->GetChildren());
        }

        return AddMapping(TypeSpecVst, Move(DefinitionAst));
    }
    
    TSRef<CExpressionBase> DesugarCall(bool bCalledWithBrackets, const Vst::Clause& CallArgs, TSRef<CExpressionBase>&& Callee)
    {
        // Create an invocation AST node.
        return AddMapping(CallArgs, TSRef<CExprInvocation>::New(
            bCalledWithBrackets 
            ? CExprInvocation::EBracketingStyle::SquareBrackets 
            : CExprInvocation::EBracketingStyle::Parentheses,
            Move(Callee),
            DesugarExpressionListAsExpression(CallArgs, CallArgs.GetForm(), false)));
    }

    TSRef<CExpressionBase> DesugarPrePostCall(const Vst::PrePostCall& Ppc)
    {
        using PpcOp = Vst::PrePostCall::Op;

        const int32_t NumPpcNodes = Ppc.GetChildCount();

        const int32_t ExpressionIndex = [&]()
        {
            for (int32_t i = 0; i < NumPpcNodes; i += 1)
            {
                const Vst::Node* PpcChildNode = Ppc.GetChildren()[i];
                if (PpcChildNode->GetTag<PpcOp>() == PpcOp::Expression)
                {
                    return i;
                }
            }

            ULANG_ERRORF("Malformed Vst : DotIndent cannot be a prefix.");
            return -1;
        }();

        //~~~~ HANDLE POSTFIXES ~~~~~~~~~~~~~~~~
        TSPtr<CExpressionBase> Lhs;
        for (int32_t i = ExpressionIndex; i < NumPpcNodes; i += 1)
        {
            const Vst::Node& PpcChildNode     = *Ppc.GetChildren()[i];
            switch (PpcChildNode.GetTag<PpcOp>())
            {
            case PpcOp::Expression:
            {
                Lhs = DesugarExpressionVst(PpcChildNode);
            }
            break;
            // Handle <expr>?
            case PpcOp::Option:
            {
                ULANG_ASSERTF(Lhs, "Expected expr on LHS of QMark");
                Lhs = AddMapping(PpcChildNode, TSRef<CExprQueryValue>::New(Move(Lhs.AsRef())));
            }
            break;
            case PpcOp::Pointer:
            {
                ULANG_ASSERTF(Lhs, "Expected expr on LHS of Hat");
                Lhs = AddMapping(PpcChildNode, TSRef<CExprPointerToReference>::New(Move(Lhs.AsRef())));
            }
            break;
            case PpcOp::DotIdentifier:
            {
                ULANG_ASSERTF(Lhs, "Expected expr on LHS of DotIdentifier");
                const Vst::Identifier& IdentifierNode = PpcChildNode.As<Vst::Identifier>();
                Lhs = DesugarIdentifier(IdentifierNode, Move(Lhs));
                if (IdentifierNode.HasAttributes())
                {
                    Lhs->_Attributes = DesugarAttributes(IdentifierNode.GetAux()->GetChildren());
                }
            }
            break;
            case PpcOp::SureCall:
            case PpcOp::FailCall:
            {
                ULANG_ASSERTF(Lhs, "Expected expr on LHS of call");
                Lhs = DesugarCall(
                    PpcChildNode.GetTag<PpcOp>() == PpcOp::FailCall,
                    PpcChildNode.As<Vst::Clause>(),   // Arguments
                    Move(Lhs.AsRef())                 // Receiver expression
                );
            }
            break;
            default: ULANG_ERRORF("Unknown PrePostCall tag!"); break;
            }
        }
        

        TSRef<CExpressionBase> Rhs = Move(Lhs.AsRef());

        //~~~~ HANDLE PREFIXES ~~~~~~~~~~~~~~~~~
        // If ExpressionIndex > 0, this expression has prefix subexpressions.
        if (ExpressionIndex > 0)
        {
            // Prefixes are handles right to left.
            // We start with the expression, and work our way to the left, applying
            // whatever modifier we might encounter.
            // e.g. Given `?[]Item` we would have the following `RhsType`
            //   1. RhsType = Item
            //   2. RhsType = []RhsType = []Item  a.k.a array of items
            //   3. RhsType = ?RhsType  = ?[]Item a.k.a. option array of items

            //@jira SOL-998 : This use-case needs to be updated
            for (int32_t i = ExpressionIndex-1; i >= 0; i -= 1)
            {
                const TSRef<Vst::Node>& PpcChildNode = Ppc.GetChildren()[i];
                switch (PpcChildNode->GetTag<PpcOp>())
                {
                case PpcOp::Expression: { ULANG_ERRORF("Expression should have been processed by the 'HANDLE POSTFIXES' above."); } break;
                case PpcOp::DotIdentifier: { ULANG_ERRORF("Malformed Vst : DotIndent cannot be a prefix."); } break;
                case PpcOp::Pointer:
                {
                    AppendGlitch(
                        PpcChildNode.Get(),
                        EDiagnostic::ErrSemantic_Unsupported,
                        CUTF8String("Non-unique pointers are not supported yet"));
                    
                    Rhs = AddMapping(*PpcChildNode, TSRef<CExprError>::New());
                }
                break;
                case PpcOp::Option:
                {
                    Rhs = AddMapping(*PpcChildNode, TSRef<CExprOptionTypeFormer>::New(Move(Rhs)));
                }
                break;
                case PpcOp::FailCall:
                {
                    if (PpcChildNode->GetChildCount())
                    {
                        // Desugar the key expressions.
                        ULANG_ASSERTF(PpcChildNode->IsA<Vst::Clause>(), "Expected prefix [] operand to be a clause");
                        Vst::Clause& LhsClause = PpcChildNode->As<Vst::Clause>();
                        TArray<TSRef<CExpressionBase>> LhsAsts;
                        for (const TSRef<Vst::Node>& LhsVst : LhsClause.GetChildren())
                        {
                            LhsAsts.Add(DesugarExpressionVst(*LhsVst));
                        }

                        Rhs = AddMapping(*PpcChildNode, TSRef<CExprMapTypeFormer>::New(Move(LhsAsts), Move(Rhs)));
                    }
                    else
                    {
                        Rhs = AddMapping(*PpcChildNode, TSRef<CExprArrayTypeFormer>::New(Move(Rhs)));
                    }
                }
                break;
                case PpcOp::SureCall:
                {
                    AppendGlitch(
                        PpcChildNode.Get(),
                        EDiagnostic::ErrSemantic_Unsupported,
                        CUTF8String("Unsupported: prefix'()' not supported yet"));
                    
                    Rhs = AddMapping(*PpcChildNode, TSRef<CExprError>::New());
                }
                break;
                default: ULANG_UNREACHABLE();
                }
            }
        }

        return Move(Rhs);
    }
    
    TSRef<CExpressionBase> DesugarIdentifier(const Vst::Identifier& IdentifierNode, TSPtr<CExpressionBase>&& Context = nullptr)
    {
        if (IdentifierNode.IsQualified())
        {
            if (IdentifierNode.GetChildCount() > 1)
            {
                AppendGlitch(IdentifierNode.GetChildren()[0], EDiagnostic::ErrSemantic_ExpectedSingleExpression, "Only one qualifying expression is allowed.");
                return AddMapping(*IdentifierNode.GetChildren()[0], TSRef<CExprError>::New());
            }

            const CSymbol Symbol = VerifyAddSymbol(&IdentifierNode, IdentifierNode.GetSourceText());
            TSRef<CExpressionBase> QualifierAst = DesugarExpressionVst(*IdentifierNode.GetQualification());
            return AddMapping(IdentifierNode, TSRef<CExprIdentifierUnresolved>::New(Symbol, Move(Context), Move(QualifierAst)));
        }
        else
        {
            const CSymbol Symbol = VerifyAddSymbol(&IdentifierNode, IdentifierNode.GetSourceText());
            return AddMapping(IdentifierNode, TSRef<CExprIdentifierUnresolved>::New(Symbol, Move(Context)));
        }
    }
    
    TSRef<CExpressionBase> DesugarFlowIf(const Vst::FlowIf& IfNode)
    {
        // All `if` nodes will have clause block children though they may be empty
        // The simplest forms that can get past the parser (though will have semantic issues) is `if:` and `if ():`
        const int32_t         NumChildren = IfNode.GetChildCount();
        const Vst::NodeArray& Clauses     = IfNode.GetChildren();

        // First, desugar the optional final else clause.
        TSPtr<CExpressionBase> Result;
        int32_t Index = NumChildren - 1;
        if (Clauses[Index]->GetTag<Vst::FlowIf::ClauseTag>() == Vst::FlowIf::ClauseTag::else_body)
        {
            Result = DesugarClauseAsCodeBlock(Clauses[Index]->As<Vst::Clause>());
            --Index;
        }
        
        // Desugar pairs of clauses into nested CExprIf nodes.
        // Must be in this order:
        //   - if identifier   ]
        //   - condition block  |- Repeating
        //   - [then block]    ]
        //   - [else block]    -- Optional last node
        // Loop in reverse order, with the first corresponding to the outermost CExprIf.
        while(Index >= 0)
        {
            switch(Clauses[Index]->GetTag<Vst::FlowIf::ClauseTag>())
            {
            case Vst::FlowIf::ClauseTag::if_identifier:
            {
                Index--;
                break;
            }
            case Vst::FlowIf::ClauseTag::then_body:
            {
                ULANG_ASSERTF(Index > 1, "Clause of FlowIf node is unexpectedly a then clause");
                if (Clauses[Index-1]->GetTag<Vst::FlowIf::ClauseTag>() != Vst::FlowIf::ClauseTag::condition)
                {
                    AppendGlitch(Clauses[Index-1], EDiagnostic::ErrSemantic_MalformedConditional, "Expected condition.");
                    TSRef<CExprError> ErrorNode = TSRef<CExprError>::New();
                    ErrorNode->AppendChild(Move(Result));
                    Result = Move(ErrorNode);
                    --Index;
                }
                else
                {
                    const Vst::Clause& Condition = Clauses[Index-1]->As<Vst::Clause>();
                    TSRef<CExprCodeBlock> ConditionCodeBlock = DesugarClauseAsCodeBlock(Condition);

                    const Vst::Clause& ThenClause = Clauses[Index]->As<Vst::Clause>();
                    TSRef<CExprCodeBlock> ThenCodeBlock = DesugarClauseAsCodeBlock(ThenClause);
            
                    Result = TSRef<CExprIf>::New(Move(ConditionCodeBlock), Move(ThenCodeBlock), Move(Result));
                    Index -= 2;
                }

                break;
            }
            case Vst::FlowIf::ClauseTag::condition:
            {
                ULANG_ASSERTF(Index > 0, "Clause of FlowIf node is unexpectedly a condition clause");
                ULANG_ASSERTF(Clauses[Index-1]->GetTag<Vst::FlowIf::ClauseTag>() == Vst::FlowIf::ClauseTag::if_identifier, "if_identifier clause of FlowIf should precede the condition clause");
                const Vst::Clause& Condition = Clauses[Index]->As<Vst::Clause>();
                TSRef<CExprCodeBlock> ConditionCodeBlock = DesugarClauseAsCodeBlock(Condition);
                --Index;

                Result = TSRef<CExprIf>::New(Move(ConditionCodeBlock), nullptr, Move(Result));

                break;
            }
            case Vst::FlowIf::ClauseTag::else_body:
            {
                AppendGlitch(
                    Clauses[Index],
                    EDiagnostic::ErrSemantic_MalformedConditional,
                    "Expected then clause or condition while parsing `if`.");
                TSRef<CExprError> ErrorNode = TSRef<CExprError>::New();
                ErrorNode->AppendChild(Move(Result));
                Result = Move(ErrorNode);
                Index -= 2;
                break;
            }
            default:
                ULANG_UNREACHABLE();
            };
        };
        
        return AddMapping(IfNode, Move(Result.AsRef()));
    }
    
    TSRef<CExpressionBase> DesugarIntLiteral(const Vst::IntLiteral& IntLiteralNode)
    {
        // We look back at the mapped Vst node during analysis
        return AddMapping(IntLiteralNode, TSRef<CExprNumber>::New());
    }

    TSRef<CExpressionBase> DesugarFloatLiteral(const Vst::FloatLiteral& FloatLiteralNode)
    {
        return AddMapping(FloatLiteralNode, TSRef<CExprNumber>::New());
    }

    TSRef<CExpressionBase> DesugarCharLiteral(const Vst::CharLiteral& CharLiteralNode)
    {
        const CUTF8String& String = CharLiteralNode.GetSourceText();
        if (String.ByteLen() == 0)
        {
            AppendGlitch(&CharLiteralNode, EDiagnostic::ErrSemantic_CharLiteralDoesNotContainOneChar);
            return AddMapping(CharLiteralNode, TSRef<CExprError>::New());
        }

        if (CharLiteralNode._Format == Vst::CharLiteral::EFormat::UTF8CodeUnit)
        {
            // interpret the single byte literally as a code unit
            return AddMapping(CharLiteralNode, TSRef<CExprChar>::New(String[0], CExprChar::EType::UTF8CodeUnit));
        }
        else if (CharLiteralNode._Format == Vst::CharLiteral::EFormat::UnicodeCodePoint)
        {
            // decode utf8 to unicode code point
            SUniCodePointLength CodePointAndLength;
            CodePointAndLength = CUnicode::DecodeUTF8((UTF8Char*)String.AsCString(), String.ByteLen());

            if (CodePointAndLength._ByteLengthUTF8 != uint32_t(String.ByteLen()))
            {
                AppendGlitch(&CharLiteralNode, EDiagnostic::ErrSemantic_CharLiteralDoesNotContainOneChar);
            }

            return AddMapping(CharLiteralNode, TSRef<CExprChar>::New(CodePointAndLength._CodePoint, CExprChar::EType::UnicodeCodePoint));
        }
        else
        {
            ULANG_UNREACHABLE();
        }
    }

    // The extra optional VstNode parameter is used when the string literal is created from a temporary StringLiteralNode. 
    TSRef<CExpressionBase> DesugarStringLiteral(const Vst::StringLiteral& StringLiteralNode, const Vst::Node* VstNode = nullptr)
    {
        return AddMapping(VstNode ? *VstNode : StringLiteralNode, TSRef<CExprString>::New(StringLiteralNode.GetSourceText()));
    }

    TSRef<CExpressionBase> DesugarPathLiteral(const Vst::PathLiteral& PathLiteralNode)
    {
        return AddMapping(PathLiteralNode, TSRef<CExprPath>::New(PathLiteralNode.GetSourceText()));
    }

    TSRef<CExpressionBase> DesugarInterpolatedString(const Vst::InterpolatedString& InterpolatedStringNode)
    {
        if(InterpolatedStringNode.GetChildCount() == 1)
        {
            const TSRef<Vst::Node>& PrePostCall = InterpolatedStringNode.GetChildren()[0];
            // For any empty string interpolations used to define multi-line string literals, we just elide the empty interpolation
            // invocations and form a single string literal from the splices before and after it.
            if (PrePostCall->IsA<Vst::PrePostCall>())
            {
                const Vst::NodeArray& PrePostCallChildren = PrePostCall->GetChildren();
                if (PrePostCallChildren.Num() == 2
                    && PrePostCallChildren[0]->IsA<Vst::Identifier>()
                    && PrePostCallChildren[0]->As<Vst::Identifier>().GetSourceText() == uLang::ConcatenateIdentifierText
                    && PrePostCallChildren[1]->IsA<Vst::Clause>()
                    && PrePostCallChildren[1]->GetChildCount() > 0)
                {
                    TSRefArray<Vst::Node> AccumulatedNodes;
                    AccumulatedNodes.Reserve(8); // Arbitrary choice
                    CUTF8StringBuilder AccumulatedString;
                    Verse::SLocus AccumulatedLocus = {};
                    for (const TSRef<Vst::Node>& CurNode : PrePostCallChildren[1]->GetChildren())
                    {
                        AccumulatedLocus = AccumulatedLocus.IsEmpty() ? CurNode->Whence() : AccumulatedLocus |= CurNode->Whence();
                        if (CurNode->IsA<Vst::StringLiteral>())
                        {
                            AccumulatedString.Append(CurNode->As<Vst::StringLiteral>().GetSourceText());
                        }
                        else if (CurNode->IsA<Vst::PrePostCall>()
                                 && CurNode->GetChildCount() == 2
                                 && CurNode->GetChildren()[0]->IsA<Vst::Identifier>()
                                 && CurNode->GetChildren()[0]->As<Vst::Identifier>().GetSourceText() == uLang::ToStringIdentifierText
                                 && CurNode->GetChildren()[1]->IsA<Vst::Clause>()
                                 && CurNode->GetChildren()[1]->GetChildCount() == 0)
                        {
                            continue;
                        }
                        else
                        {
                            AccumulatedNodes.Add(TSRef<Vst::StringLiteral>::New(AccumulatedLocus, AccumulatedString.MoveToString()));
                            AccumulatedString.Reset();
                            AccumulatedLocus.Reset();
                            AccumulatedNodes.Add(CurNode);
                        }
                    }
                    if (!AccumulatedString.IsEmpty())
                    {
                        AccumulatedNodes.Add(TSRef<Vst::StringLiteral>::New(AccumulatedLocus, AccumulatedString.MoveToString()));
                    }

                    // This indicates that only empty interpolation invocations were present, in which case we coalesce this
                    // to a single string literal.
                    if (AccumulatedNodes.Num() == 1 && AccumulatedNodes[0]->IsA<Vst::StringLiteral>())
                    {
                        // Need to use InterpolatedStringNode as Vst, since there is nothing keeping the one used to create the literal alive
                        return DesugarStringLiteral({AccumulatedNodes[0]->As<Vst::StringLiteral>()}, &InterpolatedStringNode);
                    }
                }
            }

            return DesugarExpressionVst(*InterpolatedStringNode.GetChildren()[0]);
        }

        AppendGlitch(&InterpolatedStringNode, EDiagnostic::ErrSemantic_Unsupported, "The interpolation syntax is not supported.");
        return AddMapping(InterpolatedStringNode, TSRef<CExprError>::New());
    }

    TSRef<CExpressionBase> DesugarLambda(const Vst::Lambda& LambdaVst)
    {
        TSRef<CExpressionBase> DomainAst = DesugarExpressionVst(*LambdaVst.GetChildren()[0]);
        TSRef<CExpressionBase> RangeAst = DesugarClauseAsExpression(*LambdaVst.GetChildren()[1]);
        return AddMapping(LambdaVst, TSRef<CExprFunctionLiteral>::New(Move(DomainAst), Move(RangeAst)));
    }
    
    TSRef<CExpressionBase> DesugarControl(const Vst::Control& ControlNode)
    {
        switch (ControlNode._Keyword)
        {
        case Vst::Control::EKeyword::Return:
            {
                TSPtr<CExpressionBase> ResultAst;
                if (ControlNode.GetChildCount() == 1)
                {
                    ResultAst = DesugarExpressionVst(*ControlNode.GetReturnExpression());
                }
                else if (ControlNode.GetChildCount() > 1)
                {
                    AppendGlitch(
                        &ControlNode,
                        EDiagnostic::ErrSemantic_UnexpectedNumberOfArguments,
                        "`return` may only have a single sub-expression when returning a result.");
                    return AddMapping(ControlNode, TSRef<CExprError>::New());
                }

                return AddMapping(ControlNode, TSRef<CExprReturn>::New(Move(ResultAst)));
            }
            break;
        case Vst::Control::EKeyword::Break:
            {
                if (ControlNode.GetChildCount() > 0)
                {
                    AppendGlitch(
                        &ControlNode,
                        EDiagnostic::ErrSemantic_UnexpectedNumberOfArguments,
                        "`break` may not have any sub-expressions - it does not return a result.");
                    return AddMapping(ControlNode, TSRef<CExprError>::New());
                }
                return AddMapping(ControlNode, TSRef<CExprBreak>::New());
            }
            break;
        case Vst::Control::EKeyword::Yield:
            AppendGlitch(&ControlNode, EDiagnostic::ErrSemantic_Unimplemented);
            return AddMapping(ControlNode, TSRef<CExprError>::New());
            break;
        case Vst::Control::EKeyword::Continue:
            AppendGlitch(&ControlNode, EDiagnostic::ErrSemantic_Unimplemented);
            return AddMapping(ControlNode, TSRef<CExprError>::New());
            break;
        default: 
            return AddMapping(ControlNode, TSRef<CExprError>::New());
        }  
    }
    
    TSRef<CExpressionBase> DesugarMacro(const Vst::Macro& MacroVst)
    {
        const int32_t NumMacroChildren = MacroVst.GetChildCount();

        const Vst::Node& MacroNameVst = *MacroVst.GetName();
        TSRef<CExprMacroCall> MacroCallAst = TSRef<CExprMacroCall>::New(DesugarExpressionVst(MacroNameVst), NumMacroChildren);

        // Populate the clauses in the macro
        for (int32_t i = 1; i < NumMacroChildren; i += 1)
        {
            Vst::Node& ThisMacroChild = *MacroVst.GetChildren()[i];
            if (!ThisMacroChild.IsA<Vst::Clause>())
            {
                AppendGlitch(MacroVst.GetChildren()[i].Get(), EDiagnostic::ErrSemantic_MalformedMacro,
                    "Malformed macro: expected a macro clause");
            }
            else
            {
                // Add clause and its children to the macro

                Vst::Clause& ThisClause = ThisMacroChild.As<Vst::Clause>();
                const int32_t NumClauseChildren = ThisClause.GetChildCount();

                // Don't allow attributes on macro clauses, since they'll otherwise be thrown away at this point.
                if (ThisClause.HasAttributes())
                {
                    AppendGlitch(ThisClause.GetAux()->GetChildren()[0].Get(), EDiagnostic::ErrSemantic_AttributeNotAllowed);
                }

                const EMacroClauseTag ClauseTag = [&ThisClause, this]() {
                    using res_t = vsyntax::res_t;
                    switch (ThisClause.GetTag<res_t>())
                    {
                    case res_t::res_none: return EMacroClauseTag::None;
                    case res_t::res_of: return EMacroClauseTag::Of;
                    case res_t::res_do: return EMacroClauseTag::Do;

                    case res_t::res_if:
                    case res_t::res_else:
                    case res_t::res_upon:
                    case res_t::res_where:
                    case res_t::res_catch:
                    case res_t::res_then:
                    case res_t::res_until:
                    case res_t::res_return:
                    case res_t::res_yield:
                    case res_t::res_break:
                    case res_t::res_continue:
                    case res_t::res_at:
                    case res_t::res_var:
                    case res_t::res_set:
                    case res_t::res_and:
                    case res_t::res_or:
                    case res_t::res_not:
                        AppendGlitch(&ThisClause, EDiagnostic::ErrSemantic_MalformedMacro,
                            "Malformed macro: reserved word invalid in macro clause");
                        return EMacroClauseTag::None;
                    case res_t::res_max:
                    default:
                        AppendGlitch(&ThisClause, EDiagnostic::ErrSemantic_MalformedMacro,
                            "Malformed macro: Unknown keyword");
                        return EMacroClauseTag::None;
                    }
                }();

                TArray<TSRef<CExpressionBase>> ClauseExprs;
                ClauseExprs.Reserve(NumClauseChildren);

                for (const TSRef<Vst::Node>& ClauseChildVst : ThisClause.GetChildren())
                {
                    if(!ClauseChildVst->IsA<Vst::Comment>())
                    {
                        ClauseExprs.Add(DesugarExpressionVst(*ClauseChildVst));
                    }
                }

                MacroCallAst->AppendClause(CExprMacroCall::CClause(ClauseTag, ThisClause.GetForm(), Move(ClauseExprs)));

                if (ThisClause.HasAttributes())
                {
                    MacroCallAst->_Attributes += DesugarAttributes(ThisClause.GetAux()->GetChildren());
                }
            }
        }

        return AddMapping(MacroVst, Move(MacroCallAst));
    }

    TSRefArray<CExpressionBase> DesugarExpressionList(const Vst::NodeArray& Expressions)
    {
        TSRefArray<CExpressionBase> DesugaredExpressions;
        for (const Vst::Node* Child : Expressions)
        {
            // Ignore comments in the subexpression list.
            if (!Child->IsA<Vst::Comment>())
            {
                DesugaredExpressions.Add(DesugarExpressionVst(*Child));
            }
        }
        return DesugaredExpressions;
    }

    TSRef<CExprMakeTuple> WrapExpressionListInTuple(TSRefArray<CExpressionBase>&& Expressions, const Vst::Node& OriginNode, bool bReciprocalVstMapping)
    {
        TSRef<CExprMakeTuple> Tuple = TSRef<CExprMakeTuple>::New(Expressions.Num());
        Tuple->SetSubExprs(Move(Expressions));
        if (bReciprocalVstMapping)
        {
            OriginNode.AddMapping(Tuple.Get());
        }
        else
        {
            Tuple->SetNonReciprocalMappedVstNode(&OriginNode);
        }
        return Tuple;
    }

    TSRef<CExprCodeBlock> WrapExpressionListInCodeBlock(TSRefArray<CExpressionBase>&& Expressions, const Vst::Node& OriginNode, bool bReciprocalVstMapping)
    {
        TSRef<CExprCodeBlock> Block = TSRef<CExprCodeBlock>::New(Expressions.Num());
        Block->SetSubExprs(Move(Expressions));
        if (bReciprocalVstMapping)
        {
            OriginNode.AddMapping(Block.Get());
        }
        else
        {
            Block->SetNonReciprocalMappedVstNode(&OriginNode);
        }
        return Block;
    }

    TSRef<CExpressionBase> DesugarExpressionListAsExpression(const Vst::Node& Node, Vst::Clause::EForm Form, bool bReciprocalVstMapping = true)
    {
        TSRefArray<CExpressionBase> DesugaredChildren = DesugarExpressionList(Node.GetChildren());
        if (DesugaredChildren.Num() == 1)
        {
            // If this is a single expression, return it directly.
            return DesugaredChildren[0];
        }
        else if (Form == Vst::Clause::EForm::NoSemicolonOrNewline)
        {
            // If this is an empty or comma separated list, create a tuple for the subexpressions.
            return WrapExpressionListInTuple(Move(DesugaredChildren), Node, bReciprocalVstMapping);
        }
        else
        {
            // Otherwise, create a code block for the subexpressions.
            return WrapExpressionListInCodeBlock(Move(DesugaredChildren), Node, bReciprocalVstMapping);
        }
    }

    TSRef<CExprCodeBlock> DesugarClauseAsCodeBlock(const Vst::Clause& Clause)
    {
        TSRefArray<CExpressionBase> DesugaredChildren = DesugarExpressionList(Clause.GetChildren());
        if (DesugaredChildren.Num() > 1 && Clause.GetForm() == Vst::Clause::EForm::NoSemicolonOrNewline)
        {
            // If there are multiple comma separated subexpressions, wrap them in a CExprMakeTuple that is
            // the sole subexpression of the resulting code block.
            TSRef<CExprMakeTuple> Tuple = WrapExpressionListInTuple(Move(DesugaredChildren), Clause, false);
            DesugaredChildren = {Tuple};
        }

        return WrapExpressionListInCodeBlock(Move(DesugaredChildren), Clause, true);
    }

    TSRef<CExpressionBase> DesugarParens(const Vst::Parens& Parens)
    {
        return DesugarExpressionListAsExpression(Parens, Parens.GetForm());
    }

    TSRef<CExpressionBase> DesugarCommas(const Vst::Commas& Commas)
    {
        TSRefArray<CExpressionBase> DesugaredChildren = DesugarExpressionList(Commas.GetChildren());
        ULANG_ASSERT(DesugaredChildren.Num() > 1);

        TSRef<CExprMakeTuple> Tuple = WrapExpressionListInTuple(Move(DesugaredChildren), Commas, true);

        // NOTE: (yiliang.siew) This preserves the mistake we shipped in `28.20` where mixed use of separators in
        // archetype instantiations wrapped the sub-expressions into an implicit `block`, but in other places, it
        // did not.
        if (!_Package
            || _Package->_EffectiveVerseVersion >= Verse::Version::DontMixCommaAndSemicolonInBlocks
            || VerseFN::UploadedAtFNVersion::EnforceDontMixCommaAndSemicolonInBlocks(_Package->_UploadedAtFNVersion))
        {
            return Tuple;
        }
        else
        {
            // NOTE: (yiliang.siew) This preserves the old legacy behaviour of potentially wrapping the expression in a
            // code block/tuple/returning a single expression directly.
            // This has implications on scoping (since blocks create their own scope and tuples do not) and how
            // definitions that might previously not have conflicted would conflict if we were not to do this.
            AppendGlitch(&Commas, EDiagnostic::WarnSemantic_StricterErrorCheck,
                "Mixing commas with semicolons/newlines in a clause wraps the comma-separated subexpressions in a 'block{...}' "
                "in the version of Verse you are targeting, but this behavior will change in a future version of Verse. You "
                "can preserve the current behavior in future versions of Verse by wrapping the comma-separated subexpressions "
                "in a block{...}.\n"
                "For example, instead of writing this:\n"
                "    A\n"
                "    B,\n"
                "    C\n"
                "Write this:\n"
                "    A\n"
                "    block:\n"
                "        B,\n"
                "        C");
            return WrapExpressionListInCodeBlock({ Move(Tuple) }, Commas, false);
        }
    }

    TSRef<CExpressionBase> DesugarPlaceholder(const Vst::Placeholder& PlaceholderNode)
    {
        return AddMapping(PlaceholderNode, TSRef<CExprPlaceholder>::New());
    }

    TSRef<CExpressionBase> DesugarEscape(const Vst::Escape& EscapeNode)
    {
        AppendGlitch(&EscapeNode, EDiagnostic::ErrSemantic_Unsupported, "Escaped syntax is not yet supported.");
        return AddMapping(EscapeNode, TSRef<CExprError>::New());
    }

    CSymbol VerifyAddSymbol(const Vst::Node* VstNode, const CUTF8StringView& Text)
    {
        TOptional<CSymbol> OptionalSymbol = _Symbols.Add(Text);
        if (!OptionalSymbol.IsSet())
        {
            AppendGlitch(VstNode, EDiagnostic::ErrSemantic_TooLongIdentifier);
            OptionalSymbol = _Symbols.Add(Text.SubViewBegin(CSymbolTable::MaxSymbolLength-1));
            ULANG_ASSERTF(OptionalSymbol.IsSet(), "Truncated name is to long");
        }
        return OptionalSymbol.GetValue();
    }

    template<typename... ResultArgsType>
    void AppendGlitch(const Vst::Node* VstNode, ResultArgsType&&... ResultArgs)
    {
        _Diagnostics.AppendGlitch(SGlitchResult(uLang::ForwardArg<ResultArgsType>(ResultArgs)...), SGlitchLocus(VstNode));
    }
    
    TSRef<CExpressionBase> AddMapping(const Vst::Node& VstNode, TSRef<CExpressionBase>&& AstNode)
    {
        VstNode.AddMapping(AstNode.Get());
        return Move(AstNode);
    }
    
    TArray<SAttribute> DesugarAttributes(const TArray<TSRef<Vst::Node>>& AttributeVsts)
    {
        auto FilterAcceptAll = [](const Vst::Node&)->bool { return true; };

        return DesugarAttributesFiltered(AttributeVsts, FilterAcceptAll);
    }

    template<typename TPredicate>
    TArray<SAttribute> DesugarAttributesFiltered(const TArray<TSRef<Vst::Node>>& AttributeVsts, TPredicate FilterPredicate)
    {
        TArray<SAttribute> AttributeAsts;
        for (const TSRef<Vst::Node>& AttributeWrapperVst : AttributeVsts)
        {
            // the actual attribute node is wrapped in a dummy Clause (used to preserve comments
            // in the VST and tell us whether it's a prepend attribute or append specifier)
            ULANG_ASSERTF(AttributeWrapperVst->IsA<Vst::Clause>(), "attribute nodes are expected to be wrapped in a dummy Clause node with a single child");
            ULANG_ASSERTF(AttributeWrapperVst->GetChildCount() == 1, "attribute nodes are expected to be wrapped in a dummy Clause node with a single child");

            const Vst::Clause& AttributeClauseVst = AttributeWrapperVst->As<Vst::Clause>();

            SAttribute::EType AttributeType = SAttribute::EType::Attribute;
            switch(AttributeClauseVst.GetForm())
            {
            case Vst::Clause::EForm::IsPrependAttributeHolder:
                AttributeType = SAttribute::EType::Attribute;
                break;
            case Vst::Clause::EForm::IsAppendAttributeHolder:
                AttributeType = SAttribute::EType::Specifier;
                break;

            case Vst::Clause::EForm::Synthetic:
            case Vst::Clause::EForm::NoSemicolonOrNewline:
            case Vst::Clause::EForm::HasSemicolonOrNewline:
            default:
                ULANG_UNREACHABLE();
                break;
            }

            const Vst::Node& AttributeExprVst = *AttributeClauseVst.GetChildren()[0];

            if (FilterPredicate(AttributeExprVst))
            {
                TSRef<CExpressionBase> AttributeExprAst = DesugarExpressionVst(AttributeExprVst);

                SAttribute AttributeAst{ AttributeExprAst, AttributeType };
                AttributeAsts.Add(Move(AttributeAst));
            }
        }
        return AttributeAsts;
    }

    TSRef<CAstNode> DesugarVst(const Vst::Node& VstNode)
    {
        const Vst::NodeType NodeType = VstNode.GetElementType();
        switch (NodeType)
        {
        case Vst::NodeType::Project:                    return DesugarProject(static_cast<const Vst::Project&>(VstNode));
        case Vst::NodeType::Package:                    return DesugarPackage(static_cast<const Vst::Package&>(VstNode));
        case Vst::NodeType::Module:                     return DesugarModule(static_cast<const Vst::Module&>(VstNode));
        case Vst::NodeType::Snippet:                    return DesugarSnippet(static_cast<const Vst::Snippet&>(VstNode));
        case Vst::NodeType::Where:                      return DesugarWhere(static_cast<const Vst::Where&>(VstNode));
        case Vst::NodeType::Mutation:                   return DesugarMutation(static_cast<const Vst::Mutation&>(VstNode));
        case Vst::NodeType::Definition:                 return DesugarDefinition(static_cast<const Vst::Definition&>(VstNode));
        case Vst::NodeType::Assignment:                 return DesugarAssignment(static_cast<const Vst::Assignment&>(VstNode));
        case Vst::NodeType::BinaryOpLogicalOr:
        case Vst::NodeType::BinaryOpLogicalAnd:         return DesugarBinaryOpLogicalAndOr(VstNode);
        case Vst::NodeType::PrefixOpLogicalNot:         return DesugarPrefixOpLogicalNot(static_cast<const Vst::PrefixOpLogicalNot&>(VstNode));
        case Vst::NodeType::BinaryOpCompare:            return DesugarBinaryOpCompare(static_cast<const Vst::BinaryOpCompare&>(VstNode));
        case Vst::NodeType::BinaryOpAddSub:             return DesugarBinaryOp(static_cast<const Vst::BinaryOpAddSub&>(VstNode));
        case Vst::NodeType::BinaryOpMulDivInfix:        return DesugarBinaryOp(static_cast<const Vst::BinaryOpMulDivInfix&>(VstNode));
        case Vst::NodeType::BinaryOpRange:              return DesugarBinaryOpRange(static_cast<const Vst::BinaryOpRange&>(VstNode));
        case Vst::NodeType::BinaryOpArrow:              return DesugarBinaryOpArrow(static_cast<const Vst::BinaryOpArrow&>(VstNode));
        case Vst::NodeType::TypeSpec:                   return DesugarTypeSpec(static_cast<const Vst::TypeSpec&>(VstNode));
        case Vst::NodeType::PrePostCall:                return DesugarPrePostCall(static_cast<const Vst::PrePostCall&>(VstNode));
        case Vst::NodeType::Identifier:                 return DesugarIdentifier(static_cast<const Vst::Identifier&>(VstNode));
        case Vst::NodeType::Operator:                   goto unexpected_node_type; 
        case Vst::NodeType::FlowIf:                     return DesugarFlowIf(static_cast<const Vst::FlowIf&>(VstNode));
        case Vst::NodeType::IntLiteral:                 return DesugarIntLiteral(static_cast<const Vst::IntLiteral&>(VstNode));
        case Vst::NodeType::FloatLiteral:               return DesugarFloatLiteral(static_cast<const Vst::FloatLiteral&>(VstNode));
        case Vst::NodeType::CharLiteral:                return DesugarCharLiteral(static_cast<const Vst::CharLiteral&>(VstNode));
        case Vst::NodeType::StringLiteral:              return DesugarStringLiteral(static_cast<const Vst::StringLiteral&>(VstNode));
        case Vst::NodeType::PathLiteral:                return DesugarPathLiteral(static_cast<const Vst::PathLiteral&>(VstNode));
        case Vst::NodeType::InterpolatedString:         return DesugarInterpolatedString(static_cast<const Vst::InterpolatedString&>(VstNode));
        case Vst::NodeType::Lambda:                     return DesugarLambda(static_cast<const Vst::Lambda&>(VstNode));
        case Vst::NodeType::Control:                    return DesugarControl(static_cast<const Vst::Control&>(VstNode));
        case Vst::NodeType::Macro:                      return DesugarMacro(VstNode.As<Vst::Macro>());
        case Vst::NodeType::Clause:                     goto unexpected_node_type;
        case Vst::NodeType::Parens:                     return DesugarParens(static_cast<const Vst::Parens&>(VstNode));
        case Vst::NodeType::Commas:                     return DesugarCommas(static_cast<const Vst::Commas&>(VstNode));
        case Vst::NodeType::Placeholder:                return DesugarPlaceholder(static_cast<const Vst::Placeholder&>(VstNode));
        case Vst::NodeType::ParseError:                 goto unexpected_node_type;
        case Vst::NodeType::Escape:                     return DesugarEscape(static_cast<const Vst::Escape&>(VstNode));
        case Vst::NodeType::Comment:                    goto unexpected_node_type;
        default:
        unexpected_node_type:
            ULANG_ENSUREF(false, "Did not expect this node type (%s) in an expression context.", VstNode.GetElementName());
            return AddMapping(VstNode, TSRef<CExprError>::New()); // Return something so semantic analysis can continue
        }
    }

    TSRef<CExpressionBase> DesugarExpressionVst(const Vst::Node& VstNode)
    {
        TSRef<CAstNode> AstNode = DesugarVst(VstNode);
        if (AstNode->AsExpression())
        {
            TSRef<CExpressionBase> Expression = Move(AstNode.As<CExpressionBase>());
            if (VstNode.HasAttributes())
            {
                Expression->_Attributes = DesugarAttributes(VstNode.GetAux()->GetChildren());
            }
            return Move(Expression);
        }
        else
        {
            AppendGlitch(&VstNode, EDiagnostic::ErrSyntax_ExpectedExpression);
            TSRef<CExprError> ErrorExpr = TSRef<CExprError>::New();
            ErrorExpr->AppendChild(Move(AstNode));
            return AddMapping(VstNode, Move(ErrorExpr));
        }
    }

    CSymbolTable& _Symbols;
    CDiagnostics& _Diagnostics;
    CAstPackage* _Package = nullptr;
};
}

namespace uLang
{
TSRef<CAstProject> DesugarVstToAst(const Verse::Vst::Project& VstProject, CSymbolTable& Symbols, CDiagnostics& Diagnostics)
{
    CDesugarerImpl DesugarerImpl(Symbols, Diagnostics);
    return DesugarerImpl.DesugarProject(VstProject);
}
}
