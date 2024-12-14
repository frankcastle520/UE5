// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISceneOutlinerTreeItem.h"

class UAnimNextDataInterfaceEntry;

namespace UE::AnimNext::Editor
{

class IWorkspaceOutlinerItemDetails;

struct FVariablesOutlinerDataInterfaceItem : ISceneOutlinerTreeItem
{
	static const FSceneOutlinerTreeItemType Type;

	FVariablesOutlinerDataInterfaceItem(UAnimNextDataInterfaceEntry* InEntry);

	// Begin ISceneOutlinerTreeItem overrides
	virtual bool IsValid() const override;
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;
	virtual FString GetPackageName() const override;
	// End ISceneOutlinerTreeItem overrides

	// Ptr to the underlying entry
	TWeakObjectPtr<UAnimNextDataInterfaceEntry> WeakEntry;
};

}
