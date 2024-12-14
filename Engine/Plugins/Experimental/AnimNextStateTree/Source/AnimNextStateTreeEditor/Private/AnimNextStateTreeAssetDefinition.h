// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextStateTree.h"
#include "AssetDefinitionDefault.h"
#include "AnimNextStateTreeAssetDefinition.generated.h"

#define LOCTEXT_NAMESPACE "AnimNextAssetDefinitions"

UCLASS()
class UAssetDefinition_AnimNextStateTree : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition interface
	virtual FText GetAssetDisplayName() const override { return LOCTEXT("AnimNextStateTree", "AnimNext State Tree"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(96,128,48)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UAnimNextStateTree::StaticClass(); }
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override
	{
		static const auto Categories = { FAssetCategoryPath(EAssetCategoryPaths::Animation, LOCTEXT("AnimNextSubMenu", "AnimNext")) };
		return Categories;
	}
	virtual bool ShouldSaveExternalPackages() const override { return true; }
};

#undef LOCTEXT_NAMESPACE