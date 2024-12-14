// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementHandles.h"
#include "Experimental/ContentBrowserViewExtender.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

class IEditorDataStorageProvider;

namespace UE::Editor::DataStorage
{
	class FQueryStackNode_RowView;
	class STedsTableViewer;
}

class FAssetViewItem;

namespace UE::Editor::ContentBrowser
{
	// A custom view for the content browser that uses the TEDS asset registry integration to display rows with widgets using TEDS UI
	class FTedsContentBrowserViewExtender : public IContentBrowserViewExtender
	{
	public:

		FTedsContentBrowserViewExtender();

		// IContentBrowserViewExtender interface
		virtual TSharedRef<SWidget> CreateView(TArray<TSharedPtr<FAssetViewItem>>* InItemsSource) override;
		virtual TArray<TSharedPtr<FAssetViewItem>> GetSelectedItems() override;
		virtual FOnSelectionChanged& OnSelectionChanged() override;
		virtual FOnContextMenuOpening& OnContextMenuOpened() override;
		virtual FOnItemScrolledIntoView& OnItemScrolledIntoView() override;
		virtual FOnMouseButtonClick& OnItemDoubleClicked() override;
		virtual FText GetViewDisplayName() override;
		virtual FText GetViewTooltipText() override;
		virtual void FocusList() override;
		virtual void SetSelection(const TSharedPtr<FAssetViewItem>& Item, bool bSelected, const ESelectInfo::Type SelectInfo)override;
		virtual void RequestScrollIntoView(const TSharedPtr<FAssetViewItem>& Item) override;
		virtual void ClearSelection() override;
		virtual bool IsRightClickScrolling() override;
		virtual void OnItemListChanged(TArray<TSharedPtr<FAssetViewItem>>* InItemsSource) override;
		// ~IContentBrowserViewExtender interface


		// Refresh the rows in the current view by syncing the the items source
		void RefreshRows(TArray<TSharedPtr<FAssetViewItem>>* InItemsSource);

		// Add a single row to the table viewer
		void AddRow(const TSharedPtr<FAssetViewItem>& Item);

		// Get the internal FAssetViewItem from a row handle
		TSharedPtr<FAssetViewItem> GetAssetViewItemFromRow(DataStorage::RowHandle Row);

		DataStorage::RowHandle GetRowFromAssetViewItem(const TSharedPtr<FAssetViewItem>& Item);

	private:

		// Ptr to the data storage interface
		IEditorDataStorageProvider* DataStorage;
		
		// The actual table viewer widget
		TSharedPtr<DataStorage::STedsTableViewer> TableViewer;
		
		// Query stack used by the table viewer
		TSharedPtr<DataStorage::FQueryStackNode_RowView> RowQueryStack;

		// The row handles of the items currently in the list
		TArray<DataStorage::RowHandle> Rows;

		// A map from row handle -> FAssetView item for lookups
		TMap<DataStorage::RowHandle, TWeakPtr<FAssetViewItem>> ContentBrowserItemMap;

		// Delegates fired when specific events happen on the list
		FOnSelectionChanged OnSelectionChangedDelegate;
		FOnContextMenuOpening OnContextMenuOpenedDelegate;
		FOnItemScrolledIntoView OnItemScrolledIntoViewDelegate;
		FOnMouseButtonClick OnItemDoubleClickedDelegate;
	};

	/**
	 * Implements the Teds Content Browser module.
	 */
	class FTedsContentBrowserModule
		: public IModuleInterface
	{
	public:

		FTedsContentBrowserModule() = default;
		
		static TSharedPtr<IContentBrowserViewExtender> CreateContentBrowserViewExtender();

		// IModuleInterface interface
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
	};
} // namespace UE::Editor::ContentBrowser
