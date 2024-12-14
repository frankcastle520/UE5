// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkspaceFactory.h"
#include "AnimNextWorkspaceFactory.generated.h"

UCLASS(MinimalAPI, BlueprintType)
class UAnimNextWorkspaceFactory : public UWorkspaceFactory
{
	GENERATED_BODY()

	UAnimNextWorkspaceFactory();

	// UFactory interface
	virtual bool ConfigureProperties() override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual const TArray<FText>& GetMenuCategorySubMenus() const override;
};