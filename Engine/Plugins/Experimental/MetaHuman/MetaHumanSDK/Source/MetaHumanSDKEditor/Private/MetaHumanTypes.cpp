// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanTypes.h"

#include "EditorAssetLibrary.h"
#include "MetaHumanProjectUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"

namespace UE::MetaHuman
{
FImportPaths::FImportPaths(const FString& InSourceCommonFilePath, const FString& InSourceCharacterFilePath, const FString& InDestinationCommonAssetPath, const FString& InDestinationCharacterAssetPath)
{
	// The locations we are importing files from
	SourceCommonFilePath = FPaths::ConvertRelativePathToFull(InSourceCommonFilePath);
	SourceCharacterFilePath = FPaths::ConvertRelativePathToFull(InSourceCharacterFilePath);

	// The folder to search for other MetaHumans
	SourceRootFilePath = FPaths::GetPath(SourceCharacterFilePath);

	// Destination asset paths in the project for the MetaHuman
	DestinationCommonAssetPath = InDestinationCommonAssetPath;
	DestinationCharacterAssetPath = InDestinationCharacterAssetPath;

	// Corresponding file paths on disk for those assets
	DestinationCommonFilePath = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(DestinationCommonAssetPath));
	DestinationCharacterFilePath = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(DestinationCharacterAssetPath));
}

FMetaHumanVersion FMetaHumanVersion::ReadFromFile(const FString& VersionFilePath)
{
	// This is the old behaviour. We can probably do better than this.
	if (!IFileManager::Get().FileExists(*VersionFilePath))
	{
		return FMetaHumanVersion(TEXT("0.5.1"));
	}
	const FString VersionTag = TEXT("MetaHumanVersion");
	FString VersionInfoString;
	if (FFileHelper::LoadFileToString(VersionInfoString, *VersionFilePath))
	{
		TSharedPtr<FJsonObject> VersionInfoObject;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(VersionInfoString), VersionInfoObject))
		{
			return FMetaHumanVersion(VersionInfoObject->GetStringField(VersionTag));
		}
	}
	// Invalid file
	return {};
}

FInstalledMetaHuman::FInstalledMetaHuman(const FString& InName, const FString& InCharacterFilePath, const FString& InCommonFilePath)
	: Name(InName)
	, CharacterFilePath(InCharacterFilePath)
	, CommonFilePath(InCommonFilePath)
	, CharacterAssetPath(FPackageName::FilenameToLongPackageName(InCharacterFilePath))
	, CommonAssetPath(FPackageName::FilenameToLongPackageName(InCommonFilePath))
{
}

FString FInstalledMetaHuman::GetRootAsset() const
{
	return CharacterAssetPath / FString::Format(TEXT("BP_{0}.BP_{0}"), {Name});
}

EMetaHumanQualityLevel FInstalledMetaHuman::GetQualityLevel() const
{
	static const FName MetaHumanAssetQualityLevelKey = TEXT("MHExportQuality");
	if (const UObject* Asset = LoadObject<UObject>(nullptr, *GetRootAsset()))
	{
		if (const TMap<FName, FString>* Metadata = UMetaData::GetMapForObject(Asset))
		{
			if (const FString* AssetQualityMetaData = Metadata->Find(MetaHumanAssetQualityLevelKey))
			{
				if (*AssetQualityMetaData == TEXT("Cinematic"))
				{
					return EMetaHumanQualityLevel::Cinematic;
				}
				if (*AssetQualityMetaData == TEXT("High"))
				{
					return EMetaHumanQualityLevel::High;
				}
				if (*AssetQualityMetaData == TEXT("Medium"))
				{
					return EMetaHumanQualityLevel::Medium;
				}
			}
		}
	}
	return EMetaHumanQualityLevel::Low;
}

TArray<FInstalledMetaHuman> FInstalledMetaHuman::GetInstalledMetaHumans(const FString& CharactersFolder, const FString& CommonAssetsFolder)
{
	TArray<FInstalledMetaHuman> FoundMetaHumans;
	const FString ProjectMetaHumanPath = CharactersFolder / TEXT("*");
	TArray<FString> DirectoryList;
	IFileManager::Get().FindFiles(DirectoryList, *ProjectMetaHumanPath, false, true);

	for (const FString& Directory : DirectoryList)
	{
		const FString CharacterName = FPaths::GetCleanFilename(Directory);
		FInstalledMetaHuman FoundMetaHuman(CharacterName, CharactersFolder / CharacterName, CommonAssetsFolder);
		if (UEditorAssetLibrary::DoesAssetExist(FPackageName::ObjectPathToPackageName(FoundMetaHuman.GetRootAsset())))
		{
			FoundMetaHumans.Emplace(std::move(FoundMetaHuman));
		}
	}
	return FoundMetaHumans;
}
}
