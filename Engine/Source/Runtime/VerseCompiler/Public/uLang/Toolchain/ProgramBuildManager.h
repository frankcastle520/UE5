// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Toolchain/Toolchain.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/Array.h"
#include "uLang/CompilerPasses/IPostIrFilter.h"
#include "uLang/CompilerPasses/IPostSemanticAnalysisFilter.h"
#include "uLang/Diagnostics/Diagnostics.h"

namespace uLang
{
// Forward declarations
class CUTF8StringView;

struct SToolchainOverrides
{
    TOptional<TSPtr<IParserPass>>                      Parser;
    TOptional<TSRefArray<IPostVstFilter>>              PostVstFilters;
    TOptional<TSPtr<ISemanticAnalyzerPass>>            SemanticAnalyzer;
    TOptional<TSRefArray<IPostSemanticAnalysisFilter>> PostSemanticAnalysisFilters;
    TOptional<TSRefArray<IPostIrFilter>>               PostIrFilters;
    TOptional<TSPtr<IAssemblerPass>>                   Assembler;

    TOptional<TSRefArray<IPreParseInjection>>         PreParseInjections;
    TOptional<TSRefArray<IPostParseInjection>>        PostParseInjections;
    TOptional<TSRefArray<IPreSemAnalysisInjection>>   PreSemAnalysisInjections;
    TOptional<TSRefArray<IIntraSemAnalysisInjection>> IntraSemAnalysisInjections;
    TOptional<TSRefArray<IPostSemAnalysisInjection>>  PostSemAnalysisInjections;
    TOptional<TSRefArray<IPreTranslateInjection>>     PreTranslateInjections;
    TOptional<TSRefArray<IPreLinkInjection>>          PreLinkInjections;
};

struct SBuildManagerParams
{
    // For the pieces of this that are set, the toolchain will be constructed
    // using those specified parts -- for the other toolchain pieces, the
    // build-manager will perform auto-discovery to fill the rest out.
    SToolchainOverrides _ToolchainOverrides;

    //SToolchainParams _ToolchainParams;
    TSPtr<CSemanticProgram> _ExistingProgram; // Optional existing program
};

class VERSECOMPILER_API CProgramBuildManager : public CSharedMix
{
public:
    CProgramBuildManager(const SBuildManagerParams& Params);
    ~CProgramBuildManager();

    void SetSourceProject(const TSRef<CSourceProject>& Project);
    void AddSourceSnippet(const TSRef<ISourceSnippet>& Snippet, const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath);
    void RemoveSourceSnippet(const TSRef<ISourceSnippet>& Snippet);
    const CSourceProject::SPackage& FindOrAddSourcePackage(const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath);

    SBuildResults Build(const SBuildParams& Params);

    using COnBuildStartedEvent = TEvent<>;
    COnBuildStartedEvent::Registrar& OnBuildStartedEvent()      { return _OnBuildStartedEvent; }

    using COnDiagnosticEvent = TEvent<const TSRef<SGlitch>&>;
    COnDiagnosticEvent::Registrar& OnBuildDiagnosticEvent()     { return _OnDiagnosticEvent; }

    using COnBuildCompleteEvent = TEvent<>;
    COnBuildCompleteEvent::Registrar& OnBuildCompleteEvent()    { return _OnBuildCompleteEvent; }

    using COnBuildStatisticEvent = TEvent<SBuildEventInfo>;
    COnBuildStatisticEvent::Registrar& OnBuildStatisticEvent() { return _OnBuildStatisticEvent; }

    const TSRef<CDiagnostics>& GetDiagnostics() const           { return _BuildDiagnostics; }
    const TSRef<CToolchain>& GetToolchain() const               { return _Toolchain; }
    const SProgramContext& GetProgramContext() const            { return _ProgramContext; }
    const TSRef<CSourceProject>& GetSourceProject() const       { return _SourceProject; }
    const TUPtr<SPackageUsage>& GetPackageUsage() const         { return _PackageUsage; }
    TArray<FSolLocalizationInfo> TakeLocalizationInfo()         { return _Toolchain->TakeLocalizationInfo(); }
    TArray<FSolLocalizationInfo> TakeStringInfo()               { return _Toolchain->TakeStringInfo(); }

    SBuildResults BuildProject(const CSourceProject& SourceProject, const SBuildContext& BuildContext);
    ECompilerResult ParseSnippet(const uLang::TSRef<Verse::Vst::Snippet>& OutVst, const CUTF8StringView& TextSnippet, const SBuildContext& BuildContext);
    ECompilerResult SemanticAnalyzeVst(TOptional<TSRef<CSemanticProgram>>& OutProgram, const TSRef<Verse::Vst::Project>& Vst, const SBuildContext& BuildContext);
    ECompilerResult IrGenerateProgram(const TSRef<CSemanticProgram>& Program, const SBuildContext& BuildContext);
    ECompilerResult AssembleProgram(const TSRef<CSemanticProgram>& Program, const SBuildContext& BuildContext);
    ELinkerResult Link(const SBuildContext& BuildContext);

    void ResetSemanticProgram();
    const TSPtr<Verse::Vst::Project>& GetProjectVst() const { return _Toolchain->GetProjectVst(); }
    void SetProjectVst(const TSRef<Verse::Vst::Project>& NewProject)
    {
        return _Toolchain->SetProjectVst(NewProject);
    }

    void EnablePackageUsage(bool bEnable = true);

private:
    void OnBuildDiagnostic(const TSRef<SGlitch>& Diagnostic);
    void OnBuildStatistic(const SBuildEventInfo& EventInfo);

    TSRef<CToolchain> _Toolchain;
    SProgramContext _ProgramContext;
    TSRef<CSourceProject> _SourceProject;
    TUPtr<SPackageUsage> _PackageUsage;
    bool bEnablePackageUsage = false;

    TSRef<CDiagnostics> _BuildDiagnostics;
    CDiagnostics::COnGlitchEvent::SubscriberId _OnGlitchSubscriberId;
    CDiagnostics::COnBuildStatisticEvent::SubscriberId _OnBuildStatisticSubscriberId;

    COnBuildStartedEvent _OnBuildStartedEvent;
    COnDiagnosticEvent _OnDiagnosticEvent;
    COnBuildCompleteEvent _OnBuildCompleteEvent;
    COnBuildStatisticEvent _OnBuildStatisticEvent;
};

} // namespace uLang
