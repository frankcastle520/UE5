// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TedsTableViewerModel.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

namespace UE::Editor::DataStorage
{
	class STedsTableViewer;
	
	/** Widget that represents a row.  Generates widgets for each column on demand. */
	class STedsTableViewerRow
		: public SMultiColumnTableRow< TableViewerItemPtr >
	{

	public:

		SLATE_BEGIN_ARGS( STedsTableViewerRow ) {}

		/** The list item for this row */
		SLATE_ARGUMENT( TableViewerItemPtr, Item )

		SLATE_END_ARGS()
		
		/** Construct function for this widget */
		void Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedRef<FTedsTableViewerModel>& InTableViewerModel );

		/** Overridden from SMultiColumnTableRow.  Generates a widget for this column of the tree row. */
		virtual TSharedRef<SWidget> GenerateWidgetForColumn( const FName& ColumnName ) override;

	protected:

		TSharedPtr<FTedsTableViewerModel> TableViewerModel;
		TableViewerItemPtr Item;
	};
} // namespace UE::Editor::DataStorage
