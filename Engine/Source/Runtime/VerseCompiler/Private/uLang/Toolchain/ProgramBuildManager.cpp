// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Toolchain/ProgramBuildManager.h"
#include "uLang/Toolchain/ModularFeatureManager.h"

namespace uLang
{

static TSRef<CSemanticProgram> MakeNewSemanticProgram()
{
    const auto NewSemanticProgram = TSRef<CSemanticProgram>::New();
    NewSemanticProgram->Initialize();
    NewSemanticProgram->PopulateCoreAPI();
    return NewSemanticProgram;
}

static SToolchainParams MakeToolchainParams(const SToolchainOverrides& ToolchainOverrides)
{
    SToolchainParams ToolchainParams;

    //
    // Pre-Parse Injections
    if (ToolchainOverrides.PreParseInjections.IsSet())
    {
        ToolchainParams.LayerInjections._PreParseInjections = *ToolchainOverrides.PreParseInjections;
    }
    else
    {
        ToolchainParams.LayerInjections._PreParseInjections = GetModularFeaturesOfType<IPreParseInjection>();
    }

    //
    // Parser
    if (ToolchainOverrides.Parser.IsSet())
    {
        if (ToolchainOverrides.Parser->IsValid())
        {
            ToolchainParams.Parser = ToolchainOverrides.Parser->AsRef();
        }
    }
    else
    {
        ToolchainParams.Parser = GetModularFeature<IParserPass>();
    }

    //
    // Post-Parse Injections
    if (ToolchainOverrides.PostParseInjections.IsSet())
    {
        ToolchainParams.LayerInjections._PostParseInjections = *ToolchainOverrides.PostParseInjections;
    }
    else
    {
        ToolchainParams.LayerInjections._PostParseInjections = GetModularFeaturesOfType<IPostParseInjection>();
    }

    //
    // Vst Filters
    if (ToolchainOverrides.PostVstFilters.IsSet())
    {
        ToolchainParams.PostVstFilters = *ToolchainOverrides.PostVstFilters;
    }
    else
    {
        ToolchainParams.PostVstFilters = GetModularFeaturesOfType<IPostVstFilter>();
    }

    //
    // Pre-SemanticAnalysis Injections
    if (ToolchainOverrides.PreSemAnalysisInjections.IsSet())
    {
        ToolchainParams.LayerInjections._PreSemAnalysisInjections = *ToolchainOverrides.PreSemAnalysisInjections;
    }
    else
    {
        ToolchainParams.LayerInjections._PreSemAnalysisInjections = GetModularFeaturesOfType<IPreSemAnalysisInjection>();
    }

    //
    // Semantic Analyzer
    if (ToolchainOverrides.SemanticAnalyzer.IsSet())
    {
        if (ToolchainOverrides.SemanticAnalyzer->IsValid())
        {
            ToolchainParams.SemanticAnalyzer = ToolchainOverrides.SemanticAnalyzer->AsRef();
        }
    }
    else
    {
        ToolchainParams.SemanticAnalyzer = GetModularFeature<ISemanticAnalyzerPass>();
    }

    //
    // Intra-SemanticAnalysis Injections
    if (ToolchainOverrides.IntraSemAnalysisInjections.IsSet())
    {
        ToolchainParams.LayerInjections._IntraSemAnalysisInjections = *ToolchainOverrides.IntraSemAnalysisInjections;
    }
    else
    {
        ToolchainParams.LayerInjections._IntraSemAnalysisInjections = GetModularFeaturesOfType<IIntraSemAnalysisInjection>();
    }

    //
    // Post-SemanticAnalysis Injections
    if (ToolchainOverrides.PostSemAnalysisInjections.IsSet())
    {
        ToolchainParams.LayerInjections._PostSemAnalysisInjections = *ToolchainOverrides.PostSemAnalysisInjections;
    }
    else
    {
        ToolchainParams.LayerInjections._PostSemAnalysisInjections = GetModularFeaturesOfType<IPostSemAnalysisInjection>();
    }

    //
    // AST Filters
    if (ToolchainOverrides.PostSemanticAnalysisFilters.IsSet())
    {
        ToolchainParams.PostSemanticAnalysisFilters = *ToolchainOverrides.PostSemanticAnalysisFilters;
    }
    else
    {
        ToolchainParams.PostSemanticAnalysisFilters = GetModularFeaturesOfType<IPostSemanticAnalysisFilter>();
    }

    //
    // IR Filters
    if (ToolchainOverrides.PostIrFilters.IsSet())
    {
        ToolchainParams.PostIrFilters = *ToolchainOverrides.PostIrFilters;
    }
    else
    {
        ToolchainParams.PostIrFilters = GetModularFeaturesOfType<IPostIrFilter>();
    }

    //
    // Pre-Translate Injections
    if (ToolchainOverrides.PreTranslateInjections.IsSet())
    {
        ToolchainParams.LayerInjections._PreTranslateInjections = *ToolchainOverrides.PreTranslateInjections;
    }
    else
    {
        ToolchainParams.LayerInjections._PreTranslateInjections = GetModularFeaturesOfType<IPreTranslateInjection>();
    }

    //
    // Pre-Link Injections
    if (ToolchainOverrides.PreLinkInjections.IsSet())
    {
        ToolchainParams.LayerInjections._PreLinkInjections = *ToolchainOverrides.PreLinkInjections;
    }
    else
    {
        ToolchainParams.LayerInjections._PreLinkInjections = GetModularFeaturesOfType<IPreLinkInjection>();
    }

    //
    // Intermediate representation
    {
        ToolchainParams.IrGenerator = GetModularFeature<IIrGeneratorPass>();
    }

    //
    // Backend/Assembler
    if (ToolchainOverrides.Assembler.IsSet())
    {
        if (ToolchainOverrides.Assembler->IsValid())
        {
            ToolchainParams.Assembler = ToolchainOverrides.Assembler->AsRef();
        }
    }
    else
    {
        ToolchainParams.Assembler = GetModularFeature<IAssemblerPass>();
    }

    return ToolchainParams;
}

CProgramBuildManager::CProgramBuildManager(const SBuildManagerParams& Params)
    : _Toolchain(CreateToolchain(MakeToolchainParams(Params._ToolchainOverrides)))
    , _ProgramContext(Params._ExistingProgram.IsValid() ? Params._ExistingProgram.AsRef() : MakeNewSemanticProgram())
    , _SourceProject(TSRef<CSourceProject>::New("ProgramBuildManager"))
    , _BuildDiagnostics(TSRef<CDiagnostics>::New())
    , _OnGlitchSubscriberId(_BuildDiagnostics->OnGlitchEvent().Subscribe([this](const TSRef<SGlitch>& Diagnostic) { OnBuildDiagnostic(Diagnostic); }))
    , _OnBuildStatisticSubscriberId(_BuildDiagnostics->OnBuildStatisticEvent().Subscribe([this](const uLang::SBuildEventInfo& EventInfo) { OnBuildStatistic(EventInfo); }))
{
}

SBuildResults CProgramBuildManager::BuildProject(const CSourceProject& SourceProject, const SBuildContext& BuildContext)
{
    return _Toolchain->BuildProject(SourceProject, BuildContext, _ProgramContext);
}

ECompilerResult CProgramBuildManager::ParseSnippet(const uLang::TSRef<Verse::Vst::Snippet>& OutVst, const CUTF8StringView& TextSnippet, const SBuildContext& BuildContext)
{
    return _Toolchain->ParseSnippet(OutVst, TextSnippet, BuildContext);
}

ECompilerResult CProgramBuildManager::SemanticAnalyzeVst(TOptional<TSRef<CSemanticProgram>>& OutProgram, const TSRef<Verse::Vst::Project>& Vst, const SBuildContext& BuildContext)
{
    return _Toolchain->SemanticAnalyzeVst(OutProgram, Vst, BuildContext, _ProgramContext);
}

ECompilerResult CProgramBuildManager::IrGenerateProgram(const TSRef<CSemanticProgram>& Program, const SBuildContext& BuildContext)
{
    return _Toolchain->IrGenerateProgram(Program, BuildContext, _ProgramContext);
}

ECompilerResult CProgramBuildManager::AssembleProgram(const TSRef<CSemanticProgram>& Program, const SBuildContext& BuildContext)
{
    return _Toolchain->AssembleProgram(Program, BuildContext, _ProgramContext);
}

ELinkerResult CProgramBuildManager::Link(const SBuildContext& BuildContext)
{
    return _Toolchain->Link(BuildContext, _ProgramContext);
}

CProgramBuildManager::~CProgramBuildManager()
{
    _BuildDiagnostics->OnGlitchEvent().Unsubscribe(_OnGlitchSubscriberId);
	_BuildDiagnostics->OnBuildStatisticEvent().Unsubscribe(_OnBuildStatisticSubscriberId);
}

void CProgramBuildManager::SetSourceProject(const TSRef<CSourceProject>& Project)
{
    _SourceProject = Project;
}

void CProgramBuildManager::AddSourceSnippet(const TSRef<ISourceSnippet>& Snippet, const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath)
{
    _SourceProject->AddSnippet(Snippet, PackageName, PackageVersePath);
}

void CProgramBuildManager::RemoveSourceSnippet(const TSRef<ISourceSnippet>& Snippet)
{
    _SourceProject->RemoveSnippet(Snippet);
}

const CSourceProject::SPackage& CProgramBuildManager::FindOrAddSourcePackage(const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath)
{
    return _SourceProject->FindOrAddPackage(PackageName, PackageVersePath);
}

void CProgramBuildManager::ResetSemanticProgram()
{
    _ProgramContext = SProgramContext(MakeNewSemanticProgram());
}

void CProgramBuildManager::EnablePackageUsage(bool bEnable)
{
    bEnablePackageUsage = bEnable;
}

SBuildResults CProgramBuildManager::Build(const SBuildParams& Params)
{
    _OnBuildStartedEvent.Broadcast();

    _BuildDiagnostics->Reset();
    SBuildContext BuildContext(_BuildDiagnostics);
    BuildContext._Params = Params;
    if (bEnablePackageUsage)
    {
        BuildContext._PackageUsage.SetNew();
    }

    ResetSemanticProgram();

    //Jira SOL-1805, we may need to create a new source project here
    //_SourceProject = TSRef<CSourceProject>::New("ProgramBuildManager");
    SBuildResults Results = _Toolchain->BuildProject(*_SourceProject, BuildContext, _ProgramContext);

    _PackageUsage = Move(BuildContext._PackageUsage);

    _OnBuildCompleteEvent.Broadcast();

    return Results;
}

void CProgramBuildManager::OnBuildDiagnostic(const TSRef<SGlitch>& Diagnostic)
{
    _OnDiagnosticEvent.Broadcast(Diagnostic);
}

void CProgramBuildManager::OnBuildStatistic(const SBuildEventInfo& EventInfo)
{
    _OnBuildStatisticEvent.Broadcast(EventInfo);
}
} // namespace uLang
