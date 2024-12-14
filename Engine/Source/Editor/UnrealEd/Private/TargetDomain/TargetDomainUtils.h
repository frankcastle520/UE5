// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/ContainersFwd.h"
#include "DerivedDataBuildDefinition.h"
#include "IO/IoHash.h"
#include "Misc/Optional.h"
#include "Serialization/PackageWriter.h"
#include "UObject/NameTypes.h"

class FCbObject;
class FCbObjectView;
class FCbWriter;
class FString;
class ITargetPlatform;
class UPackage;
struct FSavePackageResultStruct;
template <typename FuncType> class TUniqueFunction;

namespace UE::Cook { class FCookDependency; }
namespace UE::TargetDomain { class FCookDependencies; }
namespace UE::TargetDomain { struct FBuildDefinitionList; }
namespace UE::TargetDomain { struct FCookAttachments; }

bool LoadFromCompactBinary(FCbObjectView ObjectView, UE::TargetDomain::FCookDependencies& CookAttachments);
FCbWriter& operator<<(FCbWriter& Writer, const UE::TargetDomain::FCookDependencies& CookAttachments);
bool LoadFromCompactBinary(FCbObject&& Object, UE::TargetDomain::FBuildDefinitionList& Definitions);
FCbWriter& operator<<(FCbWriter& Writer, const UE::TargetDomain::FBuildDefinitionList& Definitions);

namespace UE::TargetDomain
{

/** Call during Startup to initialize global data used by TargetDomain functions. */
void CookInitialize();

/**
 * Information collected from a CookPackageSplitter Generated package after it is saved; this information is needed to collect
 * the CookDependencies for the generated package.
 */
struct FGeneratedPackageResultStruct
{
	FAssetPackageData AssetPackageData;
	TArray<FAssetDependency> PackageDependencies;
};

/**
 * Recording of the dependencies of a package discovered during cook, used in incremental cooks. All dependencies
 * except for those marked Runtime contribute to the packages TargetDomain Key. If HasKeyMatch returns false after
 * fetching this structure for a package at the beginning of cook, then the package is not iteratively skippable and
 * needs to be recooked, and this structure needs to be recalculated for the package.
 * 
 * Runtime fields on the structure are used to inform the cook of discovered softreferences that need to be added to
 * the cook when the package is cooked.
 */
class FCookDependencies
{
public:
	FCookDependencies();
	~FCookDependencies();
	FCookDependencies(const FCookDependencies&);
	FCookDependencies(FCookDependencies&&);
	FCookDependencies& operator=(const FCookDependencies&);
	FCookDependencies& operator=(FCookDependencies&&);
	// Build Dependencies
	const TArray<FName>& GetBuildPackageDependencies() const;
	const TArray<FString>& GetConfigDependencies() const;

	// Runtime Dependencies
	const TArray<FName>& GetRuntimePackageDependencies() const;
	// Script dependencies, needed for TryCalculateCurrentKey
	const TArray<FName>& GetScriptPackageDependencies() const;

	// Cook Dependencies
	const TArray<UE::Cook::FCookDependency>& GetCookDependencies() const;
	const TArray<UE::Cook::FCookDependency>& GetTransitiveBuildDependencies() const;

	// Data about the Key and Package that are not dependencies
	/**
	 * True if the structure has been calculated or fetched and accurately reports dependencies and
	 * key for the package. False if the stucture is default, has been reset, or was marked invalid.
	 */
	bool IsValid() const;
	FName GetPackageName() const;
	const FIoHash& GetStoredKey() const;
	const FIoHash& GetCurrentKey() const;
	bool HasKeyMatch(const FAssetPackageData* OverrideAssetPackageData);

	// Modifying the structure data
	bool TryCalculateCurrentKey(const FAssetPackageData* OverrideAssetPackageData, FString* OutErrorMessage = nullptr);
	void Reset();
	void Empty();

	// Construction functions
	/**
	 * Read dependencies for the given targetplatform of the given package out of global dependency trackers
	 * that have recorded its data during the package's load/other/save operations in the current cook session.
	 */
	static FCookDependencies Collect(UPackage* Package, const ITargetPlatform* TargetPlatform,
		FSavePackageResultStruct* SaveResult, const FGeneratedPackageResultStruct* GeneratedResult,
		TArray<FName>&& RuntimeDependencies, FString* OutErrorMessage = nullptr);
	static FCookDependencies CollectSettingsObject(const UObject* Object, FString* OutErrorMessage);

	// Fetch function to load the dependencies from a PackageStore is not yet implemented independently for
	// this structure. Use FCookAttachments instead. 

private:
	TArray<FName> BuildPackageDependencies;
	TArray<FString> ConfigDependencies;
	TArray<FName> RuntimePackageDependencies;
	TArray<FName> ScriptPackageDependencies;
	TArray<UE::Cook::FCookDependency> CookDependencies;
	TArray<UE::Cook::FCookDependency> TransitiveBuildDependencies;
	TArray<FString> ClassDependencies;
	FName PackageName;
	FIoHash StoredKey;
	FIoHash CurrentKey;
	bool bValid = false;

	friend bool ::LoadFromCompactBinary(FCbObjectView ObjectView, FCookDependencies& CookAttachments);
	friend FCbWriter& ::operator<<(FCbWriter& Writer, const FCookDependencies& CookAttachments);
	friend FCookAttachments;
};

/**
 * Non-persistent cache of groups of cookdependencies. Dependencies to a CookDependencyGroup are not persistently
 * recorded into the oplog, instead we make a copy of all of their dependencies and append those dependencies onto
 * the CookDependencies that are written for a package.
 *
 * Example: The cookdependencies used by the CDO of a settings object that itself is configured by config values.
 *          The settings object's class's schema and the list of config settings are included in the cookdependencies.
 */
class FCookDependencyGroups
{
public:
	struct FRecordedDependencies
	{
		FCookDependencies Dependencies;
		FString ErrorMessage;
		bool bInitialized = false;
	};

	static FCookDependencyGroups& Get();
	FRecordedDependencies& FindOrCreate(UPTRINT Key);

private:
	TMap<UPTRINT, FRecordedDependencies> Groups;
};

/** Wrapper around TArray<FBuildDefinition>, used to provide custom functions for compactbinary, collection, and fetch */
struct FBuildDefinitionList
{
public:
	TArray<UE::DerivedData::FBuildDefinition> Definitions;

	void Reset();
	void Empty();

	/** Collect DDC BuildDefinitions that were issued from the load/save of the given package and platform. */
	static FBuildDefinitionList Collect(UPackage* Package, const ITargetPlatform* TargetPlatform,
		FString* OutErrorMessage = nullptr);

private:
	friend bool ::LoadFromCompactBinary(FCbObject&& ObjectView, FBuildDefinitionList& Definitions);
	friend FCbWriter& ::operator<<(FCbWriter& Writer, const FBuildDefinitionList& Definitions);
};

/**
 * All of the attachments that we want to read during RequestCluster reference traversal. We read them all at once
 * to batch up the fetch of the attachments from the PackageStore.
 */
struct FCookAttachments
{
	FCookDependencies Dependencies;
	FBuildDefinitionList BuildDefinitions;

	void Reset();
	void Empty();

	static void Fetch(TArrayView<FName> PackageNames, const ITargetPlatform* TargetPlatform,
		ICookedPackageWriter* PackageWriter,
		TUniqueFunction<void(FName PackageName, FCookAttachments&& Result)>&& Callback);
};

bool TryCollectAndStoreCookDependencies(UPackage* Package, const ITargetPlatform* TargetPlatform,
	FSavePackageResultStruct* SaveResult, const FGeneratedPackageResultStruct* GeneratedResult,
	TArray<FName>&& RuntimeDependencies, IPackageWriter::FCommitAttachmentInfo& OutResult);
bool TryCollectAndStoreBuildDefinitionList(UPackage* Package, const ITargetPlatform* TargetPlatform,
	IPackageWriter::FCommitAttachmentInfo& OutResult);

template <typename ArrayType>
void CollectAndStoreCookAttachments(UPackage* Package,
	const ITargetPlatform* TargetPlatform, FSavePackageResultStruct* SaveResult,
	const FGeneratedPackageResultStruct* GeneratedResult, TArray<FName>&& RuntimeDependencies,
	ArrayType& Output)
{
	IPackageWriter::FCommitAttachmentInfo Result;
	if (TryCollectAndStoreCookDependencies(Package, TargetPlatform, SaveResult, GeneratedResult,
		MoveTemp(RuntimeDependencies), Result))
	{
		Output.Add(MoveTemp(Result));
	}
	if (TryCollectAndStoreBuildDefinitionList(Package, TargetPlatform, Result))
	{
		Output.Add(MoveTemp(Result));
	}
}

/** Return whether iterative cook is enabled for the given packagename, based on used-class allowlist/blocklist. */
bool IsIterativeEnabled(FName PackageName, bool bAllowAllClasses,
	const FAssetPackageData* OverrideAssetPackageData = nullptr);

/** Store extra information derived during save and used by the cooker for the given EditorDomain package. */
void CommitEditorDomainCookAttachments(FName PackageName, TArrayView<IPackageWriter::FCommitAttachmentInfo> Attachments);


////////////////////////////////
// Inline Implementations
////////////////////////////////

inline const TArray<FName>& FCookDependencies::GetBuildPackageDependencies() const
{
	return BuildPackageDependencies;
}

inline const TArray<FString>& FCookDependencies::GetConfigDependencies() const
{
	return ConfigDependencies;
}

inline const TArray<FName>& FCookDependencies::GetRuntimePackageDependencies() const
{
	return RuntimePackageDependencies;
}

inline const TArray<FName>& FCookDependencies::GetScriptPackageDependencies() const
{
	return ScriptPackageDependencies;
}

inline const TArray<UE::Cook::FCookDependency>& FCookDependencies::GetCookDependencies() const
{
	return CookDependencies;
}

inline const TArray<UE::Cook::FCookDependency>& FCookDependencies::GetTransitiveBuildDependencies() const
{
	return TransitiveBuildDependencies;
}

inline FName FCookDependencies::GetPackageName() const
{
	return PackageName;
}

inline const FIoHash& FCookDependencies::GetStoredKey() const
{
	return StoredKey;
}

inline const FIoHash& FCookDependencies::GetCurrentKey() const
{
	return CurrentKey;
}

} // namespace UE::TargetDomain
