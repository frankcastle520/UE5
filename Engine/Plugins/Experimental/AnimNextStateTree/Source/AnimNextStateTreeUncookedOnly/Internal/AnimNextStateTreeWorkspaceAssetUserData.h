// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/AssetUserData.h"

#include "AnimNextStateTreeWorkspaceAssetUserData.generated.h"

UCLASS()
class UAnimNextStateTreeWorkspaceAssetUserData : public UAssetUserData
{
	GENERATED_BODY()

	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
};
