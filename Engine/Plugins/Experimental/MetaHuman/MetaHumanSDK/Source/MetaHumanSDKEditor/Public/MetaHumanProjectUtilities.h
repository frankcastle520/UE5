// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::MetaHuman
{
struct FQuixelAccountMetaHumanEntry
{
	FString Name; // Character name
	FString Id; // Quixel ID
	bool bIsLegacy; // Does this MetaHuman require an Upgrade before it can be used
	FString Version; // The version of MHC used to create this character
};

struct FMetaHumanAssetImportDescription
{
	inline static const FString DefaultDestinationPath = TEXT("/Game/MetaHumans");

	FString CharacterPath; // The file path to the source unique assets for this import operation
	FString CommonPath; // The file path to the source common assets for this import operation
	FString CharacterName; // The name of the MetaHuman to import (expected to match the final part of CharacterPath)
	FString QuixelId; // The ID of the character being imported
	bool bIsBatchImport; // If this is part of a batch import
	FString SourcePath = DefaultDestinationPath; // The asset path that the exporter has written the assets out to
	FString DestinationPath = DefaultDestinationPath; // The asset path to install the MetaHuman to in the project
	TArray<FQuixelAccountMetaHumanEntry> AccountMetaHumans; // All the MetaHumans that are included in the user's account. Used to show which MetaHumans can be upgraded
	bool bForceUpdate = false; // Ignore asset version metadata and update all assets
	bool bWarnOnQualityChange = false; // Warn if the user is importing a MetaHuman at a different quality level to the existing MetaHuman in the scene.
};

class IMetaHumanProjectUtilitiesAutomationHandler
{
public:
	virtual ~IMetaHumanProjectUtilitiesAutomationHandler()
	{
	};
	virtual bool ShouldContinueWithBreakingMetaHumans(const TArray<FString>&, const TArray<FString>& UpdatedFiles) = 0;
};

class IMetaHumanBulkImportHandler
{
public:
	virtual ~IMetaHumanBulkImportHandler()
	{
	};

	// MetaHumanIds is a list of the Quixel IDs of the MetaHumans to
	// be imported. This is an asynchronous operation. This function returns
	// immediately and the import operation that called it will immediately terminate.
	virtual void DoBulkImport(const TArray<FString>& MetaHumanIds) = 0;
};

enum class EMetaHumanQualityLevel : uint8
{
	Low,
	Medium,
	High,
	Cinematic
};

// Representation of a MetaHuman Version. This is a simple semantic-versioning style version number that is stored
// in a Json file at a specific location in the directory structure that MetaHumans use.
struct FMetaHumanVersion
{
	// Currently default initialisation == 0.0.0 which is not a valid version. This needs a bit more thought
	// TODO: refactor to use TOptional and avoid needing to represent invalid versions.
	FMetaHumanVersion() = default;

	explicit FMetaHumanVersion(const FString& VersionString);

	explicit FMetaHumanVersion(const int Major, const int Minor, const int Revision)
		: Major(Major)
		, Minor(Minor)
		, Revision(Revision)
	{
	}

	// Comparison operators
	friend bool operator <(const FMetaHumanVersion& Left, const FMetaHumanVersion& Right)
	{
		return Left.Major < Right.Major || (Left.Major == Right.Major && (Left.Minor < Right.Minor || (Left.Minor == Right.Minor && Left.Revision < Right.Revision)));
	}

	friend bool operator>(const FMetaHumanVersion& Left, const FMetaHumanVersion& Right) { return Right < Left; }
	friend bool operator<=(const FMetaHumanVersion& Left, const FMetaHumanVersion& Right) { return !(Left > Right); }
	friend bool operator>=(const FMetaHumanVersion& Left, const FMetaHumanVersion& Right) { return !(Left < Right); }

	friend bool operator ==(const FMetaHumanVersion& Left, const FMetaHumanVersion& Right)
	{
		return Right.Major == Left.Major && Right.Minor == Left.Minor && Right.Revision == Left.Revision;
	}

	friend bool operator!=(const FMetaHumanVersion& Left, const FMetaHumanVersion& Right) { return !(Left == Right); }

	// Hash function
	friend uint32 GetTypeHash(FMetaHumanVersion Version)
	{
		return (Version.Major << 20) + (Version.Minor << 10) + Version.Revision;
	}

	// Currently MetaHumans are compatible so long as they are from the same major version. In the future, compatibility
	// between versions may be more complex or require inspecting particular assets.
	bool IsCompatible(const FMetaHumanVersion& Other) const
	{
		return Major && Major == Other.Major;
	}

	FString AsString() const
	{
		return FString::Format(TEXT("{0}.{1}.{2}"), {Major, Minor, Revision});
	}

	static FMetaHumanVersion ReadFromFile(const FString& VersionFilePath);

	int32 Major = 0;
	int32 Minor = 0;
	int32 Revision = 0;
};

// Class that handles the layout and filenames of a MetaHuman that has been added to a project.
class METAHUMANSDKEDITOR_API FInstalledMetaHuman
{
public:
	FInstalledMetaHuman(const FString& InName, const FString& InCharacterFilePath, const FString& InCommonFilePath);

	const FString& GetName() const
	{
		return Name;
	}

	FString GetRootAsset() const;

	FMetaHumanVersion GetVersion() const;

	EMetaHumanQualityLevel GetQualityLevel() const;

	// Finds MetaHumans in the destination of a given import
	static TArray<FInstalledMetaHuman> GetInstalledMetaHumans(const FString& CharactersFolder, const FString& CommonAssetsFolder);

private:
	FString Name;
	FString CharacterFilePath;
	FString CommonFilePath;
	FString CharacterAssetPath;
	FString CommonAssetPath;
};

class FMetaHumanProjectUtilities
{
public:
	// Disable UI and enable automation of user input for headless testing
	static void METAHUMANSDKEDITOR_API EnableAutomation(IMetaHumanProjectUtilitiesAutomationHandler* Handler);
	// Disable UI and enable automation of user input for headless testing
	static void METAHUMANSDKEDITOR_API SetBulkImportHandler(IMetaHumanBulkImportHandler* Handler);
	// Main entry-point used by Quixel Bridge
	static void METAHUMANSDKEDITOR_API ImportAsset(const FMetaHumanAssetImportDescription& AssetImportDescription);
	// Provide the Url for the versioning service to use
	static void METAHUMANSDKEDITOR_API OverrideVersionServiceUrl(const FString& BaseUrl);
	// Returns a list of all MetaHumans in the project
	static TArray<FInstalledMetaHuman> METAHUMANSDKEDITOR_API GetInstalledMetaHumans();
};
}
