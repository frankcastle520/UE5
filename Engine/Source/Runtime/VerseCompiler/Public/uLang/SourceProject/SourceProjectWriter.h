// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/SourceProject/SourceProject.h"
#include "uLang/SourceProject/IFileSystem.h"
#include "uLang/Common/Containers/Function.h"
#include "uLang/Diagnostics/Glitch.h"
#include "uLang/JSON/JSON.h"

namespace uLang
{

// Specification of a package embedded in a project file
// This mirrors FVersePackageDesc in the runtime
struct SPackageDesc
{
    CUTF8String _Name;
    CUTF8String _DirPath; // To be used for VSCode workspace. Also for compilation unless _FilePaths is specified.
    TOptional<TArray<CUTF8String>> _FilePaths; // Optional array, so we can distinguish between absence of array vs absence of files
    CSourcePackage::SSettings _Settings;
};

// A package as represented in a Verse project file
struct SPackageRef
{
    TOptional<CUTF8String> _FilePath; // Path to the vpackage file
    TOptional<SPackageDesc> _Desc;    // Or, alternatively, directly embedded package desc 
    bool _ReadOnly = false;
    bool _Build = true;
};

// The contents of a Verse project file
struct SProjectDesc
{
    TArray<SPackageRef> _Packages;
};

// A root folder inside a VSCode workspace
struct SWorkspacePackageRef
{
    CUTF8String _Name;
    CUTF8String _DirPath;
};

// A VSCode workspace
struct SWorkspaceDesc
{
    TArray<SWorkspacePackageRef> _Folders;
    CUTF8String _WorkspaceFilePath;
    bool (*_AddSettingsFunc)(uLang::JSONDocument*, const CUTF8StringView& WorkspaceFilePath);
};

/**
 * Helper class to write a source project to disk
 * This can either be a copy from one location to another, or save an in-memory project to the file system
 **/
class CSourceProjectWriter
{
public:
    CSourceProjectWriter(const TSRef<IFileSystem>& FileSystem, const TSRef<CDiagnostics>& Diagnostics)
        : _FileSystem(FileSystem)
        , _Diagnostics(Diagnostics)
    {}

    /**
     * Write the entire given source project to disk
     * Iff ResultProjectFilePath is given, a vproject file will be generated and the path to it returned
     **/
    VERSECOMPILER_API bool WriteProject(const CSourceProject& Project, const CUTF8String& DestinationDir, CUTF8String* ResultProjectFilePath = nullptr) const;

    /**
     * Write only a vproject file to the given file path
     **/
    VERSECOMPILER_API bool WriteProjectFile(const SProjectDesc& ProjectDesc, const CUTF8String& ProjectFilePath) const;

    /**
     * Write a .code-workspace file to the given file path
     **/
    VERSECOMPILER_API bool WriteVSCodeWorkspaceFile(const SWorkspaceDesc& WorkspaceDesc, const CUTF8String& WorkspaceFilePath) const;

    /**
     * Derive a project desc from a source project
     **/
    VERSECOMPILER_API static SProjectDesc GetProjectDesc(const CSourceProject& Project);

    /**
     * Derive a workspace desc from a source project
     **/
    VERSECOMPILER_API static SWorkspaceDesc GetWorkspaceDesc(const CSourceProject& Project, const CUTF8String& ProjectFilePath);

private:

    /**
     * Write a given source package to disk
     **/
    bool WritePackage(const CSourcePackage& Package, const CUTF8String& DestinationDir, SPackageDesc* OutPackageDesc = nullptr) const;

    /**
     * Write a single snippet to the given directory
     **/
    bool WriteSnippet(const CSourceModule& Module, const TSRef<ISourceSnippet>& Snippet, const CUTF8String& ContainingDir) const;

    /**
     * Serialize to JSON and write a given struct of type T to a file
     **/
    template<class T>
    bool WriteJSONFile(const T& Object, bool (*ToJSON)(const T& Value, JSONDocument* JSON), EDiagnostic SerializationError, const CUTF8String& DestinationPath) const;

    TSRef<IFileSystem> _FileSystem;
    TSRef<CDiagnostics> _Diagnostics;
};

}