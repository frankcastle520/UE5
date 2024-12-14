// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Elements/Common/TypedElementHandles.h"
#include "QueryStack/IQueryStackNode_Row.h"
#include "TedsTableViewerModel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class STableViewBase;
class SHeaderRow;
class STedsWidget;

namespace UE::Editor::DataStorage
{
	class FTedsTableViewerColumn;

	/*
	 * A table viewer widget can be used to show a visual representation of data in TEDS.
	 * The rows to display can be specified using a RowQueryStack, and the columns to display are directly input into the widget
	 * Example usage:
	 * 
	 *	SNew(STedsTableViewer)
     *		.QueryStack(MakeShared<UE::Editor::DataStorage::FQueryStackNode_RowView>(&Rows))
	 *		.Columns({FTypedElementLabelColumn::StaticStruct(), FTypedElementClassTypeInfoColumn::StaticStruct());
	 */
	class STedsTableViewer : public SCompoundWidget
	{
	public:
		
		// Delegate fired when the selection in the table viewer changes
		DECLARE_DELEGATE_OneParam(FOnSelectionChanged, RowHandle)

		SLATE_BEGIN_ARGS(STedsTableViewer)
			: _CellWidgetPurposes({TEXT("General.Cell")})
			, _ListSelectionMode(ESelectionMode::Type::Single)
		{
			
		}
		

		// Query Stack that will supply the rows to be displayed
		SLATE_ARGUMENT(TSharedPtr<IQueryStackNode_Row>, QueryStack)

		// The Columns that this table viewer will display
		// Table Viewer TODO: How do we specify column metadata (ReadOnly or ReadWrite)?
		SLATE_ARGUMENT(TArray<TWeakObjectPtr<const UScriptStruct>>, Columns)

		// The widget purposes to use to create the widgets
		SLATE_ARGUMENT(TArray<FName>, CellWidgetPurposes)
		
		// Delegate called when the selection changes
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)

		// The selection mode for the table viewer (single/multi etc)
		SLATE_ARGUMENT(ESelectionMode::Type, ListSelectionMode)

		// The message to show in place of the table viewer when there are no rows provided by the current query stack
		// Empty = simply show the column headers instead of a message
		SLATE_ATTRIBUTE(FText, EmptyRowsMessage)

		SLATE_END_ARGS()

	public:
		
		TEDSTABLEVIEWER_API void Construct(const FArguments& InArgs);

		// Clear the current list of columns being displayed and set it to the given list
		TEDSTABLEVIEWER_API void SetColumns(const TArray<TWeakObjectPtr<const UScriptStruct>>& Columns);

		// Add a custom column to display in the table viewer, that doesn't necessarily map to a Teds column
		TEDSTABLEVIEWER_API void AddCustomColumn(const TSharedRef<FTedsTableViewerColumn>& InColumn);

		// Execute the given callback for each row that is selected in the table viewer
		TEDSTABLEVIEWER_API void ForEachSelectedRow(TFunctionRef<void(RowHandle)> InCallback) const;

		// Get the row handle for the widget row the table viewer's contents are stored in
		TEDSTABLEVIEWER_API RowHandle GetWidgetRowHandle() const;
		
		// Select the given row in the table viewer
		TEDSTABLEVIEWER_API void SetSelection(RowHandle Row, bool bSelected, const ESelectInfo::Type SelectInfo) const;

		// Scroll the given row into view in the table viewer
		TEDSTABLEVIEWER_API void ScrollIntoView(RowHandle Row) const;

		TEDSTABLEVIEWER_API void ClearSelection() const;

	protected:
		
		TSharedRef<ITableRow> MakeTableRowWidget(TableViewerItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable) const;

		bool IsItemVisible(TableViewerItemPtr InItem) const;

		void AssignChildSlot();

		void RefreshColumnWidgets();

		void OnListSelectionChanged(TableViewerItemPtr Item, ESelectInfo::Type SelectInfo);

		void CreateTedsWidget();

	private:

		// The actual ListView widget that displays the rows
		TSharedPtr<SListView<TableViewerItemPtr>> ListView;

		// The actual header widget
		TSharedPtr<SHeaderRow> HeaderRowWidget;

		// Our model class
		TSharedPtr<FTedsTableViewerModel> Model;

		// Delegate fired when the selection changes
		FOnSelectionChanged OnSelectionChanged;

		// Wrapper Teds Widget around our contents so we can use Teds columns to specify behavior
		TSharedPtr<STedsWidget> TedsWidget;

		// The message to show in place of the table viewer when there are no rows provided by the current query stack
		TAttribute<FText> EmptyRowsMessage;
	};
}