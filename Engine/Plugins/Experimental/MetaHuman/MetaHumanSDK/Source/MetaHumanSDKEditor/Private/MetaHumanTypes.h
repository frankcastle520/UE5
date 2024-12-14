// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetaHumanProjectUtilities.h"

#include "Misc/PackagePath.h"
#include "Misc/Paths.h"

// Common data types used in various parts of the MetaHumanProjectUtilities module

namespace UE::MetaHuman
{
struct FMetaHumanAssetImportDescription;

struct FMetaHumanAssetVersion
{
	int32 Major;
	int32 Minor;

	// Comparison operators
	friend bool operator <(const FMetaHumanAssetVersion& Left, const FMetaHumanAssetVersion& Right)
	{
		return Left.Major < Right.Major || (Left.Major == Right.Major && Left.Minor < Right.Minor);
	}

	friend bool operator>(const FMetaHumanAssetVersion& Left, const FMetaHumanAssetVersion& Right) { return Right < Left; }
	friend bool operator<=(const FMetaHumanAssetVersion& Left, const FMetaHumanAssetVersion& Right) { return !(Left > Right); }
	friend bool operator>=(const FMetaHumanAssetVersion& Left, const FMetaHumanAssetVersion& Right) { return !(Left < Right); }

	friend bool operator ==(const FMetaHumanAssetVersion& Left, const FMetaHumanAssetVersion& Right)
	{
		return Right.Major == Left.Major && Right.Minor == Left.Minor;
	}

	friend bool operator!=(const FMetaHumanAssetVersion& Left, const FMetaHumanAssetVersion& Right) { return !(Left == Right); }

	static FMetaHumanAssetVersion FromString(const FString& String)
	{
		FString MajorPart;
		FString MinorPart;
		String.Split(TEXT("."), &MajorPart, &MinorPart);
		return FMetaHumanAssetVersion{FCString::Atoi(*MajorPart), FCString::Atoi(*MinorPart)};
	}

	FString AsString() const
	{
		return FString::Format(TEXT("{0}.{1}"), {Major, Minor});
	}
};

// Reason for performing an update (currently only version difference, but this could be extended).
struct FAssetUpdateReason
{
	FMetaHumanAssetVersion OldVersion;
	FMetaHumanAssetVersion NewVersion;

	// Whether the update is a breaking change (change in major version number)
	bool IsBreakingChange() const
	{
		return NewVersion.Major != OldVersion.Major;
	}
};

// List of relative asset paths to be Added, Replaced etc. as part of the current import action
struct FAssetOperationPaths
{
	TArray<FString> Add;
	TArray<FString> Replace;
	TArray<FString> Skip;
	TArray<FString> Update;
	TArray<FAssetUpdateReason> UpdateReasons;
};

// Helper structure to simplify management of file and asset paths. All paths are absolute and explicitly either
// a file path or an asset path.
struct FImportPaths
{
	inline static const FString MetaHumansFolderName = TEXT("MetaHumans");
	inline static const FString CommonFolderName = TEXT("Common");

	explicit FImportPaths(const FString& InSourceCommonFilePath, const FString& InSourceCharacterFilePath, const FString& InDestinationCommonAssetPath, const FString& InDestinationCharacterAssetPath);

	static FString FilenameToAssetName(const FString& Filename)
	{
		return FString::Format(TEXT("{0}.{0}"), {FPaths::GetBaseFilename(Filename)});
	}

	static FString AssetNameToFilename(const FString& AssetName)
	{
		return FString::Format(TEXT("{0}{1}"), {AssetName, LexToString(EPackageExtension::Asset)});
	}

	FString CharacterNameToBlueprintAssetPath(const FString& CharacterName) const
	{
		return DestinationCharacterAssetPath / FString::Format(TEXT("BP_{0}.BP_{0}"), {CharacterName});
	}

	/** Given a relative path from the manifest, calculate the full path to the corresponding source file. */
	FString GetSourceFile(const FString& RelativeFilePath) const
	{
		return FPaths::Combine(SourceRootFilePath, RelativeFilePath);
	}

	/** Given a relative path from the manifest, calculate the full path to the corresponding destination file. */
	FString GetDestinationFile(const FString& RelativeFilePath) const
	{
		FString RootPath;
		FString ChildPath;
		RelativeFilePath.Split(TEXT("/"), &RootPath, &ChildPath);
		const FString DestinationRoot = RootPath == CommonFolderName ? DestinationCommonFilePath : DestinationCharacterFilePath;
		return DestinationRoot / ChildPath;
	}

	/** Given a relative path from the manifest, calculate the asset path to the corresponding destination asset. */
	FString GetDestinationAsset(const FString& RelativeFilePath) const
	{
		FString RootPath;
		FString ChildPath;
		RelativeFilePath.Split(TEXT("/"), &RootPath, &ChildPath);
		const FString DestinationRoot = RootPath == CommonFolderName ? DestinationCommonAssetPath : DestinationCharacterAssetPath;
		return DestinationRoot / FPaths::GetPath(ChildPath) / FilenameToAssetName(ChildPath);
	}

	/** Given a relative path from the manifest, calculate the asset path to the corresponding destination package. */
	FString GetDestinationPackage(const FString& RelativeFilePath) const
	{
		FString RootPath;
		FString ChildPath;
		RelativeFilePath.Split(TEXT("/"), &RootPath, &ChildPath);
		const FString DestinationRoot = RootPath == CommonFolderName ? DestinationCommonAssetPath : DestinationCharacterAssetPath;
		return DestinationRoot / ChildPath;
	}

	FString SourceRootFilePath;
	FString SourceCharacterFilePath;
	FString SourceCommonFilePath;

	FString DestinationCharacterFilePath;
	FString DestinationCommonFilePath;

	FString DestinationCharacterAssetPath;
	FString DestinationCommonAssetPath;
};


// Class that handles the layout on-disk of a MetaHuman being used as the source of an Import operation
// Gives us a single place to handle simple path operations, filenames etc.
class FSourceMetaHuman
{
public:
	FSourceMetaHuman(const FString& InCharacterPath, const FString& InCommonPath, const FString& InName)
		: CharacterPath(FPaths::ConvertRelativePathToFull(InCharacterPath))
		, CommonPath(FPaths::ConvertRelativePathToFull(InCommonPath))
		, Name(InName)
	{
		const FString VersionFilePath = CharacterPath / TEXT("VersionInfo.txt");
		Version = FMetaHumanVersion::ReadFromFile(VersionFilePath);
	}

	const FString& GetName() const
	{
		return Name;
	}

	const FMetaHumanVersion& GetVersion() const
	{
		return Version;
	}

	EMetaHumanQualityLevel GetQualityLevel() const
	{
		if (CharacterPath.Contains(TEXT("Tier0")))
		{
			// For UEFN Tier0 is High, for UE Tier0 is cinematic
			if (!CharacterPath.Contains(TEXT("asset_uefn")))
			{
				return EMetaHumanQualityLevel::Cinematic;
			}
			return EMetaHumanQualityLevel::High;
		}
		if (CharacterPath.Contains(TEXT("Tier1")))
		{
			// Tier 1 only exists for UE
			return EMetaHumanQualityLevel::High;
		}
		if (CharacterPath.Contains(TEXT("Tier2")))
		{
			return EMetaHumanQualityLevel::Medium;
		}
		else
		{
			return EMetaHumanQualityLevel::Low;
		}
	}

private:
	FString CharacterPath;
	FString CommonPath;
	FString Name;
	FMetaHumanVersion Version;
};
}
