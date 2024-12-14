// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsOutlinerItem.h"

#include "Elements/Columns/TypedElementLabelColumns.h"
#include "HAL/PlatformApplicationMisc.h"
#include "TedsOutlinerImpl.h"

#define LOCTEXT_NAMESPACE "TedsOutliner"

namespace UE::Editor::Outliner
{
const FSceneOutlinerTreeItemType FTedsOutlinerTreeItem::Type(&ISceneOutlinerTreeItem::Type);

FTedsOutlinerTreeItem::FTedsOutlinerTreeItem(const DataStorage::RowHandle& InRowHandle,
	const TSharedRef<const FTedsOutlinerImpl>& InTedsOutlinerImpl)
	: ISceneOutlinerTreeItem(Type)
	, RowHandle(InRowHandle)
	, TedsOutlinerImpl(InTedsOutlinerImpl)
{
	
}

bool FTedsOutlinerTreeItem::IsValid() const
{
	return true; // TEDS-Outliner TODO: check with TEDS if the item is valid?
}

FSceneOutlinerTreeItemID FTedsOutlinerTreeItem::GetID() const
{
	return FSceneOutlinerTreeItemID(RowHandle);
}

FString FTedsOutlinerTreeItem::GetDisplayString() const
{
	return TEXT("TEDS Item"); // TEDS-Outliner TODO: Used for searching by name, how to get this from TEDS
}

bool FTedsOutlinerTreeItem::CanInteract() const
{
	return true; // TEDS-Outliner TODO: check item constness from TEDS maybe?
}

TSharedRef<SWidget> FTedsOutlinerTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner,
	const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return TedsOutlinerImpl->CreateLabelWidgetForItem(RowHandle, InRow);
}

void FTedsOutlinerTreeItem::GenerateContextMenu(UToolMenu* Menu, SSceneOutliner& Outliner)
{
	FToolMenuSection& Section = Menu->AddSection("Copy", LOCTEXT("CopySection", "Copy"));

	Section.AddMenuEntry(
		"CopyRowHandle",
		LOCTEXT("CopyRowHandle_Title", "Copy row handle"),
		LOCTEXT("CopyRowHandle_Tooltip", "Copy the row handle of this row to the clipboard."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				const FString ClipboardString(LexToString<FString>(RowHandle));
				FPlatformApplicationMisc::ClipboardCopy(*ClipboardString);
			}),
			FCanExecuteAction()
		)
	);
}

DataStorage::RowHandle FTedsOutlinerTreeItem::GetRowHandle() const
{
	return RowHandle;
}
} // namespace UE::Editor::Outliner

#undef LOCTEXT_NAMESPACE