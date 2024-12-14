// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISceneOutlinerTreeItem.h"
#include "Variables/AnimNextVariableEntryProxy.h"

class UAnimNextDataInterfaceEntry;
class UAnimNextVariableEntry;

namespace UE::AnimNext::Editor
{

struct FVariablesOutlinerEntryItem : ISceneOutlinerTreeItem
{
	static const FSceneOutlinerTreeItemType Type;

	FVariablesOutlinerEntryItem(UAnimNextVariableEntry* InEntry);

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

	// Ptr to the underlying entry
	TWeakObjectPtr<UAnimNextVariableEntry> WeakEntry;

	// The data interface entry this entry is from, if any
	TWeakObjectPtr<UAnimNextDataInterfaceEntry> WeakDataInterfaceEntry;

	// Proxy entry used for details panel editing of variables in implemented data interfaces
	TStrongObjectPtr<UAnimNextVariableEntryProxy> ProxyEntry;
};

}
