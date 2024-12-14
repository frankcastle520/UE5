// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/SemanticAnalyzer/DigestGenerator.h"
#include "uLang/Common/Algo/Cases.h"
#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/Semantics/Attributable.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/ModuleAlias.h"
#include "uLang/Semantics/ScopedAccessLevelType.h"
#include "uLang/Semantics/SemanticClass.h"
#include "uLang/Semantics/SemanticEnumeration.h"
#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/SemanticInterface.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/TypeAlias.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Syntax/VstNode.h"
#include "uLang/Syntax/vsyntax_types.h"

using Verse::NullWhence;

namespace uLang
{

/// Helper class that does the actual digest generation
class CDigestGeneratorImpl
{
    static constexpr int32_t NumNewLinesForSpacing = 2;

public:

    CDigestGeneratorImpl(
        const CSemanticProgram& Program,
        const CAstPackage& Package,
        const TSRef<CDiagnostics>& Diagnostics,
        const CUTF8String* Notes,
        bool bIncludeInternalDefinitions,
        bool bIncludeEpicInternalDefinitions)
        : _Program(Program)
        , _Package(Package)
        , _Diagnostics(Diagnostics)
        , _bIncludeInternalDefinitions(bIncludeInternalDefinitions)
        , _bIncludeEpicInternalDefinitions(bIncludeEpicInternalDefinitions)
        , _CurrentModule(Package._RootModule->GetModule())
        , _CurrentGlitchAst(nullptr)
        , _Underscore(Program.GetSymbols()->AddChecked("__")) // temporarily, single underscore is a reserved name
        , _ParameterizedMaterialSymbol(Program.GetSymbols()->AddChecked("parameterized_material"))
        , _AssetsSymbol(Program.GetSymbols()->AddChecked("Assets"))
        , _Notes(Notes)
    {
    }

    bool Generate(CUTF8String& OutDigestCode) const
    {
        using namespace Verse::Vst;
        TSRef<Snippet> DigestSnippet = TSRef<Snippet>::New(_Package._Name);
        _Usings.Reset();

        // Do the actual generation work
        GenerateForScope(*_Package._RootModule->GetModule(), DigestSnippet);

        // Prepend a list of required using declarations
        for (const CUTF8String& UsingPath : _Usings)
        {
            TSRef<Macro> UsingMacro = TSRef<Macro>::New(
                NullWhence(),
                TSRef<Identifier>::New(CUTF8String("using"), NullWhence()),
                ClauseArray{ TSRef<Clause>::New(
                    TSRef<PathLiteral>::New(UsingPath, NullWhence()).As<Node>(),
                    NullWhence(),
                    Clause::EForm::NoSemicolonOrNewline) });
            UsingMacro->SetNewLineAfter(true);
            DigestSnippet->AppendChildAt(UsingMacro, 0);
        }
        // Finally, generate the code
        OutDigestCode = Verse::PrettyPrintVst(DigestSnippet);

        // If digest is empty, make it clear there was no error but there was in fact nothing to export
        if (OutDigestCode.IsEmpty())
        {
            OutDigestCode = "# This digest intentionally left blank.\n";
        }

        if (_Notes && !_Notes->IsEmpty())
        {
            OutDigestCode = *_Notes + "\n" + OutDigestCode;
        }

        return !_Diagnostics->HasErrors();
    }

private:

    bool GenerateForScope(const CLogicalScope& Scope, const TSRef<Verse::Vst::Node>& Parent) const
    {
        bool bGeneratedAnything = false;
        for (const TSRef<CDefinition>& Definition : Scope.GetDefinitions())
        {
            switch (Definition->GetKind())
            {
            case CDefinition::EKind::Class: 
            {
                const CClass* cls = static_cast<const CClass*>(&Definition->AsChecked<CClassDefinition>());
                if (cls->IsSubclassOf(*_Program._scopedClass))
                {
                    bGeneratedAnything |= GenerateForScopedAccessLevel(Definition->AsChecked<CScopedAccessLevelDefinition>(), Parent);
                }
                else
                {
                    bGeneratedAnything |= GenerateDefinitionForClass(Definition->AsChecked<CClassDefinition>(), Parent);
                }
            }
            break;
            case CDefinition::EKind::Data: bGeneratedAnything |= GenerateForDataDefinition(Definition->AsChecked<CDataDefinition>(), Parent, IsParameterizedMaterial(Scope)); break;
            case CDefinition::EKind::Enumeration: bGeneratedAnything |= GenerateForEnumeration(Definition->AsChecked<CEnumeration>(), Parent); break;
            case CDefinition::EKind::Enumerator: bGeneratedAnything |= GenerateForEnumerator(Definition->AsChecked<CEnumerator>(), Parent); break;
            case CDefinition::EKind::Function: bGeneratedAnything |= GenerateForFunction(Definition->AsChecked<CFunction>(), Parent); break;
            case CDefinition::EKind::Interface: bGeneratedAnything |= GenerateDefinitionForInterface(Definition->AsChecked<CInterface>(), Parent); break;
            case CDefinition::EKind::Module: bGeneratedAnything |= GenerateForModule(Definition->AsChecked<CModule>(), Parent); break;
            case CDefinition::EKind::ModuleAlias: bGeneratedAnything |= GenerateForModuleAlias(Definition->AsChecked<CModuleAlias>(), Parent); break;
            case CDefinition::EKind::TypeAlias: bGeneratedAnything |= GenerateForTypeAlias(Definition->AsChecked<CTypeAlias>(), Parent); break;
            case CDefinition::EKind::TypeVariable: /* TODO */ break;
            default: ULANG_UNREACHABLE();
            }
        }
        // NOTE: (yiliang.siew) So that things appear nicely in digests, we do not set extra newlines
        // on the last definition so that the parent definition can handle adding the newline instead
        // of "doubling up" on newlines - since we want to have consistent spacing between definitions.
        if (bGeneratedAnything)
        {
            const TSRef<Verse::Vst::Node>& GeneratedDefinition = Parent->AccessChildren().Last();
            const int32_t NumNewLinesAfter = GeneratedDefinition->NumNewLinesAfter() - NumNewLinesForSpacing;
            GeneratedDefinition->SetNumNewLinesAfter(NumNewLinesAfter < 0 ? 0 : NumNewLinesAfter);
        }

        return bGeneratedAnything;
    }

    bool IsParameterizedMaterial(const CLogicalScope& Scope) const
    {
        if (const CTypeBase* Type = Scope.ScopeAsType())
        {
            return IsParameterizedMaterial(*Type);
        }
        return false;
    }

    bool IsParameterizedMaterial(const CTypeBase& Type) const
    {
        const CClass* Class = Type.GetNormalType().AsNullable<CClass>();
        if (!Class)
        {
            return false;
        }
        for (const CClass* I = Class; I; I = I->_Superclass)
        {
            if (I->Definition()->GetName() != _ParameterizedMaterialSymbol)
            {
                continue;
            }
            const CModule* Module = I->GetModule();
            if (!Module)
            {
                continue;
            }
            if (Module->GetName() != _AssetsSymbol)
            {
                continue;
            }
            const CAstPackage* Package = Module->GetPackage();
            if (!Package)
            {
                continue;
            }
            if (Module->GetParentScope() != Package->_RootModule->GetModule())
            {
                continue;
            }
            if (Package->_VersePath != "/Verse.org")
            {
                continue;
            }
            return true;
        }
        return false;
    }

    bool GenerateForModule(const CModule& Module, const TSRef<Verse::Vst::Node>& Parent) const
    {
        using namespace Verse::Vst;

        if (!ShouldGenerate(Module, false))
        {
            return false;
        }

        TGuardValue<const CModule*> CurrentModuleGuard(_CurrentModule, &Module);

        TSRef<Clause> InnerClause = TSRef<Clause>::New(NullWhence(), Clause::EForm::HasSemicolonOrNewline);

        if (!GenerateForScope(Module, InnerClause))
        {
            // TODO: (yiliang.siew) This hack is so that we don't produce module definitions for modules whose clauses
            // are empty. We should probably just always prune modules that have no significant children (i.e. comments, empty modules, etc.).
            // This flag is always set for the asset manifest.
            if (_Package._bTreatModulesAsImplicit)
            {
                return false;
            }
            const bool bModuleHasPartInThisPackage = Module.GetParts().ContainsByPredicate([this](const CModulePart* ModulePart){return ModulePart->GetIrPackage() == &_Package;});
            if (!bModuleHasPartInThisPackage)
            {
                return false;
            }
        }

        // Generate definition for this module
        InnerClause->SetNewLineAfter(true);
        TSRef<Identifier> Name = TSRef<Identifier>::New(Module.AsNameStringView(), NullWhence());
        if (!Module._Qualifier.IsUnspecified())
        {
            TSRef<Node> Qualifier = GenerateForQualifier(Module._Qualifier);
            Name->AppendChild(Qualifier);
        }
        TSRef<Definition> DefinitionVst = TSRef<Definition>::New(
            NullWhence(),
            Name,
            TSRef<Macro>::New(
                NullWhence(),
                TSRef<Identifier>::New("module", NullWhence()),
                ClauseArray{ InnerClause }));

        const CUTF8String ModuleImportPath = Module.GetScopePath('/', uLang::CScope::EPathMode::PrefixSeparator).AsCString();
        const CUTF8StringView View(ModuleImportPath.ToStringView());
        const CUTF8StringView LocalHost = "/localhost";
        if (!View.StartsWith(LocalHost))
        {
            // Create a convenience full path comment for the module
            TSRef<Comment> ImportPathComment = TSRef<Comment>::New(
                Comment::EType::line,
                CUTF8String("# Module import path: %s", *ModuleImportPath),
                NullWhence());
            ImportPathComment->SetNewLineAfter(true);
            DefinitionVst->AppendPrefixComment(ImportPathComment);
        }

        DefinitionVst->SetNumNewLinesAfter(NumNewLinesForSpacing);
        GenerateForAttributes(Module, Name, DefinitionVst);
        Parent->AppendChild(DefinitionVst);

        return true;
    }

    bool GenerateForModuleAlias(const CModuleAlias& ModuleAlias, const TSRef<Verse::Vst::Node>& Parent) const
    {
        using namespace Verse::Vst;

        if (!ShouldGenerate(ModuleAlias, true))
        {
            return false;
        }

        TGuardValue<const CAstNode*> GlitchAstGuard(_CurrentGlitchAst, ModuleAlias.GetAstNode());

        TSRef<Identifier> Name = TSRef<Identifier>::New(ModuleAlias.AsNameStringView(), NullWhence());
        if (!ModuleAlias._Qualifier.IsUnspecified())
        {
            TSRef<Node> Qualifier = GenerateForQualifier(ModuleAlias._Qualifier);
            Name->AppendChild(Qualifier);
        }
        TSRef<PrePostCall> Call = TSRef<PrePostCall>::New(NullWhence());
        TSRef<Identifier> ImportIdentifier = TSRef<Identifier>::New("import", NullWhence());
        ImportIdentifier->SetTag(PrePostCall::Op::Expression);
        Call->AppendChild(ImportIdentifier);
        TSRef<Clause> Arguments = TSRef<Clause>::New((uint8_t)PrePostCall::Op::SureCall, NullWhence(), Clause::EForm::NoSemicolonOrNewline);
        Call->AppendChild(Arguments);
        Arguments->AppendChild(TSRef<PathLiteral>::New(ModuleAlias.Module()->GetScopePath('/', CScope::EPathMode::PrefixSeparator), NullWhence()));
        TSRef<Definition> DefinitionVst = TSRef<Definition>::New(NullWhence(), Name, Call);
        DefinitionVst->SetNumNewLinesAfter(NumNewLinesForSpacing);
        GenerateForAttributes(ModuleAlias, Name, DefinitionVst);
        Parent->AppendChild(DefinitionVst);

        return true;
    }

    bool GenerateForTypeAlias(const CTypeAlias& TypeAlias, const TSRef<Verse::Vst::Node>& Parent) const
    {
        using namespace Verse::Vst;

        if (!ShouldGenerate(TypeAlias, true))
        {
            return false;
        }

        TGuardValue<const CAstNode*> GlitchAstGuard(_CurrentGlitchAst, TypeAlias.GetAstNode());

        TSRef<Identifier> Name = TSRef<Identifier>::New(TypeAlias.AsNameStringView(), NullWhence());
        if (TypeAlias._Qualifier._Type == SQualifier::EType::NominalType)
        {
            TSRef<Node> Qualifier = GenerateForQualifier(TypeAlias._Qualifier);
            Name->AppendChild(Qualifier);
        }
        
        TSRef<Node> Type = GenerateForType(TypeAlias.GetPositiveAliasedType());

        TSRef<Definition> DefinitionVst = TSRef<Definition>::New(NullWhence(), Name, Type);
        DefinitionVst->SetNumNewLinesAfter(NumNewLinesForSpacing);
        GenerateForAttributes(TypeAlias, Name, DefinitionVst);
        Parent->AppendChild(DefinitionVst);

        return true;
    }

    TArray<TSPtr<Verse::Vst::Node>> GenerateForSuperType(const CExpressionBase& SuperType, TArray<const CInterface*>& VisitedPublicSuperInterfaces) const
    {
        using namespace Verse::Vst;
        // Properly generate qualifiers for a subset of syntax.
        TArray<TSPtr<Verse::Vst::Node>> Ret;

        if (SuperType.GetNodeType() == EAstNodeType::Identifier_Class)
        {
            const CExprIdentifierClass& SuperClassIdentifier = static_cast<const CExprIdentifierClass&>(SuperType);
            const CClass* SuperClass = SuperClassIdentifier.GetClass(_Program);
            TArray<const CNominalType*> PublicSuperTypes = PublifyType(SuperClass, VisitedPublicSuperInterfaces);

            if (PublicSuperTypes.Num())
            {
                // If the type we are trying to publify is already public we just take it
                if (SuperClass == PublicSuperTypes[0])
                {
                    TSRef<Identifier> SuperClassId = TSRef<Identifier>::New(GetDependencyName(*SuperClass->Definition()), NullWhence());
                    if (SuperClassIdentifier.Qualifier())
                    {
                        const CModule* Module = SuperClass->GetModule();
                        SuperClassId->AppendChild(GenerateForQualifier(SQualifier::NominalType(*Module)));
                    }
                    Ret.Add(Move(SuperClassId));
                }
                else
                {
                    for (const CNominalType* PublicType : PublicSuperTypes)
                    {
                        Ret.Add(GenerateForType(PublicType));
                    }
                }
            }
        }
        else if (SuperType.GetNodeType() == EAstNodeType::Identifier_Interface)
        {
            const CExprInterfaceType& SuperInterfaceIdentifier = static_cast<const CExprInterfaceType&>(SuperType);
            const CInterface* SuperInterface = SuperInterfaceIdentifier.GetInterface(_Program);
            TArray<const CNominalType*> PublicSuperTypes = PublifyType(SuperInterface, VisitedPublicSuperInterfaces);

            if (PublicSuperTypes.Num())
            {
                // If the type we are trying to publify is already public we just take it
                if (SuperInterface == PublicSuperTypes[0])
                {
                    TSRef<Identifier> SuperInterfaceId = TSRef<Identifier>::New(GetDependencyName(*SuperInterface), NullWhence());
                    if (SuperInterfaceIdentifier.Qualifier())
                    {
                        const CModule* Module = SuperInterface->GetModule();
                        SuperInterfaceId->AppendChild(GenerateForQualifier(SQualifier::NominalType(*Module)));
                    }
                    Ret.Add(Move(SuperInterfaceId));
                }
                else
                {
                    for (const CNominalType* PublicType : PublicSuperTypes)
                    {
                        Ret.Add(GenerateForType(PublicType));
                    }
                }
            }
        }
        
        // Fall back to generating unqualified types.
        if (Ret.Num() == 0)
        {
            const CTypeBase* SuperResultType = SuperType.GetResultType(_Program);
            const CTypeType& SuperTypeType = SuperResultType->GetNormalType().AsChecked<CTypeType>();
            const CNormalType& SuperNormalType = SuperTypeType.PositiveType()->GetNormalType();

            if (SuperNormalType.AsNullable<CClass>() || SuperNormalType.AsNullable<CInterface>())
            {
                TArray<const CNominalType*> PublicSuperTypes = PublifyType(&SuperNormalType, VisitedPublicSuperInterfaces);

                for (const CNominalType* PublicType : PublicSuperTypes)
                {
                    Ret.Add(GenerateForType(PublicType));
                }
            }
            else
            {
                return { GenerateForType(SuperTypeType.PositiveType()) };
            }
        }

        return Ret;
    }

    TSRef<Verse::Vst::Macro> GenerateMacroForClassOrInterface(
        const CUTF8StringView& MacroName,
        const CLogicalScope& MemberScope,
        const TArray<TSRef<CExpressionBase>>& SuperTypes,
        const CAttributable* EffectsAttributable,
        const TOptional<SAccessLevel>& ConstructorAccessLevel) const
    {
        using namespace Verse::Vst;

        // Create the class/interface macro.
        TSRef<Clause> InnerClause = TSRef<Clause>::New(NullWhence(), Clause::EForm::HasSemicolonOrNewline);
        InnerClause->SetNewLineAfter(true);

        TSRef<Identifier> ClassId = TSRef<Identifier>::New(MacroName, NullWhence());
        TSRef<Identifier> ClassIdCopy = ClassId;
        TSRef<Macro> ClassMacro = TSRef<Macro>::New(
                NullWhence(),
                Move(ClassIdCopy),
                ClauseArray{InnerClause});

        if (!SuperTypes.IsEmpty())
        {
            // Create the super clause.
            TSRef<Clause> SuperClause = TSRef<Clause>::New(vsyntax::res_of, NullWhence(), Clause::EForm::NoSemicolonOrNewline);
            SuperClause->SetNewLineAfter(true);
            bool bEmptySuperClause = true;
            TArray<const CInterface*> VisitedSuperInterface;
            for (const TSRef<CExpressionBase>& SuperType : SuperTypes)
            {
                for (TSPtr<Node> SuperNode : GenerateForSuperType(*SuperType, VisitedSuperInterface))
                {
                    SuperClause->AppendChild(Move(SuperNode.AsRef()));
                    bEmptySuperClause = false;
                }
            }
            // Append the super clause after the macro name.
            if (!bEmptySuperClause)
            {
                ClassMacro->AppendChildAt(SuperClause, 1);
            }
        }

        if (EffectsAttributable)
        {
            GenerateForAttributes(*EffectsAttributable, ConstructorAccessLevel, ClassId);
        }

        // And process its members
        GenerateForScope(MemberScope, InnerClause);

        return ClassMacro;
    }

    bool GenerateDefinitionForClassOrInterface(
        const CUTF8StringView& MacroName,
        const CLogicalScope& MemberScope,
        const CDefinition& DefinitionAst,
        const TArray<TSRef<CExpressionBase>>& SuperTypes,
        const TSRef<Verse::Vst::Node>& Parent,
        const CAttributable* EffectsAttributable,
        const TOptional<SAccessLevel>& ConstructorAccessLevel) const
    {
        using namespace Verse::Vst;

        TGuardValue<const CAstNode*> GlitchAstGuard(_CurrentGlitchAst, DefinitionAst.GetAstNode());

        // Create the class/interface definition.
        TSRef<Identifier> Name = TSRef<Identifier>::New(DefinitionAst.AsNameStringView(), NullWhence());
        if (!DefinitionAst._Qualifier.IsUnspecified())
        {
            TSRef<Node> Qualifier = GenerateForQualifier(DefinitionAst._Qualifier);
            Name->AppendChild(Qualifier);
        }
        TSRef<Macro> ClassMacro = GenerateMacroForClassOrInterface(MacroName, MemberScope, SuperTypes, EffectsAttributable, ConstructorAccessLevel);
        TSRef<Definition> DefinitionVst = TSRef<Definition>::New(NullWhence(), Name, ClassMacro);
        DefinitionVst->SetNumNewLinesAfter(NumNewLinesForSpacing);
        GenerateForAttributes(DefinitionAst, Name, DefinitionVst);
        Parent->AppendChild(DefinitionVst);

        return true;
    }

    bool GenerateDefinitionForClass(const CClassDefinition& Class, const TSRef<Verse::Vst::Node>& Parent) const
    {
        // Cull inaccessible classes
        if (!ShouldGenerate(Class))
        {
            return false;
        }
        return GenerateDefinitionForClassOrInterface(
            Class.IsStruct()? "struct" : "class",
            Class,
            Class,
            Class.GetIrNode()->SuperTypes(),
            Parent,
            &Class._EffectAttributable,
            Class._ConstructorAccessLevel);
    }

    TSRef<Verse::Vst::Macro> GenerateMacroForClass(const CClassDefinition& Class) const
    {
        TGuardValue<const CAstNode*> GlitchAstGuard(_CurrentGlitchAst, Class.GetAstNode());
        return GenerateMacroForClassOrInterface(
            Class.IsStruct()? "struct" : "class",
            Class,
            Class.GetIrNode()->SuperTypes(),
            &Class._EffectAttributable,
            Class._ConstructorAccessLevel);
    }

    bool GenerateDefinitionForInterface(const CInterface& Interface, const TSRef<Verse::Vst::Node>& Parent) const
    {
        // Cull inaccessible classes
        if (!ShouldGenerate(Interface))
        {
            return false;
        }
        return GenerateDefinitionForClassOrInterface(
            "interface",
            Interface,
            Interface,
            Interface.GetIrNode()->SuperInterfaces(),
            Parent,
            &Interface._EffectAttributable,
            Interface._ConstructorAccessLevel);
    }

    TSRef<Verse::Vst::Macro> GenerateMacroForInterface(const CInterface& Interface) const
    {
        TGuardValue<const CAstNode*> GlitchAstGuard(_CurrentGlitchAst, Interface.GetAstNode());
        return GenerateMacroForClassOrInterface(
            "interface",
            Interface,
            Interface.GetIrNode()->SuperInterfaces(),
            &Interface._EffectAttributable,
            Interface._ConstructorAccessLevel);
    }

    bool GenerateForEnumerator(const CEnumerator& Enumerator, const TSRef<Verse::Vst::Node>& Parent) const
    {
        using namespace Verse::Vst;

        // Cull inaccessible enumerations
        if (!ShouldGenerate(Enumerator))
        {
            return false;
        }
        // We check here if there are any `@doc` attributes and convert them to comments as well.
        const TSRef<Identifier> EnumIdentifier = TSRef<Identifier>::New(Enumerator.AsNameStringView(), NullWhence());
        GenerateForAttributes(Enumerator, Enumerator.SelfAccessLevel(), EnumIdentifier);
        EnumIdentifier->SetNewLineAfter(true);
        Parent->AppendChild(EnumIdentifier);
        return true;
    }

    bool GenerateForEnumeration(const CEnumeration& Enumeration, const TSRef<Verse::Vst::Node>& Parent) const
    {
        using namespace Verse::Vst;

        // Cull inaccessible enumerations
        if (!ShouldGenerate(Enumeration))
        {
            return false;
        }

        TGuardValue<const CAstNode*> GlitchAstGuard(_CurrentGlitchAst, Enumeration.GetAstNode());

        // Create enum definition
        TSRef<Clause> InnerClause = TSRef<Clause>::New(NullWhence(), Clause::EForm::HasSemicolonOrNewline);
        InnerClause->SetNewLineAfter(true); // If to use vertical format
        GenerateForScope(Enumeration, InnerClause);
        TSRef<Identifier> Name = TSRef<Identifier>::New(Enumeration.AsNameStringView(), NullWhence());
        if (!Enumeration._Qualifier.IsUnspecified())
        {
            TSRef<Node> Qualifier = GenerateForQualifier(Enumeration._Qualifier);
            Name->AppendChild(Qualifier);
        }

        TSRef<Identifier> EnumIdentifierVst = TSRef<Identifier>::New("enum", NullWhence());
        GenerateForAttributes(
            Enumeration._EffectAttributable,
            TOptional<SAccessLevel>{},
            EnumIdentifierVst);
        TSRef<Definition> DefinitionVst = TSRef<Definition>::New(
            NullWhence(),
            Name,
            TSRef<Macro>::New(
                NullWhence(),
                EnumIdentifierVst,
                ClauseArray{ InnerClause }));
        DefinitionVst->SetNumNewLinesAfter(NumNewLinesForSpacing);
        GenerateForAttributes(Enumeration, Name, DefinitionVst);
        Parent->AppendChild(DefinitionVst);

        return true;
    }

    bool GenerateForScopedPaths(const CScopedAccessLevelDefinition& ScopedAccessLevel, const TSRef<Verse::Vst::Node>& Parent) const
    {
        using namespace Verse::Vst;

        Verse::Vst::ClauseArray NewClauses;
        for (const CScope* Scope : ScopedAccessLevel._Scopes)
        {
            CUTF8String PathString = Scope->GetScopePath('/', CScope::EPathMode::PrefixSeparator);
			
            TSRef<PathLiteral> NewPathLiteral = TSRef<PathLiteral>::New(Move(PathString), NullWhence());
            // The syntax should be something like `scoped {/Verse.org`}, we don't want any newlines after the
            // path literal.
            NewPathLiteral->SetNumNewLinesAfter(0);
            Parent->AppendChild(NewPathLiteral);
        }

        return true;
    }

    const TSRef<Verse::Vst::Macro> GenerateForScopedMacro(const CScopedAccessLevelDefinition& ScopedAccessLevel) const
    {
        using namespace Verse::Vst;

        // Create access level definition
        TSRef<Clause> InnerClause = TSRef<Clause>::New(NullWhence(), Clause::EForm::HasSemicolonOrNewline);
        InnerClause->SetNewLineAfter(false);

        GenerateForScopedPaths(ScopedAccessLevel, InnerClause);

        return TSRef<Macro>::New(
                NullWhence(),
                TSRef<Identifier>::New("scoped", NullWhence()),
                ClauseArray{ InnerClause });
    }

    bool GenerateForScopedAccessLevel(const CScopedAccessLevelDefinition& ScopedAccessLevel, const TSRef<Verse::Vst::Node>& Parent) const
    {
        using namespace Verse::Vst;

        // Cull inaccessible access levels
        if (!ShouldGenerate(ScopedAccessLevel))
        {
            return false;
        }

        TGuardValue<const CAstNode*> GlitchAstGuard(_CurrentGlitchAst, ScopedAccessLevel.GetAstNode());

        TSRef<Identifier> ScopedDefinitionName = TSRef<Identifier>::New(ScopedAccessLevel.AsNameStringView(), NullWhence());
        TSRef<Definition> DefinitionVst = TSRef<Definition>::New(
            NullWhence(),
            ScopedDefinitionName,
            GenerateForScopedMacro(ScopedAccessLevel));

        DefinitionVst->SetNewLineAfter(true);
        GenerateForAttributes(ScopedAccessLevel, ScopedDefinitionName, DefinitionVst);
        Parent->AppendChild(DefinitionVst);

        return true;
    }

    bool GenerateForFunction(const CFunction& Function, const TSRef<Verse::Vst::Node>& Parent) const
    {
        using namespace Verse::Vst;

        // Cull inaccessible functions
        if (!ShouldGenerate(Function) || Function.IsCoercion())
        {
            return false;
        }

        TGuardValue<const CAstNode*> GlitchAstGuard(_CurrentGlitchAst, Function.GetAstNode());

        // Create function definition
        TSRef<PrePostCall> Call = TSRef<PrePostCall>::New(NullWhence());
        TSRef<Identifier> FunctionName = TSRef<Identifier>::New(
            Function._ExtensionFieldAccessorKind == EExtensionFieldAccessorKind::ExtensionMethod
            ? _Program._IntrinsicSymbols.StripExtensionFieldOpName(Function.GetName())
            : Function.AsNameStringView()
            , NullWhence());
        if (!Function._Qualifier.IsUnspecified())
        {
            TSRef<Node> Qualifier = GenerateForQualifier(Function._Qualifier);
            FunctionName->AppendChild(Qualifier);
        }

        TSRef<Clause> ParameterList = GenerateForParameters(Function);

        switch (Function._ExtensionFieldAccessorKind)
        {
        case EExtensionFieldAccessorKind::Function:
        {
            FunctionName->SetTag(PrePostCall::Op::Expression);
            Call->AppendChild(FunctionName);
            Call->AppendChild(ParameterList);
            break;
        }
        case EExtensionFieldAccessorKind::ExtensionDataMember:
        {
            _Diagnostics->AppendGlitch(
                SGlitchResult(EDiagnostic::ErrDigest_Unimplemented, uLang::CUTF8String("Extension data members are not implemented yet.")),
                _CurrentGlitchAst);
            break;
        }
        case EExtensionFieldAccessorKind::ExtensionMethod:
        {
            FunctionName->SetTag(PrePostCall::Op::DotIdentifier);
            TSRef<Clause> LhsParameter = TSRef<Clause>::New(NullWhence(), Clause::EForm::NoSemicolonOrNewline);
            LhsParameter->SetTag(PrePostCall::Op::SureCall);
            LhsParameter->AppendChild(ParameterList->TakeChildAt(0));

            TSRef<PrePostCall> DotCall = TSRef<PrePostCall>::New(NullWhence());
            DotCall->AppendChild(LhsParameter);
            DotCall->AppendChild(FunctionName);

            Call->AppendChild(DotCall);
            Call->AppendChild(AsClause(ParameterList->TakeChildAt(0)));
            break;
        }
        default:
            ULANG_ERRORF("Missing an alternative in switch.");
        }

        // Is there an implementation?
        if (const CExpressionBase* Body = Function.GetBodyIr())
        {
            if (Body->GetNodeType() == EAstNodeType::Definition_Class)
            {
                const CClass& Class = static_cast<const CExprClassDefinition&>(*Body)._Class;
                TSRef<Definition> DefinitionVst = TSRef<Definition>::New(
                    NullWhence(),
                    Call,
                    GenerateMacroForClass(*Class._Definition));
                DefinitionVst->SetNumNewLinesAfter(NumNewLinesForSpacing);

                TArray<SAttribute> Attributes = Function._Attributes;
                if (TOptional<SAttribute> NativeAttribute = Class.FindAttribute(_Program._nativeClass, _Program))
                {
                    Attributes.Push(Move(*NativeAttribute));
                }
                GenerateForAttributes(Attributes, Function, FunctionName, DefinitionVst);
                Parent->AppendChild(DefinitionVst);
            }
            else if (Body->GetNodeType() == EAstNodeType::Definition_Interface)
            {
                TSRef<Definition> DefinitionVst = TSRef<Definition>::New(
                    NullWhence(),
                    Call,
                    GenerateMacroForInterface(static_cast<const CExprInterfaceDefinition&>(*Body)._Interface));
                DefinitionVst->SetNumNewLinesAfter(NumNewLinesForSpacing);
                GenerateForAttributes(Function, FunctionName, DefinitionVst);
                Parent->AppendChild(DefinitionVst);
            }
            else
            {
                const CTypeBase* ReturnType = Function._Signature.GetReturnType();
                if (const CTypeType* ReturnTypeType = ReturnType->GetNormalType().AsNullable<CTypeType>();
                    ReturnTypeType && !Function.GetReturnTypeIr() && Function._Signature.GetEffects() == EffectSets::Computes)
                {
                    TSRef<Definition> DefinitionVst = TSRef<Definition>::New(
                        NullWhence(),
                        Call,
                        GenerateForType(ReturnTypeType->PositiveType()));
                    DefinitionVst->SetNumNewLinesAfter(NumNewLinesForSpacing);
                    GenerateForAttributes(Function, FunctionName, DefinitionVst);
                    Parent->AppendChild(DefinitionVst);
                }
                else
                {
                    TSRef<TypeSpec> TypedCall = TSRef<TypeSpec>::New(
                        NullWhence(),
                        Call,
                        GenerateForType(Function._Signature.GetReturnType()));
                    // Generate an assignment to the external{} macro
                    TSRef<Definition> DefinitionVst = TSRef<Definition>::New(
                        NullWhence(),
                        TypedCall,
                        GenerateExternalMacro());
                    DefinitionVst->SetNumNewLinesAfter(NumNewLinesForSpacing);
                    GenerateForAttributes(Function, FunctionName, DefinitionVst);
                    Parent->AppendChild(DefinitionVst);
                    GenerateForEffectAttributes(Function._Signature.GetFunctionType()->GetEffects(), EffectSets::FunctionDefault, *Call);
                }
            }
        }
        else
        {
            TSRef<TypeSpec> TypedCall = TSRef<TypeSpec>::New(
                NullWhence(),
                Call,
                GenerateForType(Function._Signature.GetReturnType()));
            // No, just generate the function declaration by itself
            TypedCall->SetNumNewLinesAfter(NumNewLinesForSpacing);
            GenerateForAttributes(Function, FunctionName, TypedCall);
            Parent->AppendChild(TypedCall);
	        GenerateForEffectAttributes(Function._Signature.GetFunctionType()->GetEffects(), EffectSets::FunctionDefault, *Call);
        }

        return true;
    }

    bool GenerateForDataDefinition(const CDataDefinition& DataDefinition, const TSRef<Verse::Vst::Node>& Parent, bool bQualify) const
    {
        using namespace Verse::Vst;

        // Cull inaccessible data definitions
        if (!ShouldGenerate(DataDefinition))
        {
            return false;
        }

        TGuardValue<const CAstNode*> GlitchAstGuard(_CurrentGlitchAst, DataDefinition.GetAstNode());

        // Create data definition
        CSymbol Name = DataDefinition.GetName();
        TSRef<Identifier> NameNode = TSRef<Identifier>::New(Name.AsStringView(), NullWhence());
        if (!DataDefinition._Qualifier.IsUnspecified())
        {
            TSRef<Node> Qualifier = GenerateForQualifier(DataDefinition._Qualifier);
            NameNode->AppendChild(Qualifier);
        }
        TSPtr<Node> DecoratedNode;
        const CTypeBase* Type = DataDefinition.GetType();
        if (DataDefinition.IsVar())
        {
            DecoratedNode = TSRef<Mutation>::New(NullWhence(), NameNode, Mutation::EKeyword::Var);
            GenerateForAttributesGeneric({ }, DataDefinition.SelfVarAccessLevel(), [&DecoratedNode] (const CClass*) { return DecoratedNode.AsRef(); });
            Type = Type->GetNormalType().AsChecked<CPointerType>().PositiveValueType();
        }
        else
        {
            DecoratedNode = NameNode;
        }

        TSRef<TypeSpec> TypeSpecNode = TSRef<TypeSpec>::New(NullWhence(), Move(DecoratedNode.AsRef()), GenerateForType(Type, bQualify));
        STypeSpecAndName TypedDefinition{Move(TypeSpecNode), Move(NameNode)};
        // Is there a default value?
        if (DataDefinition.GetIrNode()->Value().IsValid())
        {
            // Yes, generate an assignment to the external{} macro
            TSRef<Definition> DefinitionVst = TSRef<Definition>::New(
                NullWhence(),
                TypedDefinition._TypeSpec,
                TSRef<Macro>::New(NullWhence(), TSRef<Identifier>::New("external", NullWhence()), ClauseArray{ TSRef<Clause>::New(NullWhence(), Clause::EForm::NoSemicolonOrNewline) }));
            DefinitionVst->SetNumNewLinesAfter(NumNewLinesForSpacing);
            GenerateForAttributes(DataDefinition, TypedDefinition._Name, DefinitionVst);
            Parent->AppendChild(DefinitionVst);
        }
        else
        {
            // No, just generate the type spec by itself
            TypedDefinition._TypeSpec->SetNumNewLinesAfter(NumNewLinesForSpacing);
            GenerateForAttributes(DataDefinition, TypedDefinition._Name, TypedDefinition._TypeSpec);
            Parent->AppendChild(TypedDefinition._TypeSpec);
        }

        return true;
    }

    TSRef<Verse::Vst::Clause> GenerateForParameters(const CFunction& Function) const
    {
        using namespace Verse::Vst;
        TSRefArray<CTypeVariable> TypeVariables;
        for (const TSRef<CTypeVariable>& TypeVariable : Function.GetDefinitionsOfKind<CTypeVariable>())
        {
            TypeVariables.Add(TypeVariable);
        }
        const CExprFunctionDefinition* FunctionDefinition = Function.GetIrNode();
        const TSPtr<CExpressionBase>& Element = FunctionDefinition->Element();
        ULANG_ASSERTF(Element, "Function definition IR node must have an element.");
        ULANG_ASSERTF(Element->GetNodeType() == EAstNodeType::Invoke_Invocation, "Function definition element IR node must be an invocation.");
        const CExprInvocation& Invocation = static_cast<const CExprInvocation&>(*Element);
        auto ParamDefinitionIterator = Function._Signature.GetParams().begin();
        auto TypeVariableIterator = TypeVariables.begin();
        return AsClause(GenerateForParameter(*Invocation.GetArgument(), ParamDefinitionIterator, TypeVariableIterator));
    }

    template <typename TParamDefinitionIterator, typename TTypeVariableIterator>
    TSRef<Verse::Vst::Node> GenerateForParameter(
        const CExpressionBase& Expression,
        TParamDefinitionIterator& ParamDefinitionIterator,
        TTypeVariableIterator& TypeVariableIterator) const
    {
        using namespace Verse::Vst;
        if (Expression.GetNodeType() == EAstNodeType::Invoke_MakeTuple)
        {
            TSRef<Parens> Result = TSRef<Parens>::New(NullWhence(), Clause::EForm::NoSemicolonOrNewline);
            const CExprMakeTuple& ExprMakeTuple = static_cast<const CExprMakeTuple&>(Expression);
            for (const TSPtr<CExpressionBase>& SubExpr : ExprMakeTuple.GetSubExprs())
            {
                Result->AppendChild(GenerateForParameter(
                    *SubExpr,
                    ParamDefinitionIterator,
                    TypeVariableIterator));
            }
            return Result;
        }
        else if (Expression.GetNodeType() == EAstNodeType::Definition_Where)
        {
            const CExprWhere& ExprWhere = static_cast<const CExprWhere&>(Expression);
            TSRef<Node> Lhs = GenerateForParameter(
                *ExprWhere.Lhs(),
                ParamDefinitionIterator,
                TypeVariableIterator);
            const TSPtrArray<CExpressionBase>& RhsExpressions = ExprWhere.Rhs();
            TArray<TSRef<Node>> Rhs;
            Rhs.Reserve(RhsExpressions.Num());
            for (auto I = RhsExpressions.begin(), Last = RhsExpressions.end(); I != Last; ++I)
            {
                while ((*TypeVariableIterator)->_ExplicitParam)
                {
                    ++TypeVariableIterator;
                }
                const CTypeVariable* TypeVariable = *TypeVariableIterator;
                ++TypeVariableIterator;
                Rhs.Add(GenerateForTypeSpec(TypeVariable->GetName(), TypeVariable->GetType())._TypeSpec);
            }
            return TSRef<Where>::New(NullWhence(), Move(Lhs), Move(Rhs));
        }
        else
        {
            ULANG_ASSERTF(
                Expression.GetNodeType() == EAstNodeType::Definition,
                "Digest generation for '%s' is unimplemented.", Expression.GetErrorDesc().AsCString());
            const CExprDefinition& ExprDefinition = static_cast<const CExprDefinition&>(Expression);
            const CDataDefinition* ParamDefinition = *ParamDefinitionIterator;
            ++ParamDefinitionIterator;
            CUTF8StringView Name;
            const CTypeBase* Type;
            if (const CTypeVariable* ImplicitParam = ParamDefinition->_ImplicitParam)
            {
                Name = ImplicitParam->AsNameStringView();
                Type = ImplicitParam->GetType();
            }
            else
            {
                Name = ParamDefinition->AsNameStringView();
                Type = ParamDefinition->GetType();
            }
            TSRef<Identifier> IdentifierNode = TSRef<Identifier>::New(Name, NullWhence());
            TSPtr<Node> ElementNode;
            if (ParamDefinition->_bNamed)
            {
                TSRef<PrePostCall> QMarkNode = TSRef<PrePostCall>::New(IdentifierNode, NullWhence());
                QMarkNode->PrependQMark(NullWhence());
                ElementNode = Move(QMarkNode);
            }
            else
            {
                ElementNode = IdentifierNode;
            }
            TSRef<Node> Result = TSRef<TypeSpec>::New(
                NullWhence(),
                Move(ElementNode.AsRef()),
                GenerateForType(Type));
            if (ExprDefinition.Value())
            {
                Result = TSRef<Definition>::New(
                    NullWhence(),
                    Move(Result),
                    GenerateExternalMacro());
            }
            return Result;
        }
    }

    static TSRef<Verse::Vst::Clause> AsClause(TSRef<Verse::Vst::Node> Node)
    {
        namespace Vst = Verse::Vst;
        if (Node->IsA<Vst::Clause>())
        {
            return Node.As<Vst::Clause>();
        }
        if (const Vst::Parens* Parens = Node->AsNullable<Vst::Parens>())
        {
            TSRef<Vst::Clause> Clause = TSRef<Vst::Clause>::New(NullWhence(), Parens->GetForm());
            Clause->SetTag(Vst::PrePostCall::Op::SureCall);
            Vst::Node::TransferChildren(Node, Clause);
            return Clause;
        }
        TSRef<Vst::Clause> Clause = TSRef<Vst::Clause>::New(NullWhence(), Vst::Clause::EForm::NoSemicolonOrNewline);
        Clause->SetTag(Vst::PrePostCall::Op::SureCall);
        Clause->AppendChild(Node);
        return Clause;
    }

    TSRef<Verse::Vst::Clause> GenerateForParamTypes(CFunctionType::ParamTypes ParamTypes, bool bQualify) const
    {
        using namespace Verse::Vst;

        TSRef<Clause> ParamList = TSRef<Clause>::New(NullWhence(), Clause::EForm::NoSemicolonOrNewline);
        for (const CTypeBase* ParamType : ParamTypes)
        {
            ParamList->AppendChild(GenerateForTypeSpec(CSymbol(), &SemanticTypeUtils::AsPositive(*ParamType, {}), bQualify)._TypeSpec);
        }

        // We assume this node will be used in a PrePostCall so set it up properly for that
        ParamList->SetTag(PrePostCall::Op::SureCall);

        return ParamList;
    }

    struct STypeSpecAndName
    {
        TSRef<Verse::Vst::TypeSpec> _TypeSpec;
        TSRef<Verse::Vst::Identifier> _Name;
    };

    STypeSpecAndName GenerateForTypeSpec(const CSymbol& Name, const CTypeBase* Type, bool bQualify = false) const
    {
        using namespace Verse::Vst;

        TSRef<Identifier> NameNode = TSRef<Identifier>::New(Name.AsStringView(), NullWhence());
        return {TSRef<TypeSpec>::New(NullWhence(), NameNode, GenerateForType(Type, bQualify)), NameNode};
    }

    // Create VST node representing supplied tuple type
    TSRef<Verse::Vst::PrePostCall> GenerateForTupleType(const CTupleType& Tuple, bool bQualify) const
    {
        using namespace Verse::Vst;

        TSRef<PrePostCall> Call = TSRef<PrePostCall>::New(NullWhence());

        // Add `tuple` identifier
        TSRef<Identifier> TupleIdent = TSRef<Identifier>::New("tuple", NullWhence());
        TupleIdent->SetTag(PrePostCall::Op::Expression);
        Call->AppendChild(TupleIdent);


        // Add element types
        TSRef<Clause> Elements = TSRef<Clause>::New(NullWhence(), Clause::EForm::NoSemicolonOrNewline);
        for (const CTypeBase* ElementType : Tuple.GetElements())
        {
            Elements->AppendChild(GenerateForType(ElementType, bQualify));
        }

        Elements->SetTag(PrePostCall::Op::SureCall);
        Call->AppendChild(Elements);

        return Call;
    }

    TSRef<Verse::Vst::Node> GenerateForInvocation(
        const CUTF8StringView& CalleeName,
        const TSRef<Verse::Vst::Node>& Argument) const
    {
        using namespace Verse::Vst;
        TSRef<Identifier> CalleeIdentifier = TSRef<Identifier>::New(CalleeName, NullWhence());
        CalleeIdentifier->SetTag(PrePostCall::Op::Expression);
        Argument->SetTag(PrePostCall::Op::SureCall);
        TSRef<PrePostCall> Call = TSRef<PrePostCall>::New(NullWhence());
        Call->AppendChild(CalleeIdentifier);
        Call->AppendChild(Argument);
        return Call;
    }

    TSRef<Verse::Vst::Node> GenerateForTypeInvocation(
        const CUTF8StringView& CalleeName,
        const CTypeBase* TypeArgment) const
    {
        using namespace Verse::Vst;
        return GenerateForInvocation(
            CalleeName,
            TSRef<Clause>::New(
                GenerateForType(TypeArgment),
                NullWhence(),
                Clause::EForm::NoSemicolonOrNewline));
    }

    TSRef<Verse::Vst::Node> GenerateForSubtypeType(const CTypeBase* NegativeType) const
    {
        return GenerateForTypeInvocation("subtype", NegativeType);
    }

    TSRef<Verse::Vst::Node> GenerateForSupertypeType(const CTypeBase* PositiveType) const
    {
        return GenerateForTypeInvocation("supertype", PositiveType);
    }

    TSRef<Verse::Vst::Node> GenerateForWeakMapType(const CTypeBase* KeyType, const CTypeBase* ValueType) const
    {
        using namespace Verse::Vst;
        return GenerateForInvocation(
            "weak_map",
            TSRef<Clause>::New(
                TSRefArray<Node>{GenerateForType(KeyType), GenerateForType(ValueType)},
                NullWhence(),
                Clause::EForm::NoSemicolonOrNewline));
    }

    TSRef<Verse::Vst::Node> GenerateForGeneratorType(const CTypeBase* ElementType, bool bQualify) const
    {
        using namespace Verse::Vst;
        return GenerateForInvocation(
            "generator",
            TSRef<Clause>::New(
                TSRefArray<Node>{GenerateForType(ElementType, bQualify)},
                NullWhence(),
                Clause::EForm::NoSemicolonOrNewline));
    }

    TSRef<Verse::Vst::Node> GenerateForType(const CNormalType& Type, bool bQualify) const
    {
        using namespace Verse::Vst;
        switch (Type.GetKind())
        {
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
            return TSRef<Identifier>::New(Type.AsCode(), NullWhence());

        case ETypeKind::Class:
        {
            const CClass& Class = Type.AsChecked<CClass>();
            if (Class.GetParentScope()->GetKind() != CScope::EKind::Function)
            {
                TSRef<Identifier> Name = TSRef<Identifier>::New(GetDependencyName(*Class.Definition()), NullWhence());
                if (bQualify)
                {
                    if (const CModule* Module = Class.GetModule())
                    {
                        Name->AppendChild(GenerateQualifierAsPath(*Module));
                    }
                }
                return Name;
            }
            TSRef<Identifier> Name = TSRef<Identifier>::New(
                GetDependencyName(*static_cast<const CFunction*>(Class.GetParentScope())),
                NullWhence());
            Name->SetTag(PrePostCall::Expression);
            TSRef<Clause> ArgumentsClause = TSRef<Clause>::New(NullWhence(), Clause::EForm::NoSemicolonOrNewline);
            for (const STypeVariableSubstitution& InstTypeVariable : Class._TypeVariableSubstitutions)
            {
                if (InstTypeVariable._TypeVariable->_ExplicitParam && InstTypeVariable._TypeVariable->_NegativeTypeVariable)
                {
                    ArgumentsClause->AppendChild(GenerateForType(InstTypeVariable._PositiveType));
                }
            }
            ArgumentsClause->SetTag(PrePostCall::SureCall);
            TSRef<PrePostCall> Invocation = TSRef<PrePostCall>::New(NullWhence());
            Invocation->AppendChild(Move(Name));
            Invocation->AppendChild(Move(ArgumentsClause));
            return Move(Invocation);
        }
        case ETypeKind::Interface:
        {
            const CInterface& Interface = Type.AsChecked<CInterface>();
            if (Interface.GetParentScope()->GetKind() != CScope::EKind::Function)
            {
                TSRef<Identifier> Name = TSRef<Identifier>::New(GetDependencyName(Interface), NullWhence());
                if (bQualify)
                {
                    if (const CModule* Module = Interface.GetModule())
                    {
                        Name->AppendChild(GenerateQualifierAsPath(*Module));
                    }
                }
                return Name;
            }
            TSRef<Identifier> Name = TSRef<Identifier>::New(
                GetDependencyName(*static_cast<const CFunction*>(Interface.GetParentScope())),
                NullWhence());
            Name->SetTag(PrePostCall::Expression);
            TSRef<Clause> ArgumentsClause = TSRef<Clause>::New(NullWhence(), Clause::EForm::NoSemicolonOrNewline);
            for (const STypeVariableSubstitution& InstTypeVariable : Interface._TypeVariableSubstitutions)
            {
                if (InstTypeVariable._TypeVariable->_ExplicitParam && InstTypeVariable._TypeVariable->_NegativeTypeVariable)
                {
                    ArgumentsClause->AppendChild(GenerateForType(InstTypeVariable._PositiveType));
                }
            }
            ArgumentsClause->SetTag(PrePostCall::SureCall);
            TSRef<PrePostCall> Invocation = TSRef<PrePostCall>::New(NullWhence());
            Invocation->AppendChild(Move(Name));
            Invocation->AppendChild(Move(ArgumentsClause));
            return Move(Invocation);
        }
        case ETypeKind::Tuple:
            return GenerateForTupleType(Type.AsChecked<CTupleType>(), bQualify);
        case ETypeKind::Enumeration:
        {
            const CEnumeration& Enumeration = Type.AsChecked<CEnumeration>();
            TSRef<Identifier> Name = TSRef<Identifier>::New(GetDependencyName(Enumeration), NullWhence());
            if (bQualify)
            {
                if (const CModule* Module = Enumeration.GetModule())
                {
                    Name->AppendChild(GenerateQualifierAsPath(*Module));
                }
            }
            return Name;
        }
        case ETypeKind::Option:
        {
            TSRef<PrePostCall> Option = TSRef<PrePostCall>::New(NullWhence());
            Option->AppendChild(TSRef<Clause>::New((uint8_t)PrePostCall::Op::Option, NullWhence(), Clause::EForm::Synthetic));
            Option->AppendChild(GenerateForType(Type.AsChecked<COptionType>().GetValueType(), bQualify));
            return Option;
        }
        case ETypeKind::Type:
        {
            const CTypeType& TypeType = Type.AsChecked<CTypeType>();
            const CNormalType& NegativeType = TypeType.NegativeType()->GetNormalType();
            const CNormalType& PositiveType = TypeType.PositiveType()->GetNormalType();
            if (NegativeType.IsA<CFalseType>())
            {
                if (PositiveType.IsA<CAnyType>())
                {
                    return TSRef<Identifier>::New("type", NullWhence());
                }
                return GenerateForSubtypeType(TypeType.PositiveType());
            }
            if (PositiveType.IsA<CAnyType>())
            {
                return GenerateForSupertypeType(TypeType.NegativeType());
            }
            if (&PositiveType != &NegativeType)
            {
                _Diagnostics->AppendGlitch(
                    SGlitchResult(
                        EDiagnostic::ErrSemantic_Unimplemented,
                        CUTF8String("Use of type `%s` in a digest is currently unsupported.", TypeType.AsCode().AsCString())),
                    _CurrentGlitchAst);
            }
            return GenerateForType(&SemanticTypeUtils::AsPositive(PositiveType, {}));
        }
        case ETypeKind::Function:
        {
            TSRef<Identifier> Name = TSRef<Identifier>::New(_Underscore.AsStringView(), NullWhence());
            TSRef<PrePostCall> PrePost = TSRef<PrePostCall>::New(NullWhence());
            const CFunctionType* FunctionType = &Type.AsChecked<CFunctionType>();
            PrePost->AppendChild(Name);
            PrePost->AppendChild(GenerateForParamTypes(FunctionType->GetParamTypes(), bQualify));
            GenerateForEffectAttributes(FunctionType->GetEffects(), EffectSets::FunctionDefault, *PrePost);
            return TSRef<Macro>::New(
                NullWhence(),
                TSRef<Identifier>::New("type", NullWhence()),
                ClauseArray{ TSRef<Clause>::New(TSRef<TypeSpec>::New(NullWhence(), PrePost, GenerateForType(&FunctionType->GetReturnType(), bQualify)).As<Node>(), NullWhence(), Clause::EForm::NoSemicolonOrNewline) });
        }
        case ETypeKind::Array:
        {
            TSRef<PrePostCall> ArrayTypeFormer = TSRef<PrePostCall>::New(NullWhence());
            ArrayTypeFormer->AppendChild(TSRef<Clause>::New((uint8_t)PrePostCall::Op::FailCall, NullWhence(), Clause::EForm::NoSemicolonOrNewline));
            ArrayTypeFormer->AppendChild(GenerateForType(Type.AsChecked<CArrayType>().GetElementType(), bQualify));
            return ArrayTypeFormer;
        }
        case ETypeKind::Generator:
        {
            const CGeneratorType& GeneratorType = Type.AsChecked<CGeneratorType>();
            return GenerateForGeneratorType(GeneratorType.GetElementType(), bQualify);
        }
        case ETypeKind::Map:
        {
            const CMapType& MapType = Type.AsChecked<CMapType>();
            if (MapType.IsWeak())
            {
                return GenerateForWeakMapType(MapType.GetKeyType(), MapType.GetValueType());
            }
            TSRef<PrePostCall> MapTypeFormer = TSRef<PrePostCall>::New(NullWhence());
            TSRef<Clause> KeyTypeClause = TSRef<Clause>::New(GenerateForType(MapType.GetKeyType(), bQualify), NullWhence(), Clause::EForm::NoSemicolonOrNewline);
            KeyTypeClause->SetTag(PrePostCall::FailCall);
            MapTypeFormer->AppendChild(Move(KeyTypeClause));
            MapTypeFormer->AppendChild(GenerateForType(MapType.GetValueType(), bQualify));
            return MapTypeFormer;
        }
        case ETypeKind::Pointer:
            ULANG_ERRORF("Pointers must be handled at the type spec level.");
            break;
        case ETypeKind::Variable:
        {
            return TSRef<Identifier>::New(Type.AsChecked<CTypeVariable>().AsNameStringView(), NullWhence());
        }

        case ETypeKind::Unknown:
        case ETypeKind::Module:
        case ETypeKind::Reference:
        case ETypeKind::Named:
        case ETypeKind::Persistable:
        default:
            ULANG_ERRORF("Digest generation for %s '%s' is unimplemented.", TypeKindAsCString(Type.GetKind()), Type.AsCode().AsCString());
        };

        return TSRef<Identifier>::New("<unknown>", NullWhence());
    }

    TSRef<Verse::Vst::Node> GenerateForType(const CTypeBase* Type, bool bQualify = false) const
    {
        // If the type is a usable type alias, generate a use of that type alias.
        if (const CAliasType* AliasType = Type->AsAliasType())
        {
            if (IsUsable(AliasType->GetDefinition()))
            {
                return TSRef<Verse::Vst::Identifier>::New(GetDependencyName(AliasType->GetDefinition()), NullWhence());
            }
        }

        return GenerateForType(Type->GetNormalType(), bQualify);
    }

    TSRef<Verse::Vst::Node> GenerateForQualifier(const SQualifier& Qualifier) const
    {
        if (Qualifier._Type == SQualifier::EType::Local)
        {
            return TSRef<Verse::Vst::Identifier>::New("local", NullWhence());
        }
        // Use GenerateForType, but handle module "types" as a reference to the module value.
        else if (Qualifier._Definition && Qualifier._Definition->GetKind() == ETypeKind::Module)
        {
            const CModule& Module = Qualifier._Definition->AsChecked<CModule>();
            const CUTF8String UsingVersePath = Module.GetScopePath('/', CScope::EPathMode::PrefixSeparator);
            return TSRef<Verse::Vst::PathLiteral>::New(UsingVersePath, NullWhence());
        }
        else
        {
            ULANG_ASSERTF(Qualifier._Definition, "Invalid qualifier state encountered.");
            // TODO: (yiliang.siew) For now we are always using the full path for the qualifier in this case until we implement
            // logic to use the minimum qualification path possible.
            const CDefinition* Definition = Qualifier._Definition->Definition();
            if (ULANG_ENSUREF(Definition, "Invalid qualifier; no valid definition exists for it."))
            {
                const CLogicalScope* LogicalScope = Definition->DefinitionAsLogicalScopeNullable();
                if (ULANG_ENSUREF(LogicalScope, "Invalid qualifier; definition for it is not a logical scope."))
                {
                    const CUTF8String QualifierPath = LogicalScope->GetScopePath('/', CScope::EPathMode::PrefixSeparator);
                    return TSRef<Verse::Vst::PathLiteral>::New(QualifierPath, NullWhence());
                }
            }
            ULANG_UNREACHABLE();
        }
    }

    TSRef<Verse::Vst::Node> GenerateQualifierAsPath(const CModule& Module) const
    {
        return TSRef<Verse::Vst::PathLiteral>::New(Module.GetScopePath('/', CScope::EPathMode::PrefixSeparator), NullWhence());
    }

    void GenerateForEffectAttributes(const SEffectSet Effects, const SEffectSet DefaultEffects, Verse::Vst::Node& CallAttributable) const
    {
        using namespace Verse::Vst;

        if (TOptional<TArray<const CClass*>> EffectClasses = _Program.ConvertEffectSetToEffectClasses(Effects, DefaultEffects))
        {
            for (const CClass* EffectClass : EffectClasses.GetValue())
            {
                CallAttributable.AppendAux(TSRef<Clause>::New(TSRef<Identifier>::New(EffectClass->Definition()->AsNameStringView(), NullWhence()).As<Node>(), NullWhence(), Clause::EForm::IsAppendAttributeHolder));
            }
        }
    }

    bool IsEpicInternalOnlyAttributeClass(const CClassDefinition& AttributeClass) const
    {
        // Allow the built-in attributes so the attribute scope attributes used to define attributes
        // that *aren't* epic_internal are preserved.
        if (AttributeClass.IsBuiltIn())
        {
            return false;
        }

        // Also make an exception for the import_as attribute until we can link against a digest without it.
        if (&AttributeClass == _Program._import_as_attribute.Get())
        {
            return false;
        }

        const CDefinition* WorkingDefinition = &AttributeClass;
        while (WorkingDefinition)
        {
            if (WorkingDefinition->DerivedAccessLevel()._Kind == SAccessLevel::EKind::EpicInternal)
            {
                return true;
            }

            for (const CScope* Scope = &WorkingDefinition->_EnclosingScope; Scope; Scope = Scope->GetParentScope())
            {
                WorkingDefinition = Scope->ScopeAsDefinition();
                if (WorkingDefinition)
                {
                    break;
                }
            }
        };

        return false;
    }

    TArray<CUTF8String> ReformatDocCommentAsComments(const CUTF8String& TextValue) const
    {
        TArray<CUTF8String> Result;
        Result.Reserve(8);    // Completely arbitrary choice of how many comments we predict to have as multiline at a time.
        const CUTF8StringView TextView = TextValue.ToStringView();
        const UTF8Char* const ViewEnd = TextView._End;
        const UTF8Char* CommentStart = TextView._Begin;

        // Skip the leading newline that is often found in multiline doc strings.
        if (*CommentStart == '\n')
        {
            ++CommentStart;
        }

        for (const UTF8Char* Ptr = CommentStart; Ptr != ViewEnd; ++Ptr)
        {
            if (*Ptr == '\n')
            {
                const CUTF8StringView CurrentCommentView = CUTF8StringView(CommentStart, Ptr);
                Result.Add(CUTF8String("# %s", CUTF8String(CurrentCommentView).AsCString()));
                CommentStart = Ptr + 1; // Next comment starts after the new line character
            }
        }

        // If the comment doesn't have any newlines at all, or doesn't end in a newline.
        if (CommentStart != ViewEnd)
        {
            const CUTF8StringView CurrentCommentView = CUTF8StringView(CommentStart, ViewEnd);
            Result.Add(CUTF8String("# %s", CUTF8String(CurrentCommentView).AsCString()));
        }
        return Result;
    }

    template<typename Func>
    void GenerateForScopedAttribute(const CScopedAccessLevelDefinition& AccessLevelDefinition, const Func& SelectAttributable) const
    {
        using namespace Verse::Vst;
            
        // Determine which attributable to put the attribute on
        TSRef<Verse::Vst::Node> Attributable = SelectAttributable(&AccessLevelDefinition);
        TSRef<Macro> newScopedMacro = GenerateForScopedMacro(AccessLevelDefinition);

        // attribute nodes must be wrapped in a Clause node (elsewhere used to preserve source comments)
        TSRef<Clause> WrapperClause = TSRef<Clause>::New(newScopedMacro.As<Node>(), NullWhence(), Clause::EForm::IsAppendAttributeHolder);
        Attributable->AppendAux(WrapperClause);
    }

    const CClass* GetClassForExpression(TSPtr<CExpressionBase> Expression) const
    {
        const CNormalType& ExpressionResultType = Expression->GetResultType(_Program)->GetNormalType();

        if (const CClass* Class = ExpressionResultType.AsNullable<CClass>())
        {
            return Class;
        }
        else if (const CTypeType* TypeType = ExpressionResultType.AsNullable<CTypeType>())
        {
            return TypeType->PositiveType()->GetNormalType().AsNullable<CClass>();
        }

        return nullptr;
    }

    TSRef<Verse::Vst::Node> GenerateForExpression(TSPtr<CExpressionBase> ExprValue) const
    {
        using namespace Verse::Vst;

        TGuardValue<const CAstNode*> GlitchAstGuard(_CurrentGlitchAst, ExprValue);

        TSPtr<Verse::Vst::Node> ResultValue;
        const CTypeBase* TypeBase = ExprValue->GetResultType(_Program);

        switch (ExprValue->GetNodeType())
        {

        case EAstNodeType::Invoke_ArchetypeInstantiation:
        {
            // Archetypes look like:              @TypeName{ arg0 := value0, arg1 := value1 ... }
            // The VST looks like this:
            // Clause:                            @TypeName { A := 0 } breaks down into
            //     Macro:
            //         [0]Identifier:             TypeName
            //         [1]Clause:
            //             [0]Definition:         arg0 := value0
            //                 [0]Identifier:     arg0
            //                 [1]Clause:
            //                     [0]TYPE:       value0
            //             [1]Definition:         arg1 := value1
            //                 [0]Identifier:     arg1
            //                 [1]Clause:
            //                     [0]TYPE:       value1

            TSPtr<CExprArchetypeInstantiation> ArchInstValue = ExprValue.As<CExprArchetypeInstantiation>();

            const CClass* AttributeClass = GetClassForExpression(ExprValue);

            CUTF8String AttributeName;

            // special case for parametric types
            if (ArchInstValue->_ClassAst->GetNodeType() == EAstNodeType::Invoke_Invocation)
            {
                TSPtr<CExprInvocation> ExprInvoke = ArchInstValue->_ClassAst.As<CExprInvocation>();
                const CFunctionType* FuncType = ExprInvoke->GetResolvedCalleeType();
                if (const CTypeType* ReturnTypeType = FuncType->GetReturnType().GetNormalType().AsNullable<CTypeType>())
                {
                    AttributeName = ReturnTypeType->PositiveType()->AsCode();
                }
            }
            else
            {
                AttributeName = GetDependencyName(*AttributeClass->Definition());
            }

            TSRef<Identifier> AttribIdent = TSRef<Identifier>::New(AttributeName, NullWhence());

            TSRef<Clause> ValueClause = TSRef<Clause>::New(NullWhence(), Clause::EForm::Synthetic, Clause::EPunctuation::Braces);

            // iterate each argument clause and append
            for (const TSRef<uLang::CExpressionBase>& AttribArg : ArchInstValue->Arguments())
            {
                TSPtr<Identifier> ArgIdent;
                TSPtr<Clause> ArgValueClause;
                if (AttribArg->GetNodeType() == EAstNodeType::Definition)
                {
                    TSRef<CExprDefinition> AttribArgDef = AttribArg.As<CExprDefinition>();

                    { // IDENT
                        TSPtr<CExprIdentifierData> AttribArgIdentData = AttribArgDef->Element().As<CExprIdentifierData>();
                        ArgIdent.SetNew(AttribArgIdentData->GetName().AsStringView(), NullWhence());
                    }

                    { // Value
                        TSPtr<CExprInvokeType> AttribArgValue = AttribArgDef->Value().As<CExprInvokeType>();
                        TSPtr<Node> ArgValue = GenerateForExpression(AttribArgValue->_Argument);

                        ArgValueClause.SetNew(NullWhence(), Clause::EForm::NoSemicolonOrNewline, Clause::EPunctuation::Unknown);
                        ArgValueClause->AppendChild(ArgValue.AsRef());
                    }
                }

                TSRef<Definition> Arg = TSRef<Definition>::New(NullWhence(), ArgIdent.AsRef(), ArgValueClause.AsRef());
                ValueClause->AppendChild(Arg);
            }

            TArray<TSRef<Clause>> ValueClauses = { ValueClause };

            ResultValue = TSRef<Macro>::New(NullWhence(), AttribIdent, TArray<TSRef<Clause>>(ValueClauses));
        }
        break;

        case EAstNodeType::Literal_Logic:
        {
            TSPtr<CExprLogic> LogicValue = ExprValue.As<CExprLogic>();
            ResultValue = TSRef<Identifier>::New(LogicValue->_Value ? "true" : "false", NullWhence());
        }
        break;

        case EAstNodeType::Literal_Number:
        {
            TSPtr<CExprNumber> Number = ExprValue.As<CExprNumber>();
            if (Number->IsFloat())
            {
                CUTF8String FloatStr = uLang::CUTF8String("%lf", Number->GetFloatValue());
                ResultValue = TSRef<FloatLiteral>::New(FloatStr, FloatLiteral::EFormat::F64, NullWhence()).As<Node>();
            }
            else
            {
                CUTF8String IntStr = uLang::CUTF8String("%ld", Number->GetIntValue());
                ResultValue = TSRef<IntLiteral>::New(IntStr, NullWhence());
            }
        }
        break;

        case EAstNodeType::Literal_Char:
        {
            TSPtr<CExprChar> CharValue = ExprValue.As<CExprChar>();
            switch (CharValue->_Type)
            {
            case CExprChar::EType::UTF8CodeUnit:
                ResultValue = TSRef<CharLiteral>::New(uLang::CUTF8String("0o%X", CharValue->_CodePoint), CharLiteral::EFormat::UTF8CodeUnit, NullWhence()).As<Node>();
                break;
            case CExprChar::EType::UnicodeCodePoint:
                ResultValue = TSRef<CharLiteral>::New(uLang::CUTF8String("0u%X", CharValue->_CodePoint), CharLiteral::EFormat::UnicodeCodePoint, NullWhence()).As<Node>();
                break;
            default:
                _Diagnostics->AppendGlitch(
                    SGlitchResult(EDiagnostic::ErrDigest_Unimplemented, uLang::CUTF8String("Unknown character format type.")),
                    _CurrentGlitchAst);
                break;
            }

        }
        break;

        case EAstNodeType::Literal_String:
        {
            TSPtr<CExprString> StringValue = ExprValue.As<CExprString>();
            ResultValue = TSRef<StringLiteral>::New(NullWhence(), StringValue->_String).As<Node>();
        }
        break;

        case EAstNodeType::Literal_Path:
        {
            TSPtr<CExprPath> PathValue = ExprValue.As<CExprPath>();
            ResultValue = TSRef<PathLiteral>::New(PathValue->_Path, NullWhence()).As<Node>();
        }
        break;

        case EAstNodeType::Literal_Enum:
        {
            TSPtr<CExprEnumLiteral> EnumValue = ExprValue.As<CExprEnumLiteral>();
            ResultValue = TSRef<Identifier>::New(EnumValue->_Enumerator->AsCode(), NullWhence()).As<Node>();
        }
        break;

        case EAstNodeType::Literal_Type:
        {
            TSPtr<CExprType> TypeValue = ExprValue.As<CExprType>();
            ResultValue = GenerateForType(TypeBase);
        }
        break;

        case EAstNodeType::Identifier_Class:
        {
            TSPtr<CExprIdentifierClass> ClassIdent = ExprValue.As<CExprIdentifierClass>();
            ResultValue = TSRef<Identifier>::New(GetDependencyName(*ClassIdent->GetClass(_Program)->Definition()), NullWhence()).As<Node>();
        }
        break;
        case EAstNodeType::Identifier_Data:
        {
            TSPtr<CExprIdentifierData> DataIdent = ExprValue.As<CExprIdentifierData>();
            ResultValue = TSRef<Identifier>::New(DataIdent->_DataDefinition.AsNameStringView(), NullWhence()).As<Node>();
        }
        break;

        case EAstNodeType::Invoke_MakeOption:
        {
            TSPtr<CExprMakeOption> MakeOptionExpr = ExprValue.As<CExprMakeOption>();
            if (TSPtr<CExpressionBase> SubExpr = MakeOptionExpr->Operand())
            {
                TSPtr<Clause> OptionElementClause = TSPtr<Clause>::New(NullWhence(), Clause::EForm::NoSemicolonOrNewline, Clause::EPunctuation::Braces);

                OptionElementClause->AppendChild(GenerateForExpression(SubExpr));

                TArray<TSRef<Clause>> MacroClauseArray;
                MacroClauseArray.Add(OptionElementClause.AsRef());

                ResultValue = TSRef<Macro>::New(NullWhence(),
                    TSRef<Identifier>::New("option", NullWhence()),
                    MacroClauseArray
                ).As<Node>();
            }
            else
            {
                // unset option
                ResultValue = TSRef<Identifier>::New("false", NullWhence());
            }
        }
        break;

        case EAstNodeType::Invoke_MakeArray:
        {
            const CArrayType& ArrayNormalType = TypeBase->GetNormalType().AsChecked<CArrayType>();
            const CTypeBase* InnerType = ArrayNormalType.GetInnerType();
            if (InnerType == &_Program._char8Type)
            {
                // string
                TSPtr<CExprString> StringValue = ExprValue.As<CExprString>();
                ResultValue = TSRef<StringLiteral>::New(NullWhence(), StringValue->_String).As<Node>();
            }
            else
            {
                TSRef<Clause> ArrayElementClause = TSRef<Clause>::New(NullWhence(), Clause::EForm::NoSemicolonOrNewline, Clause::EPunctuation::Braces);

                TSPtr<CExprMakeArray> MakeArrayExpr = ExprValue.As<CExprMakeArray>();
                for (TSPtr<CExpressionBase> SubExpr : MakeArrayExpr->GetSubExprs())
                {
                    ArrayElementClause->AppendChild(GenerateForExpression(SubExpr));
                }

                TArray<TSRef<Clause>> MacroClauseArray;
                MacroClauseArray.Add(ArrayElementClause);

                ResultValue = TSRef<Macro>::New(NullWhence(),
                    TSRef<Identifier>::New("array", NullWhence()),
                    MacroClauseArray
                ).As<Node>();
            }
        }
        break;

        // Currently Unsupported
        case EAstNodeType::Error_:
        case EAstNodeType::Placeholder_:
        case EAstNodeType::External:
        case EAstNodeType::PathPlusSymbol:
        case EAstNodeType::Literal_Function:
        case EAstNodeType::Identifier_Unresolved:
        case EAstNodeType::Identifier_Module:
        case EAstNodeType::Identifier_ModuleAlias:
        case EAstNodeType::Identifier_Enum:
        case EAstNodeType::Identifier_Interface:
        case EAstNodeType::Identifier_TypeAlias:
        case EAstNodeType::Identifier_TypeVariable:
        case EAstNodeType::Identifier_Function:
        case EAstNodeType::Identifier_OverloadedFunction:
        case EAstNodeType::Identifier_Self:
        case EAstNodeType::Identifier_Local:
        case EAstNodeType::Identifier_BuiltInMacro:
        case EAstNodeType::Definition:
        case EAstNodeType::MacroCall:
        case EAstNodeType::Invoke_Invocation:
        case EAstNodeType::Invoke_UnaryArithmetic:
        case EAstNodeType::Invoke_BinaryArithmetic:
        case EAstNodeType::Invoke_ShortCircuitAnd:
        case EAstNodeType::Invoke_ShortCircuitOr:
        case EAstNodeType::Invoke_LogicalNot:
        case EAstNodeType::Invoke_Comparison:
        case EAstNodeType::Invoke_QueryValue:
        case EAstNodeType::Invoke_TupleElement:
        case EAstNodeType::Invoke_MakeMap:
        case EAstNodeType::Invoke_MakeTuple:
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
        case EAstNodeType::Flow_CodeBlock:
        case EAstNodeType::Flow_Let:
        case EAstNodeType::Flow_Defer:
        case EAstNodeType::Flow_If:
        case EAstNodeType::Flow_Iteration:
        case EAstNodeType::Flow_Loop:
        case EAstNodeType::Flow_Break:
        case EAstNodeType::Flow_Return:
        case EAstNodeType::Flow_ProfileBlock:
        case EAstNodeType::Ir_For:
        case EAstNodeType::Ir_ForBody:
        case EAstNodeType::Ir_ArrayAdd:
        case EAstNodeType::Ir_MapAdd:
        case EAstNodeType::Ir_ArrayUnsafeCall:
        case EAstNodeType::Ir_ConvertToDynamic:
        case EAstNodeType::Ir_ConvertFromDynamic:
        case EAstNodeType::Concurrent_Sync:
        case EAstNodeType::Concurrent_Rush:
        case EAstNodeType::Concurrent_Race:
        case EAstNodeType::Concurrent_SyncIterated:
        case EAstNodeType::Concurrent_RushIterated:
        case EAstNodeType::Concurrent_RaceIterated:
        case EAstNodeType::Concurrent_Branch:
        case EAstNodeType::Concurrent_Spawn:
        case EAstNodeType::Definition_Module:
        case EAstNodeType::Definition_Enum:
        case EAstNodeType::Definition_Interface:
        case EAstNodeType::Definition_Class:
        case EAstNodeType::Definition_Data:
        case EAstNodeType::Definition_IterationPair:
        case EAstNodeType::Definition_Function:
        case EAstNodeType::Definition_TypeAlias:
        case EAstNodeType::Definition_Using:
        case EAstNodeType::Definition_Import:
        case EAstNodeType::Definition_Where:
        case EAstNodeType::Definition_Var:
        case EAstNodeType::Definition_ScopedAccessLevel:
        case EAstNodeType::Invoke_MakeNamed:
        case EAstNodeType::Context_Project:
        case EAstNodeType::Context_CompilationUnit:
        case EAstNodeType::Context_Package:
        case EAstNodeType::Context_Snippet:
        default:
            _Diagnostics->AppendGlitch(
                SGlitchResult(EDiagnostic::ErrDigest_Unimplemented, uLang::CUTF8String("Unsupported expression type in digest generation.")),
                _CurrentGlitchAst);
            break;
        }

        return ResultValue.AsRef();
    }

    template<typename Func>
    void GenerateForAttributeArchetype(TSPtr<uLang::CExprArchetypeInstantiation> AttributeExpr, const CClass* AttributeClass, const Func& SelectAttributable) const
    {
        using namespace Verse::Vst;

        // Determine which attributable to put the attribute on
        TSRef<Verse::Vst::Node> Attributable = SelectAttributable(AttributeClass);
        
        if (!_bIncludeEpicInternalDefinitions && IsEpicInternalOnlyAttributeClass(*AttributeClass->_Definition))
        {
            // Filter out Epic-internal attributes from public-only digests.
            return;
        }

        TSRef<Node> MacroInst = GenerateForExpression(AttributeExpr);
        MacroInst->SetNewLineAfter(true);  // For digest purposes, we force newlines after so that it looks neater.

        // attribute nodes must be wrapped in a Clause node (elsewhere used to preserve source comments)
        TSRef<Clause> WrapperClause = TSRef<Clause>::New(MacroInst.As<Node>(), NullWhence(), Clause::EForm::IsAppendAttributeHolder);
        Attributable->AppendAux(WrapperClause);
    }

    template<typename Func>
    void GenerateForAttributeGeneric(const CClass* AttributeClass, const uLang::TOptional<CUTF8String>& TextValue, const Func& SelectAttributable) const
    {
        using namespace Verse::Vst;

        // Determine which attributable to put the attribute on
        TSRef<Verse::Vst::Node> Attributable = SelectAttributable(AttributeClass);
        if (AttributeClass == _Program._doc_attribute.Get() && TextValue.IsSet())
        {
            // Replace doc comments with line comments in the digest, regardless of whether it includes epic_internal definitions.
            for (const CUTF8String& CommentString : ReformatDocCommentAsComments(TextValue.GetValue()))
            {
                TSRef<Comment> NewComment = TSRef<Comment>::New(Comment::EType::line, CommentString, NullWhence());
                NewComment->SetNumNewLinesAfter(1);
                Attributable->AppendPrefixComment(NewComment);
            }
            return;
        }
        else if (!_bIncludeEpicInternalDefinitions && IsEpicInternalOnlyAttributeClass(*AttributeClass->_Definition))
        {
            // Filter out Epic-internal attributes from public-only digests.
            return;
        }
        else if (AttributeClass == _Program._getterClass || AttributeClass == _Program._setterClass)
        {
            // getters/setters are special; we don't want them to appear in any digests, regardless
            // of access level
            return;
        }

        CUTF8StringView AttributeName = GetDependencyName(*AttributeClass->Definition());

        // SOL-972 & SOL-2577: Some attributes are implemented as a function and a class, and they can't have the same name.
        // The function has the same name as the attribute, and is what we need here.
        // The class has "_attribute" appended, and is what we have.
        // This is not done for all attributes, hence the if-statement.
        if (AttributeName.EndsWith("_attribute")) {
            AttributeName = CUTF8StringView(AttributeName._Begin, AttributeName._End - strlen("_attribute"));
        }

        if (TextValue.IsSet())
        {
            TSRef<StringLiteral> Value = TSRef<StringLiteral>::New(NullWhence(), TextValue.GetValue());
            TSRef<Clause> ValueClause = TSRef<Clause>::New(Value.As<Node>(), NullWhence(), Clause::EForm::Synthetic);
            TSRef<Identifier> Name = TSRef<Identifier>::New(AttributeName, NullWhence());
            Name->SetTag(PrePostCall::Op::Expression);
            ValueClause->SetTag(PrePostCall::Op::SureCall);
            TSRef<PrePostCall> Call = TSRef<PrePostCall>::New(NullWhence());
            Call->AppendChild(Name);
            Call->AppendChild(ValueClause);
            Call->SetNewLineAfter(true);  // For digest purposes, we force newlines after so that it looks neater.
            // attribute nodes must be wrapped in a Clause node (elsewhere used to preserve source comments)
            TSRef<Clause> WrapperClause = TSRef<Clause>::New(Call.As<Node>(), NullWhence(), Clause::EForm::IsAppendAttributeHolder);
            Attributable->AppendAux(WrapperClause);
        }
        else
        {
            // attribute nodes must be wrapped in a Clause node (elsewhere used to preserve source comments)
            TSRef<Identifier> AttributeIdentifier = TSRef<Identifier>::New(AttributeName, NullWhence());
            // NOTE: (yiliang.siew) We do this so that `<epic_internal>` and other attributes that are suffixed to the identifier
            // do not get newlines after them, only the prefix attributes on the definition itself.
            if (Attributable->IsA<Definition>() || Attributable->IsA<TypeSpec>())
            {
                AttributeIdentifier->SetNewLineAfter(true);
            }
            TSRef<Clause> WrapperClause = TSRef<Clause>::New(AttributeIdentifier.As<Node>(), NullWhence(), Clause::EForm::IsAppendAttributeHolder);
            Attributable->AppendAux(WrapperClause);
        }
    }

    template<typename Func>
    void GenerateForAttributesGeneric(const TArray<SAttribute>& Attributes, const TOptional<SAccessLevel>& AccessLevel, const Func& SelectAttributable) const
    {
        for (const SAttribute& Attribute : Attributes)
        {
            // Determine the attribute class
            const CClass* AttributeClass = GetClassForExpression(Attribute._Expression);

            if (ULANG_ENSUREF(AttributeClass, "Unrecognized attribute type."))
            {
                const bool bIsAccessLevelAttribute = 
                       AttributeClass == _Program._publicClass
                    || AttributeClass == _Program._internalClass
                    || AttributeClass == _Program._protectedClass
                    || AttributeClass == _Program._privateClass
                    || AttributeClass == _Program._epicInternalClass;

                if (AttributeClass->IsSubclassOf(*_Program._scopedClass))
                {
                    GenerateForScopedAttribute(*static_cast<const CScopedAccessLevelDefinition*>(AttributeClass), SelectAttributable);
                }
                else if (!bIsAccessLevelAttribute)
                {
                    if (Attribute._Expression->GetNodeType() == EAstNodeType::Invoke_ArchetypeInstantiation)
                    {
                        GenerateForAttributeArchetype(Attribute._Expression.As<CExprArchetypeInstantiation>(), AttributeClass, SelectAttributable);
                    }
                    else
                    {
                        const uLang::TOptional<CUTF8String> TextValue = CAttributable::GetAttributeTextValue(Attributes, AttributeClass, _Program);
                        GenerateForAttributeGeneric(AttributeClass, TextValue, SelectAttributable);
                    }
                }
            }
        }

        if (AccessLevel.IsSet())
        {
            switch(AccessLevel.GetValue()._Kind)
            {
            case SAccessLevel::EKind::Public: GenerateForAttributeGeneric(_Program._publicClass, {}, SelectAttributable); break;
            case SAccessLevel::EKind::Internal: GenerateForAttributeGeneric(_Program._internalClass, {}, SelectAttributable); break;
            case SAccessLevel::EKind::Protected: GenerateForAttributeGeneric(_Program._protectedClass, {}, SelectAttributable); break;
            case SAccessLevel::EKind::Private: GenerateForAttributeGeneric(_Program._privateClass, {}, SelectAttributable); break;
            case SAccessLevel::EKind::Scoped: /* handled above */ break;
            case SAccessLevel::EKind::EpicInternal: GenerateForAttributeGeneric(_Program._epicInternalClass, {}, SelectAttributable); break;
            default: ULANG_UNREACHABLE();
            }
        }
    }

    void GenerateForAttributes(const CAttributable& Attributes, const TOptional<SAccessLevel>& AccessLevel, const TSRef<Verse::Vst::Node>& Attributable) const
    {
        GenerateForAttributesGeneric(Attributes._Attributes, AccessLevel, [&Attributable] (const CClass*) { return Attributable; });
    }

    void GenerateForAttributes(const TArray<SAttribute>& Attributes, const CDefinition& Definition, const TSRef<Verse::Vst::Identifier>& NameAttributable, const TSRef<Verse::Vst::Node>& DefAttributable) const
    {
        GenerateForAttributesGeneric(
            Attributes,
            Definition.SelfAccessLevel(),
            [this, &NameAttributable, &DefAttributable] (const CClass* AttributeClass)
            {
                TSPtr<Verse::Vst::Node> Attributable;
                if (AttributeClass->HasAttributeClass(_Program._attributeScopeName, _Program))
                {
                    Attributable = NameAttributable;
                }
                else
                {
                    Attributable = DefAttributable;
                }
                return Attributable.AsRef();
            });
    }

    void GenerateForAttributes(const CDefinition& Definition, const TSRef<Verse::Vst::Identifier>& NameAttributable, const TSRef<Verse::Vst::Node>& DefAttributable) const
    {
        GenerateForAttributes(Definition._Attributes, Definition, NameAttributable, DefAttributable);
    }

    TSRef<Verse::Vst::Macro> GenerateExternalMacro() const
    {
        using namespace Verse::Vst;
        return TSRef<Macro>::New(
            NullWhence(),
            TSRef<Identifier>::New("external", NullWhence()),
            ClauseArray{TSRef<Clause>::New(NullWhence(), Clause::EForm::NoSemicolonOrNewline)});
    }

    bool IsUsable(const CDefinition& Definition) const
    {
        SAccessLevel AccessLevel;
        if (Definition._EnclosingScope.GetKind() == CScope::EKind::Function)
        {
            AccessLevel = static_cast<const CFunction&>(Definition._EnclosingScope).DerivedAccessLevel();
        }
        else
        {
            AccessLevel = Definition.DerivedAccessLevel();
        }

        if (AccessLevel._Kind == Cases<SAccessLevel::EKind::Private, SAccessLevel::EKind::Internal>)
        {
            return false;
        }

        bool bSpecialException = false;
        bSpecialException |= IsEpicInternalLocalizationDefinition(Definition);

        if (AccessLevel._Kind == SAccessLevel::EKind::EpicInternal
            && !_bIncludeEpicInternalDefinitions
            // Don't cull inheriting from epic_internal definitions in the intrinsically defined
            // built-in snippet (e.g.attribute).
            && !Definition.IsBuiltIn()
            && !bSpecialException)
        {
            return false;
        }

        const CAstPackage* DefinitionPackage = Definition._EnclosingScope.GetPackage();
        if (!_bIncludeEpicInternalDefinitions &&
            DefinitionPackage && DefinitionPackage->_VerseScope == EVerseScope::InternalAPI)
        {
            return false;
        }

        return true;
    }

    // For a given class, find the nearest ancestor that is public
    const CClass* PublifySuperclass(const CClass* Class) const
    {
        while (Class && !IsUsable(*Class->_Definition))
        {
            Class = Class->_Superclass;
        }

        return Class;
    }

    // For a given single interface, find the set of nearest ancestors that are all public
    TArray<const CInterface*> PublifySuperInterface(const CInterface* Interface) const
    {
        // Is it public?
        if (IsUsable(*Interface))
        {
            // Yes, return as-is
            return {Interface};
        }

        // No, find public super interfaces
        TArray<const CInterface*> Result;
        for (const CInterface* SuperInterface : Interface->_SuperInterfaces)
        {
            TArray<const CInterface*> PublicSuperInterfaces = PublifySuperInterface(SuperInterface);
            for (const CInterface* PublicSuperInterface : PublicSuperInterfaces)
            {
                Result.Add(PublicSuperInterface);
            }
        }

        return Result;
    }

    // For a given set of interfaces, find the set of nearest ancestors that are all public
    TArray<const CInterface*> PublifySuperInterfaces(const TArray<CInterface*>& Interfaces) const
    {
        TArray<const CInterface*> Result;
        for (const CInterface* Interface : Interfaces)
        {
            TArray<const CInterface*> PublicSuperInterfaces = PublifySuperInterface(Interface);
            for (const CInterface* PublicSuperInterface : PublicSuperInterfaces)
            {
                Result.Add(PublicSuperInterface);
            }
        }

        return Result;
    }

    TArray<const CNominalType*> PublifyType(const CTypeBase* TypeToPublify, TArray<const CInterface*>& VisitedPublicSuperInterfaces) const
    {
        if (const CClass* Class = TypeToPublify->GetNormalType().AsNullable<CClass>())
        {
            if (IsUsable(*Class->_Definition))
            {
                return { Class };
            }
            else
            {
                TArray<const CNominalType*> Publified;

                if (Class->_Superclass)
                {
                    Publified.Append(PublifyType(Class->_Superclass, VisitedPublicSuperInterfaces));
                }
                
                for (const CInterface* SuperInterface : PublifySuperInterfaces(Class->_SuperInterfaces))
                {
                    int32_t NumVisitedSuperInterfaces = VisitedPublicSuperInterfaces.Num();
                    if (VisitedPublicSuperInterfaces.AddUnique(SuperInterface) == NumVisitedSuperInterfaces)
                    {
                        Publified.Add(SuperInterface);
                    }
                }

                return Publified;
            }
        }
        else if (const CInterface* Interface = TypeToPublify->GetNormalType().AsNullable<CInterface>())
        {
            if (IsUsable(*Interface))
            {
                int32_t NumVisitedSuperInterfaces = VisitedPublicSuperInterfaces.Num();
                if (VisitedPublicSuperInterfaces.AddUnique(Interface) == NumVisitedSuperInterfaces)
                {
                    return { Interface };
                }
                else
                {
                    return {};
                }
            }
            
            TArray<const CNominalType*> Publified;

            for (const CInterface* SuperInterface : PublifySuperInterfaces(Interface->_SuperInterfaces))
            {
                int32_t NumVisitedSuperInterfaces = VisitedPublicSuperInterfaces.Num();
                if (VisitedPublicSuperInterfaces.AddUnique(SuperInterface) == NumVisitedSuperInterfaces)
                {
                    Publified.Add(SuperInterface);
                }
            }

            return Publified;
        }

        return {};
    }

    bool DefinitionSubjectToScopedAccess(const CDefinition& Definition) const
    {
        if (Definition.DerivedAccessLevel()._Kind == SAccessLevel::EKind::Scoped)
        {
            return true;
        }

        for (const CScope* Scope = &Definition._EnclosingScope; Scope != nullptr; Scope = Scope->GetParentScope())
        {
            if (const CDefinition* ScopeDefinition = Scope->ScopeAsDefinition())
            {
                if (ScopeDefinition->DerivedAccessLevel()._Kind == SAccessLevel::EKind::Scoped)
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool ShouldGenerate(const CDefinition& Definition, bool bCheckPackage = true) const
    {
        /*
         * We have this check here because if we have the code:
         * ```
         * foothis<epic_internal> := interface:
         *     bar<public>():string
         *
         * baz<public> := class(foothis):
         *     bar<override>():string=
         *     return "test"
         * ```
         *
         * We still want to generate the following in the digest:
         *
         * ```
         * foothis<epic_internal> := interface:
         *    bar<public>():[]char

         * baz<public> := class:
         *     bar<override>():[]char = external {}
         * ```
         *
         * Since `baz.bar` is public and marked as `<override>`, digest compilation would fail otherwise if `foothis.bar` was not present.
         */
        auto IsOrHasPubliclyOverriddenAbstractMethod = [](const CDefinition& Definition, auto&& IsPubliclyOverriddenAbstractMethod) -> bool
        {
            if (Definition.IsA<CInterface>())
            {
                for (const auto& ChildDefinition : Definition.AsChecked<CInterface>().GetDefinitions())
                {
                    if (IsPubliclyOverriddenAbstractMethod(*ChildDefinition, IsPubliclyOverriddenAbstractMethod))
                    {
                        return true;
                    }
                }
                return false;
            }
            const CDefinition* EnclosingDefinition = Definition._EnclosingScope.ScopeAsDefinition();
            if (EnclosingDefinition && EnclosingDefinition->IsA<CInterface>())
            {
                const SResolvedDefinitionArray OtherDefinitions = Definition._EnclosingScope.ResolveDefinition(Definition.GetName(), Definition._Qualifier);
                // The other definitions being looked up would include the original one within the current enclosing scope in the first place.
                for (const SResolvedDefinition& ResolvedDefn : OtherDefinitions)
                {
                    if (ResolvedDefn._Definition != &Definition && ResolvedDefn._Definition->DerivedAccessLevel()._Kind == SAccessLevel::EKind::Public)
                    {
                        // Also walk up enclosing scopes to ensure that this is actually a public-ly accessible symbol.
                        const CScope* CurrentScope = &ResolvedDefn._Definition->_EnclosingScope;
                        while (CurrentScope)
                        {
                            if (CurrentScope->IsAuthoredByEpic())
                            {
                                return false;
                            }
                            CurrentScope = CurrentScope->GetParentScope();
                        }
                        return true;
                    }
                }
            }

            return false;
        };

        if (Definition._EnclosingScope.GetKind() == CScope::EKind::Function)
        {
            return ShouldGenerate(static_cast<const CFunction&>(Definition._EnclosingScope), bCheckPackage);
        }

        // Don't generate digest definitions for the intrinsically defined built-in snippet, or definitions outside the current package.
        const CAstPackage* Package = Definition._EnclosingScope.GetPackage();
        if (Definition.IsBuiltIn()
            || (bCheckPackage && Package && Package != &_Package))
        {
            return false;
        }

        bool bSpecialException = false;

        // Make an exception for the import_as attribute until we can link against a digest without it.
        bSpecialException |= &Definition == _Program._import_as_attribute.Get();
        bSpecialException |= &Definition == _Program._import_as.Get();

        // Make a special exception for localization related functionality that 
        // is epic_internal and needs to be visible to user code
        bSpecialException |= IsEpicInternalLocalizationDefinition(Definition);

        // If this definition is living under a scoped access level, then we can't be sure it's not being accessed from elsewhere
        bool bWithinAScopedScope = DefinitionSubjectToScopedAccess(Definition);

        const SAccessLevel& AccessLevel = Definition.DerivedAccessLevel();

        return AccessLevel._Kind == Cases<SAccessLevel::EKind::Public, SAccessLevel::EKind::Protected>
            || (_bIncludeInternalDefinitions && AccessLevel._Kind == SAccessLevel::EKind::Internal)
            || (_bIncludeEpicInternalDefinitions && AccessLevel._Kind == SAccessLevel::EKind::EpicInternal)
            || bSpecialException
            || (bWithinAScopedScope && AccessLevel._Kind != SAccessLevel::EKind::EpicInternal)
            // If the definition is an abstract method that is being overriden by something that is public,
            // the abstract method itself must be visible in the digest too regardless of its access level.
            || IsOrHasPubliclyOverriddenAbstractMethod(Definition, IsOrHasPubliclyOverriddenAbstractMethod);
    }

    CUTF8StringView GetDependencyName(const CDefinition& Definition) const
    {
        const CModule* UsingModule = Definition._EnclosingScope.GetModule();

        ULANG_ASSERTF(UsingModule != nullptr, "Definition._EnclosingScope does not have a module.");

        if (!_CurrentModule->IsSameOrChildOf(UsingModule)
            && UsingModule != _Program._VerseModule)
        {
            CUTF8String UsingVersePath = UsingModule->GetScopePath('/', CScope::EPathMode::PrefixSeparator);
            if (_Usings.Find(UsingVersePath) == IndexNone)
            {
                const CAstPackage* UsingPackage = Definition._EnclosingScope.GetPackage();
                if (!_bIncludeEpicInternalDefinitions && UsingPackage->_VerseScope == EVerseScope::InternalAPI)
                {
                    _Diagnostics->AppendGlitch(
                        SGlitchResult(EDiagnostic::ErrDigest_DisallowedUsing, uLang::CUTF8String("Package `%s` is publicly visible but its public interface depends on `%s` from package `%s` which is not publicly visible.",
                            *_Package._Name, *GetQualifiedNameString(Definition), *UsingPackage->_Name)),
                        _CurrentGlitchAst);
                }
                else
                {
                    _Usings.Add(UsingVersePath);
                }
            }
        }

        return Definition.AsNameStringView();
    }

    // Temporary helper function for identifying localization-related definitions 
    // that are EpicInternal but are required to appear in the digest for user code
    // to see.
    bool IsEpicInternalLocalizationDefinition(const CDefinition& Definition) const
    {
        bool Result = false;

        Result |= (Definition.DerivedAccessLevel()._Kind == SAccessLevel::EKind::EpicInternal) && Definition.AsNameStringView() == "MakeMessageInternal";
        Result |= (Definition.DerivedAccessLevel()._Kind == SAccessLevel::EKind::EpicInternal) && Definition.AsNameStringView() == "MakeLocalizableValue";
        Result |= (Definition.DerivedAccessLevel()._Kind == SAccessLevel::EKind::EpicInternal) && Definition.AsNameStringView() == "LocalizeValue";
        Result |= (Definition.DerivedAccessLevel()._Kind == SAccessLevel::EKind::EpicInternal) && Definition.AsNameStringView().StartsWith("localizable_");

        return Result;
    }

    const CSemanticProgram& _Program;
    const CAstPackage& _Package;
    TSRef<CDiagnostics> _Diagnostics;
    bool _bIncludeInternalDefinitions;
    bool _bIncludeEpicInternalDefinitions;
    mutable TArray<CUTF8String> _Usings;
    mutable const CModule* _CurrentModule;
    mutable const CAstNode* _CurrentGlitchAst;
    CSymbol _Underscore;
    CSymbol _ParameterizedMaterialSymbol;
    CSymbol _AssetsSymbol;
    const CUTF8String* _Notes;
};

//====================================================================================
// Public API
//====================================================================================

namespace DigestGenerator
{

bool Generate(
    const CSemanticProgram& Program, 
    const CAstPackage& Package,
    bool bIncludeInternalDefinitions,
    bool bIncludeEpicInternalDefinitions,
    const TSRef<CDiagnostics>& Diagnostics,
    const CUTF8String* Notes,
    CUTF8String& OutDigestCode)
{
    CDigestGeneratorImpl Generator(Program, Package, Diagnostics, Notes, bIncludeInternalDefinitions, bIncludeEpicInternalDefinitions);
    return Generator.Generate(OutDigestCode);
}

} // namespace DigestGenerator

} // namespace uLang
