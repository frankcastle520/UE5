// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "Delegates/Delegate.h"
#include "Elements/Common/TypedElementHandles.h"
#include "Templates/SharedPointer.h"
#include "TypedElementUITypes.h"
#include "UObject/NameTypes.h"

class IEditorDataStorageCompatibilityProvider;
class IEditorDataStorageUiProvider;
class IEditorDataStorageProvider;

namespace UE::Editor::DataStorage
{
	class FTedsTableViewerColumn;
	class IQueryStackNode_Row;

	// Typedef for an item in the table viewer
	using TableViewerItemPtr = FTedsRowHandle;

	// Model class for the TEDS Table Viewer that can be plugged into any widget that is a UI representation of data in TEDS
	// @see STedsTableViewer
	class FTedsTableViewerModel
	{
	public:

		DECLARE_DELEGATE_RetVal_OneParam(bool, FIsItemVisible, TableViewerItemPtr);
		DECLARE_MULTICAST_DELEGATE(FOnModelChanged);

		TEDSTABLEVIEWER_API FTedsTableViewerModel(const TSharedPtr<IQueryStackNode_Row>& RowQueryStack, const TArray<TWeakObjectPtr<const UScriptStruct>>& RequestedColumns,
			const TArray<FName>& CellWidgetPurposes, const FIsItemVisible& InIsItemVisibleDelegate);

		~FTedsTableViewerModel();

		// Get the items this table viewer is viewing
		TEDSTABLEVIEWER_API const TArray<TableViewerItemPtr>& GetItems() const;

		// Get the number of rows currently being observed
		TEDSTABLEVIEWER_API uint64 GetRowCount() const;
		
		// Get the number of columns being displayed
		TEDSTABLEVIEWER_API uint64 GetColumnCount() const;
		
		// Get a specific column that the table viewer is displaying by name
		TEDSTABLEVIEWER_API TSharedPtr<FTedsTableViewerColumn> GetColumn(const FName& ColumnName) const;

		// Execute a delegate for each column in the model
		TEDSTABLEVIEWER_API void ForEachColumn(const TFunctionRef<void(const TSharedRef<FTedsTableViewerColumn>&)>& Delegate) const;

		// Delegate when the item list changes
		TEDSTABLEVIEWER_API FOnModelChanged& GetOnModelChanged();
		
		// Clear the current list of columns being displayed and set it to the given list
		TEDSTABLEVIEWER_API void SetColumns(const TArray<TWeakObjectPtr<const UScriptStruct>>& InColumns);

		// Add a custom column to display in the table viewer, that doesn't necessarily map to a Teds column
		TEDSTABLEVIEWER_API void AddCustomColumn(const TSharedRef<FTedsTableViewerColumn>& InColumn);

		TEDSTABLEVIEWER_API IEditorDataStorageProvider* GetDataStorageInterface() const;

	protected:

		// Generate the actual columns to display in the UI using TEDS UI
		void GenerateColumns();

		// Check if the given row is currently visible in the UI
		bool IsRowVisible(RowHandle InRowHandle) const;

		bool Tick(float DeltaTime);

		void Refresh();

		// Check whether a row is allowed to be displayed in the table viewer
		bool IsRowDisplayable(RowHandle InRowHandle) const;

	private:

		// The row query stack used to supply the rows to display
		TSharedPtr<IQueryStackNode_Row> RowQueryStack;

		// The cached list of rows we are currently displaying
		TArray<TableViewerItemPtr> Items;

		// List of columns the table viewer is currently displaying
		TArray<TSharedRef<FTedsTableViewerColumn>> ColumnsView;

		// The initial TEDS columns the widget was requested to display
		TArray<TWeakObjectPtr<const UScriptStruct>> RequestedTedsColumns;

		// The widget purposes used to create widgets in this table viewer
		TArray<FName> CellWidgetPurposes;

		// Cached revision ID for the query stack used to check when the table viewer needs a refresh
		uint32 CachedRowQueryStackRevision = 0;

		// Delegate supplied by the widget to check if an item is visible in the UI currently
		FIsItemVisible IsItemVisible;

		FTSTicker::FDelegateHandle TickerHandle;

		// Delegate executed when the row list changes
		FOnModelChanged OnModelChanged;
		
		// Teds Constructs
		IEditorDataStorageProvider* Storage = nullptr;
		IEditorDataStorageUiProvider* StorageUi = nullptr;
		IEditorDataStorageCompatibilityProvider* StorageCompatibility = nullptr;
	};
} // namespace UE::Editor::DataStorage
