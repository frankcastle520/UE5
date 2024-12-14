// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Diagnostics/Diagnostics.h"
#include "uLang/CompilerPasses/ApiLayerInjections.h"

namespace uLang
{

// Forward declarations
enum class EAstNodeSetKey : uintptr_t;

/** Collection of all injection types for the toolchain -- conveniently bundled up for portability. */
struct SToolchainInjections
{
    TSRefArray<IPreParseInjection>         _PreParseInjections;
    TSRefArray<IPostParseInjection>        _PostParseInjections;
    TSRefArray<IPreSemAnalysisInjection>   _PreSemAnalysisInjections;
    TSRefArray<IIntraSemAnalysisInjection> _IntraSemAnalysisInjections;
    TSRefArray<IPostSemAnalysisInjection>  _PostSemAnalysisInjections;
    TSRefArray<IPreTranslateInjection>     _PreTranslateInjections;
    TSRefArray<IPreLinkInjection>          _PreLinkInjections;
};

/** */
struct SCommandLine
{
    TArray<CUTF8String> _Tokens;
    TArray<CUTF8String> _Switches;
    CUTF8String _Unparsed;
};

/** Per package, remember what dependencies it uses */
struct SPackageUsageEntry
{
    CUTF8String _PackageName;
    TArray<CUTF8String> _UsedDependencies; // Only _directly_ used dependencies, not transitive closure
};

/** Remember what packages use which dependencies */
struct SPackageUsage
{
    TArray<SPackageUsageEntry> _Packages;
};

/** Params passed to the build command */
struct SBuildParams
{
    enum class ELinkParam
    {
        RequireComplete,    // Require complete link
        Skip,               // Skip link step
        Default,
    };

    /// If to generate digests when possible
    bool _bGenerateDigests = true;

    /// If to generate bytecode
    bool _bGenerateCode = true;

    /// Link-step settings
    ELinkParam _LinkType = ELinkParam::Default;

    /// If true, we'll run the build only up to semantic analysis
    bool _bSemanticAnalysisOnly = false;

    /// Maximum number of allowed persistent `var` definitions
    int32_t _MaxNumPersistentVars = 0;

    /// This allows us to determine when a package was uploaded for a given Fortnite release version.
    /// It is a HACK that conditionally enables/disables behaviour in the compiler in order to
    /// support previous mistakes allowed to slip through in previous Verse langauge releases  but
    /// now need to be supported for backwards compatability.
    /// When we can confirm that all Fortnite packages that are currently uploaded are beyond this
    /// version being used in all instances of the codebase, this can then be removed.
    uint32_t _UploadedAtFNVersion = VerseFN::UploadedAtFNVersion::Latest;

    // HACK_VMSWITCH - remove this once VerseVM is fully brought up
    // Specifies the VM we are compiling the code for
    // We need a variable for this, since even though in most cases
    // WITH_VERSE_BPVM will select the correct value, in the VNI tool we
    // take this information from the command line
    enum class EWhichVM { VerseVM, BPVM };
#if WITH_VERSE_BPVM
    EWhichVM _TargetVM = EWhichVM::BPVM;
#else
    EWhichVM _TargetVM = EWhichVM::VerseVM;
#endif
};

/** Settings pertaining to individual runs through the toolchain (build flags, etc.) */
struct SBuildContext
{
    /// Accumulated issues/glitches over all compile phases
    TSRef<CDiagnostics>  _Diagnostics;
    /// Additional API injections for the individual build pass only
    SToolchainInjections _AddedInjections;
    /// Name of package providing built-in functionality
    TArray<CUTF8String>  _BuiltInPackageNames = { "Solaris/VerseNative" }; // HACK for now, at least just in one place :-)
    /// Optional database of dependencies actually used by packages
    TUPtr<SPackageUsage> _PackageUsage;
    /// Params passed into the Build command
    SBuildParams         _Params;

    explicit SBuildContext(TSRef<CDiagnostics> Diagnostics)
        : _Diagnostics(Move(Diagnostics))
    {}

    SBuildContext()
        : _Diagnostics(TSRef<CDiagnostics>::New())
    {}
};

/** Persistent data from consecutive toolchain runs -- provides a holistic view of the entire program. */
struct SProgramContext
{
    /// Whole view of checked program ready for conversion to runtime equivalent - its types, routines and code bodies of expressions
    TSRef<CSemanticProgram> _Program;

    SProgramContext(const TSRef<CSemanticProgram>& Program)
        : _Program(Program)
    {}
};

} // namespace uLang
