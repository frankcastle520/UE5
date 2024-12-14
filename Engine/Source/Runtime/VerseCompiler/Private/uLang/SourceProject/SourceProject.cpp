// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/SourceProject/SourceProject.h"
#include "uLang/SourceProject/SourceDataProject.h"
#include "uLang/SourceProject/SourceProjectUtils.h"
#include "uLang/Common/Text/FilePathUtils.h"
#include "uLang/Common/Misc/Archive.h"

namespace uLang
{

//====================================================================================
// CSourceModule implementation
//====================================================================================

CUTF8StringView CSourceModule::GetNameFromFile() const
{
    return GetNameFromFile(GetFilePath());
}

CUTF8StringView CSourceModule::GetNameFromFile(const CUTF8StringView& ModuleFilePath)
{
    if (ModuleFilePath.IsEmpty())
    {
        return CUTF8StringView();
    }

    CUTF8StringView DirPath, FileName;
    FilePathUtils::SplitPath(ModuleFilePath, DirPath, FileName);
    CUTF8StringView Stem, Extension;
    FilePathUtils::SplitFileName(FileName, Stem, Extension);
    return Stem;
}

TOptional<TSRef<CSourceModule>> CSourceModule::FindSubmodule(const CUTF8StringView& ModuleName) const
{
    return _Submodules.FindByKey(ModuleName);
}

void CSourceModule::AddSnippet(const TSRef<ISourceSnippet>& Snippet)
{
    ULANG_ASSERTF(!_SourceSnippets.Contains(Snippet), "Duplicate Snippet `%s`!", *Snippet->GetPath());

    _SourceSnippets.Add(Snippet);
}

bool CSourceModule::RemoveSnippet(const TSRef<ISourceSnippet>& Snippet, bool bRecursive)
{
    return !VisitAll([&Snippet, bRecursive](CSourceModule& Module)
        {
            return Module._SourceSnippets.Remove(Snippet) == 0 && bRecursive;
        });
}

//====================================================================================
// CSourcePackage implementation
//====================================================================================

int32_t CSourcePackage::GetNumSnippets() const
{
    int32_t NumSnippets = 0;
    _RootModule->VisitAll([&NumSnippets](const CSourceModule& Module)
        {
            NumSnippets += Module._SourceSnippets.Num();
            return true;
        });
    return NumSnippets;
}

void CSourcePackage::SetDependencyPackages(TArray<CUTF8String>&& PackageNames)
{
    _Settings._DependencyPackages = Move(PackageNames);
}

void CSourcePackage::AddDependencyPackage(const CUTF8StringView& PackageName)
{
    _Settings._DependencyPackages.Add(PackageName);
}

void CSourcePackage::TruncateVniDestDir()
{
    CUTF8StringView VniParentDir, VniBaseDir;
    if (_Settings._VniDestDir)
    {
        _Settings._VniDestDir = FilePathUtils::GetFileName(*_Settings._VniDestDir);
    }
}

bool CSourcePackage::RemoveSnippet(const uLang::TSRef<ISourceSnippet>& Snippet)
{
    return _RootModule->RemoveSnippet(Snippet, true);
}

CSourceProject::CSourceProject(const CSourceProject& Other)
    : _Packages(Other._Packages)
    , _Name(Other._Name)
{
}

int32_t CSourceProject::GetNumSnippets() const
{
    int32_t NumSnippets = 0;
    for (const SPackage& Package : _Packages)
    {
        NumSnippets += Package._Package->GetNumSnippets();
    }
    return NumSnippets;
}

const CSourceProject::SPackage* CSourceProject::FindPackage(const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath) const
{
    return _Packages.FindByPredicate([&PackageName, &PackageVersePath](const SPackage& Package)
    {
        return
            Package._Package->GetName() == PackageName &&
            Package._Package->GetSettings()._VersePath == PackageVersePath;
    });
}

const CSourceProject::SPackage& CSourceProject::FindOrAddPackage(const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath)
{
    const SPackage* Package = FindPackage(PackageName, PackageVersePath);
    if (Package)
    {
        return *Package;
    }

    SPackage& NewPackage = _Packages[_Packages.Add({TSRef<CSourcePackage>::New(PackageName, TSRef<CSourceModule>::New("")), false})];
    NewPackage._Package->SetVersePath(PackageVersePath);
    return NewPackage;
}

void CSourceProject::AddSnippet(const uLang::TSRef<ISourceSnippet>& Snippet, const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath)
{
    FindOrAddPackage(PackageName, PackageVersePath)._Package->_RootModule->AddSnippet(Snippet);
}

bool CSourceProject::RemoveSnippet(const TSRef<ISourceSnippet>& Snippet)
{
    for (const SPackage& Package : _Packages)
    {
        if (Package._Package->RemoveSnippet(Snippet))
        {
            return true;
        }
    }

    return false;
}

void CSourceProject::TruncateVniDestDirs()
{
    for (const SPackage& Package : _Packages)
    {
        Package._Package->TruncateVniDestDir();
    }
}

}
