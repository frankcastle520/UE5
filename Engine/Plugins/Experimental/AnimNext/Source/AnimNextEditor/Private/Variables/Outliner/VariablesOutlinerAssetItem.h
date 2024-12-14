// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISceneOutlinerTreeItem.h"

class UAnimNextRigVMAsset;

namespace UE::AnimNext::Editor
{

class IWorkspaceOutlinerItemDetails;

struct FVariablesOutlinerAssetItem : ISceneOutlinerTreeItem
{
	static const FSceneOutlinerTreeItemType Type;

	FVariablesOutlinerAssetItem(TSoftObjectPtr<UAnimNextRigVMAsset> InAsset);

	// Begin ISceneOutlinerTreeItem overrides
	virtual bool IsValid() const override;
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;
	virtual FString GetPackageName() const override;
	// End ISceneOutlinerTreeItem overrides

	// Renames the item to the specified name
	void Rename(const FText& InNewName);

	// Validates the new item name
	bool ValidateName(const FText& InNewName, FText& OutErrorMessage) const;

	// Soft ptr to the underlying asset, which may not be loaded yet
	TSoftObjectPtr<UAnimNextRigVMAsset> SoftAsset;
};

}
