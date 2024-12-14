// Copyright Epic Games, Inc. All Rights Reserved.
#include "Engine/ExternalObjectAndActorDependencyGatherer.h"

#if WITH_EDITOR

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "ExternalPackageHelper.h"

void FExternalObjectAndActorDependencyGatherer::GatherDependencies(const FAssetData& AssetData, const FAssetRegistryState& AssetRegistryState, TFunctionRef<FARCompiledFilter(const FARFilter&)> CompileFilterFunc, TArray<IAssetDependencyGatherer::FGathereredDependency>& OutDependencies, TArray<FString>& OutDependencyDirectories) const
{		
	const FString ExternalActorsPath = ULevel::GetExternalActorsPath(AssetData.PackageName.ToString());
	const FString ExternalObjectPath = FExternalPackageHelper::GetExternalObjectsPath(AssetData.PackageName.ToString());

	OutDependencyDirectories.Add(ExternalActorsPath);
	OutDependencyDirectories.Add(ExternalObjectPath);

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bIncludeOnlyOnDiskAssets = true;

	Filter.PackagePaths.Add(*ExternalActorsPath);
	Filter.PackagePaths.Add(*ExternalObjectPath);

	TArray<FAssetData> FilteredAssets;
	AssetRegistryState.GetAssets(CompileFilterFunc(Filter), {}, FilteredAssets, true);

	for (const FAssetData& FilteredAsset : FilteredAssets)
	{
		OutDependencies.Emplace(IAssetDependencyGatherer::FGathereredDependency{ FilteredAsset.PackageName, UE::AssetRegistry::EDependencyProperty::Game | UE::AssetRegistry::EDependencyProperty::Build });
	}
}

REGISTER_ASSETDEPENDENCY_GATHERER(FExternalObjectAndActorDependencyGatherer, UWorld);

#endif

