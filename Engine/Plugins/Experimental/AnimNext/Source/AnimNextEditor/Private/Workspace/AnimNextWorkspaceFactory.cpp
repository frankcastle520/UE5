// Copyright Epic Games, Inc. All Rights Reserved.

#include "Workspace/AnimNextWorkspaceFactory.h"
#include "AnimNextWorkspaceSchema.h"
#include "AssetTypeCategories.h"

#define LOCTEXT_NAMESPACE "AnimNextWorkspaceFactory"

UAnimNextWorkspaceFactory::UAnimNextWorkspaceFactory()
{
	SetSchemaClass(UAnimNextWorkspaceSchema::StaticClass());
}

bool UAnimNextWorkspaceFactory::ConfigureProperties()
{
	return true;
}

FText UAnimNextWorkspaceFactory::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "AnimNext Workspace");
}

uint32 UAnimNextWorkspaceFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Animation;
}

const TArray<FText>& UAnimNextWorkspaceFactory::GetMenuCategorySubMenus() const
{
	static const TArray<FText> Categories = { LOCTEXT("AnimNextSubMenu", "AnimNext") };
	return Categories;
}

#undef LOCTEXT_NAMESPACE