// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

class UChooserTable;

namespace UE::ChooserEditor
{

class FChooserTableEditor;

struct FNestedChooserTreeEntry
{
	UChooserTable* Chooser = nullptr;
	bool bExpanded = true;
};

class SNestedChooserTree : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNestedChooserTree)
	{}
	SLATE_ARGUMENT(FChooserTableEditor*, ChooserEditor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SNestedChooserTree() override;

	void RefreshAll();
private:
	TSharedPtr<SWidget> TreeViewContextMenuOpening();
	TSharedRef<ITableRow> TreeViewGenerateRow(TSharedPtr<FNestedChooserTreeEntry> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void TreeViewGetChildren(TSharedPtr<FNestedChooserTreeEntry> InItem, TArray<TSharedPtr<FNestedChooserTreeEntry>>& OutChildren);
	void TreeViewDoubleClicked(TSharedPtr<FNestedChooserTreeEntry> SelectedObject);
	void DeleteNestedChooser();
	void RenameNestedChooser();
	
	FChooserTableEditor* ChooserEditor = nullptr;
	UChooserTable* RootChooser = nullptr;
	TSharedPtr<STreeView<TSharedPtr<FNestedChooserTreeEntry>>> TreeView;

	TArray<TSharedPtr<FNestedChooserTreeEntry>> TreeEntries;
	TArray<TSharedPtr<FNestedChooserTreeEntry>> AllChoosers;
};

}
