// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlCheckedOutDialog.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Styling/AppStyle.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Framework/Application/SlateApplication.h"
#include "SPrimaryButton.h"
#include "ISourceControlModule.h"
#include "SourceControlAssetDataCache.h"
#include "Algo/Find.h"
#include "Algo/Count.h"
#include "ActorFolder.h"
#include "ActorFolderDesc.h"
#include "AssetDefinitionRegistry.h"

#define LOCTEXT_NAMESPACE "SSourceControlConflict"

namespace SSourceControlConflictWarningWidgetDefs
{
	const FName ColumnID_AssetLabel("Asset");
	const FName ColumnID_AssetClassLabel("AssetClass");
	const FName ColumnID_UserNameLabel("UserName");
}

void SSourceControlCheckedOutDialog::Construct(const FArguments& InArgs)
{
	ParentFrame = InArgs._ParentWindow;
	SortByColumn = SSourceControlConflictWarningWidgetDefs::ColumnID_AssetLabel;
	SortMode = EColumnSortMode::Ascending;

	ListViewItems.Reserve(InArgs._Items.Num());
	for (const auto & Item : InArgs._Items)
	{
		ListViewItems.Add(MakeShared<FFileTreeItem>(Item));
	}

	TSharedPtr<SHeaderRow> HeaderRowWidget;
	HeaderRowWidget = SNew(SHeaderRow);

	bool bShowColumnAssetName = InArgs._ShowColumnAssetName;
	bool bShowColumnAssetClass = InArgs._ShowColumnAssetClass;
	bool bShowColumnUserName = InArgs._ShowColumnUserName;

	if (bShowColumnUserName)
	{
		bool bAnyCheckedOut = false;
		for (auto& Item : ListViewItems)
		{
			if (!Item->GetCheckedOutByUser().IsEmpty())
			{
				bAnyCheckedOut = true;
				break;
			}
		}
		bShowColumnUserName = bAnyCheckedOut;
	}

	if (bShowColumnAssetName)
	{
		HeaderRowWidget->AddColumn(
			SHeaderRow::Column(SSourceControlConflictWarningWidgetDefs::ColumnID_AssetLabel)
			.DefaultLabel(LOCTEXT("AssetColumnLabel", "Asset Name"))
			.SortMode(this, &SSourceControlCheckedOutDialog::GetColumnSortMode, SSourceControlConflictWarningWidgetDefs::ColumnID_AssetLabel)
			.OnSort(this, &SSourceControlCheckedOutDialog::OnColumnSortModeChanged)
			.FillWidth(0.5f)
		);
	}

	if (bShowColumnAssetName)
	{
		HeaderRowWidget->AddColumn(
			SHeaderRow::Column(SSourceControlConflictWarningWidgetDefs::ColumnID_AssetClassLabel)
			.DefaultLabel(LOCTEXT("AssetClassLabel", "Asset Class"))
			.FillWidth(0.5f)
		);
	}

	if (bShowColumnUserName)
	{
		HeaderRowWidget->AddColumn(
			SHeaderRow::Column(SSourceControlConflictWarningWidgetDefs::ColumnID_UserNameLabel)
			.DefaultLabel(LOCTEXT("UserNameLabel", "User Name"))
			.FillWidth(0.25f)
		);
	}

	TSharedPtr<SHorizontalBox> ButtonsBox = SNew(SHorizontalBox);

	bool bShowCheckBox = !InArgs._CheckBoxText.IsEmpty();
	if (bShowCheckBox)
	{
		CheckBox = SNew(SCheckBox);
		CheckBox->SetIsChecked(ECheckBoxState::Checked);

		ButtonsBox->AddSlot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5, 0)
				[
					CheckBox.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5, 5)
				[
					SNew(STextBlock)
					.Text(InArgs._CheckBoxText)
				]
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.Padding(5, 0)
			[
				SAssignNew(CloseButton, SPrimaryButton)
				.Text(InArgs._CloseText)
				.OnClicked(this, &SSourceControlCheckedOutDialog::CloseClicked)
			]
		];
	}
	else
	{
		ButtonsBox->AddSlot()
		.AutoWidth()
		.Padding(5, 0)
		[
			SAssignNew(CloseButton, SPrimaryButton)
			.Text(InArgs._CloseText)
			.OnClicked(this, &SSourceControlCheckedOutDialog::CloseClicked)
		];
	}

	TSharedPtr<SVerticalBox> Contents;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(16.f)
		[
			SAssignNew(Contents, SVerticalBox)
		]
	];

	Contents->AddSlot()
	.AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		.Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.WarningWithColor.Large"))
		]
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(SRichTextBlock)
			.DecoratorStyleSet(&FAppStyle::Get())
			.Text(InArgs._MessageText)
			.AutoWrapText(true)
		]
	];

	Contents->AddSlot()
	.Padding(0.0f, 16.0f, 0.0f, 0.0f)
	[
		SNew(SBorder)
		.Visibility_Lambda([this]() -> EVisibility
		{
			return ListViewItems.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SBox)
			.HeightOverride(200.0f)
			.WidthOverride(800.0f)
			[
				SAssignNew(ListView, SListView<FFileTreeItemRef>)
				.ListItemsSource(&ListViewItems)
				.OnGenerateRow(this, &SSourceControlCheckedOutDialog::OnGenerateRowForList)
				.HeaderRow(HeaderRowWidget)
				.SelectionMode(ESelectionMode::Single)
			]
		]
	];

	Contents->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 16.0f, 0.0f, 0.0f)
	.HAlign(bShowCheckBox ? HAlign_Fill : HAlign_Right)
	.VAlign(VAlign_Bottom)
	[
		ButtonsBox.ToSharedRef()
	];

	RequestSort();
}

TSharedRef<ITableRow> SSourceControlCheckedOutDialog::OnGenerateRowForList(FFileTreeItemRef InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<ITableRow> Row =
		SNew(SSourceControlCheckedOutDialogListRow, OwnerTable)
		.Item(InItem);

	return Row;
}

EColumnSortMode::Type SSourceControlCheckedOutDialog::GetColumnSortMode(const FName ColumnId) const
{
	if (SortByColumn != ColumnId)
	{
		return EColumnSortMode::None;
	}

	return SortMode;
}

void SSourceControlCheckedOutDialog::OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode)
{
	SortByColumn = ColumnId;
	SortMode = InSortMode;

	RequestSort();
}

void SSourceControlCheckedOutDialog::RequestSort()
{
	SortTree();

	ListView->RequestListRefresh();
}

void SSourceControlCheckedOutDialog::SortTree()
{
	if (SortByColumn == SSourceControlConflictWarningWidgetDefs::ColumnID_AssetLabel)
	{
		if (SortMode == EColumnSortMode::Ascending)
		{
			ListViewItems.Sort([this](const FFileTreeItemRef& A, const FFileTreeItemRef& B) {
				return A->GetAssetName().CompareTo(B->GetAssetName()) < 0; });
		}
		else if (SortMode == EColumnSortMode::Descending)
		{
			ListViewItems.Sort([this](const FFileTreeItemRef& A, const FFileTreeItemRef& B) {
				return A->GetAssetName().CompareTo(B->GetAssetName()) >= 0; });
		}
	}
}

FReply SSourceControlCheckedOutDialog::CloseClicked()
{
	ParentFrame.Pin()->RequestDestroyWindow();

	return FReply::Handled();
}

FReply SSourceControlCheckedOutDialog::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if ((InKeyEvent.GetKey() == EKeys::Escape && CloseButton.IsValid()))
	{
		return CloseClicked();
	}

	return FReply::Unhandled();
}

void SSourceControlCheckedOutDialogListRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Item = InArgs._Item;

	SMultiColumnTableRow<FFileTreeItemRef>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
}

TSharedRef<SWidget> SSourceControlCheckedOutDialogListRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	const FMargin RowPadding(8, 2, 2, 2);

	TSharedPtr<SWidget> ItemContentWidget;
	if (ColumnName == SSourceControlConflictWarningWidgetDefs::ColumnID_AssetLabel)
	{
		ItemContentWidget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(RowPadding)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Item->GetAssetName())
				.ToolTipText(Item->GetPackageName())
			];
	}
	else if (ColumnName == SSourceControlConflictWarningWidgetDefs::ColumnID_AssetClassLabel)
	{
		ItemContentWidget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(RowPadding)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Item->GetAssetTypeName())
				.ToolTipText(Item->GetAssetType())
			];
	}
	else if (ColumnName == SSourceControlConflictWarningWidgetDefs::ColumnID_UserNameLabel)
	{
		ItemContentWidget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(RowPadding)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Item->GetCheckedOutByUser())
				.ToolTipText(Item->GetCheckedOutByUser())
			];
	}

	return ItemContentWidget.ToSharedRef();
}

#undef LOCTEXT_NAMESPACE
