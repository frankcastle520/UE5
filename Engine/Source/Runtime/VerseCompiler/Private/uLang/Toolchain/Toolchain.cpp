// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Toolchain/Toolchain.h"
#include "uLang/Common/Common.h"
#include "uLang/Common/Algo/Cases.h"
#include "uLang/Toolchain/ModularFeatureManager.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/SourceProject/UploadedAtFNVersion.h"
#include "uLang/SourceProject/VerseVersion.h"
#include "uLang/Common/Misc/FloatingPointState.h"
#include "uLang/VerseLocalizationGen.h"
#include "uLang/Syntax/vsyntax_types.h" // HACK_VMSWITCH

namespace uLang
{

namespace Private_ToolchainImpl
{
    template<class InjectionType, typename... ArgsType>
    ULANG_FORCEINLINE bool InvokeApiInjections(const TSRefArray<InjectionType>& ToolchainInjections, const TSRefArray<InjectionType>& BuildInjections, const SBuildContext& BuildContext, ArgsType&&... Args)
    {
        bool bHalt = false;
        for (const TSRef<InjectionType>& Injection : ToolchainInjections)
        {
            bHalt = Injection->Ingest(uLang::ForwardArg<ArgsType>(Args)..., BuildContext);
            if (bHalt)
            {
                break;
            }
        }
        for (const TSRef<InjectionType>& Injection : BuildInjections)
        {
            bHalt = Injection->Ingest(uLang::ForwardArg<ArgsType>(Args)..., BuildContext);
            if (bHalt)
            {
                break;
            }
        }
        return bHalt;
    }

    template<class InjectionType, class PassType, typename... ArgsType>
    ULANG_FORCEINLINE ECompilerResult RunCompilerPrePass(TOptional<TSRef<PassType>> Pass, const TSRefArray<InjectionType>& ToolchainInjections, const TSRefArray<InjectionType>& BuildInjections, const SBuildContext& BuildContext, ArgsType&&... Args)
    {
        ECompilerResult Result = ECompilerResult::Compile_NoOp;
        if (InvokeApiInjections(ToolchainInjections, BuildInjections, BuildContext, uLang::ForwardArg<ArgsType>(Args)...))
        {
            Result = ECompilerResult::Compile_SkippedByInjection;
        }
        else if (!Pass.IsSet())
        {
            Result = ECompilerResult::Compile_SkippedByEmptyPass;
        }

        return Result;
    }

    // HACK_VMSWITCH - remove this once VerseVM is fully brought up
    void HackVerseVMFilterInternal(const uLang::TSRef<Verse::Vst::Node>& Vst, const SBuildContext& BuildContext)
    {
        using namespace Verse::Vst;

        for (int32_t TopLevelChildIndex = 0; TopLevelChildIndex < Vst->GetChildCount(); ++TopLevelChildIndex)
        {
            TSRef<Node> TopLevelNode = Vst->GetChildren()[TopLevelChildIndex];

			HackVerseVMFilterInternal(TopLevelNode, BuildContext);

            if (TopLevelNode->IsA<Macro>())
            {
                Macro& MacroNode = TopLevelNode->As<Macro>();

                if (const Identifier* MacroIdentifier = MacroNode.GetName()->AsNullable<Identifier>())
                {
                    const CUTF8String& MacroName = MacroIdentifier->GetSourceText();

                    const bool bVMExclude = (MacroName == "verse_vm_exclude");
                    const bool bBPExclude = (MacroName == "bp_vm_exclude");
                    if (bVMExclude || bBPExclude)
                    {
                        // Validate the macro has a single clause containing 1+ children.
                        if (MacroNode.GetChildCount() != 2)
                        {
                            BuildContext._Diagnostics->AppendGlitch(
                                SGlitchResult(EDiagnostic::ErrSemantic_Internal,
                                    "'bp_vm_exclude'/'verse_vm_exclude' macro must have exactly 1 clause."),
                                SGlitchLocus(&MacroNode));
                        }
                        else if (MacroNode.GetClause(0)->GetChildCount() == 0)
                        {
                            BuildContext._Diagnostics->AppendGlitch(
                                SGlitchResult(EDiagnostic::ErrSemantic_Internal,
                                    "'bp_vm_exclude'/'verse_vm_exclude' macro clause must contain at least one expression."),
                                SGlitchLocus(MacroNode.GetClause(0)));
                        }
                        else if (MacroNode.GetClause(0)->GetTag<vsyntax::res_t>() != vsyntax::res_none)
                        {
                            BuildContext._Diagnostics->AppendGlitch(
                                SGlitchResult(EDiagnostic::ErrSemantic_Internal,
                                    "'bp_vm_exclude'/'verse_vm_exclude' macro clause must not be preceded by a keyword."),
                                SGlitchLocus(MacroNode.GetClause(0)));
                        }
                        else
                        {
							TopLevelNode->RemoveFromParent(TopLevelChildIndex--);

							auto ShouldInclude = [&]
							{
								if (BuildContext._Params._TargetVM == SBuildParams::EWhichVM::BPVM)
								{
									return !bBPExclude;
								}
								return !bVMExclude;
							};

                            if (ShouldInclude())
                            {
                                // Hoist remaining children of this macro up to the top level
                                NodeArray ClauseChildren = MacroNode.GetClause(0)->TakeChildren();
                                Vst->AccessChildren().Reserve(Vst->GetChildCount() + ClauseChildren.Num());
                                for (const TSRef<Node>& Child : ClauseChildren)
                                {
                                    Vst->AppendChildAt(Child, ++TopLevelChildIndex);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // HACK_VMSWITCH - remove this once VerseVM is fully brought up
    void HackVerseVMFilter(const uLang::TSRef<Verse::Vst::Snippet>& Vst, const SBuildContext& BuildContext)
    {
        HackVerseVMFilterInternal(Vst, BuildContext);
    }
}

/* Public toolchain API
 ******************************************************************************/

TSRef<CToolchain> CreateToolchain(const SToolchainParams& Params)
{
    return TSRef<CToolchain>::New(Params);
}

bool SBuildResults::HasFailure() const
{
    return _IOErrorsFound || IsCompileFailure(_CompilerResult) || (_LinkerResult == ELinkerResult::Link_Failure);
}

SBuildResults& SBuildResults::operator|=(const SBuildResults& Other)
{
    _CompilerResult |= Other._CompilerResult;
    if (_LinkerResult == ELinkerResult::Link_Success || Other._LinkerResult == ELinkerResult::Link_Failure)
    {
        _LinkerResult = Other._LinkerResult;
    }
    return *this;
}

namespace Private
{
bool CaselessLessThan(const CUTF8String& Lhs, const CUTF8String& Rhs)
{
    for (int32_t Index = 0; Index < Lhs.ByteLen() && Index < Rhs.ByteLen(); ++Index)
    {
        UTF8Char UnitA = CUnicode::ToLower_ASCII(Lhs[Index]);
        UTF8Char UnitB = CUnicode::ToLower_ASCII(Rhs[Index]);
        if (UnitA != UnitB) 
        { 
            return UnitA < UnitB; 
        }
    }
    return Lhs.ByteLen() < Rhs.ByteLen();
};
}

/* CToolchain
 ******************************************************************************/

CToolchain::CToolchain(const SToolchainParams& Params)
    : _Params(Params)
{
}

SBuildResults CToolchain::BuildProject(const CSourceProject& SourceProject, const SBuildContext& BuildContext, const SProgramContext& ProgramContext)
{
    using namespace Private_ToolchainImpl;
    CFloatStateSaveRestore FloatStateScopeGuard;

    SBuildResults BuildResults;
    TSPtr<CDiagnostics> SnippetDiagnostics = TSPtr<CDiagnostics>::New();

    TSRef<Verse::Vst::Project> VstProject = TSRef<Verse::Vst::Project>::New(SourceProject.GetName());
    VstProject->_FilePath = SourceProject.GetFilePath();
    VstProject->AccessChildren().Reserve(SourceProject._Packages.Num());
    _ProjectVst = VstProject;

    // Loop over all packages and build a giant Vst
    for (const CSourceProject::SPackage& Package : SourceProject._Packages)
    {
        TSRef<Verse::Vst::Package> VstPackage = TSRef<Verse::Vst::Package>::New(Package._Package->GetName());
        const CSourcePackage::SSettings& PackageSettings = Package._Package->GetSettings();
        VstPackage->_DirPath = Package._Package->GetDirPath();
        VstPackage->_FilePath = Package._Package->GetFilePath();
        VstPackage->_VersePath = PackageSettings._VersePath;
        VstPackage->_DependencyPackages = PackageSettings._DependencyPackages;
        VstPackage->_VniDestDir = PackageSettings._VniDestDir;
        VstPackage->_Role = PackageSettings._Role;
        VstPackage->_VerseScope = PackageSettings._VerseScope;
        VstPackage->_bTreatModulesAsImplicit = PackageSettings._bTreatModulesAsImplicit;
        VstPackage->_UploadedAtFNVersion = PackageSettings.GetUploadedAtFNVersion(BuildContext._Params._UploadedAtFNVersion);
        VstPackage->_VerseVersion = PackageSettings._VerseVersion;
        VstPackage->_bAllowExperimental = PackageSettings._bAllowExperimental;
        if (VstPackage->_FilePath.IsFilled())
        {
            VstPackage->SetWhence(Verse::SLocus(0, 0, 0, 0)); // Set locus to beginning of package file
        }
        VstPackage->AccessChildren().Reserve(Package._Package->_RootModule->_Submodules.Num());
        VstProject->AppendChild(VstPackage);

        // Lambda for processing a single snippet
        auto ProcessSnippet = [this, &BuildContext, &BuildResults, &SnippetDiagnostics](const TSRef<ISourceSnippet>& SourceSnippet, const TSRef<Verse::Vst::Node>& ParentVstNode)
        {
            // Do we have a ready-made Vst snippet?
            const uLang::CUTF8String SourceSnippetPath = SourceSnippet->GetPath();
            TOptional<TSRef<Verse::Vst::Snippet>> VstSnippet = SourceSnippet->GetVst();
            if (VstSnippet.GetResult() == EResult::Error)
            {
                BuildContext._Diagnostics->AppendGlitch({
                uLang::EDiagnostic::ErrSystem_CannotReadVst,
                uLang::CUTF8String("Error getting Vst contents of snippet `%s`.", *SourceSnippetPath) });
                BuildResults._IOErrorsFound = true;
            }
            else
            {
                // Generate Vst from text
                TOptional<CUTF8String> Text = SourceSnippet->GetText();
                if (Text)
                {
                    SBuildResults SnippetBuildResults;

                    // Temporarily swap diagnostics so each snippet sees a clean diagnostics object
                    Swap(*BuildContext._Diagnostics, *SnippetDiagnostics);

                    TSRef<Verse::Vst::Snippet> Snippet = TSRef<Verse::Vst::Snippet>::New(SourceSnippetPath);
                    VstSnippet.Emplace(Snippet);
                    SourceSnippet->SetVst(Snippet);
                    ParentVstNode->AppendChild(*VstSnippet);
                    SnippetBuildResults._CompilerResult = ParseSnippet(*VstSnippet, *Text, BuildContext);

                    BuildResults |= SnippetBuildResults;

                    // Swap back diagnostics and merge glitches
                    Swap(*BuildContext._Diagnostics, *SnippetDiagnostics);
                    BuildContext._Diagnostics->Append(Move(*SnippetDiagnostics));
                }
                else
                {
                    ULANG_ENSUREF(Text.GetResult() != EResult::Unspecified, "ISourceSnippet has neither text nor Vst.");

                    BuildContext._Diagnostics->AppendGlitch({
                        uLang::EDiagnostic::ErrSystem_CannotReadText,
                        uLang::CUTF8String("Error getting text contents of snippet `%s`.", *SourceSnippet->GetPath()) });
                    BuildResults._IOErrorsFound = true;
                }
            }
        };

        // Determine if to process source or digest
        if (VstPackage->_Role == ExternalPackageRole && Package._Package->_Digest.IsSet())
        {
            // Just parse the digest of this package
            ProcessSnippet(Package._Package->_Digest->_Snippet, VstPackage);

            // Use the digest's version instead of the source version.
            // This can differ when the digest comes from e.g. cooked content that wasn't created from the local source files.
            VstPackage->_VerseVersion.Emplace(Package._Package->_Digest->_EffectiveVerseVersion);
        }
        else
        {
            const bool bSortFiles = VerseFN::UploadedAtFNVersion::SortSourceFilesLexicographically(VstPackage->_UploadedAtFNVersion);
            const bool bSortSubmodules = VerseFN::UploadedAtFNVersion::SortSourceSubmodulesLexicographically(VstPackage->_UploadedAtFNVersion);

            // Parse the full source of this package
            auto ProcessModule = [&ProcessSnippet, bSortFiles, bSortSubmodules](const CSourceModule& SourceModule, const TSRef<Verse::Vst::Node>& VstModule, auto& ProcessModule) -> void
            {
                // Ensure a consistent order for files within the module, which are not added in any particular order.
                // This makes the few remaining order-dependent behaviors in the compiler deterministic.
                // ASCII-case-insensitive lexicographic ordering is close to the typical NTFS ordering that many
                // source files are gathered with. (The asset registry typically reverses this order.)
                TSRefArray<ISourceSnippet> SortedSnippets = SourceModule._SourceSnippets;
                if (bSortFiles)
                {
                    SortedSnippets.Sort([](ISourceSnippet* A, ISourceSnippet* B) { return Private::CaselessLessThan(A->GetPath(), B->GetPath()); });
                }

                // Process source snippets
                for (const TSRef<ISourceSnippet>& SourceSnippet : SortedSnippets)
                {
                    ProcessSnippet(SourceSnippet, VstModule);
                }

                TSRefArray<CSourceModule> SortedSubmodules = SourceModule._Submodules;
                if (bSortSubmodules)
                {
                    SortedSubmodules.Sort([](CSourceModule* A, CSourceModule* B) { return Private::CaselessLessThan(A->GetFilePath(), B->GetFilePath()); });
                }

                // And recurse into submodules
                VstModule->AccessChildren().Reserve(SortedSubmodules.Num());
                for (const TSRef<CSourceModule>& Submodule : SortedSubmodules)
                {
                    // Create VST node equivalent for the submodule
                    TSRef<Verse::Vst::Module> VstSubmodule = TSRef<Verse::Vst::Module>::New(Submodule->GetName());
                    VstSubmodule->_FilePath = Submodule->GetFilePath();
                    VstModule->AppendChild(VstSubmodule);
                    // And recursively process it
                    ProcessModule(*Submodule, VstSubmodule, ProcessModule);
                }
            };

            ProcessModule(*Package._Package->_RootModule, VstPackage, ProcessModule);
        }
    }

    // Now, run semantic analysis on all Vst nodes
    if (!BuildResults.HasFailure())
    {
        BuildResults._CompilerResult |= CompileVst(VstProject, BuildContext, ProgramContext);
        
        if (!IsAbortedCompile(BuildResults._CompilerResult) && !BuildContext._Params._bSemanticAnalysisOnly && BuildContext._Params._LinkType != SBuildParams::ELinkParam::Skip)
        {
            BuildResults._LinkerResult = Link(BuildContext, ProgramContext);
            BuildResults._Statistics = BuildContext._Diagnostics->GetStatistics();
        }
    }

    return BuildResults;
}

ECompilerResult CToolchain::ParseSnippet(const uLang::TSRef<Verse::Vst::Snippet>& OutVst, const CUTF8StringView& TextSnippet, const SBuildContext& BuildContext)
{
    using namespace Private_ToolchainImpl;
    CFloatStateSaveRestore FloatStateScopeGuard;
    ECompilerResult Result = RunCompilerPrePass(_Params.Parser, _Params.LayerInjections._PreParseInjections, BuildContext._AddedInjections._PreParseInjections, BuildContext, TextSnippet);

    if (!IsAbortedCompile(Result))
    {
        (*_Params.Parser)->ProcessSnippet(OutVst, TextSnippet, BuildContext);
        Result |= ECompilerResult::Compile_RanSyntaxPass;

        if (!BuildContext._Diagnostics->HasErrors())
        {
            // HACK_VMSWITCH - remove this once VerseVM is fully brought up
            const Verse::Vst::Package* SnippetPackage = OutVst->GetParentOfType<Verse::Vst::Package>();
            // Only filter code that is known to not be user code
            if (SnippetPackage
                && SnippetPackage->_VerseScope != uLang::EVerseScope::PublicUser)
            {
                Private_ToolchainImpl::HackVerseVMFilter(OutVst, BuildContext);
            }

            for (const TSRef<IPostVstFilter>& VstFilter : _Params.PostVstFilters)
            {
                VstFilter->Filter(OutVst, BuildContext);
            }
        }
        
        // If either the parser or an Vst filter produced errors, return that there was a syntax error.
        if (BuildContext._Diagnostics->HasErrors())
        {
            Result |= ECompilerResult::Compile_SyntaxError;
        }

        if (!IsAbortedCompile(Result) && InvokeApiInjections(_Params.LayerInjections._PostParseInjections, BuildContext._AddedInjections._PostParseInjections, BuildContext, OutVst))
        {
            Result |= ECompilerResult::Compile_SkippedByInjection;
        }
    }

    return Result;
}

ECompilerResult CToolchain::CompileVst(const TSRef<Verse::Vst::Project>& Vst, const SBuildContext& BuildContext, const SProgramContext& ProgramContext)
{
    CFloatStateSaveRestore FloatStateScopeGuard;
    TOptional<TSRef<CSemanticProgram>> Program;
    ECompilerResult Result = SemanticAnalyzeVst(Program, Vst, BuildContext, ProgramContext);

    if (!BuildContext._Params._bSemanticAnalysisOnly)
    {
        if (!IsAbortedCompile(Result))
        {
            Result |= ExtractLocalization(*Program, BuildContext, ProgramContext);
        }

        if (!IsAbortedCompile(Result))
        {
            Result |= IrGenerateProgram(*Program, BuildContext, ProgramContext);
        }

        if (!IsAbortedCompile(Result))
        {
            Result |= AssembleProgram(*Program, BuildContext, ProgramContext);
        }
    }

    return Result;
}

ECompilerResult CToolchain::SemanticAnalyzeVst(TOptional<TSRef<CSemanticProgram>>& OutProgram, const TSRef<Verse::Vst::Project>& Vst, const SBuildContext& BuildContext, const SProgramContext& ProgramContext)
{
    using namespace Private_ToolchainImpl;
    CFloatStateSaveRestore FloatStateScopeGuard;

    ECompilerResult Result = RunCompilerPrePass(_Params.SemanticAnalyzer, _Params.LayerInjections._PreSemAnalysisInjections, BuildContext._AddedInjections._PreSemAnalysisInjections, BuildContext, Vst, ProgramContext);

    if (!IsAbortedCompile(Result))
    {
        TSRef<ISemanticAnalyzerPass> SemanticAnalyzer = *_Params.SemanticAnalyzer;
        SemanticAnalyzer->Initialize(BuildContext, ProgramContext);
        SIntraSemInjectArgs SemaInjectionArgs(ProgramContext._Program);
        for (ESemanticPass SemaPass = ESemanticPass::SemanticPass__MinValid;
            SemaPass <= ESemanticPass::SemanticPass__MaxValid;
            SemaPass = ESemanticPass(int32_t(SemaPass) + 1))
        {
            OutProgram = SemanticAnalyzer->ProcessVst(*Vst, static_cast<ESemanticPass>(SemaPass));
            SemaInjectionArgs._InjectionPass = static_cast<ESemanticPass>(SemaPass);
            if (InvokeApiInjections(_Params.LayerInjections._IntraSemAnalysisInjections, BuildContext._AddedInjections._IntraSemAnalysisInjections, BuildContext, SemaInjectionArgs, ProgramContext))
            {
                Result |= ECompilerResult::Compile_SkippedByInjection;
                break;
            }
        }
        SemanticAnalyzer->CleanUp();

        Result |= ECompilerResult::Compile_RanSemanticPass;

        if (!BuildContext._Diagnostics->HasErrors() && OutProgram.IsSet())
        {
            for (const TSRef<IPostSemanticAnalysisFilter>& PostSemanticAnalysisFilter : _Params.PostSemanticAnalysisFilters)
            {
                PostSemanticAnalysisFilter->FilterAst(*OutProgram, BuildContext, ProgramContext);
            }
        }
        else
        {
            Result |= ECompilerResult::Compile_SemanticError;
        }
    }

    if (!IsAbortedCompile(Result) && InvokeApiInjections(_Params.LayerInjections._PostSemAnalysisInjections, BuildContext._AddedInjections._PostSemAnalysisInjections, BuildContext, *OutProgram, ProgramContext))
    {
        Result |= ECompilerResult::Compile_SkippedByInjection;
    }

    return Result;
}

ECompilerResult CToolchain::ExtractLocalization(const TSRef<CSemanticProgram>& Program, const SBuildContext& BuildContext, const SProgramContext& ProgramContext)
{
    FVerseLocalizationGen Gen;
    Gen(*Program, *BuildContext._Diagnostics, _LocalizationInfo, _StringInfo );
    return ECompilerResult::Compile_RanLocalizationPass;
}

 TArray<FSolLocalizationInfo> CToolchain::TakeLocalizationInfo()
{
    return Move(_LocalizationInfo);
}

 TArray<FSolLocalizationInfo> CToolchain::TakeStringInfo()
 {
     return Move(_StringInfo);
 }

ECompilerResult CToolchain::IrGenerateProgram(const TSRef<CSemanticProgram>& Program, const SBuildContext& BuildContext, const SProgramContext& ProgramContext)
{
    using namespace Private_ToolchainImpl;
    CFloatStateSaveRestore FloatStateScopeGuard;

    TSRef<IIrGeneratorPass> IrGenerator = *_Params.IrGenerator;
    IrGenerator->Initialize(BuildContext, ProgramContext);
    IrGenerator->ProcessAst(); // Updates Program
    IrGenerator->CleanUp();
 
    ECompilerResult Result = ECompilerResult::Compile_RanIrPass;
    if (!BuildContext._Diagnostics->HasErrors())
    {
        for (const TSRef<IPostIrFilter>& PostIrFilter : _Params.PostIrFilters)
        {
            PostIrFilter->FilterIr(Program, BuildContext, ProgramContext);
        }
    }
    else
    {
        Result |= ECompilerResult::Compile_IrError;
    }
    return Result;
}

ECompilerResult CToolchain::AssembleProgram(const TSRef<CSemanticProgram>& Program, const SBuildContext& BuildContext, const SProgramContext& ProgramContext)
{
    using namespace Private_ToolchainImpl;
    CFloatStateSaveRestore FloatStateScopeGuard;

    ECompilerResult Result = RunCompilerPrePass(_Params.Assembler, _Params.LayerInjections._PreTranslateInjections, BuildContext._AddedInjections._PreTranslateInjections, BuildContext, Program, ProgramContext);
    if (!IsAbortedCompile(Result))
    {
        (*_Params.Assembler)->TranslateExpressions(Program, BuildContext, ProgramContext);
        Result |= ECompilerResult::Compile_RanCodeGenPass;

        if (BuildContext._Diagnostics->HasErrors())
        {
            Result |= ECompilerResult::Compile_CodeGenError;
        }
    }

    return Result;
}

ELinkerResult CToolchain::Link(const SBuildContext& BuildContext, const SProgramContext& ProgramContext)
{
    CFloatStateSaveRestore FloatStateScopeGuard;
    ELinkerResult Result = ELinkerResult::Link_Success;
    if (Private_ToolchainImpl::InvokeApiInjections(_Params.LayerInjections._PreLinkInjections, BuildContext._AddedInjections._PreLinkInjections, BuildContext, ProgramContext))
    {
        Result = ELinkerResult::Link_Skipped_ByInjection;
    }
    else if (!_Params.Assembler.IsSet())
    {
        Result = ELinkerResult::Link_Skipped_ByEmptyPass;
    }

    if (!Result)
    {
        Result = (*_Params.Assembler)->Link(BuildContext, ProgramContext);
    }

    return Result;
}

} // namespace uLang
