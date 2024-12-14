// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Common/Containers/SharedPointerArray.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Misc/EnumUtils.h" // for ULANG_ENUM_BIT_FLAGS()
#include "uLang/CompilerPasses/IParserPass.h"
#include "uLang/CompilerPasses/IPostVstFilter.h"
#include "uLang/CompilerPasses/ISemanticAnalyzerPass.h"
#include "uLang/CompilerPasses/IPostSemanticAnalysisFilter.h"
#include "uLang/CompilerPasses/IPostIrFilter.h"
#include "uLang/CompilerPasses/IIRGeneratorPass.h"
#include "uLang/CompilerPasses/IAssemblerPass.h"
#include "uLang/CompilerPasses/ApiLayerInjections.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Syntax/VstNode.h" // for Vst::Node
#include "uLang/SourceProject/SourceProject.h"
#include "uLang/VerseLocalizationGen.h"

namespace uLang
{

    // Forward declarations
    class  CToolchain;
    class  CUTF8StringView;

    struct SToolchainParams
    {
        /**
         * --- Parser --------------------------------
         * The parser is responsible for ingesting a source file, parsing and tokenizing it, and
         * generating an abstract syntax tree (Vst) for the rest of the compiler to consume.  It
         * should not need to worry about semantics, just syntax
         */
        TOptional<TSRef<IParserPass>>           Parser;

        /**
         * --- Post Vst Filters ----------------------
         * The post Vst filters take an Vst, and transform it in any way it deems fit.  Applications
         * of this stage can include optimizers (operation reduction, constant folding, etc.), or
         * metadata stripping or remapping.  There can be as many filters as needed, and are called
         * in array order, starting at 0.  It is the loader's responsibility for establishing that
         * order.
         */
        TSRefArray<IPostVstFilter>              PostVstFilters;

        /**
         * --- Semantic Analyzer ------------------
         * The semantic analyzer consumes the generated and optimized VST, transforms it to an AST,
         * and semantically analyzes the AST, annotating the AST with inferred types and other
         * analysis products.
         */
        TOptional<TSRef<ISemanticAnalyzerPass>> SemanticAnalyzer;

        /**
         * --- Post Semantic Analysis Filters ---------------
         * This stage takes the semantically analyzed AST, and performs operations on the result
         * before passing along to the IR generator.
         */
        TSRefArray<IPostSemanticAnalysisFilter>  PostSemanticAnalysisFilters;
        
        /**
         * --- Post IR generation Filters ---------------
         * This stage takes the generated IR, and performs operations on the result
         * before passing along to the assembler.
         */
        TSRefArray<IPostIrFilter>                PostIrFilters;

        /**
        * --- IR (Intermediate Representation) -------
        * The IrGenerator creates an intermediate representation intended for lenient analysis and code generation.
        */
        TOptional<TSRef<IIrGeneratorPass>>       IrGenerator;

        /**
         * --- Assembler -----------------------------
         * The assembler is responsible both for code-gen and linking (resolving any symbols between
         * language objects). Note that there may be runtime bindings that can't be resolved at this
         * stage, but all uLang internal bindings should be validated and linked here.
         */
        TOptional<TSRef<IAssemblerPass>>        Assembler;

        /**
         * --- API Layer Injections ---------------
         */
        SToolchainInjections                    LayerInjections;
    };

    VERSECOMPILER_API TSRef<CToolchain> CreateToolchain(const SToolchainParams& Params);

    enum class ECompilerResult : uint32_t
    {
        Compile_NoOp                = (0x00),

        Compile_RanSyntaxPass       = (1<<0),
        Compile_RanSemanticPass     = (1<<1),
        Compile_RanLocalizationPass = (1<<2),
        Compile_RanIrPass           = (1<<3),
        Compile_RanCodeGenPass      = (1<<4),

        Compile_SkippedByInjection  = (1<<5),
        Compile_SkippedByEmptyPass  = (1<<6),

        Compile_SyntaxError         = (1<<7),
        Compile_SemanticError       = (1<<8),
        Compile_IrError             = (1<<9),
        Compile_LocalizationError   = (1<<10),
        Compile_CodeGenError        = (1<<11),

        CompileMask_Failure = (Compile_SyntaxError | Compile_SemanticError | Compile_IrError | Compile_LocalizationError | Compile_CodeGenError),
        CompileMask_Skipped = (Compile_SkippedByInjection | Compile_SkippedByEmptyPass),
        CompileMask_Aborted = (CompileMask_Failure | CompileMask_Skipped),
    };
    ULANG_ENUM_BIT_FLAGS(ECompilerResult, inline);

    ULANG_FORCEINLINE bool IsCompileFailure(ECompilerResult E)    { return Enum_HasAnyFlags(E, ECompilerResult::CompileMask_Failure); }
    ULANG_FORCEINLINE bool IsAbortedCompile(ECompilerResult E)    { return Enum_HasAnyFlags(E, ECompilerResult::CompileMask_Aborted); }
    ULANG_FORCEINLINE bool IsCompileIncomplete(ECompilerResult E) { return !E || IsAbortedCompile(E); }
    ULANG_FORCEINLINE bool IsCompileComplete(ECompilerResult E)   { return !IsCompileIncomplete(E); }

    struct SBuildResults
    {
        VERSECOMPILER_API bool HasFailure() const;
        SBuildResults& operator |= (const SBuildResults& Other);

        SBuildStatistics _Statistics = {};

        bool            _IOErrorsFound  = false;
        ECompilerResult _CompilerResult = ECompilerResult::Compile_NoOp;
        ELinkerResult   _LinkerResult   = ELinkerResult::Link_Skipped;
    };

    /* Compiler+Linker toolchain
     **************************************************************************/

    /**
     * The compiler toolchain, which has five stages of compilation.  It's structured as a layered,
     * multi-stage compiler API.  Each stage is interchangeable, which means the frontend and the
     * backend are retargetable.
     *
     * This class needs to be assembled by the Toolchain Loader, which uses the Modular Features
     * to find a module or modules that implement the five stages of the compiler.  This also lets
     * the user mix in various optimizing passes (post-Vst filters), or bytecode packing
     * (post-expression filters) from any source.
     */
    class VERSECOMPILER_API CToolchain : public CSharedMix
    {
    public:
        /**
         * Compile and link all text snippets in the given project
         */
        SBuildResults BuildProject(const CSourceProject& SourceProject, const SBuildContext& BuildContext, const SProgramContext& ProgramContext);

        /**
         * Parse a snippet of text and return the resulting Vst snippet
         * Steps:
         * 1. Pre parse injections (from both toolchain and build context)
         * 2. Parse
         * 3. Post Vst filters
         * 4. Post parse injections (from both toolchain and build context)
         */
        ECompilerResult ParseSnippet(const uLang::TSRef<Verse::Vst::Snippet>& OutVst, const CUTF8StringView& TextSnippet, const SBuildContext& BuildContext);

        /**
         * Run SemanticAnalyzeVst and AssembleProgram on an Vst.
         */
        ECompilerResult CompileVst(const TSRef<Verse::Vst::Project>& Vst, const SBuildContext& BuildContext, const SProgramContext& ProgramContext);

        /**
         * Run semantic analysis on a given Vst snippet
         * Steps:
         * 1. Pre semantic analysis injections (from both toolchain and build context)
         * 2. Semantic analysis
         * 3. Post expression filters
         * 4. Post semantic analysis injections (from both toolchain and build context)
         */
        ECompilerResult SemanticAnalyzeVst(TOptional<TSRef<CSemanticProgram>>& OutProgram, const TSRef<Verse::Vst::Project>& Vst, const SBuildContext& BuildContext, const SProgramContext& ProgramContext);

        /**
        * Extract localization information.
        */
        ECompilerResult ExtractLocalization(const TSRef<CSemanticProgram>& Program, const SBuildContext& BuildContext, const SProgramContext& ProgramContext);

        /**
        * Run IR generation on a given Ast snippet.
        * Steps:
        * 1. IR generation
        * 2. (future lenient analysis)
        */
        ECompilerResult IrGenerateProgram(const TSRef<CSemanticProgram>& Program, const SBuildContext& BuildContext, const SProgramContext& ProgramContext);

        /**
         * Run semantic analysis and code generation on a given Vst snippet
         * Steps:
         * 1. Pre translate injections
         * 2. Assembler (code generation)
         */
        ECompilerResult AssembleProgram(const TSRef<CSemanticProgram>& Program, const SBuildContext& BuildContext, const SProgramContext& ProgramContext);

        /**
         * Run linker (not currently used)
         */
        ELinkerResult Link(const SBuildContext& BuildContext, const SProgramContext& ProgramContext);

        /**
         * Accessors for the various stages of the compiler.  Most users will never actually need
         * to grab these stages individual, but in the cases where you need to do partial compilation,
         * they can be useful to manually run
         */
        const TOptional<TSRef<IParserPass>>&            GetParser() const { return _Params.Parser; }
        const TSRefArray<IPostVstFilter>&               GetPostVstFilters() const { return _Params.PostVstFilters; }
        const TOptional<TSRef<ISemanticAnalyzerPass>>&  GetSemanticAnalyzer() const { return _Params.SemanticAnalyzer; }
        const TSRefArray<IPostSemanticAnalysisFilter>&  GetPostSemanticAnalysisFilters() const { return _Params.PostSemanticAnalysisFilters; }
        const TSRefArray<IPostIrFilter>&                GetPostIrFilters() const { return _Params.PostIrFilters; }
        const TOptional<TSRef<IAssemblerPass>>&         GetAssembler() const { return _Params.Assembler; }
        const TSPtr<Verse::Vst::Project>&               GetProjectVst() const { return _ProjectVst; }

        /**
         * Directly sets the cached VST project to the new project specified.
         *
         * Warning: doing this will cause the previously cached VST project's destructor to be called,
         * which means that any AST nodes that still hold references to the VST nodes within will now
         * have those references be invalidated. The only time you should use this is if you don't care
         * about the previous VST project anymore and its accompanying AST either.
         *
         * @param NewProject    The new project to store.
         */
        void SetProjectVst(const TSRef<Verse::Vst::Project>& NewProject)
        {
            this->_ProjectVst = NewProject;
        }

        // Take localization information, i.e, it removes it from this object.
        TArray<FSolLocalizationInfo> TakeLocalizationInfo();

        // Take string information, i.e, it removes it from this object.
        TArray<FSolLocalizationInfo> TakeStringInfo();

    private:
        friend VERSECOMPILER_API TSRef<CToolchain> CreateToolchain(const SToolchainParams& Params);
        friend TSRef<CToolchain>;
        CToolchain(const SToolchainParams& Params); // use CreateToolchain() to construct

        struct SOrderedPackage
        {
            const CSourcePackage* _Package = nullptr;
            int32_t _DependencyDepth = -1;
        };

        // Build a list of packages ordered by dependency depth
        // Returns true if successful, or false if glitches were encountered
        bool BuildOrderedPackageList(const CSourceProject& SourceProject, const SBuildContext& BuildContext, TArray<SOrderedPackage>& OutOrderedPackageList) const;

        SToolchainParams _Params;

        TSPtr<Verse::Vst::Project> _ProjectVst; 

        // Localization and string information stored here.
        TArray<FSolLocalizationInfo> _LocalizationInfo;
        TArray<FSolLocalizationInfo> _StringInfo;
    };
}
