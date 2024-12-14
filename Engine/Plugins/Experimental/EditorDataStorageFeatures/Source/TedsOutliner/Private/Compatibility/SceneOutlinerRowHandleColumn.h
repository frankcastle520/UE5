// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneOutlinerFwd.h"
#include "ISceneOutliner.h"
#include "ISceneOutlinerColumn.h"

namespace UE::Editor::DataStorage
{
	class FTedsTableViewerColumn;
}

/**
 * This is a custom column for the TEDS Outliner/Table Viewer to display row handles for items. It is a special cased column instead of going through
 * the TEDS UI layer because row handles are not information stored in a TEDS column
 */
class FSceneOutlinerRowHandleColumn : public ISceneOutlinerColumn
{
public:
	FSceneOutlinerRowHandleColumn(ISceneOutliner& SceneOutliner);
	
	virtual ~FSceneOutlinerRowHandleColumn() {}

	static FName GetID();
	
	//////////////////////////////////////////////////////////////////////////
	// Begin ISceneOutlinerColumn Implementation
	virtual FName GetColumnID() override;
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;
	virtual void PopulateSearchStrings( const ISceneOutlinerTreeItem& Item, TArray< FString >& OutSearchStrings ) const override;
	virtual bool SupportsSorting() const override { return true; }
	virtual void SortItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems, const EColumnSortMode::Type SortMode) const override;
	// End ISceneOutlinerColumn Implementation
	//////////////////////////////////////////////////////////////////////////

private:
	TWeakPtr<ISceneOutliner> WeakSceneOutliner;

	// The Table Viewer column we are going to use internally to create the widget
	TSharedPtr<UE::Editor::DataStorage::FTedsTableViewerColumn> TableViewerColumn;
};
