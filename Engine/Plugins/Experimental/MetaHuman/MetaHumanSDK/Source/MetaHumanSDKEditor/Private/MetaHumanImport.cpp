// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanImport.h"
#include "MetaHumanImportUI.h"
#include "MetaHumanProjectUtilities.h"
#include "MetaHumanSDKSettings.h"
#include "MetaHumanTypes.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Internationalization/Text.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "PackageTools.h"
#include "UObject/CoreRedirects.h"
#include "UObject/Linker.h"
#include "UObject/Object.h"
#include "UObject/SavePackage.h"
#include "UObject/MetaData.h"

#define LOCTEXT_NAMESPACE "MetaHumanImport"
DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanImport, Log, All)

namespace UE::MetaHuman
{
namespace Private
{
	// Helper functions *************************************

	// Calculate which assets to add to the project, which to replace, which to update and which to skip
	FAssetOperationPaths DetermineAssetOperations(const TMap<FString, FMetaHumanAssetVersion>& SourceVersionInfo, const FImportPaths& ImportPaths, bool ForceUpdate)
	{
		FScopedSlowTask AssetScanProgress(SourceVersionInfo.Num(), FText::FromString(TEXT("Scanning existing assets")), true);
		AssetScanProgress.MakeDialog();
		static const FName MetaHumanAssetVersionKey = TEXT("MHAssetVersion");
		FAssetOperationPaths AssetOperations;

		for (const TTuple<FString, FMetaHumanAssetVersion>& SourceAssetInfo : SourceVersionInfo)
		{
			AssetScanProgress.EnterProgressFrame();
			// If there is no existing asset, we add it
			if (!IFileManager::Get().FileExists(*ImportPaths.GetDestinationFile(SourceAssetInfo.Key)))
			{
				AssetOperations.Add.Add(SourceAssetInfo.Key);
				continue;
			}

			// If we are doing a force update or the asset is unique to the MetaHuman we always replace it
			if (ForceUpdate || !SourceAssetInfo.Key.StartsWith(FImportPaths::CommonFolderName + TEXT("/")))
			{
				AssetOperations.Replace.Add(SourceAssetInfo.Key);
				continue;
			}

			// If the asset is part of the common assets, we only update it if the source asset has a greater version number
			// If the file has no metadata then we assume it is old and will update it.
			FString TargetVersion = TEXT("0.0");
			if (const UObject* Asset = LoadObject<UObject>(nullptr, *ImportPaths.GetDestinationAsset(SourceAssetInfo.Key)))
			{
				if (const TMap<FName, FString>* Metadata = UMetaData::GetMapForObject(Asset))
				{
					if (const FString* VersionMetaData = Metadata->Find(MetaHumanAssetVersionKey))
					{
						TargetVersion = *VersionMetaData;
					}
				}
			}

			const FMetaHumanAssetVersion OldVersion = FMetaHumanAssetVersion::FromString(TargetVersion);
			const FMetaHumanAssetVersion NewVersion = SourceAssetInfo.Value;
			if (NewVersion > OldVersion)
			{
				AssetOperations.Update.Add(SourceAssetInfo.Key);
				AssetOperations.UpdateReasons.Add({OldVersion, NewVersion});
			}
			else
			{
				AssetOperations.Skip.Add(SourceAssetInfo.Key);
			}
		}

		return AssetOperations;
	}


	// Check if the project contains any incompatible MetaHuman characters
	TSet<FString> CheckVersionCompatibility(const FSourceMetaHuman& SourceMetaHuman, const TArray<FInstalledMetaHuman>& InstalledMetaHumans)
	{
		TSet<FString> IncompatibleCharacters;
		const FMetaHumanVersion& SourceVersion = SourceMetaHuman.GetVersion();
		for (const FInstalledMetaHuman& InstalledMetaHuman : InstalledMetaHumans)
		{
			if (!SourceVersion.IsCompatible(InstalledMetaHuman.GetVersion()))
			{
				IncompatibleCharacters.Emplace(InstalledMetaHuman.GetName());
			}
		}
		return IncompatibleCharacters;
	}

	TMap<FString, FMetaHumanAssetVersion> ParseVersionInfo(const FString& AssetVersionFilePath)
	{
		FString VersionInfoString;
		FFileHelper::LoadFileToString(VersionInfoString, *AssetVersionFilePath);
		TMap<FString, FMetaHumanAssetVersion> VersionInfo;
		TSharedPtr<FJsonObject> AssetsVersionInfoObject;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(VersionInfoString), AssetsVersionInfoObject);
		TArray<TSharedPtr<FJsonValue>> AssetsVersionInfoArray = AssetsVersionInfoObject->GetArrayField(TEXT("assets"));

		for (const TSharedPtr<FJsonValue>& AssetVersionInfoObject : AssetsVersionInfoArray)
		{
			FString AssetPath = AssetVersionInfoObject->AsObject()->GetStringField(TEXT("path"));
			// Remove leading "MetaHumans/" as this can be configured to an arbitrary value by the users
			if (const FString DefaultRoot = FImportPaths::MetaHumansFolderName + TEXT("/"); AssetPath.StartsWith(DefaultRoot))
			{
				AssetPath = AssetPath.RightChop(DefaultRoot.Len());
			}
			FMetaHumanAssetVersion AssetVersion = FMetaHumanAssetVersion::FromString(AssetVersionInfoObject->AsObject()->GetStringField(TEXT("version")));
			VersionInfo.Add(AssetPath, AssetVersion);
		}

		return VersionInfo;
	}

	void CopyFiles(const FAssetOperationPaths& AssetOperations, const FImportPaths& ImportPaths)
	{
		TArray<UPackage*> PackagesToReload;
		TArray<UPackage*> BPsToReload;

		{
			int32 CommonFilesCount = AssetOperations.Add.Num() + AssetOperations.Replace.Num() + AssetOperations.Update.Num();
			FScopedSlowTask AssetLoadProgress(CommonFilesCount, FText::FromString(TEXT("Updating assets.")), true);
			AssetLoadProgress.MakeDialog();

			IAssetRegistry& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

			for (const FString& AssetToAdd : AssetOperations.Add)
			{
				AssetLoadProgress.EnterProgressFrame();
				IFileManager::Get().Copy(*ImportPaths.GetDestinationFile(AssetToAdd), *ImportPaths.GetSourceFile(AssetToAdd), true, true);
			}

			TArray<FString> UpdateOperations = AssetOperations.Replace;
			UpdateOperations.Append(AssetOperations.Update);
			for (const FString& AssetToUpdate : UpdateOperations)
			{
				AssetLoadProgress.EnterProgressFrame();
				if (AssetToUpdate.EndsWith(LexToString(EPackageExtension::Asset)))
				{
					const FSoftObjectPath AssetToReplace(ImportPaths.GetDestinationAsset(AssetToUpdate));
					FAssetData GameAssetData = AssetRegistry.GetAssetByObjectPath(AssetToReplace);
					// If the asset is not loaded we can just overwrite the file and do not need to worry about unloading
					// and reloading the package.
					if (GameAssetData.IsAssetLoaded())
					{
						UObject* ItemObject = GameAssetData.GetAsset();

						if (!ItemObject->GetPackage()->IsFullyLoaded())
						{
							FlushAsyncLoading();
							ItemObject->GetPackage()->FullyLoad();
						}

						// We are about to replace this object, so ignore any pending changes
						ItemObject->GetPackage()->ClearDirtyFlag();

						if (Cast<UBlueprint>(ItemObject) != nullptr)
						{
							BPsToReload.Add(ItemObject->GetPackage());
						}

						ResetLoaders(ItemObject->GetPackage());

						PackagesToReload.Add(ItemObject->GetPackage());
					}
				}
				IFileManager::Get().Copy(*ImportPaths.GetDestinationFile(AssetToUpdate), *ImportPaths.GetSourceFile(AssetToUpdate), true, true);
			}
		}

		FScopedSlowTask PackageReloadProgress(PackagesToReload.Num() + BPsToReload.Num(), FText::FromString(TEXT("Reloading packages.")), true);
		PackageReloadProgress.MakeDialog();

		PackageReloadProgress.EnterProgressFrame(PackagesToReload.Num());
		UPackageTools::ReloadPackages(PackagesToReload);

		for (const UPackage* Package : BPsToReload)
		{
			PackageReloadProgress.EnterProgressFrame();
			UObject* Obj = Package->FindAssetInPackage();
			if (UBlueprint* BPObject = Cast<UBlueprint>(Obj))
			{
				FKismetEditorUtilities::CompileBlueprint(BPObject, EBlueprintCompileOptions::SkipGarbageCollection);
				BPObject->PreEditChange(nullptr);
				BPObject->PostEditChange();
			}
		}
	}

	bool MHInLevel(const FString& CharacterBPPath)
	{
		const FString CharacterPathInLevel = CharacterBPPath + TEXT("_C");
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GEngine->GetWorldContexts()[0].World(), AActor::StaticClass(), FoundActors);

		for (const AActor* FoundActor : FoundActors)
		{
			FString ActorPath = FoundActor->GetClass()->GetPathName();
			if (ActorPath.Equals(CharacterPathInLevel))
			{
				return true;
			}
		}

		return false;
	}
}

// FMetaHumanImport Definition *****************************************
TSharedPtr<FMetaHumanImport> FMetaHumanImport::MetaHumanImportInst;

TSharedPtr<FMetaHumanImport> FMetaHumanImport::Get()
{
	if (!MetaHumanImportInst.IsValid())
	{
		MetaHumanImportInst = MakeShareable(new FMetaHumanImport);
	}
	return MetaHumanImportInst;
}

void FMetaHumanImport::SetAutomationHandler(IMetaHumanProjectUtilitiesAutomationHandler* Handler)
{
	AutomationHandler = Handler;
}

void FMetaHumanImport::SetBulkImportHandler(IMetaHumanBulkImportHandler* Handler)
{
	BulkImportHandler = Handler;
}

void FMetaHumanImport::ImportAsset(const FMetaHumanAssetImportDescription& ImportDescription)
{
	using namespace UE::MetaHuman::Private;

	// Determine the source and destination paths. There are two ways they can be updated from the standard /Game/MetaHumans
	// location. In UEFN we can request that instead of installing to /Game we install to the content folder of the
	// project. Also, we can use project settings to override the destination paths for both cinematic and optimized
	// MetaHumans
	FString DestinationCommonAssetPath = ImportDescription.DestinationPath / FImportPaths::CommonFolderName; // At the moment this can not be changed
	FString CharactersRootImportPath = ImportDescription.DestinationPath; // This is the location we will look for other characters in the project

	// If the ImportDescription does not target a specific location (i.e. not UEFN) then look for a project-based override
	const FSourceMetaHuman SourceMetaHuman{ImportDescription.CharacterPath, ImportDescription.CommonPath, ImportDescription.CharacterName};
	if (ImportDescription.DestinationPath == FMetaHumanAssetImportDescription::DefaultDestinationPath)
	{
		// Get overrides from settings
		const UMetaHumanSDKSettings* ProjectSettings = GetDefault<UMetaHumanSDKSettings>();
		if (SourceMetaHuman.GetQualityLevel() == EMetaHumanQualityLevel::Cinematic)
		{
			if (!ProjectSettings->CinematicImportPath.Path.IsEmpty())
			{
				// Use the project-configured destination path for cinematic MHs
				CharactersRootImportPath = ProjectSettings->CinematicImportPath.Path;
			}
		}
		else if (!ProjectSettings->OptimizedImportPath.Path.IsEmpty())
		{
			// Use the project-configured destination path for optimized MHs
			CharactersRootImportPath = ProjectSettings->OptimizedImportPath.Path;
		}
	}

	// Check we are trying to import to a valid content root
	if (!(FPackageName::IsValidPath(DestinationCommonAssetPath) && FPackageName::IsValidPath(CharactersRootImportPath)))
	{
		FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok, LOCTEXT("InvalidImportRootError", "Attempting to import to an invalid root location. Please check your Import Paths in the MetaHuman SDK Project Settings."));
		UE_LOG(LogMetaHumanImport, Error, TEXT("Invalid import root. Common files import root: \"%s\", character files import root: \"%s\""), *DestinationCommonAssetPath, *CharactersRootImportPath);
		return;
	}

	// Calculate whether we need to fixup references in the assets after importing (which we need to do if the asset
	// path has changed for any imported assets).
	const bool bRequiresReferenceFixup = CharactersRootImportPath != ImportDescription.SourcePath;

	// This is the location we are installing the character to
	FString DestinationCharacterAssetPath{CharactersRootImportPath / ImportDescription.CharacterName};
	UE_LOG(LogMetaHumanImport, Display, TEXT("Importing MetaHuman: %s to %s"), *ImportDescription.CharacterName, *DestinationCharacterAssetPath);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	// Helpers for managing source data
	const FImportPaths ImportPaths(ImportDescription.CommonPath, ImportDescription.CharacterPath, DestinationCommonAssetPath, DestinationCharacterAssetPath);

	// sanitize our import destination
	const int MaxImportPathLength = FPlatformMisc::GetMaxPathLength() - 100; // longest asset path in a MetaHuman ~100 chars
	if (ImportPaths.DestinationCharacterFilePath.Len() > MaxImportPathLength)
	{
		FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok, LOCTEXT("ImportPathLengthError", "The requested import path is too long. Please set the Import Path in the MetaHuman SDK Project Settings to a shorter path, or move your project to a file location with a shorter path."));
		UE_LOG(LogMetaHumanImport, Error, TEXT("Import path \"%s\", exceeds maximum length of %d"), *ImportPaths.DestinationCharacterFilePath, MaxImportPathLength);
		return;
	}

	// Determine what other MetaHumans are installed and if any are incompatible
	const TArray<FInstalledMetaHuman> InstalledMetaHumans = FInstalledMetaHuman::GetInstalledMetaHumans(CharactersRootImportPath, ImportPaths.DestinationCommonFilePath);
	const TSet<FString> IncompatibleCharacters = CheckVersionCompatibility(SourceMetaHuman, InstalledMetaHumans);

	// Get the names of all installed MetaHumans and see if the MetaHuman we are trying to install is among them
	TSet<FString> InstalledMetaHumanNames;
	Algo::Transform(InstalledMetaHumans, InstalledMetaHumanNames, &FInstalledMetaHuman::GetName);
	bool bIsNewCharacter = !InstalledMetaHumanNames.Contains(ImportDescription.CharacterName);


	// Get Manifest of files and version information included in downloaded MetaHuman
	IFileManager& FileManager = IFileManager::Get();
	const FString SourceAssetVersionFilePath = ImportPaths.SourceRootFilePath / TEXT("MHAssetVersions.txt");
	if (!FileManager.FileExists(*SourceAssetVersionFilePath))
	{
		FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok, LOCTEXT("CorruptedDownloadError", "The downloaded MetaHuman is corrupted and can not be imported. Please re-generate and re-download the MetaHuman and try again."));
		return;
	}
	const FAssetOperationPaths AssetOperations = DetermineAssetOperations(ParseVersionInfo(SourceAssetVersionFilePath), ImportPaths, ImportDescription.bForceUpdate);

	// If we are updating common files, have incompatible characters and are not updating all of them, then ask the user if they want to continue.
	if (IncompatibleCharacters.Num() > 0 && !ImportDescription.bIsBatchImport && !AssetOperations.Update.IsEmpty())
	{
		if (AutomationHandler)
		{
			if (!AutomationHandler->ShouldContinueWithBreakingMetaHumans(IncompatibleCharacters.Array(), AssetOperations.Update))
			{
				return;
			}
		}
		else
		{
			TSet<FString> AvailableMetaHumans;
			for (const FQuixelAccountMetaHumanEntry& Entry : ImportDescription.AccountMetaHumans)
			{
				if (!Entry.bIsLegacy)
				{
					AvailableMetaHumans.Add(Entry.Name);
				}
			}
			EImportOperationUserResponse Response = DisplayUpgradeWarning(SourceMetaHuman, IncompatibleCharacters, InstalledMetaHumans, AvailableMetaHumans, AssetOperations);
			if (Response == EImportOperationUserResponse::Cancel)
			{
				return;
			}

			if (Response == EImportOperationUserResponse::BulkImport && BulkImportHandler)
			{
				TArray<FString> ImportIds{ImportDescription.QuixelId};
				for (const FString& Name : IncompatibleCharacters)
				{
					for (const FQuixelAccountMetaHumanEntry& Entry : ImportDescription.AccountMetaHumans)
					{
						// TODO - this just selects the first entry that matches the MetaHuman's name. We need to handle more complex mapping between Ids and entry in the UI
						if (!Entry.bIsLegacy && Entry.Name == Name)
						{
							ImportIds.Add(Entry.Id);
							break;
						}
					}
				}
				BulkImportHandler->DoBulkImport(ImportIds);
				return;
			}
		}
	}

	// If the user is changing the export quality level of the MetaHuman then warn them that they are doing do
	if (!bIsNewCharacter && ImportDescription.bWarnOnQualityChange)
	{
		const FInstalledMetaHuman TargetMetaHuman(ImportDescription.CharacterName, ImportPaths.DestinationCharacterFilePath, ImportPaths.DestinationCommonFilePath);
		const EMetaHumanQualityLevel SourceQualityLevel = SourceMetaHuman.GetQualityLevel();
		const EMetaHumanQualityLevel TargetQualityLevel = TargetMetaHuman.GetQualityLevel();
		if (SourceQualityLevel != TargetQualityLevel)
		{
			const bool bContinue = DisplayQualityLevelChangeWarning(SourceQualityLevel, TargetQualityLevel);
			if (!bContinue)
			{
				return;
			}
		}
	}

	TSet<FString> TouchedAssets;
	TouchedAssets.Reserve(AssetOperations.Update.Num() + AssetOperations.Replace.Num() + AssetOperations.Add.Num());
	TouchedAssets.Append(AssetOperations.Update);
	TouchedAssets.Append(AssetOperations.Replace);
	TouchedAssets.Append(AssetOperations.Add);

	FText CharacterCopyMsgDialogMessage = FText::FromString((bIsNewCharacter ? TEXT("Importing : ") : TEXT("Re-Importing : ")) + ImportDescription.CharacterName);
	FScopedSlowTask ImportProgress(bRequiresReferenceFixup ? 3.0f : 2.0f, CharacterCopyMsgDialogMessage, true);
	ImportProgress.MakeDialog();

	// If required, set up redirects
	TArray<FCoreRedirect> Redirects;
	if (bRequiresReferenceFixup)
	{
		const FString AssetExtension = LexToString(EPackageExtension::Asset);
		for (const FString& AssetFilePath : TouchedAssets)
		{
			if (AssetFilePath.EndsWith(AssetExtension))
			{
				const FString PackageName = AssetFilePath.LeftChop(AssetExtension.Len());
				FString SourcePackage = ImportDescription.SourcePath / PackageName;
				FString DestinationPackage = ImportPaths.GetDestinationPackage(PackageName);
				if (SourcePackage != DestinationPackage)
				{
					Redirects.Emplace(ECoreRedirectFlags::Type_Package, SourcePackage, DestinationPackage);
				}
			}
		}
		FCoreRedirects::AddRedirectList(Redirects, TEXT("MetaHumanImportTool"));
	}

	// Update assets
	ImportProgress.EnterProgressFrame();
	CopyFiles(AssetOperations, ImportPaths);

	// Copy in text version files
	const FString VersionFile = TEXT("VersionInfo.txt");
	FileManager.Copy(*(ImportPaths.DestinationCharacterFilePath / VersionFile), *(ImportPaths.SourceCharacterFilePath / VersionFile), true, true);
	FileManager.Copy(*(ImportPaths.DestinationCommonFilePath / VersionFile), *(ImportPaths.SourceCommonFilePath / VersionFile), true, true);

	// Copy in optional DNA files
	const FString SourceAssetsFolder = TEXT("SourceAssets");
	const FString SourceAssetsPath = FPaths::Combine(ImportPaths.SourceCharacterFilePath, SourceAssetsFolder);
	if (FileManager.DirectoryExists(*SourceAssetsPath))
	{
		FPlatformFileManager::Get().GetPlatformFile().CopyDirectoryTree(*FPaths::Combine(ImportPaths.DestinationCharacterFilePath, SourceAssetsFolder), *SourceAssetsPath, true);
	}

	// Refresh asset registry
	TArray<FString> AssetBasePaths;
	AssetBasePaths.Add(ImportPaths.DestinationCommonAssetPath);
	AssetBasePaths.Add(ImportPaths.DestinationCharacterAssetPath);
	ImportProgress.EnterProgressFrame();
	AssetRegistryModule.Get().ScanPathsSynchronous(AssetBasePaths, true);


	if (bRequiresReferenceFixup)
	{
		// Re-save assets to bake-in new reference paths
		ImportProgress.EnterProgressFrame();
		FScopedSlowTask MetaDataWriteProgress(TouchedAssets.Num(), FText::FromString(TEXT("Finalizing imported assets")));
		MetaDataWriteProgress.MakeDialog();
		for (const FString& AssetToUpdate : TouchedAssets)
		{
			MetaDataWriteProgress.EnterProgressFrame();
			const FString FullFilePath = ImportPaths.GetDestinationFile(AssetToUpdate);
			if (!FileManager.FileExists(*FullFilePath))
			{
				continue;
			}
			const FString AssetPath = ImportPaths.GetDestinationAsset(AssetToUpdate);
			if (UObject* ItemObject = LoadObject<UObject>(nullptr, *AssetPath))
			{
				if (UPackage* Package = ItemObject->GetOutermost())
				{
					Package->FullyLoad();
					FSavePackageArgs SaveArgs;
					SaveArgs.TopLevelFlags = RF_Standalone;
					UPackage::Save(Package, nullptr, *FullFilePath, SaveArgs);
				}
			}
		}

		// Remove Redirects
		FCoreRedirects::RemoveRedirectList(Redirects, TEXT("MetaHumanImportTool"));
	}
}
}

#undef LOCTEXT_NAMESPACE
