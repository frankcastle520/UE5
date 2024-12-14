// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compatibility/SceneOutlinerRowHandleColumn.h"

#include "SortHelper.h"
#include "TedsOutlinerItem.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "TedsTableViewerColumn.h"

#define LOCTEXT_NAMESPACE "SceneOutlinerRowHandleColumn"

FSceneOutlinerRowHandleColumn::FSceneOutlinerRowHandleColumn(ISceneOutliner& SceneOutliner)
	: WeakSceneOutliner(StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared()))
{
	using namespace UE::Editor::DataStorage;
	auto AssignWidgetToColumn = [this](TUniquePtr<FTypedElementWidgetConstructor> Constructor, TConstArrayView<TWeakObjectPtr<const UScriptStruct>>)
	{
		TSharedPtr<FTypedElementWidgetConstructor> WidgetConstructor(Constructor.Release());
		TableViewerColumn = MakeShared<FTedsTableViewerColumn>(TEXT("Row Handle"), WidgetConstructor);
		return false;
	};
	
	IEditorDataStorageUiProvider* StorageUi = GetMutableDataStorageFeature<IEditorDataStorageUiProvider>(UiFeatureName);
	checkf(StorageUi, TEXT("FSceneOutlinerRowHandleColumn created before data storage interfaces were initialized."))

	StorageUi->CreateWidgetConstructors(TEXT("General.Cell.RowHandle"), UE::Editor::DataStorage::FMetaDataView(), AssignWidgetToColumn);
}


FName FSceneOutlinerRowHandleColumn::GetID()
{
	static const FName ID("Row Handle");
	return ID;
}

FName FSceneOutlinerRowHandleColumn::GetColumnID()
{
	return GetID();
}

SHeaderRow::FColumn::FArguments FSceneOutlinerRowHandleColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetID())
	.FillWidth(2)
	.HeaderComboVisibility(EHeaderComboVisibility::OnHover);
}

const TSharedRef<SWidget> FSceneOutlinerRowHandleColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	auto SceneOutliner = WeakSceneOutliner.Pin();
	check(SceneOutliner.IsValid());

	if (const UE::Editor::Outliner::FTedsOutlinerTreeItem* OutlinerTreeItem = TreeItem->CastTo<UE::Editor::Outliner::FTedsOutlinerTreeItem>())
	{
		const UE::Editor::DataStorage::RowHandle RowHandle = OutlinerTreeItem->GetRowHandle();

		if(TSharedPtr<SWidget> Widget = TableViewerColumn->ConstructRowWidget(RowHandle))
		{
			return Widget.ToSharedRef();
		}
	}
	return SNullWidget::NullWidget;
}

void FSceneOutlinerRowHandleColumn::PopulateSearchStrings(const ISceneOutlinerTreeItem& Item, TArray<FString>& OutSearchStrings) const
{
	if (const UE::Editor::Outliner::FTedsOutlinerTreeItem* OutlinerTreeItem = Item.CastTo<UE::Editor::Outliner::FTedsOutlinerTreeItem>())
	{
		OutSearchStrings.Add(LexToString<FString>(OutlinerTreeItem->GetRowHandle()));
	}

}

void FSceneOutlinerRowHandleColumn::SortItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems, const EColumnSortMode::Type SortMode) const
{
	FSceneOutlinerSortHelper<UE::Editor::DataStorage::RowHandle>()
		/** Sort by type first */
		.Primary([this](const ISceneOutlinerTreeItem& Item)
		{
			if (const UE::Editor::Outliner::FTedsOutlinerTreeItem* OutlinerTreeItem = Item.CastTo<UE::Editor::Outliner::FTedsOutlinerTreeItem>())
			{
				return OutlinerTreeItem->GetRowHandle();
			}

			return UE::Editor::DataStorage::InvalidRowHandle;
		}, SortMode)
		.Sort(OutItems);
}

#undef LOCTEXT_NAMESPACE
