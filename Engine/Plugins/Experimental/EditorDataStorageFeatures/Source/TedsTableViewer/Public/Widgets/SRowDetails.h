// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Elements/Common/TypedElementHandles.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class IEditorDataStorageProvider;
class IEditorDataStorageUiProvider;
struct FTypedElementWidgetConstructor;

namespace UE::Editor::DataStorage
{
	// A row in the SRowDetails widget that represents a column on the TEDS row we are viewing
	struct FRowDetailsItem
	{
		// The column we this row is displaying data for
		TWeakObjectPtr<const UScriptStruct> ColumnType;

		// Widget for the column
		TUniquePtr<FTypedElementWidgetConstructor> WidgetConstructor;
		
		RowHandle Row = InvalidRowHandle;
		RowHandle WidgetRow = InvalidRowHandle;

		FRowDetailsItem(const TWeakObjectPtr<const UScriptStruct>& InColumnType, TUniquePtr<FTypedElementWidgetConstructor> InWidgetConstructor,
			RowHandle InRow);
	};
	
	using RowDetailsItemPtr = TSharedPtr<FRowDetailsItem>;

	// A widget to display all the columns/tags on a given row
	class TEDSTABLEVIEWER_API SRowDetails : public SCompoundWidget
	{
	public:
		
		~SRowDetails() override = default;
		
		SLATE_BEGIN_ARGS(SRowDetails)
			: _ShowAllDetails(true)
		{}

			// Whether or not to show columns that don't have a dedicated widget to represent them
			SLATE_ARGUMENT(bool, ShowAllDetails)

			// Override for the default widget purposes used to create widgets for the columns
			SLATE_ARGUMENT(TArray<FName>, WidgetPurposesOverride)

		
		SLATE_END_ARGS()
		
		void Construct(const FArguments& InArgs);

		// Set the row to view
		void SetRow(RowHandle Row);

		// Clear the row to view
		void ClearRow();
		
	private:
		
		TSharedRef<ITableRow> CreateRow(RowDetailsItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable);

		TSharedPtr<SListView<RowDetailsItemPtr>> ListView;

		TArray<RowDetailsItemPtr> Items;

		IEditorDataStorageProvider* DataStorage = nullptr; 
		IEditorDataStorageUiProvider* DataStorageUi = nullptr;

		bool bShowAllDetails = true;

		TArray<FName> WidgetPurposes;

	};

	class SRowDetailsRow : public SMultiColumnTableRow<RowDetailsItemPtr>
	{
	public:
		
		SLATE_BEGIN_ARGS(SRowDetailsRow) {}
		
			SLATE_ARGUMENT(RowDetailsItemPtr, Item)

		SLATE_END_ARGS()
		
		void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView, IEditorDataStorageProvider* InDataStorage,
			IEditorDataStorageUiProvider* InDataStorageUi);
		
		TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

	private:
		RowDetailsItemPtr Item;
		IEditorDataStorageProvider* DataStorage = nullptr;
		IEditorDataStorageUiProvider* DataStorageUi = nullptr;
	};
} // namespace UE::Editor::DataStorage
