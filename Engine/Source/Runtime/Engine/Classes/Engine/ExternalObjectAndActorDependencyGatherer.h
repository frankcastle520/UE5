// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "AssetRegistry/AssetDependencyGatherer.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/World.h"

class ENGINE_API FExternalObjectAndActorDependencyGatherer : public IAssetDependencyGatherer
{
public:	
	FExternalObjectAndActorDependencyGatherer() = default;
	virtual ~FExternalObjectAndActorDependencyGatherer() = default;

	virtual void GatherDependencies(const FAssetData& AssetData, const FAssetRegistryState& AssetRegistryState, TFunctionRef<FARCompiledFilter(const FARFilter&)> CompileFilterFunc, TArray<IAssetDependencyGatherer::FGathereredDependency>& OutDependencies, TArray<FString>& OutDependencyDirectories) const override;
};

#endif