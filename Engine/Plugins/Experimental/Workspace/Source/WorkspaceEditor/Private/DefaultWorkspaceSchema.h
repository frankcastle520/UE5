// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorkspaceSchema.h"
#include "DefaultWorkspaceSchema.generated.h"

// Workspace schema allowing all asset types
UCLASS()
class UDefaultWorkspaceSchema : public UWorkspaceSchema
{
	GENERATED_BODY()

	// UWorkspaceSchema interface
	virtual FText GetDisplayName() const { return FText::GetEmpty(); }
	virtual TConstArrayView<FTopLevelAssetPath> GetSupportedAssetClassPaths() const override { return TConstArrayView<FTopLevelAssetPath>(); }
};