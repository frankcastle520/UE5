// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataInterface/AnimNextDataInterface.h"
#include "AssetDefinitionDefault.h"
#include "AnimNextDataInterfaceAssetDefinition.generated.h"

#define LOCTEXT_NAMESPACE "AnimNextAssetDefinitions"

UCLASS()
class UAssetDefinition_AnimNextDataInterface : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition interface
	virtual FText GetAssetDisplayName() const override { return LOCTEXT("AnimNextDataInterface", "AnimNext Data Interface"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(128,64,32)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UAnimNextDataInterface::StaticClass(); }
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override
	{
		static const auto Categories = { FAssetCategoryPath(EAssetCategoryPaths::Animation, LOCTEXT("AnimNextSubMenu", "AnimNext")) };
		return Categories;
	}
	virtual bool ShouldSaveExternalPackages() const override { return true; }
};

#undef LOCTEXT_NAMESPACE