// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsOutlinerImpl.h"

#include "SSceneOutliner.h"
#include "TedsTableViewerUtils.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageCompatibilityInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Columns/SlateDelegateColumns.h"
#include "Compatibility/SceneOutlinerRowHandleColumn.h"
#include "TedsOutlinerFilter.h"
#include "TedsOutlinerItem.h"
#include "Columns/TedsOutlinerColumns.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementOverrideColumns.h"
#include "Elements/Columns/TypedElementSelectionColumns.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Filters/FilterBase.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "TedsOutliner"

namespace UE::Editor::Outliner
{
namespace QueryUtils
{
	static bool CanDisplayRow(DataStorage::IQueryContext& Context, const FTedsOutlinerColumn& TedsOutlinerColumn, DataStorage::RowHandle Row, SSceneOutliner& SceneOutliner)
	{
		/*
		 * Don't display widgets that are created for rows in this table viewer. Widgets are only created for rows that are currently visible, so if we
		 * display the rows for them we are now adding/removing rows to the table viewer based on currently visible rows. But adding rows can cause
		 * scrolling and change the currently visible rows which in turn again adds/removes widget rows. This chain keeps continuing which can cause
		 * flickering/scrolling issues in the table viewer.
		 */
		if (Context.HasColumn<FTypedElementSlateWidgetReferenceColumn>(Row))
		{
			// Check if this widget row belongs to the same table viewer it is being displayed in
			if (const TSharedPtr<ISceneOutliner> TableViewer = TedsOutlinerColumn.Outliner.Pin())
			{
				return &SceneOutliner != TableViewer.Get();
			}
			
		}
		return true;
	}

	static bool HasItemParentChanged(DataStorage::IQueryContext& Context, DataStorage::RowHandle Row, DataStorage::RowHandle ParentRowHandle, SSceneOutliner& SceneOutliner)
	{
		const FSceneOutlinerTreeItemPtr Item = SceneOutliner.GetTreeItem(Row, true);

		// If the item doesn't exist, it doesn't make sense to say its parent changed
		if (!Item)
		{
			return false;
		}
										
		const FSceneOutlinerTreeItemPtr ParentItem = Item->GetParent();

		// If the item doesn't have a parent, but ParentRowHandle is valid: The item just got added a parent so we want to dirty it
		if (!ParentItem)
		{
			return Context.IsRowAvailable(ParentRowHandle);
		}
										
		const FTedsOutlinerTreeItem* TedsParentItem = ParentItem->CastTo<FTedsOutlinerTreeItem>();

		if (TedsParentItem)
		{
			// return true if the row handle of the parent item doesn't match what we are given, i.e the parent has changed
			return TedsParentItem->GetRowHandle() != ParentRowHandle;
		}

		return false;
	};
}
FTedsOutlinerImpl::FTedsOutlinerImpl(const FTedsOutlinerParams& InParams, ISceneOutlinerMode* InMode)
	: CreationParams(InParams)
	, CellWidgetPurposes(InParams.CellWidgetPurposes)
	, InitialQueryDescription(InParams.QueryDescription)
	, HierarchyData(InParams.HierarchyData)
	, SelectionSetName(InParams.SelectionSetOverride)
	, SceneOutlinerMode(InMode)
	, SceneOutliner(InParams.SceneOutliner)
{
	// Initialize the TEDS constructs
	using namespace UE::Editor::DataStorage;
	Storage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
	StorageUi = GetMutableDataStorageFeature<IEditorDataStorageUiProvider>(UiFeatureName);
	StorageCompatibility = GetMutableDataStorageFeature<IEditorDataStorageCompatibilityProvider>(CompatibilityFeatureName);
}

void FTedsOutlinerImpl::CreateFilterQueries()
{
	using namespace UE::Editor::DataStorage::Queries;

	if (CreationParams.bUseDefaultTedsFilters)
	{
		// Create separate categories for columns and tags
		TSharedRef<FFilterCategory> TedsColumnFilterCategory = MakeShared<FFilterCategory>(LOCTEXT("TedsColumnFilters", "TEDS Columns"), LOCTEXT("TedsColumnFiltersTooltip", "Filter by TEDS columns"));
		TSharedRef<FFilterCategory> TedsTagFilterCategory = MakeShared<FFilterCategory>(LOCTEXT("TedsTagFilters", "TEDS Tags"), LOCTEXT("TedsTagFiltersTooltip", "Filter by TEDS Tags"));

		const UStruct* TedsColumn = DataStorage::FColumn::StaticStruct();
		const UStruct* TedsTag = DataStorage::FTag::StaticStruct();

		// Grab all UStruct types to see if they derive from FColumn or FTag
		ForEachObjectOfClass(UScriptStruct::StaticClass(), [&](UObject* Obj)
		{
			if (UScriptStruct* Struct = Cast<UScriptStruct>(Obj))
			{
				if (Struct->IsChildOf(TedsColumn) || Struct->IsChildOf(TedsTag))
				{
					// Create a query description to filter for this tag/column
					DataStorage::FQueryDescription FilterQueryDesc =
					Select()
					.Where()
						.All(Struct)
					.Compile();

					// Create the filter
					TSharedRef<FTedsOutlinerFilter> TedsFilter = MakeShared<FTedsOutlinerFilter>(Struct->GetFName(), Struct->GetDisplayNameText(),
						Struct->IsChildOf(TedsColumn) ? TedsColumnFilterCategory : TedsTagFilterCategory, AsShared(), FilterQueryDesc);
					SceneOutliner->AddFilterToFilterBar(TedsFilter);
				}
			}
		});
	}

	// Custom filters input by the user
	TSharedRef<FFilterCategory> CustomFiltersCategory = MakeShared<FFilterCategory>(LOCTEXT("TedsFilters", "TEDS Custom Filters"), LOCTEXT("TedsFiltersTooltip", "Filter by custom TEDS queries"));

	for(const TPair<FName, const DataStorage::FQueryDescription>& FilterQuery : CreationParams.FilterQueries)
	{
		// TEDS-Outliner TODO: Custom filters need a localizable display name instead of using the FName, but we need to change how they are added first
		// to see if it can be consolidated with the SFilterBar API
		TSharedRef<FTedsOutlinerFilter> TedsFilter = MakeShared<FTedsOutlinerFilter>(FilterQuery.Key, FText::FromName(FilterQuery.Key), CustomFiltersCategory, AsShared(), FilterQuery.Value);
		SceneOutliner->AddFilterToFilterBar(TedsFilter);
	
	}
}

void FTedsOutlinerImpl::Init()
{
	CreateFilterQueries();
	
	if (SelectionSetName.IsSet())
	{
		// Ticker for selection updates so we don't fire the delegate multiple times in one frame for multi select
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
				[this](float DeltaTimeInSeconds)
				{
					Tick();
					return true;
				}));
	}
}

FTedsOutlinerImpl::~FTedsOutlinerImpl()
{
	UnregisterQueries();
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
}

FTedsOutlinerImpl::FIsItemCompatible& FTedsOutlinerImpl::IsItemCompatible()
{
	return IsItemCompatibleWithTeds;
}

void FTedsOutlinerImpl::SetSelection(const TArray<DataStorage::RowHandle>& InSelectedRows)
{
	if (!SelectionSetName.IsSet())
	{
		return;
	}
	
	ClearSelection();

	for(DataStorage::RowHandle Row : InSelectedRows)
	{
		Storage->AddColumn(Row, FTypedElementSelectionColumn{ .SelectionSet = SelectionSetName.GetValue() });
	}
}

TSharedRef<SWidget> FTedsOutlinerImpl::CreateLabelWidgetForItem(DataStorage::RowHandle InRowHandle, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) const
{
	// Find the best matching label widget for a given list of columns
	auto CreateWidgetConstructor = [this](TArray<TWeakObjectPtr<const UScriptStruct>>& ColumnTypes) -> TSharedPtr<FTypedElementWidgetConstructor>
	{
		TSharedPtr<FTypedElementWidgetConstructor> OutWidgetConstructorPtr;

		bool bFoundWidget = false;

		static TArray<FName> Purposes{TEXT("SceneOutliner.RowLabel"), TEXT("General.RowLabel")};

		for(const FName& Purpose : Purposes)
		{
			StorageUi->CreateWidgetConstructors(Purpose, IEditorDataStorageUiProvider::EMatchApproach::LongestMatch, ColumnTypes, {},
					[&OutWidgetConstructorPtr, ColumnTypes, &bFoundWidget](
					TUniquePtr<FTypedElementWidgetConstructor> CreatedConstructor, 
					TConstArrayView<TWeakObjectPtr<const UScriptStruct>> MatchedColumnTypes)
					{
						OutWidgetConstructorPtr = TSharedPtr<FTypedElementWidgetConstructor>(CreatedConstructor.Release());
						bFoundWidget = true;
					
						// Either this was the exact match so no need to search further or the longest possible chain didn't match so the next ones will 
						// always be shorter in both cases just return.
						return false;
					});

			if(bFoundWidget)
			{
				break;
			}
		}
		return OutWidgetConstructorPtr;
	};
	
	using namespace DataStorage::Queries;
	// Query description to pass as metadata to allow the label column to be writable
	static FQueryDescription MetaDataQueryDescription = Select().ReadWrite<FTypedElementLabelColumn>().Where().Compile();

	// Create a widget for this row using the given widget constructor
	auto CreateWidget = [InRowHandle, this, &InRow](const TSharedPtr<FTypedElementWidgetConstructor>& WidgetConstructor) -> TSharedPtr<SWidget>
	{
		// Create metadata for the query
		FQueryMetaDataView QueryMetaDataView(MetaDataQueryDescription);

		RowHandle UiRowHandle = Storage->AddRow(Storage->FindTable(DataStorage::TableViewerUtils::GetWidgetTableName()));

		if (FTypedElementRowReferenceColumn* RowReference = Storage->GetColumn<FTypedElementRowReferenceColumn>(UiRowHandle))
		{
			RowReference->Row = InRowHandle;
		}

		Storage->AddColumn(UiRowHandle, FTedsOutlinerColumn{.Outliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner->AsShared())});
		
		TSharedPtr<SWidget> Widget = StorageUi->ConstructWidget(UiRowHandle, *WidgetConstructor, QueryMetaDataView);
		
		if (FExternalWidgetSelectionColumn* ExternalWidgetSelectionColumn = Storage->GetColumn<FExternalWidgetSelectionColumn>(UiRowHandle))
		{
			ExternalWidgetSelectionColumn->IsSelected = FIsSelected::CreateSP(&InRow, &STableRow<FSceneOutlinerTreeItemPtr>::IsSelectedExclusively);
		}
		return Widget;
		
	};

	// Get all the columns on the given row
	TArray<TWeakObjectPtr<const UScriptStruct>> Columns;
	Storage->ListColumns(InRowHandle, [&Columns](const UScriptStruct& ColumnType)
	{
		Columns.Add(&ColumnType);
	});

	// Create the label widget
	if(const TSharedPtr<FTypedElementWidgetConstructor> WidgetConstructor = CreateWidgetConstructor(Columns))
	{
		if(const TSharedPtr<SWidget> Widget = CreateWidget(WidgetConstructor))
		{
			return SNew(SBox)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						Widget.ToSharedRef()
					];
		}
	}

	return SNullWidget::NullWidget;
}

void FTedsOutlinerImpl::AppendQuery(DataStorage::FQueryDescription& Query1, const DataStorage::FQueryDescription& Query2)
{
	// TEDS-Outliner TODO: We simply discard duplicate types for now but we probably want a more robust system to detect duplicates and conflicting conditions
	for(int32 i = 0; i < Query2.ConditionOperators.Num(); ++i)
	{
		// Make sure we don't add duplicate conditions
		DataStorage::FQueryDescription::FOperator* FoundCondition = Query1.ConditionOperators.FindByPredicate([&Query2, i](const DataStorage::FQueryDescription::FOperator& Op)
		{
			return Op.Type == Query2.ConditionOperators[i].Type;
		});

		// We also can't have a duplicate selection type and condition
		TWeakObjectPtr<const UScriptStruct>* FoundSelection = Query1.SelectionTypes.FindByPredicate([&Query2, i](const TWeakObjectPtr<const UScriptStruct>& Selection)
		{
			return Selection == Query2.ConditionOperators[i].Type;
		});

		if (!FoundCondition && !FoundSelection)
		{
			Query1.ConditionOperators.Add(Query2.ConditionOperators[i]);
			Query1.ConditionTypes.Add(Query2.ConditionTypes[i]);
		}
	}
}

void FTedsOutlinerImpl::AddExternalQuery(FName QueryName, const DataStorage::FQueryDescription& InQueryDescription)
{
	ExternalQueries.Emplace(QueryName, InQueryDescription);

	RecompileQueries();
}

void FTedsOutlinerImpl::RemoveExternalQuery(FName QueryName)
{
	ExternalQueries.Remove(QueryName);
}

void FTedsOutlinerImpl::AppendExternalQueries(DataStorage::FQueryDescription& OutQuery)
{
	for(const TPair<FName, DataStorage::FQueryDescription>& ExternalQuery : ExternalQueries)
	{
		AppendQuery(OutQuery, ExternalQuery.Value);
	}
}


bool FTedsOutlinerImpl::CanDisplayRow(DataStorage::RowHandle ItemRowHandle) const
{
	/*
	 * Don't display widgets that are created for rows in this table viewer. Widgets are only created for rows that are currently visible, so if we
	 * display the rows for them we are now adding/removing rows to the table viewer based on currently visible rows. But adding rows can cause
	 * scrolling and change the currently visible rows which in turn again adds/removes widget rows. This chain keeps continuing which can cause
	 * flickering/scrolling issues in the table viewer.
	 */
	if (Storage->HasColumns<FTypedElementSlateWidgetReferenceColumn>(ItemRowHandle))
	{
		// Check if this widget row belongs to the same table viewer it is being displayed in
		if (const FTedsOutlinerColumn* TedsOutlinerColumn = Storage->GetColumn<FTedsOutlinerColumn>(ItemRowHandle))
		{
			if (const TSharedPtr<ISceneOutliner> TableViewer = TedsOutlinerColumn->Outliner.Pin())
		{
				return SceneOutliner != TableViewer.Get();
			}
		}
	}
	return true;
}

void FTedsOutlinerImpl::CreateItemsFromQuery(TArray<FSceneOutlinerTreeItemPtr>& OutItems, ISceneOutlinerMode* InMode) const
{
	using namespace UE::Editor::DataStorage::Queries;
	
	TArray<RowHandle> Rows;
	
	DirectQueryCallback RowCollector = CreateDirectQueryCallbackBinding(
		[&Rows](IDirectQueryContext& Context)
		{
			TConstArrayView<RowHandle> ContextRows = Context.GetRowHandles();
			Rows.Append(ContextRows);
		});

	Storage->RunQuery(RowHandleQuery, RowCollector);
	
	for (const RowHandle& Row : Rows)
	{
		if (!CanDisplayRow(Row))
		{
			continue;
		}
		
		if (FSceneOutlinerTreeItemPtr TreeItem = InMode->CreateItemFor<FTedsOutlinerTreeItem>(FTedsOutlinerTreeItem(Row, AsShared()), false))
		{
			OutItems.Add(TreeItem);
		}
	}

}

void FTedsOutlinerImpl::CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const
{
	/* TEDS-Outliner TODO: This can probably be improved or optimized in the future
	 * 
	 * TEDS currently only supports one way lookup for parents, so to get the children
	 * for a given row we currently have to go through every row (that matches our populate query) with a parent column to check if the parent
	 * is our row.
	 * This has to be done recursively to grab our children, grandchildren and so on...
	 */

	// If there's no hierarchy data, there is no need to create children
	if (!HierarchyData.IsSet())
	{
		return;
	}

	using namespace UE::Editor::DataStorage::Queries;
	
	const FTedsOutlinerTreeItem* TedsTreeItem = Item->CastTo<FTedsOutlinerTreeItem>();

	// If this item is not a TEDS item, we are not handling it
	if (!TedsTreeItem)
	{
		return;
	}
		
	RowHandle ItemRowHandle = TedsTreeItem->GetRowHandle();

	if(!Storage->IsRowAssigned(ItemRowHandle))
	{
		return;
	}


	TSet<RowHandle> MatchedRowsWithParentColumn;
	
	// Collect all entities that are owned by our entity
	DirectQueryCallback ChildRowCollector = CreateDirectQueryCallbackBinding(
	[&MatchedRowsWithParentColumn] (const IDirectQueryContext& Context)
	{
		MatchedRowsWithParentColumn.Append(Context.GetRowHandles());
	});

	Storage->RunQuery(ChildRowHandleQuery, ChildRowCollector);

	TArray<RowHandle> ChildItems;

	// Recursively get the children for each entity
	TFunction<void(RowHandle)> GetChildrenRecursive = [&ChildItems, &MatchedRowsWithParentColumn, DataStorage = Storage, &GetChildrenRecursive, InHierarchyData = HierarchyData]
	(RowHandle EntityRowHandle) -> void
	{
		for(RowHandle ChildEntityRowHandle : MatchedRowsWithParentColumn)
		{
			void* ParentColumnData = DataStorage->GetColumnData(ChildEntityRowHandle, InHierarchyData.GetValue().HierarchyColumn);

			if (ensureMsgf(ParentColumnData, TEXT("We should always the a parent column since we only grabbed rows with those ")))
			{
				// Get the parent row handle
				const RowHandle ParentRowHandle = InHierarchyData.GetValue().GetParent.Execute(ParentColumnData);
				
				// Check if this entity is owned by the entity we are looking children for
				if (ParentRowHandle == EntityRowHandle)
				{
					ChildItems.Add(ChildEntityRowHandle);

					// Recursively look for children of this item
					GetChildrenRecursive(ChildEntityRowHandle);
				}

			}
			
		}
	};
		
	GetChildrenRecursive(ItemRowHandle);

	// Actually create the items for the child entities 
	for (RowHandle ChildItemRowHandle : ChildItems)
	{
		if (!CanDisplayRow(ChildItemRowHandle))
		{
			continue;
		}
		
		if (FSceneOutlinerTreeItemPtr ChildActorItem = SceneOutlinerMode->CreateItemFor<FTedsOutlinerTreeItem>(FTedsOutlinerTreeItem(ChildItemRowHandle, AsShared())))
		{
			OutChildren.Add(ChildActorItem);
		}
	}
}

DataStorage::RowHandle FTedsOutlinerImpl::GetParentRow(DataStorage::RowHandle InRowHandle)
{
	// No parent if there is no hierarchy data specified
	if (!HierarchyData.IsSet())
	{
		return DataStorage::InvalidRowHandle;
	}
	
	// If this entity does not have a parent entity, return InvalidRowHandle
	void* ParentColumnData = Storage->GetColumnData(InRowHandle, HierarchyData.GetValue().HierarchyColumn);
	
	if (!ParentColumnData)
	{
		return DataStorage::InvalidRowHandle;
	}

	// If the parent is invalid for some reason, return InvalidRowHandle
	const DataStorage::RowHandle ParentRowHandle = HierarchyData.GetValue().GetParent.Execute(ParentColumnData);
	
	if (!Storage->IsRowAvailable(ParentRowHandle))
	{
		return DataStorage::InvalidRowHandle;
	}
	
	if (!CanDisplayRow(ParentRowHandle))
	{
		return DataStorage::InvalidRowHandle;
	}

	return ParentRowHandle;
}

void FTedsOutlinerImpl::OnItemAdded(DataStorage::RowHandle ItemRowHandle)
{
	if (!CanDisplayRow(ItemRowHandle))
	{
		return;
	}
	
	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::Added;
	EventData.Items.Add(SceneOutlinerMode->CreateItemFor<FTedsOutlinerTreeItem>(FTedsOutlinerTreeItem(ItemRowHandle, AsShared())));
	HierarchyChangedEvent.Broadcast(EventData);
}

void FTedsOutlinerImpl::OnItemRemoved(DataStorage::RowHandle ItemRowHandle)
{
	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::Removed;
	EventData.ItemIDs.Add(ItemRowHandle);
	HierarchyChangedEvent.Broadcast(EventData);
}

void FTedsOutlinerImpl::RecompileQueries()
{
	using namespace UE::Editor::DataStorage::Queries;

	UnregisterQueries();

	if (!InitialQueryDescription.IsSet())
	{
		return;
	}

	// Our final query to collect rows to populate the Outliner - currently the same as the initial query the user provided
	FQueryDescription FinalQueryDescription(InitialQueryDescription.Get());

	// Add the filters the user has active to the query
	AppendExternalQueries(FinalQueryDescription);

	// Row to track addition of rows to the Outliner
	FQueryDescription RowAdditionQueryDescription =
		Select(
			TEXT("Add Row to Outliner"),
			FObserver::OnAdd<FTypedElementLabelColumn>().SetExecutionMode(EExecutionMode::GameThread),
			[this](IQueryContext& Context, DataStorage::RowHandle Row)
			{
				OnItemAdded(Row);
			})
		.Compile();

	// Add the conditions from FinalQueryDescription to ensure we are tracking addition of the rows the user requested
	AppendQuery(RowAdditionQueryDescription, FinalQueryDescription);
	
	// Row to track removal of rows from the Outliner
	FQueryDescription RowRemovalQueryDescription =
		Select(
				TEXT("Remove Row from Outliner"),
				FObserver::OnRemove<FTypedElementLabelColumn>().SetExecutionMode(EExecutionMode::GameThread),
				[this](IQueryContext& Context, DataStorage::RowHandle Row)
				{
					OnItemRemoved(Row);
				})
			.Compile();

	// Add the conditions from FinalQueryDescription to ensure we are tracking removal of the rows the user requested
	AppendQuery(RowRemovalQueryDescription, FinalQueryDescription);

	// Queries to track parent info, only required if we have hierarchy data
	if (HierarchyData.IsSet())
	{
		const UScriptStruct* ParentColumnType = HierarchyData.GetValue().HierarchyColumn;
		
		// Query to get all rows that match our conditions with a parent column (i.e all child rows)
		FQueryDescription ChildHandleQueryDescription =
							Select()
							.Where()
								.All(ParentColumnType)
							.Compile();

		// Add the conditions from FinalQueryDescription to ensure we are tracking removal of the rows the user requested
		AppendQuery(ChildHandleQueryDescription, FinalQueryDescription);

		FQueryDescription UpdateParentQueryDescription =
			Select(
			TEXT("Update item parent"),
			FProcessor(EQueryTickPhase::DuringPhysics, Storage->GetQueryTickGroupName(EQueryTickGroups::Update))
				.SetExecutionMode(EExecutionMode::GameThread),
			[this](IQueryContext& Context, const DataStorage::RowHandle* Rows, const FTedsOutlinerColumn* TedsOutlinerColumns)
			{
				const FTableRowParentColumn* ParentColumnBegin = Context.GetColumn<FTableRowParentColumn>();
				
				for (int32 RowIndex = 0; RowIndex < (int32)Context.GetRowCount(); ++RowIndex)
				{
					RowHandle Row = Rows[RowIndex];
					DataStorage::RowHandle ParentRowHandle = InvalidRowHandle;
					if (ParentColumnBegin)
					{
						ParentRowHandle = ParentColumnBegin[RowIndex].Parent;
					}
									
					if (QueryUtils::HasItemParentChanged(Context, Row, ParentRowHandle, *SceneOutliner))
					{
						const FTedsOutlinerColumn& TedsOutlinerColumn = TedsOutlinerColumns[RowIndex];
						if (!QueryUtils::CanDisplayRow(Context, TedsOutlinerColumn, Row, *SceneOutliner))
						{
							return;
						}
						
						FSceneOutlinerHierarchyChangedData EventData;
						EventData.Type = FSceneOutlinerHierarchyChangedData::Moved;
						EventData.ItemIDs.Add(Row);
						HierarchyChangedEvent.Broadcast(EventData);
					}
				}
			})
			.ReadOnly<FTableRowParentColumn>(EOptional::Yes)
		.Where()
			.All<FTypedElementSyncFromWorldTag>()
		.Compile();
		
		// Add the conditions from FinalQueryDescription to ensure we are the rows the user requested
		AppendQuery(UpdateParentQueryDescription, FinalQueryDescription);

		ChildRowHandleQuery = Storage->RegisterQuery(MoveTemp(ChildHandleQueryDescription));
		UpdateParentQuery = Storage->RegisterQuery(MoveTemp(UpdateParentQueryDescription));
	}

	if (SelectionSetName.IsSet())
	{
		// Query to grab all selected rows
		FQueryDescription SelectedRowsQueryDescription =
						Select()
							.Where()
								.All<FTypedElementSelectionColumn>()
							.Compile();
	
		// Query to track when a row gets selected
		FQueryDescription SelectionAddedQueryDescription =
							Select(
							TEXT("Row selected"),
							FObserver::OnAdd<FTypedElementSelectionColumn>().SetExecutionMode(EExecutionMode::GameThread),
							[this](IQueryContext& Context, DataStorage::RowHandle Row)
							{
								bSelectionDirty = true;
							})
							.Compile();

		// Add the conditions from FinalQueryDescription to ensure we are the rows the user requested
		AppendQuery(SelectionAddedQueryDescription, FinalQueryDescription);

		// Query to track when a row gets deselected
		FQueryDescription SelectionRemovedQueryDescription =
							Select(
							TEXT("Row deselected"),
							FObserver::OnRemove<FTypedElementSelectionColumn>().SetExecutionMode(EExecutionMode::GameThread),
							[this](IQueryContext& Context, DataStorage::RowHandle Row)
							{
								bSelectionDirty = true;
							})
							.Compile();

		// Add the conditions from FinalQueryDescription to ensure we are the rows the user requested
		AppendQuery(SelectionRemovedQueryDescription, FinalQueryDescription);

		SelectedRowsQuery = Storage->RegisterQuery(MoveTemp(SelectedRowsQueryDescription));
		SelectionAddedQuery = Storage->RegisterQuery(MoveTemp(SelectionAddedQueryDescription));
		SelectionRemovedQuery = Storage->RegisterQuery(MoveTemp(SelectionRemovedQueryDescription));
	}
	
	RowHandleQuery = Storage->RegisterQuery(MoveTemp(FinalQueryDescription));
	RowAdditionQuery = Storage->RegisterQuery(MoveTemp(RowAdditionQueryDescription));
	RowRemovalQuery = Storage->RegisterQuery(MoveTemp(RowRemovalQueryDescription));

}

void FTedsOutlinerImpl::UnregisterQueries() const
{
	if (Storage)
	{
		Storage->UnregisterQuery(RowHandleQuery);
		Storage->UnregisterQuery(RowAdditionQuery);
		Storage->UnregisterQuery(RowRemovalQuery);
		Storage->UnregisterQuery(ChildRowHandleQuery);
		Storage->UnregisterQuery(UpdateParentQuery);
		Storage->UnregisterQuery(SelectedRowsQuery);
		Storage->UnregisterQuery(SelectionAddedQuery);
		Storage->UnregisterQuery(SelectionRemovedQuery);
	}
}

void FTedsOutlinerImpl::ClearSelection() const
{
	if (!SelectionSetName.IsSet())
	{
		return;
	}

	using namespace UE::Editor::DataStorage::Queries;

	TArray<RowHandle> RowsToRemoveSelectionColumn;

	// Query to remove the selection column from all rows that belong to this selection set
	DirectQueryCallback RowCollector = CreateDirectQueryCallbackBinding(
	[this, &RowsToRemoveSelectionColumn](const IDirectQueryContext& Context, const RowHandle* RowHandles)
	{
		const TConstArrayView<RowHandle> Rows(RowHandles, Context.GetRowCount());

		for(const RowHandle RowHandle : Rows)
		{
			if (const FTypedElementSelectionColumn* SelectionColumn = Storage->GetColumn<FTypedElementSelectionColumn>(RowHandle))
			{
				if (SelectionColumn->SelectionSet == SelectionSetName)
				{
					RowsToRemoveSelectionColumn.Add(RowHandle);
				}
			}
		}
	});

	Storage->RunQuery(SelectedRowsQuery, RowCollector);

	for(const RowHandle RowHandle : RowsToRemoveSelectionColumn)
	{
		Storage->RemoveColumn<FTypedElementSelectionColumn>(RowHandle);
	}

}

void FTedsOutlinerImpl::Tick()
{
	if (bSelectionDirty)
	{
		OnTedsOutlinerSelectionChanged.Broadcast();
		bSelectionDirty = false;
	}
}

IEditorDataStorageProvider* FTedsOutlinerImpl::GetStorage() const
{
	return Storage;
}

IEditorDataStorageUiProvider* FTedsOutlinerImpl::GetStorageUI() const
{
	return StorageUi;
}

IEditorDataStorageCompatibilityProvider* FTedsOutlinerImpl::GetStorageCompatibility() const
{
	return StorageCompatibility;
}

TOptional<FName> FTedsOutlinerImpl::GetSelectionSetName() const
{
	return SelectionSetName;
}

FTedsOutlinerImpl::FOnTedsOutlinerSelectionChanged& FTedsOutlinerImpl::OnSelectionChanged()
{
	return OnTedsOutlinerSelectionChanged;
}

ISceneOutlinerHierarchy::FHierarchyChangedEvent& FTedsOutlinerImpl::OnHierarchyChanged()
{
	return HierarchyChangedEvent;
}

const TOptional<FTedsOutlinerHierarchyData>& FTedsOutlinerImpl::GetHierarchyData()
{
	return HierarchyData;
}
} // namespace UE::Editor::Outliner

#undef LOCTEXT_NAMESPACE
