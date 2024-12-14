// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_DMTextureSet.h"

#include "DMTextureSet.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_DMTextureSet"

FText UAssetDefinition_DMTextureSet::GetAssetDisplayName() const
{
	return LOCTEXT("DMTextureSet", "Material Designer Texture Set");
}

FText UAssetDefinition_DMTextureSet::GetAssetDisplayName(const FAssetData& InAssetData) const
{
	return GetAssetDisplayName();
}

TSoftClassPtr<> UAssetDefinition_DMTextureSet::GetAssetClass() const
{
	return UDMTextureSet::StaticClass();
}

FLinearColor UAssetDefinition_DMTextureSet::GetAssetColor() const
{
	return FLinearColor(FColor(192, 64, 64));
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_DMTextureSet::GetAssetCategories() const
{
	static const TArray<FAssetCategoryPath> Categories = {EAssetCategoryPaths::Texture};
	return Categories;
}

#undef LOCTEXT_NAMESPACE
