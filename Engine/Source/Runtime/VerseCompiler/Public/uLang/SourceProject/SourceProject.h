// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/SharedPointerArray.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/CompilerPasses/CompilerTypes.h"

namespace uLang
{

class CArchive;
class CSourcePackage;
class CSourceProject;

/**
 * Abstraction of a source file
 **/
class ISourceSnippet : public CSharedMix
{
public:

    virtual ~ISourceSnippet() {}

    /**
     * Globally unique path of this snippet, e.g. a fully qualified file path
     **/
    virtual CUTF8String GetPath() const = 0;
    virtual void SetPath(const CUTF8String& Path) = 0;

    /**
     * Get text representation of this snippet
     * When the result is EResult::Unspecified, no text representation exists
     * When the result is EResult::Error, text representation exists, but an error occurred retrieving it
     **/
    virtual TOptional<CUTF8String> GetText() const = 0;

    /**
     * Get Vst representation of this snippet
     * When the result is EResult::Unspecified, no Vst representation exists
     * When the result is EResult::Error, Vst representation exists, but an error occurred retrieving it
     **/
    virtual TOptional<TSRef<Verse::Vst::Snippet>> GetVst() const = 0;
    virtual void SetVst(TSRef<Verse::Vst::Snippet> Snippet) = 0;
};

/**
 * A module
 **/
class CSourceModule : public CSharedMix
{
public:

    /// The source files contained in the module
    TSRefArray<ISourceSnippet> _SourceSnippets;

    /// Submodules of this module
    TSRefArray<CSourceModule> _Submodules;

    /// Construct from name
    CSourceModule(const CUTF8StringView& Name) : _Name(Name) {}
    virtual ~CSourceModule() = default;

    const CUTF8String& GetName() const { return _Name; }
    virtual const CUTF8String& GetFilePath() const { return CUTF8String::GetEmpty(); }
    VERSECOMPILER_API CUTF8StringView GetNameFromFile() const;
    VERSECOMPILER_API static CUTF8StringView GetNameFromFile(const CUTF8StringView& ModuleFilePath);

    VERSECOMPILER_API TOptional<TSRef<CSourceModule>> FindSubmodule(const CUTF8StringView& ModuleName) const;

    VERSECOMPILER_API void AddSnippet(const uLang::TSRef<ISourceSnippet>& Snippet);
    VERSECOMPILER_API bool RemoveSnippet(const uLang::TSRef<ISourceSnippet>& Snippet, bool bRecursive);

    /// Visit this module and all its submodules
    /// Lambda returns true to continue visiting, false to terminate search
    /// Returns true if all modules have been visited, false if search was terminated early
    template<typename FunctionType>
    bool VisitAll(FunctionType&& Lambda) const;
    template<typename FunctionType>
    bool VisitAll(FunctionType&& Lambda); // non-const version

    /// For lookup by name
    ULANG_FORCEINLINE bool operator==(const CUTF8StringView& Name) const { return _Name == Name; }

protected:

    /// Name of this module
    CUTF8String _Name;
};

/**
 * A package of modules
 **/
class CSourcePackage : public CSharedMix
{
public:
    /// Settings of a package
    /// This mirrors FVersePackageSettings in the runtime
    struct SSettings
    {
        CUTF8String            _VersePath;            // Verse path of the root module of this package
        EVerseScope            _VerseScope = EVerseScope::PublicUser; // Origin/visibility of Verse code in this package
        EPackageRole           _Role = EPackageRole::Source; // The role this package plays in the project.
        TOptional<uint32_t>    _VerseVersion;         // The language version the package targets. Note that this value is ignored for digests, which are assumed to target the latest unstable version.
        TOptional<uint32_t>    _UploadedAtFNVersion;  // Only set for constraint `_Role`.
        bool                   _bTreatModulesAsImplicit = false; // If true, module macros in this package's source and digest will be treated as implicit
        TArray<CUTF8String>    _DependencyPackages;   // Names of packages this package is dependent on
        TOptional<CUTF8String> _VniDestDir;           // Destination directory for VNI generated C++ code (fully qualified)
        bool                   _bAllowExperimental = false; // If true, this package can use experimental definitions (but cannot publish in UEFN).

        uint32_t GetUploadedAtFNVersion(uint32_t Default) const
        {
            return _UploadedAtFNVersion.Get(Default);
        }
    };

    struct SVersionedDigest
    {
        TSRef<ISourceSnippet> _Snippet;
        uint32_t _EffectiveVerseVersion;
    };

    /// Where the source code of this package originates
    enum EOrigin : uint8_t
    {
        Unknown,
        Memory,
        FileSystem
    };


    /// The root module of this package, equivalent to the _VersePath specified in _Settings
    TSRef<CSourceModule> _RootModule;

    /// Optional digest to be used instead of source if desired
    TOptional<SVersionedDigest> _Digest;

    /// The public-only digest, if it exists.
    TOptional<SVersionedDigest> _PublicDigest;

    /// Construct from name
    CSourcePackage(const CUTF8StringView& Name, const TSRef<CSourceModule>& RootModule) : _RootModule(RootModule), _Name(Name) {}
    virtual ~CSourcePackage() = default;

    const CUTF8String& GetName() const { return _Name; }
    const SSettings& GetSettings() const { return _Settings; }
    virtual EOrigin GetOrigin() const { return EOrigin::Unknown; }
    virtual const CUTF8String& GetDirPath() const { return CUTF8String::GetEmpty(); }
    virtual const CUTF8String& GetFilePath() const { return CUTF8String::GetEmpty(); }
    VERSECOMPILER_API int32_t GetNumSnippets() const;

    void SetName(const CUTF8StringView& NewName) { _Name = NewName; }
    void SetVersePath(const CUTF8StringView& VersePath) { _Settings._VersePath = VersePath; }
    void SetVerseScope(const EVerseScope VerseScope) { _Settings._VerseScope = VerseScope; }
    void SetVerseVersion(const TOptional<uint32_t> VerseVersion) { _Settings._VerseVersion = VerseVersion; }
    void SetRole(const EPackageRole Role) { _Settings._Role = Role; }
    void SetTreatDefinitionsAsImplicit(bool bTreatAsImplicit) { _Settings._bTreatModulesAsImplicit = bTreatAsImplicit; }
    VERSECOMPILER_API void SetDependencyPackages(TArray<CUTF8String>&& PackageNames);
    VERSECOMPILER_API void AddDependencyPackage(const CUTF8StringView& PackageName);
    VERSECOMPILER_API void TruncateVniDestDir(); // Strip parent path from VniDestDir

    VERSECOMPILER_API bool RemoveSnippet(const uLang::TSRef<ISourceSnippet>& Snippet);

private:

    /// Name of this package
    CUTF8String _Name;

protected:

    /// Settings, e.g. parsed from .vpackage file
    SSettings _Settings;

};

/**
 * A project, holding packages and other information
 **/
class CSourceProject : public CSharedMix
{
public:

    /// Entry for a package contained in this project
    struct SPackage
    {
        TSPtr<CSourcePackage>   _Package;
        bool                    _bReadonly = false;
    };

    /// The packages contained in this project
    TArray<SPackage> _Packages;

    /// Construct from name
    CSourceProject(const CUTF8StringView& Name) : _Name(Name) {}
    /// Construct from other project by making a shallow copy
    VERSECOMPILER_API CSourceProject(const CSourceProject& Other);
    virtual ~CSourceProject() = default;

    const CUTF8String& GetName() const { return _Name; }
    virtual const CUTF8String& GetFilePath() const { return CUTF8String::GetEmpty(); }
    VERSECOMPILER_API int32_t GetNumSnippets() const;

    VERSECOMPILER_API const SPackage* FindPackage(const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath) const;
    VERSECOMPILER_API const SPackage& FindOrAddPackage(const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath);
    VERSECOMPILER_API void AddSnippet(const uLang::TSRef<ISourceSnippet>& Snippet, const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath);
    VERSECOMPILER_API bool RemoveSnippet(const uLang::TSRef<ISourceSnippet>& Snippet);

    VERSECOMPILER_API void TruncateVniDestDirs(); // Strip parent path from all VniDestDirs

private:

    /// Name of this project
    CUTF8String _Name;
};

/* Implementations
 *****************************************************************************/

template<typename FunctionType>
bool CSourceModule::VisitAll(FunctionType&& Lambda) const
{
    if (!Lambda(*this))
    {
        return false;
    }

    for (const TSRef<CSourceModule>& Submodule : _Submodules)
    {
        if (!Submodule->VisitAll(Lambda))
        {
            return false;
        }
    }

    return true;
}
template<typename FunctionType>
bool CSourceModule::VisitAll(FunctionType&& Lambda) // non-const version
{
    if (!Lambda(*this))
    {
        return false;
    }

    for (const TSRef<CSourceModule>& Submodule : _Submodules)
    {
        if (!Submodule->VisitAll(Lambda))
        {
            return false;
        }
    }

    return true;
}

}
