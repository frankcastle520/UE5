// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/RigVMTreeToolkitNode.h"
#include "Widgets/RigVMTreeToolkitFilter.h"
#include "Widgets/RigVMTreeToolkitContext.h"
#include "Widgets/RigVMTreeToolkitTask.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Input/SSearchBox.h"

class SRigVMChangesTreeView;

class RIGVMEDITOR_API SRigVMChangesTreeRow
	: public STableRow<TSharedRef<FRigVMTreeNode>>
{
public:
	
	SLATE_BEGIN_ARGS(SRigVMChangesTreeRow)
	{}
	SLATE_ARGUMENT(TSharedPtr<FRigVMTreeNode>, Node)
	SLATE_ARGUMENT(TSharedPtr<SRigVMChangesTreeView>, OwningWidget)
	SLATE_END_ARGS()

	virtual ~SRigVMChangesTreeRow() override;
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView);

private:

	EVisibility GetCheckBoxVisibility() const;
	ECheckBoxState GetCheckBoxState() const;
	void OnCheckBoxStateChanged(ECheckBoxState InNewState);
	const FSlateBrush* GetBackgroundImage() const;
	FSlateColor GetBackgroundColor() const;
	FOptionalSize GetIndentWidth() const;
	FSlateColor GetTextColor() const;
	const FSlateBrush* GetIcon() const;
	FSlateColor GetIconColor() const;
	const FSlateBrush* GetExpanderImage() const;
	FReply OnExpanderMouseButtonDown(const FGeometry& SenderGeometry, const FPointerEvent& MouseEvent);
	EVisibility GetExpanderVisibility() const;
	TArray<FRigVMTag> GetVariantTags() const;
	void RequestRefresh(bool bForce);

	TSharedPtr<FRigVMTreeNode> Node;
	SRigVMChangesTreeView* OwningWidget = nullptr;
	mutable TOptional<TArray<FRigVMTag>> Tags;
};

DECLARE_DELEGATE_RetVal_OneParam(FReply,FOnRigVMTreeNodeSelected,TSharedRef<FRigVMTreeNode>);
DECLARE_DELEGATE_RetVal_OneParam(FReply,FOnRigVMTreeNodeDoubleClicked,TSharedRef<FRigVMTreeNode>);

class RIGVMEDITOR_API SRigVMChangesTreeView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRigVMChangesTreeView)
	{}
	SLATE_ATTRIBUTE(TSharedPtr<FRigVMTreePhase>, Phase)
	SLATE_EVENT(FOnRigVMTreeNodeSelected, OnNodeSelected)
	SLATE_EVENT(FOnRigVMTreeNodeDoubleClicked, OnNodeDoubleClicked)
	SLATE_END_ARGS()

	virtual ~SRigVMChangesTreeView() override;

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	EVisibility GetPathFilterVisibility() const;
	void OnPathFilterTextChanged(const FText& SearchText);
	void OnPathFilterTextCommitted(const FText& SearchText, ETextCommit::Type CommitInfo);

	void RequestRefresh_AnyThread(bool bForce = false);
	void RefreshFilteredNodes(bool bForce = false);
	void RefreshFilteredNodesIfRequired();

	void OnPhaseChanged();
	void SetSelection(const TSharedPtr<FRigVMTreeNode>& InNode, bool bRequestScrollIntoView = false);

	TSharedPtr<STreeView<TSharedRef<FRigVMTreeNode>>> GetTreeView()
	{
		return TreeView;
	}

	TSharedRef<FRigVMTreePhase> GetPhase() const;
	TSharedRef<FRigVMTreeContext> GetContext() const;
	TSharedPtr<FRigVMTreePathFilter> GetPathFilter() const;

	TArray<TSharedRef<FRigVMTreeNode>> GetSelectedNodes() const;
	bool HasAnyVisibleCheckedNode() const;
	TArray<TSharedRef<FRigVMTreeNode>> GetCheckedNodes() const;
	
private:

	TAttribute<TSharedPtr<FRigVMTreePhase>> PhaseAttribute;
	TArray<TSharedRef<FRigVMTreeNode>> FilteredNodes;
	
	FText GetPathFilterText();
	TSharedPtr<SSearchBox> PathFilterBox;

	TSharedPtr<STreeView<TSharedRef<FRigVMTreeNode>>> TreeView;

	FOnRigVMTreeNodeSelected OnNodeSelected;
	FOnRigVMTreeNodeDoubleClicked OnNodeDoubleClicked;

	TAttribute<FText> BulkEditTitle;
	TAttribute<FText> BulkEditConfirmMessage;
	TAttribute<FString> BulkEditConfirmIniField;

	TSharedRef<ITableRow> MakeTreeRowWidget(TSharedRef<FRigVMTreeNode> InNode, const TSharedRef<STableViewBase>& OwnerTable);

	void GetChildrenForNode(TSharedRef<FRigVMTreeNode> InNode, TArray<TSharedRef<FRigVMTreeNode>>& OutChildren);
	void OnSelectionChanged(TSharedPtr<FRigVMTreeNode> Selection, ESelectInfo::Type SelectInfo);
	void OnTreeElementDoubleClicked(TSharedRef<FRigVMTreeNode> InNode);
	ESelectionMode::Type GetSelectionMode() const;

	TSharedPtr<SWidget> OnGetNodeContextMenuContent();

	EVisibility GetSettingsButtonVisibility() const;
	FReply OnSettingsButtonClicked();

	std::atomic<int32> RequestRefreshCount;
	std::atomic<int32> RequestRefreshForceCount;
};
