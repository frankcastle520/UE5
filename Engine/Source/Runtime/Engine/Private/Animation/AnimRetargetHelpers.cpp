// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimRetargetHelpers.h"


#if WITH_EDITOR
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Animation/AnimSequence.h"
#include "Animation/PoseAsset.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Templates/UnrealTemplate.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "AnimRetargetHelpers"

namespace UE::Anim::RetargetHelpers
{

#if WITH_EDITOR

namespace Private
{

template<typename AssetType>
ERetargetSourceAssetStatus CheckRetargetSourceAssetDataImpl(const AssetType* InAsset)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const TSoftObjectPtr<USkeletalMesh>& RetargetSourceAsset = InAsset->RetargetSourceAsset;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	FAssetData AssetData;
	UE::AssetRegistry::EExists AssetDataStatus = RetargetSourceAsset.IsNull() 
		? UE::AssetRegistry::EExists::DoesNotExist
		: IAssetRegistry::GetChecked().TryGetAssetByObjectPath(RetargetSourceAsset.ToSoftObjectPath(), AssetData);

	if (InAsset->RetargetSourceAssetReferencePose.Num() > 0)
	{
		if (AssetDataStatus != AssetRegistry::EExists::Unknown)
		{
			if (AssetDataStatus == UE::AssetRegistry::EExists::Exists)
			{
				return ERetargetSourceAssetStatus::RetargetDataOk;
			}
			else // if (AssetDataStatus == UE::AssetRegistry::EExists::DoesNotExist)
			{
				UE_LOG(LogAnimation, Warning, TEXT("Asset [%s] references a missing Retarget Source Asset [%s]. Retarget Reference Pose has [%d] elements. Please, add a correct retarget source asset and resave.")
					, *InAsset->GetFullName()
					, *(RetargetSourceAsset.GetLongPackageName() + FString(TEXT("/")) + RetargetSourceAsset.GetAssetName())
					, InAsset->RetargetSourceAssetReferencePose.Num());
				return ERetargetSourceAssetStatus::RetargetSourceMissing;
			}
		}
		else // if Asset registry is not ready, we go with a slow path
		{
			const USkeletalMesh* SourceReferenceMesh = RetargetSourceAsset.LoadSynchronous();
			if (SourceReferenceMesh == nullptr)
			{
				UE_LOG(LogAnimation, Warning, TEXT("Asset [%s] references a missing Retarget Source Asset [%s]. Retarget Reference Pose has [%d] elements. Please, add a correct retarget source asset and resave.")
					, *InAsset->GetFullName()
					, *(RetargetSourceAsset.GetLongPackageName() + FString(TEXT("/")) + RetargetSourceAsset.GetAssetName())
					, InAsset->RetargetSourceAssetReferencePose.Num());
				return ERetargetSourceAssetStatus::RetargetSourceMissing;
			}
			else
			{
				return ERetargetSourceAssetStatus::RetargetDataOk;
			}
		}
	}

	return ERetargetSourceAssetStatus::NoRetargetDataSet;
}

void CheckRetargetSourceAssetData(bool bFixAssets, const TArray<FString>& IncludedPaths, const TArray<FString>& ExcludedPaths)
{
	TArray<FAssetData> Assets;

	FARFilter AssetFilter;
	if (IncludedPaths.Num() > 0)
	{
		for (const FString& IncludedPath : IncludedPaths)
		{
			UE_LOG(LogAnimation, Log, TEXT("Check Retarget Source Assets scan folder [%s]."), *IncludedPath);
			AssetFilter.PackagePaths.AddUnique(*IncludedPath);
		}
		AssetFilter.bRecursivePaths = true;
	}
	else
	{
		UE_LOG(LogAnimation, Warning, TEXT("Check Retarget Source Assets will scan all folders (this might take some time and require a lot of memory)."));
	}
	AssetFilter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	AssetFilter.ClassPaths.Add(UPoseAsset::StaticClass()->GetClassPathName());

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.GetAssets(AssetFilter, Assets);

	UE_LOG(LogAnimation, Log, TEXT("Check Retarget Source Assets found [%d] assets."), Assets.Num());

	// Run through paths and classes that should be excluded
	if (Assets.Num() > 0 && ExcludedPaths.Num() > 0)
	{
		FARFilter Filter;
		Filter.bIncludeOnlyOnDiskAssets = true;
		Filter.bRecursivePaths = true;
		for (const FString& ExcludedPath : ExcludedPaths)
		{
			UE_LOG(LogAnimation, Log, TEXT("Check Retarget Source Assets Excluded folder : [%s]."), *ExcludedPath);
			Filter.PackagePaths.AddUnique(*ExcludedPath);
		}
		TArray<FAssetData> ExcludedAssetList;
		AssetRegistry.GetAssets(Filter, ExcludedAssetList);
		Assets.RemoveAll([&ExcludedAssetList](const FAssetData& Asset) {return ExcludedAssetList.Contains(Asset); });
	}

	UE_LOG(LogAnimation, Log, TEXT("Check Retarget Source Assets after filtering exclusions : [%d] assets."), Assets.Num());

	const int32 NumAssets = Assets.Num();
	for (int32 Idx = 0; Idx < NumAssets; Idx++)
	{
		if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(Assets[Idx].GetAsset()))
		{
			const ERetargetSourceAssetStatus Status = Private::CheckRetargetSourceAssetDataImpl(AnimSequence);
			if (bFixAssets)
			{
				if (Status == ERetargetSourceAssetStatus::RetargetSourceMissing)
				{
					AnimSequence->Modify();
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					AnimSequence->RetargetSourceAsset.Reset();
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
					AnimSequence->RetargetSourceAssetReferencePose.Empty();
					AnimSequence->MarkPackageDirty();
				}
			}
		}
		else if (UPoseAsset* PoseAsset = Cast<UPoseAsset>(Assets[Idx].GetAsset()))
		{
			const ERetargetSourceAssetStatus Status = Private::CheckRetargetSourceAssetDataImpl(PoseAsset);
			if (bFixAssets)
			{
				if (Status == ERetargetSourceAssetStatus::RetargetSourceMissing)
				{
					PoseAsset->Modify();
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					PoseAsset->RetargetSourceAsset.Reset();
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
					PoseAsset->RetargetSourceAssetReferencePose.Empty();
					PoseAsset->MarkPackageDirty();
				}
			}
		}
	}
}

static FAutoConsoleCommand CheckRetargetSourceAssetDataCmd(
	TEXT("a.CheckRetargetSourceAssetData"),
	TEXT("Checks if Anim Sequences and Pose Assets RetargetSourceAsset is valid. Type: 'a.CheckRetargetSourceAssetData /Game' to check assets in the Game (Content) folder.  'a.CheckRetargetSourceAssetData /Game true' to check and fix all the assets in the Game (Content) folder."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			static const FString IncludedPathsSwitch = TEXT("Include=");
			static const FString ExcludedPathsSwitch = TEXT("Exclude=");
			static const TCHAR* ParamDelims[] =
			{
				TEXT(";"),
				TEXT("+"),
				TEXT(","),
			};

			TArray<FString> IncludedPaths;
			TArray<FString> ExcludedPaths;

			bool bWantsFix = false;
			FString SwitchValue;

			const int32 NumArgs = Args.Num();
			for (int32 i = 0; i < NumArgs; i++)
			{
				const FString& Arg = Args[i];
				if (Arg.ToLower() == FString("FixAssets"))
				{
					bWantsFix = true;
				}
				else if (FParse::Value(*Arg, *IncludedPathsSwitch, SwitchValue))
				{
					SwitchValue.ParseIntoArray(IncludedPaths, ParamDelims, 3);
				}
				else if (FParse::Value(*Arg, *ExcludedPathsSwitch, SwitchValue))
				{
					SwitchValue.ParseIntoArray(ExcludedPaths, ParamDelims, 3);
				}
				else
				{
					IncludedPaths.AddUnique(Arg);
				}
			}

			CheckRetargetSourceAssetData(bWantsFix, IncludedPaths, ExcludedPaths);
		}
	));
} // end namespace Private

ERetargetSourceAssetStatus CheckRetargetSourceAssetData(const UAnimSequence* InAsset)
{
	return Private::CheckRetargetSourceAssetDataImpl(InAsset);
}

ERetargetSourceAssetStatus CheckRetargetSourceAssetData(const UPoseAsset* InAsset)
{
	return Private::CheckRetargetSourceAssetDataImpl(InAsset);
}
#endif // WITH_EDITOR

} // end namespace UE::Anim::RetargetHelpers

#undef LOCTEXT_NAMESPACE 
