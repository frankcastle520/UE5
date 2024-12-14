// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/SourceProject/SourceProjectWriter.h"
#include "uLang/SourceProject/PackageRole.h"
#include "uLang/SourceProject/SourceProjectUtils.h"
#include "uLang/Common/Text/FilePathUtils.h"
#include "uLang/JSON/JSON.h"

namespace uLang
{

bool ToJSON(const SWorkspacePackageRef& Value, JSONValue* JSON, JSONMemoryPoolAllocator& Allocator)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetObject();

    return ToJSON(Value._Name, "name", JSON, Allocator)
        && ToJSON(Value._DirPath, "path", JSON, Allocator);
}

bool ToJSON(const SWorkspaceDesc& Value, JSONDocument* JSON)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetObject();
    auto& Allocator = JSON->GetAllocator();

    if (!ToJSON(Value._Folders, "folders", JSON, Allocator))
    {
        return false;
    }
    
    if (Value._AddSettingsFunc && !Value._AddSettingsFunc(JSON, Value._WorkspaceFilePath))
    {
        return false;   
    }

    return true;
}

bool ToJSON(const CSourceModule& Value, JSONDocument* JSON)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetObject();

    return true;
}

bool ToJSON(const EVerseScope Scope, JSONValue* JSON, JSONMemoryPoolAllocator& Allocator)
{
    if (!JSON)
    {
        return false;
    }
    return ToJSON(CUTF8StringView(ToString(Scope)), JSON, Allocator);
}

bool ToJSON(const EPackageRole Role, JSONValue* JSON, JSONMemoryPoolAllocator& Allocator)
{
    if (!JSON)
    {
        return false;
    }
    return ToJSON(CUTF8StringView(ToString(Role)), JSON, Allocator);
}

bool ToJSON(const CSourcePackage::SSettings& Value, JSONValue* JSON, JSONMemoryPoolAllocator& Allocator)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetObject();

    return ToJSON(Value._VersePath, "versePath", JSON, Allocator)
        && ToJSON(Value._VerseScope, "verseScope", JSON, Allocator)
        && ToJSON(Value._Role, "role", JSON, Allocator)
        && ToJSON(Value._VerseVersion, "verseVersion", JSON, Allocator)
        && (!Value._bTreatModulesAsImplicit || ToJSON(Value._bTreatModulesAsImplicit, "treatModulesAsImplicit", JSON, Allocator))
        && ToJSON(Value._DependencyPackages, "dependencyPackages", JSON, Allocator)
        && ToJSON(Value._VniDestDir, "vniDestDir", JSON, Allocator)
        && ToJSON(Value._bAllowExperimental, "allowExperimental", JSON, Allocator);
}

bool ToJSON(const CSourcePackage& Value, JSONDocument* JSON)
{
    return ToJSON(Value.GetSettings(), JSON, JSON->GetAllocator());
}

bool ToJSON(const SPackageDesc& Value, JSONValue* JSON, JSONMemoryPoolAllocator& Allocator)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetObject();

    return ToJSON(Value._Name, "name", JSON, Allocator)
        && ToJSON(Value._DirPath, "dirPath", JSON, Allocator)
        && ToJSON(Value._FilePaths, "filePaths", JSON, Allocator)
        && ToJSON(Value._Settings, "settings", JSON, Allocator);
}

bool ToJSON(const SPackageRef& Value, JSONValue* JSON, JSONMemoryPoolAllocator& Allocator)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetObject();

    return ToJSON(Value._FilePath, "path", JSON, Allocator)
        && ToJSON(Value._Desc, "desc", JSON, Allocator)
        && ToJSON(Value._ReadOnly, "readOnly", JSON, Allocator);
}

bool ToJSON(const SProjectDesc& Value, JSONDocument* JSON)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetObject();

    return ToJSON(Value._Packages, "packages", JSON, JSON->GetAllocator());
}

bool CSourceProjectWriter::WritePackage(const CSourcePackage& Package, const CUTF8String& DestinationDir, SPackageDesc* OutPackageDesc) const
{
    // Reject packages with no name
    if (Package.GetName().IsEmpty())
    {
        _Diagnostics->AppendGlitch({
            EDiagnostic::ErrSyntax_MalformedPackageFile,
            CUTF8String("Package has no name.")
            });

        return false;
    }

    // Make flattened package name
    CUTF8String FlatPackageName = Package.GetName().Replace('/', '-');

    // Build directory for new package
    CUTF8String NewPackageDir = FilePathUtils::CombinePaths(DestinationDir, FlatPackageName);

    // Remove the package directory if it already exists
    if (_FileSystem->DoesDirectoryExist(NewPackageDir.AsCString()))
    {
        if (!_FileSystem->DeleteDirectory(NewPackageDir.AsCString()))
        {
            _Diagnostics->AppendGlitch({
                EDiagnostic::ErrSystem_CannotDeleteDirectory,
                CUTF8String("Failed to remove preexisting package directory `%s`.", *DestinationDir)
                });

            return false;
        }
    }

    // Recreate a new, empty directory
    if (!_FileSystem->CreateDirectory(NewPackageDir.AsCString()))
    {
        _Diagnostics->AppendGlitch({
            EDiagnostic::ErrSystem_CannotCreateDirectory,
            CUTF8String("Unable to create directory `%s`.", *NewPackageDir)
            });

        return false;
    }

    // Then loop over all modules
    auto WriteModule = [this](const CSourceModule& Module, const CUTF8String& ParentModuleDir, auto& WriteModule)
        {
            // Build directory for new module
            CUTF8String NewModuleDir = FilePathUtils::CombinePaths(ParentModuleDir, Module.GetName());
            if (!_FileSystem->CreateDirectory(NewModuleDir.AsCString()))
            {
                fprintf(stderr, "Failed to create module directory.\n");
                return false;
            }

            // Loop over all source snippets and place them into the module folder
            for (const TSRef<ISourceSnippet>& Snippet : Module._SourceSnippets)
            {
                if (!WriteSnippet(Module, Snippet, NewModuleDir))
                {
                    return false;
                }
            }

            // Recurse into submodules
            for (const TSRef<CSourceModule>& Submodule : Module._Submodules)
            {
                if (!WriteModule(*Submodule, NewModuleDir, WriteModule))
                {
                    return false;
                }
            }

            return true;
        };
    if (!WriteModule(*Package._RootModule, NewPackageDir, WriteModule))
    {
        return false;
    }

    // Write digest if present and no source exists
    bool bIsDigestPackage = Package._Digest.IsSet() && Package.GetNumSnippets() == 0;
    if (bIsDigestPackage)
    {
        if (!WriteSnippet(*Package._RootModule, Package._Digest->_Snippet, NewPackageDir))
        {
            return false;
        }
    }

    // Create package descriptor if requested
    if (OutPackageDesc)
    {
        OutPackageDesc->_DirPath = NewPackageDir;
        OutPackageDesc->_Name = Package.GetName();
        OutPackageDesc->_Settings = Package.GetSettings();
        if (bIsDigestPackage && OutPackageDesc->_Settings._Role == EPackageRole::Source)
        {
            // Make sure this reflects what was written out
            OutPackageDesc->_Settings._Role = EPackageRole::External;
        }
    }

    return true;
}

bool CSourceProjectWriter::WriteProject(const CSourceProject& Project, const CUTF8String& DestinationDir, CUTF8String* ResultProjectFilePath /*= nullptr*/) const
{
    // Remove the destination directory if it already exists
    if (_FileSystem->DoesDirectoryExist(DestinationDir.AsCString()))
    {
        if (!_FileSystem->DeleteDirectory(DestinationDir.AsCString()))
        {
            _Diagnostics->AppendGlitch({
                EDiagnostic::ErrSystem_CannotDeleteDirectory,
                CUTF8String("Failed to remove preexisting destination directory `%s`.", *DestinationDir)
                });

            return false;
        }
    }

    // Create destination directory
    if (!_FileSystem->CreateDirectory(DestinationDir.AsCString()))
    {
        _Diagnostics->AppendGlitch({
            EDiagnostic::ErrSystem_CannotCreateDirectory,
            CUTF8String("Unable to create directory `%s`.", *DestinationDir)
            });

        return false;
    }

    // Loop over packages and write them out
    SProjectDesc ProjectDesc;
    ProjectDesc._Packages.Reserve(Project._Packages.Num());
    for (const CSourceProject::SPackage& Package : Project._Packages)
    {
        // Write each package
        SPackageDesc PackageDesc;
        if (!WritePackage(*Package._Package, DestinationDir, &PackageDesc))
        {
            return false;
        }

        // Keep track in project descriptor
        ProjectDesc._Packages.Add({ EResult::Unspecified, PackageDesc, Package._bReadonly, true });
    }

    if (ResultProjectFilePath)
    {
        CUTF8String ProjectFilePath = FilePathUtils::CombinePaths(DestinationDir, CUTF8String("%s%s", *Project.GetName(), ProjectExt._Begin));
        WriteProjectFile(ProjectDesc, ProjectFilePath);
        *ResultProjectFilePath = ProjectFilePath;
    }

    return true;
}

bool CSourceProjectWriter::WriteProjectFile(const SProjectDesc& ProjectDesc, const CUTF8String& ProjectFilePath) const
{
    return WriteJSONFile(ProjectDesc, &ToJSON, EDiagnostic::ErrSyntax_MalformedProjectFile, ProjectFilePath);
}

bool CSourceProjectWriter::WriteVSCodeWorkspaceFile(const SWorkspaceDesc& WorkspaceDesc, const CUTF8String& WorkspaceFilePath) const
{
    return WriteJSONFile(WorkspaceDesc, &ToJSON, EDiagnostic::ErrSyntax_MalformedProjectFile, WorkspaceDesc._WorkspaceFilePath);
}

SProjectDesc CSourceProjectWriter::GetProjectDesc(const CSourceProject& Project)
{
    SProjectDesc ProjectDesc;

    for (const CSourceProject::SPackage& Package : Project._Packages)
    {
        SPackageRef PackageRef{};
        if (Package._Package->GetFilePath().IsFilled())
        {
            PackageRef._FilePath = Package._Package->GetFilePath();
        }
        else
        {
            PackageRef._Desc = SPackageDesc{ Package._Package->GetName(), Package._Package->GetDirPath(), {}, Package._Package->GetSettings() };
        }
        ProjectDesc._Packages.Add(PackageRef);
    }

    return ProjectDesc;
}

SWorkspaceDesc CSourceProjectWriter::GetWorkspaceDesc(const CSourceProject& Project, const CUTF8String& ProjectFilePath)
{
    SWorkspaceDesc WorkspaceDesc;

    for (const CSourceProject::SPackage& Package : Project._Packages)
    {
        if (Package._Package->GetSettings()._Role != ConstraintPackageRole)
        {
            WorkspaceDesc._Folders.Add({ Package._Package->GetName(), Package._Package->GetDirPath() });
        }
    }

    if (ProjectFilePath.IsFilled())
    {
        WorkspaceDesc._Folders.Add({"vproject - DO NOT MODIFY", FilePathUtils::GetDirectory(ProjectFilePath)});
    }

    return WorkspaceDesc;
}

namespace Private
{
    // In order to preserve compilation order, we need to preseve any subdirectories in the module.
    CUTF8String GetSnippetRelativeDirectory(const CSourceModule& Module, const TSRef<ISourceSnippet>& Snippet)
    {
        const CUTF8String& ModulePath = Module.GetFilePath();
        if (ModulePath.IsFilled() && ModulePath != "/")
        {
            return FilePathUtils::ConvertFullPathToRelative(Snippet->GetPath(), FilePathUtils::GetDirectory(ModulePath));
        }
        else
        {
            return FilePathUtils::GetFileName(Snippet->GetPath());
        }
    }
}

bool CSourceProjectWriter::WriteSnippet(const CSourceModule& Module, const TSRef<ISourceSnippet>& Snippet, const CUTF8String& ContainingDir) const
{
    TOptional<CUTF8String> SnippetText = Snippet->GetText();
    if (SnippetText)
    {
        const CUTF8String NewSnippetPath = FilePathUtils::CombinePaths(ContainingDir, Private::GetSnippetRelativeDirectory(Module, Snippet));
        if (!_FileSystem->CreateDirectory(FilePathUtils::GetDirectory(NewSnippetPath).AsCString()) ||
            !_FileSystem->FileWrite(*NewSnippetPath, **SnippetText, (*SnippetText).ByteLen()))
        {
            _Diagnostics->AppendGlitch({
                EDiagnostic::ErrSystem_CannotWriteText,
                CUTF8String("Unable to write snippet file `%s`.", *NewSnippetPath)
                });

            return false;
        }
    }

    return true;
}

template<class T>
bool CSourceProjectWriter::WriteJSONFile(const T& Object, bool (*ToJSON)(const T& Value, JSONDocument* JSON), EDiagnostic SerializationError, const CUTF8String& DestinationPath) const
{
    // Set up RapidJSON memory management
    JSONAllocator Allocator;
    JSONMemoryPoolAllocator MemoryPoolAllocator(RAPIDJSON_ALLOCATOR_DEFAULT_CHUNK_CAPACITY, &Allocator);
    const size_t JSONStackCapacity = 1024;

    // Create document from object
    JSONDocument Document(&MemoryPoolAllocator, JSONStackCapacity, &Allocator);
    const bool bIsSerializeSuccess = ToJSON(Object, &Document);
    if (!bIsSerializeSuccess)
    {
        _Diagnostics->AppendGlitch({
            SerializationError,
            CUTF8String("Cannot serialize contents of file `%s`.", *DestinationPath)
            });

        return false;
    }

    // Serialize to a memory buffer
    JSONStringBuffer Buffer;
    JSONStringWriter Writer(Buffer);
    const bool bJSONWriteSuccess = Document.Accept(Writer);
    if (!bJSONWriteSuccess)
    {
        _Diagnostics->AppendGlitch({
            SerializationError,
            CUTF8String("Cannot serialize contents of file `%s`.", *DestinationPath)
            });

        return false;
    }

    // Write to file
    const bool bWriteSuccess = _FileSystem->FileWrite(DestinationPath.AsCString(), Buffer.GetString(), Buffer.GetSize());
    if (!bWriteSuccess)
    {
        _Diagnostics->AppendGlitch({
            EDiagnostic::ErrSystem_CannotWriteText,
            CUTF8String("Unable to write file `%s`.", *DestinationPath)
            });

        return false;
    }

    return true;
}

}
