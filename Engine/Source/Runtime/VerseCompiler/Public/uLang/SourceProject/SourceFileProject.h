// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/SourceProject/SourceProject.h"
#include "uLang/SourceProject/IFileSystem.h"


namespace uLang
{

struct SPackageDesc;

/**
 * A source snippet on disk
 **/
class CSourceFileSnippet : public ISourceSnippet
{
public:
    CSourceFileSnippet(CUTF8String&& FilePath, const TSRef<IFileSystem>& FileSystem)
        : _FilePath(Move(FilePath))
        , _FileSystem(FileSystem)
    {}

    // This mimics what Unreal allows for package names, except that we allow periods in filenames as well.
    static constexpr char _InvalidSnippetCharacters[] = "\\:*?\"<>|' ,&!~\n\r\t@#";

    //~ Begin ISourceSnippet interface
    virtual CUTF8String GetPath() const override { return _FilePath; }
    virtual void SetPath(const CUTF8String& Path) override { _FilePath = Path; }
    VERSECOMPILER_API virtual TOptional<CUTF8String> GetText() const override;
    virtual TOptional<uLang::TSRef<Verse::Vst::Snippet>> GetVst() const override { return _Vst; }
    virtual void SetVst(TSRef<Verse::Vst::Snippet> Snippet) override { _Vst = Snippet; };
    //~ End ISourceSnippet interface

    const CUTF8StringView& GetFilePath() const { return _FilePath; }

    bool HasModifiedText() const                      { return _ModifiedText.IsSet(); }
    const CUTF8StringView& GetModifiedText() const    { return *_ModifiedText; }
    void SetModifiedText(const CUTF8StringView& Text) { _ModifiedText = Text; }
    void UnsetModifiedText()                          { _ModifiedText = EResult::Unspecified; }

    operator const CUTF8String&() const { return _FilePath; } // For set/map lookup

private:
    CUTF8String            _FilePath;     // Path on disk
    TOptional<CUTF8String> _ModifiedText; // Version of the file content that was edited in memory, if exists
    TSRef<IFileSystem>     _FileSystem;

    TOptional<TSRef<Verse::Vst::Snippet>> _Vst;
};

/**
 * A module of source snippets on disk
 **/
class CSourceFileModule : public CSourceModule
{
public:
    CSourceFileModule(const CUTF8StringView& ModuleName, const CUTF8StringView& ModuleFilePath)
        : CSourceModule(ModuleName), _FilePath(ModuleFilePath)
    {}
    CSourceFileModule(const CUTF8StringView& ModuleName, CUTF8String&& ModuleFilePath)
        : CSourceModule(ModuleName), _FilePath(Move(ModuleFilePath))
    {}

    virtual const CUTF8String& GetFilePath() const override { return _FilePath; }
    VERSECOMPILER_API CUTF8StringView GetDirPath() const;

    VERSECOMPILER_API TOptional<TSRef<CSourceFileModule>> FindSubmodule(const CUTF8StringView& ModuleName) const;
    VERSECOMPILER_API TSRef<CSourceFileModule> FindOrAddSubmodule(const CUTF8StringView& ModuleName, const CUTF8StringView& DirPath);

    VERSECOMPILER_API TOptional<TSRef<CSourceFileSnippet>> FindSnippetByFilePath(const CUTF8StringView& FilePath, bool bRecursive) const;
    void AddSnippet(const uLang::TSRef<ISourceSnippet>& Snippet) { CSourceModule::AddSnippet(Snippet); }
    bool RemoveSnippet(const uLang::TSRef<ISourceSnippet>& Snippet, bool bRecursive) { return CSourceModule::RemoveSnippet(Snippet, bRecursive); }

private:

    friend class CSourceFilePackage;

    // Path to the module file on disk
    // If no module file exists, this is the module directory with a slash `/` at the end
    CUTF8String _FilePath;

};

/**
 * A package of source modules/snippets on disk
 **/
class CSourceFilePackage : public CSourcePackage
{
public:

    VERSECOMPILER_API CSourceFilePackage(const CUTF8String& PackageFilePath, const TSRef<IFileSystem>& FileSystem, const TSRef<CDiagnostics>& Diagnostics);
    VERSECOMPILER_API CSourceFilePackage(const SPackageDesc& PackageDesc, const TSRef<IFileSystem>& FileSystem, const TSRef<CDiagnostics>& Diagnostics);

    virtual const CUTF8String& GetDirPath() const override { return _DirPath; }
    virtual const CUTF8String& GetFilePath() const override { return _PackageFilePath; }
    virtual EOrigin GetOrigin() const override { return EOrigin::FileSystem; }

    VERSECOMPILER_API CSourceFileModule* GetModuleForFilePath(const CUTF8StringView& FilePath);
    VERSECOMPILER_API TOptional<TSRef<CSourceFileSnippet>> FindSnippetByFilePath(const CUTF8StringView& FilePath) const;
    VERSECOMPILER_API TOptional<TSRef<CSourceFileSnippet>> AddSnippet(const CUTF8StringView& FilePath); // Look up module by file path and add snippet to it - might fail if file path is not under root path
    VERSECOMPILER_API bool RemoveSnippet(const CUTF8StringView& FilePath); // Look up module by file path and remove snippet from it

private:
    void ReadPackageFile(const CUTF8String& PackageFilePath, const TSRef<CDiagnostics>& Diagnostics);
    TSRef<CSourceFileModule> ResolveModuleForRelativeVersePath(const CUTF8String& RelativeVersePath, const TSRef<CDiagnostics>& Diagnostics) const;
    void GatherPackageSourceFiles(const CUTF8String& PackageFilePath, const TSRef<IFileSystem>& FileSystem, const TSRef<CDiagnostics>& Diagnostics);

    CUTF8String _PackageFilePath;
    CUTF8String _DirPath;    // Can be the containing folder of the package file, or point somewhere else
    TOptional<TArray<CUTF8String>> _FilePaths; // If set, use these _FilePaths for compilation instead of _DirPath (all _FilePaths must be under _DirPath)

    const TSRef<IFileSystem> _FileSystem;
};

/**
 * A project of source packages on disk
 **/
class CSourceFileProject : public CSourceProject
{
public:
    /**
     * This ctor will load the vproject given a path
     */
    VERSECOMPILER_API CSourceFileProject(const CUTF8String& ProjectFilePath, const TSRef<IFileSystem>& FileSystem, const TSRef<CDiagnostics>& Diagnostics);

    /**
     * This ctor will create vproject given the path and packages
     */
    VERSECOMPILER_API CSourceFileProject(const CUTF8String& Name, const TSRef<IFileSystem>& FileSystem, const TArray<SPackageDesc>& Packages, const TSRef<CDiagnostics>& Diagnostics);

    virtual const CUTF8String& GetFilePath() const override { return _FilePath; }

    VERSECOMPILER_API bool WriteProjectFile(const CUTF8String& ProjectFilePath, const TSRef<CDiagnostics>& Diagnostics);
    VERSECOMPILER_API bool WriteVSCodeWorkspaceFile(const CUTF8String& WorkspaceFilePath, const CUTF8String& ProjectFilePath, const TSRef<CDiagnostics>& Diagnostics);

    VERSECOMPILER_API TOptional<TSRef<CSourceFileSnippet>> FindSnippetByFilePath(const CUTF8StringView& FilePath) const;
    VERSECOMPILER_API TOptional<TSRef<CSourceFileSnippet>> AddSnippet(const CUTF8StringView& FilePath); // Look up module by file path and add snippet to it - might fail if file path is not under root path
    VERSECOMPILER_API bool RemoveSnippet(const CUTF8StringView& FilePath); // Look up module by file path and remove snippet from it

    VERSECOMPILER_API static bool IsSnippetFile(const CUTF8StringView& FilePath);
    VERSECOMPILER_API static bool IsModuleFile(const CUTF8StringView& FilePath);
    VERSECOMPILER_API static bool IsPackageFile(const CUTF8StringView& FilePath);
    VERSECOMPILER_API static bool IsProjectFile(const CUTF8StringView& FilePath);
    VERSECOMPILER_API static bool IsValidModuleName(const CUTF8StringView& ModuleName);
    VERSECOMPILER_API static bool IsValidSnippetFileName(const CUTF8StringView& FileName);

private:

    CUTF8String              _FilePath;
    const TSRef<IFileSystem> _FileSystem;
};

}
