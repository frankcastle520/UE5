// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetDefinitionDefault.h"
#include "AssetDefinition_ClothAsset.generated.h"

class UChaosClothAsset;

UCLASS()
class UAssetDefinition_ClothAsset : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:

	UE_DEPRECATED(5.5, "Dataflow assets are now created through the physics menu.")
	static UObject* NewOrOpenDataflowAsset(const UChaosClothAsset* ClothAsset);

private:

	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual UThumbnailInfo* LoadThumbnailInfo(const FAssetData& InAssetData) const override;

	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};

