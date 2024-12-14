// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalizedAssetTools.h"

#include "AssetDefinitionRegistry.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetTools.h"
#include "AssetToolsLog.h"
#include "AssetToolsModule.h"
#include "Internationalization/PackageLocalizationUtil.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Logging/LogMacros.h"
#include "Misc/MessageDialog.h"
#include "SFileListReportDialog.h"
#include "SourceControlHelpers.h"
#include "SourceControlPreferences.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "LocalizedAssetTools"

FLocalizedAssetTools::FLocalizedAssetTools() : 
	RevisionControlIsNotAvailableWarningText(LOCTEXT("RevisionControlIsRequiredToChangeLocalizableAssets", "Revision Control is required to move/rename/delete localizable assets for this project and it is currently not accessible."))
	, FilesNeedToBeOnDiskWarningText(LOCTEXT("FilesToSyncDialogTitle", "Files in Revision Control need to be on disk"))
	
{

}

bool FLocalizedAssetTools::CanLocalize(const UClass* Class) const
{
	if (const UAssetDefinition* AssetDefinition = UAssetDefinitionRegistry::Get()->GetAssetDefinitionForClass(Class))
	{
		return AssetDefinition->CanLocalize(FAssetData()).IsSupported();
	}
	else
	{
		FAssetToolsModule& Module = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
		if (TSharedPtr<IAssetTypeActions> AssetActions = Module.Get().GetAssetTypeActionsForClass(Class).Pin())
		{
			return AssetActions->CanLocalize();
		}
	}

	return false;
}

ELocalizedAssetsOnDiskResult FLocalizedAssetTools::GetLocalizedVariantsOnDisk(const TArray<FName>& InPackages, TMap<FName, TArray<FName>>& OutLocalizedVariantsBySource, TArray<FName>* OutPackagesNotFound /*= nullptr*/) const
{
	FScopedSlowTask GettingLocalizedVariantsOnDiskSlowTask(1.0f, LOCTEXT("GettingLocalizedVariantsOnDiskSlowTask", "Getting localized variants on disk..."));

	OutLocalizedVariantsBySource.Reserve(InPackages.Num());
	if (OutPackagesNotFound != nullptr)
	{
		OutPackagesNotFound->Reserve(InPackages.Num());
	}

	TMap<FName, FAssetData> PackagesToAssetDataMap;
	UE::AssetRegistry::GetAssetForPackages(InPackages, PackagesToAssetDataMap);
	if (!ensureAlwaysMsgf(PackagesToAssetDataMap.Num() == InPackages.Num(), TEXT("PackageNames were not properly converted to AssetData.")))
	{
		for (const FName& OriginalAssetName : InPackages)
		{
			OutLocalizedVariantsBySource.Add({ OriginalAssetName, TArray<FName>() });
		}
		return ELocalizedAssetsOnDiskResult::PackageNamesError;
	}

	UAssetDefinitionRegistry* AssetDefinitionRegistry = UAssetDefinitionRegistry::Get();
	float ProgressStep = 1.0f / static_cast<float>(InPackages.Num());
	for (const FName& OriginalAssetName : InPackages)
	{
		GettingLocalizedVariantsOnDiskSlowTask.EnterProgressFrame(ProgressStep);
		FString SourceAssetPathStr = OriginalAssetName.ToString();
		FPackageLocalizationUtil::ConvertLocalizedToSource(SourceAssetPathStr, SourceAssetPathStr);
		const FName SourceAssetName(*SourceAssetPathStr);

		if (OutLocalizedVariantsBySource.Contains(SourceAssetName))
		{
			continue; // We want to avoid doing any unnecessary work if it was already processed
		}

		// We want to avoid doing any unnecessary work on assets that does not require checking for variants
		FAssetData SourceAssetData = PackagesToAssetDataMap[OriginalAssetName];
		UClass* SourceAssetClass = SourceAssetData.GetClass();
		const UAssetDefinition* SourceAssetDefinition = AssetDefinitionRegistry->GetAssetDefinitionForClass(SourceAssetClass);
		bool bShouldCheckForVariant = SourceAssetDefinition != nullptr && SourceAssetDefinition->CanLocalize(SourceAssetData).IsSupported();
		if (!bShouldCheckForVariant)
		{
			OutLocalizedVariantsBySource.Add({ SourceAssetName, TArray<FName>() });
			continue;
		}

		// Check on disk for localized variants first. Remember the assets that had no variants on disk
		// because we will then check in Revision Control if applicable
		TArray<FString> LocalizedVariantsPaths;
		FPackageLocalizationUtil::GetLocalizedVariantsAbsolutePaths(SourceAssetPathStr, LocalizedVariantsPaths);
		if (LocalizedVariantsPaths.IsEmpty())
		{
			if (OutPackagesNotFound != nullptr)
			{
				OutPackagesNotFound->Add(OriginalAssetName);
			}
			continue;
		}

		// If localized variants were found on disk, let's build renaming data for them too
		TArray<FName> LocalizedAssets;
		LocalizedAssets.Reserve(LocalizedVariantsPaths.Num());
		for (const FString& LocalizedVariantPath : LocalizedVariantsPaths)
		{
			FString Culture;
			FPackageLocalizationUtil::ExtractCultureFromLocalized(LocalizedVariantPath, Culture);

			FString LocalizedAsset;
			FPackageLocalizationUtil::ConvertSourceToLocalized(SourceAssetPathStr, Culture, LocalizedAsset);

			LocalizedAssets.Add(FName(LocalizedAsset));
		}

		OutLocalizedVariantsBySource.Add({ SourceAssetName, LocalizedAssets });
	}

	return ELocalizedAssetsOnDiskResult::Success;
}

ELocalizedAssetsInSCCResult FLocalizedAssetTools::GetLocalizedVariantsInRevisionControl(const TArray<FName>& InPackages, TMap<FName, TArray<FName>>& OutLocalizedVariantsBySource, TArray<FName>* OutPackagesNotFound /*= nullptr*/) const
{
	FScopedSlowTask GetLocalizedVariantsInRevisionControlSlowTask(1.0f, LOCTEXT("GetLocalizedVariantsInRevisionControlSlowTask", "Querying Revision Control for localized variants... This could take a long time."));
	GetLocalizedVariantsInRevisionControlSlowTask.EnterProgressFrame(0.05f);

	OutLocalizedVariantsBySource.Reserve(InPackages.Num());

	// Modify data to be used by USourceControlHelpers
	TArray<FString> PackagesAsString;
	PackagesAsString.Reserve(InPackages.Num());
	for (const FName& InPackageName : InPackages)
	{
		PackagesAsString.Add(InPackageName.ToString());
	}

	// Let's check the packages presence in Revision Control in a single query
	TArray<FString> LocalizedVariantsInRevisionControl;
	GetLocalizedVariantsInRevisionControlSlowTask.EnterProgressFrame(0.9f);
	bool bOutRevisionControlWasNeeded = !GetLocalizedVariantsDepotPaths(PackagesAsString, LocalizedVariantsInRevisionControl);

	// Fill a proper structure with the results
	float ProgressStep = 0.03f / static_cast<float>(LocalizedVariantsInRevisionControl.Num());
	for (const FString& LocalizedVariantInRevisionControl : LocalizedVariantsInRevisionControl)
	{
		GetLocalizedVariantsInRevisionControlSlowTask.EnterProgressFrame(ProgressStep);
		FString SourceAsset;
		FPackageLocalizationUtil::ConvertToSource(LocalizedVariantInRevisionControl, SourceAsset);
		FName SourceAssetName(SourceAsset);
		TArray<FName>* VariantsBySourceFound = OutLocalizedVariantsBySource.Find(SourceAssetName);
		TArray<FName>& VariantsBySource = (VariantsBySourceFound != nullptr ? *VariantsBySourceFound : OutLocalizedVariantsBySource.Add({ SourceAssetName, TArray<FName>() }));
		VariantsBySource.Add(FName(LocalizedVariantInRevisionControl));
	}

	// Don't forget to return the information on the packages that found nothing in Revision Control
	if (OutPackagesNotFound != nullptr)
	{
		ProgressStep = 0.02f / static_cast<float>(InPackages.Num());
		for (const FName& PackageName : InPackages)
		{
			GetLocalizedVariantsInRevisionControlSlowTask.EnterProgressFrame(ProgressStep);
			FString SourcePackage;
			FPackageLocalizationUtil::ConvertToSource(PackageName.ToString(), SourcePackage);
			if (OutLocalizedVariantsBySource.Find(FName(SourcePackage)) == nullptr)
			{
				// Package not found
				OutPackagesNotFound->Add(PackageName);
			}
		}
	}

	return bOutRevisionControlWasNeeded ? ELocalizedAssetsInSCCResult::RevisionControlNotAvailable : ELocalizedAssetsInSCCResult::Success;
}

ELocalizedAssetsResult FLocalizedAssetTools::GetLocalizedVariants(const TArray<FName>& InPackages, TMap<FName, TArray<FName>>& OutLocalizedVariantsBySourceOnDisk, bool bAlsoCheckInRevisionControl, TMap<FName, TArray<FName>>& OutLocalizedVariantsBySourceInRevisionControl, TArray<FName>* OutPackagesNotFound /*= nullptr*/) const
{
	ELocalizedAssetsResult Result = ELocalizedAssetsResult::Success;

	// Check on disk first
	// Call the LocalizedAssetTools interface to GetLocalizedVariantsOnDisk of a list of packages
	TMap<FName, TArray<FName>> VariantsBySources;
	TArray<FName> VariantsMaybeInRevisionControl;
	ELocalizedAssetsOnDiskResult DiskResult = GetLocalizedVariantsOnDisk(InPackages, OutLocalizedVariantsBySourceOnDisk, bAlsoCheckInRevisionControl ? &VariantsMaybeInRevisionControl : OutPackagesNotFound);
	Result = (DiskResult == ELocalizedAssetsOnDiskResult::PackageNamesError ? ELocalizedAssetsResult::PackageNamesError : Result);

	// Check in Revision Control if applicable
	// Call the LocalizedAssetTools interface to GetLocalizedVariantsInRevisionControl of a list of packages
	bool bRevisionControlWasNeeded = false;
	if (!VariantsMaybeInRevisionControl.IsEmpty())
	{
		if (Result == ELocalizedAssetsResult::Success)
		{
			ELocalizedAssetsInSCCResult SCCResult = GetLocalizedVariantsInRevisionControl(VariantsMaybeInRevisionControl, OutLocalizedVariantsBySourceInRevisionControl, OutPackagesNotFound);
			Result = (SCCResult == ELocalizedAssetsInSCCResult::RevisionControlNotAvailable ? ELocalizedAssetsResult::RevisionControlNotAvailable : Result);
		}
		else
		{
			OutPackagesNotFound->Append(VariantsMaybeInRevisionControl);
		}
	}

	return Result;
}

void FLocalizedAssetTools::OpenRevisionControlRequiredDialog() const
{
	FText WarningText = RevisionControlIsNotAvailableWarningText;
	const FText AvoidWarningText = LOCTEXT("HowToFixRevisionControlIsRequiredToChangeLocalizableAssets", "If you want to disable this project option, it is located under:\n\tProject Settings/\n\tEditor/\n\tRevision Control/\n\tRequires Revision Control To Rename Localizable Assets\n\nThis option is there to prevent breaking paths between a source asset and its localized variants if they are not on disk.");
	FMessageDialog::Open(EAppMsgType::Ok, WarningText.Format(LOCTEXT("RevisionControlIsRequiredToChangeLocalizableAssetsDialog", "{0}\n\n{1}"), WarningText, AvoidWarningText));
}

void FLocalizedAssetTools::OpenFilesInRevisionControlRequiredDialog(const TArray<FText>& FileList) const
{
	OpenLocalizedVariantsListMessageDialog(FilesNeedToBeOnDiskWarningText,
		LOCTEXT("FilesToSyncDialogHeader", "The following assets were found only in Revision Control. They need to be on your disk to be renamed."),
		FileList);
}

void FLocalizedAssetTools::OpenLocalizedVariantsListMessageDialog(const FText& Header, const FText& Message, const TArray<FText>& FileList) const
{
	SFileListReportDialog::OpenDialog(Header, Message, FileList, true);
}

const FText& FLocalizedAssetTools::GetRevisionControlIsNotAvailableWarningText() const
{
	return RevisionControlIsNotAvailableWarningText;
}

const FText& FLocalizedAssetTools::GetFilesNeedToBeOnDiskWarningText() const
{
	return FilesNeedToBeOnDiskWarningText;
}

bool FLocalizedAssetTools::GetLocalizedVariantsDepotPaths(const TArray<FString>& InPackagesNames, TArray<FString>& OutLocalizedVariantsPaths) const
{
	// Ensure source control system is up and running with a configured provider
	ISourceControlModule& SCModule = ISourceControlModule::Get();
	if (!SCModule.IsEnabled())
	{
		return false;
	}
	ISourceControlProvider& Provider = SCModule.GetProvider();
	if (!Provider.IsAvailable())
	{
		return false;
	}

	// Other providers don't work for now
	if (Provider.GetName() == "Perforce")
	{
		TArray<FString> LocalizedVariantsRegexPaths;
		LocalizedVariantsRegexPaths.Reserve(InPackagesNames.Num());
		for (const FString& InPackageName : InPackagesNames)
		{
			FString SourcePackageName;
			FPackageLocalizationUtil::ConvertToSource(InPackageName, SourcePackageName);
			FString LocalizedVariantsRegexPath;
			FPackageLocalizationUtil::ConvertSourceToRegexLocalized(SourcePackageName, LocalizedVariantsRegexPath);
			LocalizedVariantsRegexPath += FPackageName::GetAssetPackageExtension();
			LocalizedVariantsRegexPaths.Add(LocalizedVariantsRegexPath);
		}

		bool bSilent = true;
		bool bIncludeDeleted = true;
		USourceControlHelpers::GetFilesInDepotAtPaths(LocalizedVariantsRegexPaths, OutLocalizedVariantsPaths, bIncludeDeleted, bSilent, true);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

