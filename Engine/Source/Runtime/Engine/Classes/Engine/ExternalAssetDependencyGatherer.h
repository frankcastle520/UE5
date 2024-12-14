// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "AssetRegistry/AssetDependencyGatherer.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/World.h"

class ENGINE_API FExternalAssetDependencyGatherer : public IAssetDependencyGatherer
{
public:	
	FExternalAssetDependencyGatherer() = default;
	virtual ~FExternalAssetDependencyGatherer() = default;

	virtual void GatherDependencies(const FAssetData& AssetData, const FAssetRegistryState& AssetRegistryState, TFunctionRef<FARCompiledFilter(const FARFilter&)> CompileFilterFunc, TArray<IAssetDependencyGatherer::FGathereredDependency>& OutDependencies, TArray<FString>& OutDependencyDirectories) const override;
};

#endif