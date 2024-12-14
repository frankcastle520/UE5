// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "HAL/Platform.h"
#include "UObject/CookEnums.h"
#include "UObject/NameTypes.h"
#endif

#if WITH_EDITOR

enum class EDataValidationResult : uint8;
class FDataValidationContext;
class ITargetPlatform;
class UPackage;

namespace UE::Cook { class IMPCollector; }

namespace UE::Cook
{

/**
 * List the keywords that should be enum values of the EInstigator enum
 * The list is declared as a macro instead of ordinary C keywords in the enum declaration
 * so that we can reduce duplication in the functions that specify the string name and other properties for each
 * enum value; see the uses in ICookInfo.cpp.
 */
#define EINSTIGATOR_VALUES(CallbackMacro) \
	/** CallbackMacro(CPPToken Name, bool bAllowUnparameterized) */ \
	CallbackMacro(InvalidCategory, true) \
	CallbackMacro(NotYetRequested, true) \
	CallbackMacro(Unspecified, false) \
	CallbackMacro(StartupPackage, true) \
	CallbackMacro(AlwaysCookMap, true) \
	CallbackMacro(IniMapSection, false) \
	CallbackMacro(IniAllMaps, true) \
	CallbackMacro(CommandLinePackage, true) \
	CallbackMacro(CommandLineDirectory, true) \
	CallbackMacro(DirectoryToAlwaysCook, false) \
	CallbackMacro(FullDepotSearch, true) \
	CallbackMacro(GameDefaultObject, false) \
	CallbackMacro(InputSettingsIni, true) \
	CallbackMacro(StartupSoftObjectPath, true) \
	CallbackMacro(PackagingSettingsMapToCook, true) \
	CallbackMacro(CookModificationDelegate, true) \
	CallbackMacro(ModifyCookDelegate, true) \
	CallbackMacro(AssetManagerModifyCook, true) \
	CallbackMacro(AssetManagerModifyDLCCook, true) \
	CallbackMacro(TargetPlatformExtraPackagesToCook, true) \
	CallbackMacro(ConsoleCommand, true) \
	CallbackMacro(CookOnTheFly, true) \
	CallbackMacro(IterativeCook, true) \
	CallbackMacro(PreviousAssetRegistry, true) \
	CallbackMacro(RequestPackageFunction, true) \
	CallbackMacro(Dependency, false) \
	CallbackMacro(HardDependency, false) \
	CallbackMacro(HardEditorOnlyDependency, false) \
	CallbackMacro(SoftDependency, false) \
	CallbackMacro(Unsolicited, false) \
	CallbackMacro(EditorOnlyLoad, false) \
	CallbackMacro(SaveTimeHardDependency, false) \
	CallbackMacro(SaveTimeSoftDependency, false) \
	CallbackMacro(ForceExplorableSaveTimeSoftDependency, false) \
	CallbackMacro(GeneratedPackage, false) \


/** The different ways a package can be discovered by the cooker. */
enum class EInstigator : uint8
{
#define EINSTIGATOR_VALUE_CALLBACK(Name, bAllowUnparameterized) Name,
	EINSTIGATOR_VALUES(EINSTIGATOR_VALUE_CALLBACK)
#undef EINSTIGATOR_VALUE_CALLBACK
	Count,
};
COREUOBJECT_API const TCHAR* LexToString(EInstigator Value);

/** Category and referencer for how a package was discovered by the cooker. */
struct FInstigator
{
	FName Referencer;
	EInstigator Category;

	FInstigator() : Category(EInstigator::InvalidCategory)
	{
	}
	FInstigator(EInstigator InCategory, FName InReferencer = NAME_None) : Referencer(InReferencer), Category(InCategory)
	{
	}

	COREUOBJECT_API FString ToString() const;
};

/** Engine interface for information provided by UCookOnTheFlyServer in cook callbacks. */
class ICookInfo
{
public:
	/**
	 * Return the instigator that first caused the package to be requested by the cook.
	 * Returns category EInstigator::NotYetRequested if package is not yet known to the cook.
	 */
	virtual FInstigator GetInstigator(FName PackageName) = 0;
	/**
	 * Return the chain of instigators that caused the package to be requested by the cook.
	 * First element is the direct instigator of the package, last is the root instigator that started the chain.
	 */
	virtual TArray<FInstigator> GetInstigatorChain(FName PackageName) = 0;

	/** The type (e.g. CookByTheBook) of the running cook. This function will not return ECookType::Unknown. */
	virtual UE::Cook::ECookType GetCookType() = 0;
	/** Whether dlc is being cooked (e.g. via "-dlcname=<PluginName>"). This function will not return ECookingDLC::Unknown. */
	virtual UE::Cook::ECookingDLC GetCookingDLC() = 0;
	/** The role the current process plays in its MPCook session, or EProcessType::SingleProcess if it is running standalone. */
	virtual UE::Cook::EProcessType GetProcessType() = 0;
	/**
	 * Returns true if the cooker is cooking after a previous cook session and is cooking only the changed files.
	 * Returns false if the cooker is doing a recook of all packages discovered in the session.
	 * Returns false if not yet initialized, but it will be initialized whenever a session is in progress (GetSessionPlatforms
	 * is non-empty).
	 * When IsIterative is true, systems that write artifacts to the cook output should load/update/resave their
	 * artifacts.
	 */
	virtual bool IsIterative() = 0;

	/**
	 * Returns the list of platforms that will be/are being/have been cooked for the current cook session. Returns
	 * empty array when outside of a cooksession, including in the case that GetCookType() == ECookType::OnTheFly and
	 * no platforms have been requested yet. During cook by the book, this list will not change throughout the cook,
	 * during CookOnTheFly it can be added to or removed from when platforms are requested or go idle and are dropped.
	 */
	virtual TArray<const ITargetPlatform*> GetSessionPlatforms() = 0;
	/**
	 * Returns the output folder being used by the cooker for the given platform in the given session.
	 * Returns empty string if not in a session or the given platform is not in GetSessionPlatforms().
	 * Returns the path to the root folder of the output, so e.g. GetCookOutputFolder()/<ProjectName>/Metadata
	 * is the path to the output metadata for the cook.
	 * Returns the path in FPaths::MakeStandardFilename format ("../../../<ProjectName>/Saved/Cooked/<PlatformName>")
	 */
	virtual FString GetCookOutputFolder(const ITargetPlatform* TargetPlatform) = 0;

	/**
	 * MPCook: register in the current process a collector that replicates system-specific and package-specific
	 * information between CookWorkers and the CookDirector. Registration will be skipped if the current cook is
	 * singleprocess, or if the provided ProcessType does not match the current processtype. If registration is
	 * skipped, the Collector will be referenced but then immediately released, which will delete the Collector if
	 * the caller does not have their own TRefCountPtr to it.
	 * 
	 * @param ProcessType: The given collector will only be registered on the given process types, allowing you
	 *                     to register a different class on Workers than on the Director if desired. 
	 */
	virtual void RegisterCollector(IMPCollector* Collector,
		UE::Cook::EProcessType ProcessType=UE::Cook::EProcessType::AllMPCook) = 0;
	/**
	 * MPCook: Unregister in the current process a collector that was registered via RegisterCollector. Silently
	 * returns if the collector is not registered. References to the Collector will be released, which will
	 * delete the Collector if he caller does not have their own TRefCountPtr to it.
	 */
	virtual void UnregisterCollector(IMPCollector* Collector) = 0;


	/**
	 * Gets the current cultures that are being cooked.
	 */
	virtual void GetCulturesToCook(TArray<FString>& OutCulturesToCook) const = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FCookInfoEvent, ICookInfo&);
DECLARE_DELEGATE_RetVal_TwoParams(EDataValidationResult, FValidateSourcePackage, UPackage* /*Package*/, FDataValidationContext& /*ValidationContext*/);

/** UE::Cook::FDelegates: callbacks for cook events. */
struct FDelegates
{
public:
	UE_DEPRECATED(5.5, "Use CookStarted, possibly restricting to the case CookInfo.GetCookType() == ECookType::ByTheBook.")
	static COREUOBJECT_API FCookInfoEvent CookByTheBookStarted;
	UE_DEPRECATED(5.5, "Use CookFinished, possibly restricting to the case CookInfo.GetCookType() == ECookType::ByTheBook.")
	static COREUOBJECT_API FCookInfoEvent CookByTheBookFinished;
	static COREUOBJECT_API FCookInfoEvent CookStarted;
	static COREUOBJECT_API FCookInfoEvent CookFinished;
	static COREUOBJECT_API FValidateSourcePackage ValidateSourcePackage;
};


/**
 * Return the relative path under the cook output MetaData folder to the ReferencedSet file.
 * The ReferencedSet file is a text file list of package names, one per line, that were referenced from the cook
 * session.
 * It does not include previously cooked files in an incremental cook that are no longer referenced.
 * It does not include packages from the base game for a DLC cook.
 */
COREUOBJECT_API const TCHAR* GetReferencedSetFilename();

} // namespace UE::Cook

/**
 * A scope around loads when cooking that indicates whether the loaded package is needed in game or not.
 * The default is Unexpected. Declare an FCookLoadScope to set the value.
 * Packages loads that are declared via AssetRegistry dependencies (imports or softobjectpaths in
 * the editor package) change the default to UsedInGame or EditorOnly dependending on whether they were saved
 * in an EditorOnly property or scope.
 */
enum class ECookLoadType : uint8
{
	Unexpected,
	EditorOnly,
	UsedInGame,
};

/** Set the ECookLoadType value in the current scope.*/
struct FCookLoadScope
{
	COREUOBJECT_API explicit FCookLoadScope(ECookLoadType ScopeType);
	COREUOBJECT_API ~FCookLoadScope();

	COREUOBJECT_API static ECookLoadType GetCurrentValue();

private:
	ECookLoadType PreviousScope;
};

#endif // WITH_EDITOR