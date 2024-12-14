// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/SourceProject/SourceFileProject.h"
#include "uLang/SourceProject/SourceProjectWriter.h"
#include "uLang/Common/Text/FilePathUtils.h"
#include "uLang/Common/Templates/Storage.h"
#include "uLang/JSON/JSON.h"
#include "uLang/SourceProject/SourceProjectUtils.h"
#include "uLang/SourceProject/VerseVersion.h"

// #SUPPORT_LEGACY_VMODULES
// Temporary switch allowing legacy named vmodule files
// until we have converted all Verse code to new directory-driven module hierarchy
#define VERSE_ALLOW_VMODULE_FILES 1

namespace uLang
{

//====================================================================================
// CSourceFileSnippet implementation
//====================================================================================

TOptional<CUTF8String> CSourceFileSnippet::GetText() const
{
    // If a modified version exists, return that
    if (_ModifiedText)
    {
        return *_ModifiedText;
    }

    // Otherwise fetch current version from disk
    CUTF8StringBuilder FileContents;
    const bool bReadSuccess = _FileSystem->FileRead(_FilePath.AsCString(), [&FileContents](int32_t ByteSize)
    {
        return (void*)FileContents.AppendBuffer(ByteSize);
    });

    if (bReadSuccess)
    {
        const CUTF8StringView& ContentsView = FileContents.ToStringView();

        const int32_t ByteSize = FileContents.ByteLen();
        if (ByteSize >= 2 && !(ByteSize & 1) && ((ContentsView[0] == 0xff && ContentsView[1] == 0xfe) || (ContentsView[0] == 0xfe && ContentsView[1] == 0xff)))
        {
            // It's UTF-16 - we don't support that
            return uLang::EResult::Error;
        }

        if (ByteSize >= 3 && ContentsView[0] == 0xef && ContentsView[1] == 0xbb && ContentsView[2] == 0xbf)
        {
            // Found UTF-8 BOM, trim it away
            return CUTF8String(ContentsView.SubViewTrimBegin(3));
        }

        return FileContents.MoveToString();
    }
    return uLang::EResult::Error;
}

//====================================================================================
// CSourceFileModule implementation
//====================================================================================

CUTF8StringView CSourceFileModule::GetDirPath() const
{
    CUTF8StringView DirPath, FileName;
    FilePathUtils::SplitPath(_FilePath, DirPath, FileName);
    return DirPath;
}

TOptional<TSRef<CSourceFileModule>> CSourceFileModule::FindSubmodule(const CUTF8StringView& ModuleName) const
{
    TOptional<TSRef<CSourceModule>> Module = _Submodules.FindByKey(ModuleName);
    return reinterpret_cast<TOptional<TSRef<CSourceFileModule>>&>(Module);
}

TSRef<CSourceFileModule> CSourceFileModule::FindOrAddSubmodule(const CUTF8StringView& ModuleName, const CUTF8StringView& DirPath)
{
    TOptional<TSRef<CSourceFileModule>> Module = FindSubmodule(ModuleName);
    if (!Module)
    {
        Module = TSRef<CSourceFileModule>::New(ModuleName, DirPath);
        _Submodules.Add(*Module);
    }
    return *Module;
}

TOptional<TSRef<CSourceFileSnippet>> CSourceFileModule::FindSnippetByFilePath(const CUTF8StringView& FilePath, bool bRecursive) const
{
    TOptional<TSRef<ISourceSnippet>> FoundSnippet;
    
    VisitAll([&FilePath, bRecursive, &FoundSnippet](const CSourceModule& Module)
        {
            const CUTF8StringView& ModulePath = static_cast<const CSourceFileModule&>(Module).GetDirPath();
            if (ModulePath.IsEqualCaseIndependent(FilePath.SubViewBegin(ModulePath.ByteLen())))
            {
                FoundSnippet = Module._SourceSnippets.FindByPredicate([&FilePath](const ISourceSnippet* Snippet)
                    {
                        return static_cast<const CSourceFileSnippet*>(Snippet)->GetFilePath().IsEqualCaseIndependent(FilePath);
                    });
            }
            return bRecursive && !FoundSnippet;
        });

    return reinterpret_cast<TOptional<TSRef<CSourceFileSnippet>>&>(FoundSnippet);
}

//====================================================================================
// CSourceFilePackage implementation
//====================================================================================

bool FromJSON(const JSONValue& JSON, EVerseScope* Value)
{
    if (JSON.IsString())
    {
        const CUTF8StringView ValueString(JSON.GetString(), JSON.GetStringLength());
        if (ValueString == "PublicAPI")
        {
            *Value = EVerseScope::PublicAPI;
            return true;
        }
        if (ValueString == "InternalAPI")
        {
            *Value = EVerseScope::InternalAPI;
            return true;
        }
        if (ValueString == "PublicUser")
        {
            *Value = EVerseScope::PublicUser;
            return true;
        }
        if (ValueString == "InternalUser")
        {
            *Value = EVerseScope::InternalUser;
            return true;
        }
    }

    return false;
}

bool FromJSON(const JSONValue& JSON, EPackageRole* Value)
{
    if (JSON.IsString())
    {
        const CUTF8StringView ValueString(JSON.GetString(), JSON.GetStringLength());
        TOptional<EPackageRole> MaybeRole = ToPackageRole(ValueString);
        if (MaybeRole.IsSet())
        {
            *Value = *MaybeRole;
            return true;
        }
    }

    return false;
}

bool FromJSON(const JSONValue& JSON, CSourcePackage::SSettings* Value)
{
    return FromJSON(JSON, "versePath", &Value->_VersePath, false)
        && FromJSON(JSON, "verseScope", &Value->_VerseScope, false)
        && FromJSON(JSON, "dependencyPackages", &Value->_DependencyPackages, false)
        && FromJSON(JSON, "role", &Value->_Role, false)
        && FromJSON(JSON, "verseVersion", &Value->_VerseVersion, false)
        && FromJSON(JSON, "treatModulesAsImplicit", &Value->_bTreatModulesAsImplicit, false)
        && FromJSON(JSON, "vniDestDir", &Value->_VniDestDir, false)
        && FromJSON(JSON, "allowExperimental", &Value->_bAllowExperimental, false);
}

CSourceFilePackage::CSourceFilePackage(const CUTF8String& PackageFilePath, const TSRef<IFileSystem>& FileSystem, const TSRef<CDiagnostics>& Diagnostics)
    : CSourcePackage(FilePathUtils::GetNameFromFileOrDir(PackageFilePath),
        TSRef<CSourceFileModule>::New("", FilePathUtils::GetDirectory(PackageFilePath, true)))
    , _PackageFilePath(PackageFilePath)
    , _DirPath(_RootModule->GetFilePath())
    , _FileSystem(FileSystem)
{
    // Load package file from disk
    ReadPackageFile(PackageFilePath, Diagnostics);

    // Gather modules and snippets
    GatherPackageSourceFiles(PackageFilePath, FileSystem, Diagnostics);
}

CSourceFilePackage::CSourceFilePackage(const SPackageDesc& PackageDesc, const TSRef<IFileSystem>& FileSystem, const TSRef<CDiagnostics>& Diagnostics)
    : CSourcePackage(PackageDesc._Name, 
        TSRef<CSourceFileModule>::New("", FilePathUtils::AppendSlash(PackageDesc._DirPath)))
    , _DirPath(PackageDesc._DirPath)
    , _FilePaths(PackageDesc._FilePaths)
    , _FileSystem(FileSystem)
{
    _Settings = PackageDesc._Settings;

    // Gather modules and snippets
    GatherPackageSourceFiles(PackageDesc._Name, FileSystem, Diagnostics);
}

CSourceFileModule* CSourceFilePackage::GetModuleForFilePath(const CUTF8StringView& FilePath)
{
    CSourceFileModule* Result = nullptr;

    _RootModule->VisitAll([&FilePath, &Result](CSourceModule& Module)->bool {
        CSourceFileModule& FileModule = static_cast<CSourceFileModule&>(Module);
        const CUTF8StringView& ModulePath = FileModule.GetDirPath();
        if (FilePath.ByteLen() > ModulePath.ByteLen())
        {
            if (ModulePath == FilePathUtils::GetDirectory(FilePath))
            {
                Result = &FileModule;
                return false;
            }
        }
        return true;
        });

    return Result;
}

TOptional<TSRef<CSourceFileSnippet>> CSourceFilePackage::FindSnippetByFilePath(const CUTF8StringView& FilePath) const
{
    TOptional<TSRef<CSourceFileSnippet>> Result = _RootModule.As<CSourceFileModule>()->FindSnippetByFilePath(FilePath, true);
    if (Result)
    {
        return Result;
    }

    if (_Digest.IsSet() && _Digest->_Snippet.As<CSourceFileSnippet>()->GetFilePath().IsEqualCaseIndependent(FilePath))
    {
        return _Digest->_Snippet.As<CSourceFileSnippet>();
    }

    if (_PublicDigest.IsSet() && _PublicDigest->_Snippet.As<CSourceFileSnippet>()->GetFilePath().IsEqualCaseIndependent(FilePath))
    {
        return _PublicDigest->_Snippet.As<CSourceFileSnippet>();
    }

    return EResult::Unspecified;
}

TOptional<TSRef<CSourceFileSnippet>> CSourceFilePackage::AddSnippet(const CUTF8StringView& FilePath)
{
    CSourceFileModule* Module = GetModuleForFilePath(FilePath);
    if (Module)
    {
        TSRef<CSourceFileSnippet> Snippet = TSRef<CSourceFileSnippet>::New(FilePath, _FileSystem);
        Module->AddSnippet(Snippet);
        return Snippet;
    }

    return EResult::Error;
}

bool CSourceFilePackage::RemoveSnippet(const CUTF8StringView& FilePath)
{
    CSourceFileModule* Module = GetModuleForFilePath(FilePath);
    if (Module)
    {
        TOptional<TSRef<CSourceFileSnippet>> Snippet = Module->FindSnippetByFilePath(FilePath, false);
        if (Snippet)
        {
            Module->RemoveSnippet(*Snippet, false);
            return true;
        }
    }

    return false;
}

void CSourceFilePackage::ReadPackageFile(const CUTF8String& PackageFilePath, const TSRef<CDiagnostics>& Diagnostics)
{
    // Parse package file and settings
    if (!CSourceFileProject::IsPackageFile(PackageFilePath))
    {
        Diagnostics->AppendGlitch({
            EDiagnostic::ErrSystem_BadPackageFileName,
            CUTF8String("Package file `%s` has incorrect file extension.", *PackageFilePath) });
    }

    CUTF8StringBuilder PackageFileContents;
    const bool bReadSuccess = _FileSystem->FileRead(PackageFilePath.AsCString(), [&PackageFileContents](int32_t ByteSize)
        {
            return (void*)PackageFileContents.AppendBuffer(ByteSize);
        });

    if (bReadSuccess)
    {
        // Set up RapidJSON document
        JSONAllocator Allocator;
        JSONMemoryPoolAllocator MemoryPoolAllocator(RAPIDJSON_ALLOCATOR_DEFAULT_CHUNK_CAPACITY, &Allocator);
        const size_t StackCapacity = 1024;
        JSONDocument PackageDocument(&MemoryPoolAllocator, StackCapacity, &Allocator);

        // Parse package file into document
        PackageDocument.Parse(PackageFileContents.AsCString(), PackageFileContents.ByteLen());
        if (!PackageDocument.HasParseError() && FromJSON(PackageDocument, &_Settings))
        {
            if (_Settings._VniDestDir)
            {
                // Fully qualify VNI directory
                _Settings._VniDestDir.Emplace(FilePathUtils::ConvertRelativePathToFull(*_Settings._VniDestDir, _DirPath));
            }
        }
        else
        {
            Diagnostics->AppendGlitch({
                EDiagnostic::ErrSyntax_MalformedPackageFile,
                CUTF8String("Cannot parse contents of package file `%s`.", *PackageFilePath) });
        }
    }
    else
    {
        Diagnostics->AppendGlitch({
            EDiagnostic::WarnSystem_CannotReadPackage,
            CUTF8String("Unable to read package file `%s`.", *PackageFilePath) });
    }
}

TSRef<CSourceFileModule> CSourceFilePackage::ResolveModuleForRelativeVersePath(const CUTF8String& RelativeVersePath, const TSRef<CDiagnostics>& Diagnostics) const
{
    TSRef<CSourceFileModule> Module = _RootModule.As<CSourceFileModule>();
    FilePathUtils::ForeachPartOfPath(RelativeVersePath, [&Module, &RelativeVersePath, &Diagnostics](const CUTF8StringView& Part)
        {
            if (Part.IsFilled()) // If the path component is relative, just check the next component of the path.
            {
                if (Part == ".." || Part == ".")
                {
                    return;
                }
                if (CSourceFileProject::IsValidModuleName(Part))
                {
                    Module = Module->FindOrAddSubmodule(Part, FilePathUtils::AppendSlash(FilePathUtils::CombinePaths(Module->GetDirPath(), Part)));
                }
                else
                {
                    Diagnostics->AppendGlitch({
                        EDiagnostic::ErrSystem_InvalidModuleName,
                        CUTF8String("The relative Verse path `%s` contains disallowed characters that would lead to the invalid module name `%s`.", *RelativeVersePath, *CUTF8String(Part)) });
                }
            }
        });
    return Module;
}

void CSourceFilePackage::GatherPackageSourceFiles(const CUTF8String& PackageFilePath, const TSRef<IFileSystem>& FileSystem, const TSRef<CDiagnostics>& Diagnostics)
{
    TArray<CUTF8String> SourceFilePaths;
    TArray<CUTF8String> StrayPackageFilePaths; // Additional package files found underneath package

    // Helper to process a single file discovered on disk
    auto ProcessFile = [this, &PackageFilePath, &StrayPackageFilePaths, &Diagnostics, &FileSystem](const CUTF8String& FilePath)
    {
        CUTF8String NormalizedFilePath = FilePathUtils::NormalizePath(FilePath);

        bool bIsSnippetFile = CSourceFileProject::IsSnippetFile(NormalizedFilePath);
        bool bIsModuleFile = CSourceFileProject::IsModuleFile(NormalizedFilePath);
        if (bIsSnippetFile || bIsModuleFile)
        {
            // Find or create module for this file
            CUTF8String RelativeFilePath = FilePathUtils::ConvertFullPathToRelative(NormalizedFilePath, _DirPath);
            if (!ULANG_ENSUREF(RelativeFilePath != NormalizedFilePath, "File path `%s` appears to be not under package directory `%s`", *NormalizedFilePath, *_DirPath))
            {
                return;
            }

            if (bIsSnippetFile)
            {
                TSRef<CSourceFileSnippet> Snippet = TSRef<CSourceFileSnippet>::New(Move(NormalizedFilePath), FileSystem);
                if (Snippet->GetFilePath().EndsWith(".digest.verse"))
                {
                    if (_Digest.IsSet())
                    {
                        Diagnostics->AppendGlitch({
                            EDiagnostic::ErrSystem_DuplicateDigestFile,
                            CUTF8String("Found duplicate digest `%s` for package `%s` when digest `%s` already exists.", *Snippet->GetPath(), *GetName(), *_Digest->_Snippet->GetPath())});
                    }
                    else
                    {
                        _Digest.Emplace(SVersionedDigest{Move(Snippet), _Settings._VerseVersion.Get(Verse::Version::Default)});
                    }
                }
                else
                {
                    bool bIsVNIPackage = _Settings._VniDestDir.IsSet();
                    bool bHasNativeFileExtension = Snippet->GetFilePath().EndsWith(".native.verse");
                    CUTF8StringView Dir;
                    CUTF8StringView FileName;
                    FilePathUtils::SplitPath(Snippet->GetFilePath(), Dir, FileName);
                    CUTF8StringView Stem;
                    CUTF8StringView Extension;
                    FilePathUtils::SplitFileName(FileName, Stem, Extension);
                    if (!VerseFN::UploadedAtFNVersion::EnforceSnippetNameValidity(_Settings._UploadedAtFNVersion.Get(VerseFN::UploadedAtFNVersion::Latest))
                        && !bHasNativeFileExtension)
                    {
                        if (Stem.Contains('.'))
                        {
                            return;
                        }
                    }

                    if (VerseFN::UploadedAtFNVersion::EnforceSnippetNameValidity(_Settings._UploadedAtFNVersion.Get(VerseFN::UploadedAtFNVersion::Latest))
                        && !CSourceFileProject::IsValidSnippetFileName(FileName))
                    {
                        Diagnostics->AppendGlitch({EDiagnostic::ErrSystem_BadSnippetFileName,
                            CUTF8String("Verse file `%s` does not have a valid snippet name. Verse snippet names must end in `.verse` and cannot contain any of the following characters: %s.", *Snippet->GetPath(), CSourceFileSnippet::_InvalidSnippetCharacters)});
                    }
                    if (bIsVNIPackage && !bHasNativeFileExtension)
                    {
                        Diagnostics->AppendGlitch({
                            EDiagnostic::ErrSystem_InconsistentNativeFileExtension,
                            CUTF8String("Verse file `%s` is in VNI-capable package `%s`, therefore should have the `.native.verse` file extension.", *Snippet->GetPath(), *GetName()) });

                    }
                    else if (!bIsVNIPackage && bHasNativeFileExtension)
                    {
                        Diagnostics->AppendGlitch({
                            EDiagnostic::ErrSystem_InconsistentNativeFileExtension,
                            CUTF8String("Verse file `%s` is in non-VNI-capable package `%s`, therefore should not have the `.native.verse` file extension.", *Snippet->GetPath(), *GetName()) });
                    }

                    TSRef<CSourceFileModule> Module = ResolveModuleForRelativeVersePath(FilePathUtils::GetDirectory(RelativeFilePath), Diagnostics);
                    Module->AddSnippet(Snippet);
                }
            }
            else
            {
                ULANG_ASSERTF(bIsModuleFile, "Must be a module when we get here.");

#if VERSE_ALLOW_VMODULE_FILES
                // Find module based on its path
                TSRef<CSourceFileModule> Module = ResolveModuleForRelativeVersePath(FilePathUtils::GetDirectory(RelativeFilePath), Diagnostics);
                // Then gather settings and store the vmodule file path
                Module->_FilePath = Move(NormalizedFilePath);
#else
                Diagnostics->AppendGlitch({
                    EDiagnostic::ErrSystem_InvalidModuleFile,
                    CUTF8String("Found vmodule file `%s` which is not allowed.", *NormalizedFilePath) });
#endif
            }
        }
        else if (CSourceFileProject::IsPackageFile(FilePath) && FilePathUtils::NormalizePath(PackageFilePath) != NormalizedFilePath)
        {
            // Keep track of stray package files for sanity checking
            StrayPackageFilePaths.Add(NormalizedFilePath);
        }
    };

    // 1) Gather named modules and files
    if (_FilePaths.IsSet())
    {
        for (const CUTF8String& FilePath : *_FilePaths)
        {
            ProcessFile(FilePath);
        }
    }
    else
    {
        FileSystem->IterateDirectory(_DirPath.AsCString(), /*bRecursive =*/true,
            [&ProcessFile](const CUTF8StringView& FileName, const CUTF8StringView& Path, bool bIsDirectory)
            {
                if (!bIsDirectory)
                {
                    ProcessFile(FilePathUtils::CombinePaths(Path, FileName));
                }

                return true; // continue iteration
            });
    }

    // 2) Report stray package files encountered
    for (const CUTF8String& StrayPackageFilePath : StrayPackageFilePaths)
    {
        Diagnostics->AppendGlitch({
            EDiagnostic::ErrSystem_IllegalSubPackage,
            CUTF8String("Found illegal additional vpackage `%s` underneath package `%s`.", *StrayPackageFilePath , *PackageFilePath) });
    }

    // 3) Handle legacy named module files and adjust the module hierarchy accordingly
#if VERSE_ALLOW_VMODULE_FILES
    auto TryRenameModule = [&Diagnostics](const TSRef<CSourceFileModule>& Module, CSourceFileModule* RenamedParent, bool bHasRenamedParent, auto& TryRenameModule) -> bool
    {
        bool bRenamed = false;

        // Does this module have a legacy name override?
        CUTF8StringView NameOverride = Module->GetNameFromFile();
        if (NameOverride.IsFilled())
        {
            // Yes rename, and make new parent for renamed submodules
            bRenamed = true;

            if (CSourceFileProject::IsValidModuleName(NameOverride))
            {
                Module->_Name = NameOverride;
                RenamedParent = Module;
                bHasRenamedParent = true;
            }
            else
            {
                Diagnostics->AppendGlitch({
                    EDiagnostic::ErrSystem_InvalidModuleName,
                    CUTF8String("The path of the file `%s` contains disallowed characters that would lead to the invalid module name `%s`.", *Module->GetFilePath() , *CUTF8String(NameOverride)) });
            }
        }

        // Recurse into submodules
        for (int32_t Index = Module->_Submodules.Num() - 1; Index >= 0; --Index)
        {
            TSRef<CSourceFileModule> Submodule = Module->_Submodules[Index].As<CSourceFileModule>();

            if (TryRenameModule(Submodule, RenamedParent, bHasRenamedParent, TryRenameModule))
            {
                // Submodule was renamed, reparent it to the nearest renamed parent
                Module->_Submodules.RemoveAt(Index);
                RenamedParent->_Submodules.Add(Submodule);
            }
            else if (bHasRenamedParent)
            {
                // Delete submodule and move its snippets to the nearest renamed parent
                Module->_Submodules.RemoveAt(Index);
                RenamedParent->_SourceSnippets.Append(Move(Submodule->_SourceSnippets));
                ULANG_ENSUREF(Submodule->_Submodules.IsEmpty(), "Submodule must not have any submodules of its own left at this point.");
            }
        }

        return bRenamed;
    };
    
    if (TryRenameModule(_RootModule.As<CSourceFileModule>(), _RootModule.As<CSourceFileModule>(), false, TryRenameModule))
    {
        TSRef<CSourceFileModule> NewRootModule = TSRef<CSourceFileModule>::New("", FilePathUtils::AppendSlash(_RootModule.As<CSourceFileModule>()->GetDirPath()));
        NewRootModule->_Submodules.Add(_RootModule);
        _RootModule = NewRootModule;
    }
#endif
}

//====================================================================================
// CSourceFileProject implementation
//====================================================================================

bool FromJSON(const JSONValue& JSON, SPackageDesc* Value)
{
    return FromJSON(JSON, "name", &Value->_Name, true)
        && FromJSON(JSON, "dirPath", &Value->_DirPath, true)
        && FromJSON(JSON, "filePaths", &Value->_FilePaths, false)
        && FromJSON(JSON, "settings", &Value->_Settings, true);
}

bool FromJSON(const JSONValue& JSON, SPackageRef* Value)
{
    // Set optional values prior to read
    Value->_ReadOnly = false;
    Value->_Build = true;

    // There has to be one of path or desc
    bool HavePath = FromJSON(JSON, "path", &Value->_FilePath, false);
    return FromJSON(JSON, "desc", &Value->_Desc, !HavePath)
        && FromJSON(JSON, "readOnly", &Value->_ReadOnly, false)
        && FromJSON(JSON, "build", &Value->_Build, false);
}

bool FromJSON(const JSONValue& JSON, SProjectDesc* Value)
{
    return FromJSON(JSON, "packages", &Value->_Packages, true);
}

CSourceFileProject::CSourceFileProject(const CUTF8String& ProjectFilePath, const TSRef<IFileSystem>& FileSystem, const TSRef<CDiagnostics>& Diagnostics)
    : CSourceProject(FilePathUtils::GetNameFromFileOrDir(ProjectFilePath))
    , _FilePath(ProjectFilePath)
    , _FileSystem(FileSystem)
{
    // Parse project file and load packages specified in it
    CUTF8StringBuilder ProjectFileContents;
    const bool bReadSuccess = _FileSystem->FileRead(ProjectFilePath.AsCString(), [&ProjectFileContents](int32_t ByteSize)
    {
        return (void*)ProjectFileContents.AppendBuffer(ByteSize);
    });

    if (bReadSuccess)
    {
        // Set up RapidJSON document
        JSONAllocator Allocator;
        JSONMemoryPoolAllocator MemoryPoolAllocator(RAPIDJSON_ALLOCATOR_DEFAULT_CHUNK_CAPACITY, &Allocator);
        const size_t StackCapacity = 1024;
        JSONDocument ProjectDocument(&MemoryPoolAllocator, StackCapacity, &Allocator);

        // Parse project file into document
        SProjectDesc ProjectDesc;
        ProjectDocument.Parse(ProjectFileContents.AsCString(), ProjectFileContents.ByteLen());
        if (!ProjectDocument.HasParseError() && FromJSON(ProjectDocument, &ProjectDesc))
        {
            for (const SPackageRef& PackageRef : ProjectDesc._Packages)
            {
                // Add packages to build and skip packages not to build. [Could alternatively pass along `_Build` setting.]
                if (PackageRef._Build)
                {
                    if (PackageRef._FilePath)
                    {
                        CUTF8String PackageFilePath = FilePathUtils::NormalizePath(FilePathUtils::ConvertRelativePathToFull(*PackageRef._FilePath, FilePathUtils::GetDirectory(ProjectFilePath)));
                        TSRef<CSourceFilePackage> NewPackage = TSRef<CSourceFilePackage>::New(PackageFilePath, _FileSystem, Diagnostics);
                        _Packages.Add({ NewPackage, PackageRef._ReadOnly });
                    }
                    else if (ULANG_ENSUREF(PackageRef._Desc, "FromJSON must ensure that there is either a file path or a descriptor."))
                    {
                        SPackageDesc FullDesc = *PackageRef._Desc;
                        FullDesc._DirPath = FilePathUtils::NormalizePath(FilePathUtils::ConvertRelativePathToFull(FullDesc._DirPath, FilePathUtils::GetDirectory(ProjectFilePath)));
                        if (!_FileSystem.Get()->DoesDirectoryExist(FullDesc._DirPath.AsCString()))
                        {
                            continue;
                        }
                        if (FullDesc._Settings._VniDestDir)
                        {
                            FullDesc._Settings._VniDestDir = FilePathUtils::NormalizePath(FilePathUtils::ConvertRelativePathToFull(*FullDesc._Settings._VniDestDir, FilePathUtils::GetDirectory(ProjectFilePath)));
                        }
                        TSRef<CSourceFilePackage> NewPackage = TSRef<CSourceFilePackage>::New(FullDesc, _FileSystem, Diagnostics);
                        _Packages.Add({ NewPackage, PackageRef._ReadOnly });
                    }
                }
            }
        }
        else
        {
            Diagnostics->AppendGlitch({
                EDiagnostic::ErrSyntax_MalformedProjectFile,
                CUTF8String("Cannot parse contents of project file `%s`.", *ProjectFilePath) });
        }
    }
    else
    {
        Diagnostics->AppendGlitch({
            EDiagnostic::ErrSystem_CannotReadText,
            CUTF8String("Unable to read project file `%s`.", *ProjectFilePath) });
    }
}

CSourceFileProject::CSourceFileProject(
    const CUTF8String& Name,
    const TSRef<IFileSystem>& FileSystem,
    const TArray<SPackageDesc>& Packages,
    const TSRef<CDiagnostics>& Diagnostics)
    : CSourceProject(Name)
    , _FileSystem(FileSystem)
{
    if (Packages.IsEmpty())
    {
        Diagnostics->AppendGlitch({ EDiagnostic::WarnProject_EmptyProject });
    }
    else
    {
        // Assemble project packages
        for (const SPackageDesc& Package : Packages)
        {
            TSRef<CSourceFilePackage> NewPackage = TSRef<CSourceFilePackage>::New(Package, _FileSystem, Diagnostics);
            const bool bIsReadOnly = false;
            _Packages.Add({ NewPackage, bIsReadOnly });
        }
    }
}

bool CSourceFileProject::WriteProjectFile(const CUTF8String& ProjectFilePath, const TSRef<CDiagnostics>& Diagnostics)
{
    CSourceProjectWriter Writer(_FileSystem, Diagnostics);
    return Writer.WriteProjectFile(CSourceProjectWriter::GetProjectDesc(*this), ProjectFilePath);
}

bool CSourceFileProject::WriteVSCodeWorkspaceFile(const CUTF8String& WorkspaceFilePath, const CUTF8String& ProjectFilePath, const TSRef<CDiagnostics>& Diagnostics)
{
    CSourceProjectWriter Writer(_FileSystem, Diagnostics);
    return Writer.WriteVSCodeWorkspaceFile(CSourceProjectWriter::GetWorkspaceDesc(*this, ProjectFilePath), WorkspaceFilePath);
}

TOptional<TSRef<CSourceFileSnippet>> CSourceFileProject::FindSnippetByFilePath(const CUTF8StringView& FilePath) const
{
    for (const SPackage& Package : _Packages)
    {
        TOptional<TSRef<CSourceFileSnippet>> Snippet = Package._Package.As<CSourceFilePackage>()->FindSnippetByFilePath(FilePath);
        if (Snippet)
        {
            return Snippet;
        }
    }

    return EResult::Unspecified;
}

TOptional<TSRef<CSourceFileSnippet>> CSourceFileProject::AddSnippet(const CUTF8StringView& FilePath)
{
    for (const SPackage& Package : _Packages)
    {
        TOptional<TSRef<CSourceFileSnippet>> Snippet = Package._Package.As<CSourceFilePackage>()->AddSnippet(FilePath);
        if (Snippet)
        {
            return Snippet;
        }
    }

    return EResult::Error;
}

bool CSourceFileProject::RemoveSnippet(const CUTF8StringView& FilePath)
{
    for (const SPackage& Package : _Packages)
    {
        if (Package._Package.As<CSourceFilePackage>()->RemoveSnippet(FilePath))
        {
            return true;
        }
    }

    return false;
}

bool CSourceFileProject::IsSnippetFile(const CUTF8StringView& FilePath)
{
    return FilePath.SubViewEnd(SnippetExt.ByteLen()) == SnippetExt;
}

bool CSourceFileProject::IsModuleFile(const CUTF8StringView& FilePath)
{
    return FilePath.SubViewEnd(ModuleExt.ByteLen()) == ModuleExt;
}

bool CSourceFileProject::IsPackageFile(const CUTF8StringView& FilePath)
{
    return FilePath.SubViewEnd(PackageExt.ByteLen()) == PackageExt;
}

bool CSourceFileProject::IsProjectFile(const CUTF8StringView& FilePath)
{
    return FilePath.SubViewEnd(ProjectExt.ByteLen()) == ProjectExt;
}

bool CSourceFileProject::IsValidModuleName(const CUTF8StringView& ModuleName)
{
    const UTF8Char FirstCh = ModuleName.FirstByte();
    if (!CUnicode::IsAlphaASCII(FirstCh) && FirstCh != '_')
    {
        return false;
    }

    for (const UTF8Char* Ch = ModuleName._Begin; Ch < ModuleName._End; ++Ch)
    {
        if (!CUnicode::IsAlphaASCII(*Ch) && !CUnicode::IsDigitASCII(*Ch) && *Ch != '_')
        {
            return false;
        }
    }

    return true;
}

bool CSourceFileProject::IsValidSnippetFileName(const CUTF8StringView& FileName)
{
    const char* InvalidCharacters = CSourceFileSnippet::_InvalidSnippetCharacters;
    for (char Ch = *InvalidCharacters; Ch != '\0'; Ch = *(++InvalidCharacters))
    {
        if (FileName.Contains(Ch))
        {
            return false;
        }
    }
    if (FileName.EndsWith(SnippetExt))
    {
        return true;
    }

	return false;
}

}
// namespace uLang
