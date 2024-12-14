// Copyright Epic Games, Inc. All Rights Reserved.
#include "Engine/ExternalAssetDependencyGatherer.h"

#if WITH_EDITOR

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "ExternalPackageHelper.h"

void FExternalAssetDependencyGatherer::GatherDependencies(const FAssetData& AssetData, const FAssetRegistryState& AssetRegistryState, TFunctionRef<FARCompiledFilter(const FARFilter&)> CompileFilterFunc, TArray<IAssetDependencyGatherer::FGathereredDependency>& OutDependencies, TArray<FString>& OutDependencyDirectories) const
{		
	const FString ExternalObjectsPath = FExternalPackageHelper::GetExternalObjectsPath(AssetData.PackageName.ToString());
	OutDependencyDirectories.Add(ExternalObjectsPath);

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bIncludeOnlyOnDiskAssets = true;
	Filter.PackagePaths.Add(*ExternalObjectsPath);

	TArray<FAssetData> FilteredAssets;
	AssetRegistryState.GetAssets(CompileFilterFunc(Filter), {}, FilteredAssets, true);

	for (const FAssetData& FilteredAsset : FilteredAssets)
	{
		OutDependencies.Emplace(IAssetDependencyGatherer::FGathereredDependency{ FilteredAsset.PackageName, UE::AssetRegistry::EDependencyProperty::Game | UE::AssetRegistry::EDependencyProperty::Build });
	}
}

#endif
