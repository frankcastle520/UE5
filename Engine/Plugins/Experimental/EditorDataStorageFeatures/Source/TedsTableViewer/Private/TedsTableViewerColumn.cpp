// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsTableViewerColumn.h"

#include "Columns/UIPropertiesColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageCompatibilityInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Elements/Framework/TypedElementDataStorageWidget.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "TedsTableViewerUtils.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace UE::Editor::DataStorage
{
	FTedsTableViewerColumn::FTedsTableViewerColumn(const FName& InColumnName, const TSharedPtr<FTypedElementWidgetConstructor>& InCellWidgetConstructor,
		const TArray<TWeakObjectPtr<const UScriptStruct>>& InMatchedColumns, const TSharedPtr<FTypedElementWidgetConstructor>& InHeaderWidgetConstructor,
		const FMetaDataView& InWidgetMetaData)
		: ColumnName(InColumnName)
		, CellWidgetConstructor(InCellWidgetConstructor)
		, HeaderWidgetConstructor(InHeaderWidgetConstructor)
		, MatchedColumns(InMatchedColumns)
		, WidgetMetaData(InWidgetMetaData)
	{
		Storage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
		StorageUi = GetMutableDataStorageFeature<IEditorDataStorageUiProvider>(UiFeatureName);
		StorageCompatibility = GetMutableDataStorageFeature<IEditorDataStorageCompatibilityProvider>(CompatibilityFeatureName);

		// Store the matched columns as a query condition that requires all of them (i.e AND's them)
		for(const TWeakObjectPtr<const UScriptStruct>& Column : MatchedColumns)
		{
			MatchedColumnConditions = MatchedColumnConditions && Queries::TColumn(Column);
		}
		MatchedColumnConditions.Compile(Queries::FEditorStorageQueryConditionCompileContext(Storage));

		RegisterQueries();
	}
	
	FTedsTableViewerColumn::~FTedsTableViewerColumn()
	{
		UnRegisterQueries();
	}

	TSharedPtr<SWidget> FTedsTableViewerColumn::ConstructRowWidget(RowHandle InRowHandle) const
	{
		TSharedPtr<SWidget> RowWidget;
		
		if(Storage->IsRowAssigned(InRowHandle))
		{
			const RowHandle UiRowHandle = Storage->AddRow(Storage->FindTable(TableViewerUtils::GetWidgetTableName()));

			const TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes = GetMatchedColumns();
			if (ColumnTypes.Num() == 1)
			{
				Storage->AddColumn<FTypedElementScriptStructTypeInfoColumn>(UiRowHandle, FTypedElementScriptStructTypeInfoColumn{.TypeInfo = *ColumnTypes.begin()});
			}
			
			if (FTypedElementRowReferenceColumn* RowReference = Storage->GetColumn<FTypedElementRowReferenceColumn>(UiRowHandle))
			{
				RowReference->Row = InRowHandle;
			}

			if(FTypedElementSlateWidgetReferenceColumn* WidgetReferenceColumn = Storage->GetColumn<FTypedElementSlateWidgetReferenceColumn>(UiRowHandle))
			{
				WidgetReferenceColumn->WidgetConstructor = CellWidgetConstructor;
			}

			RowWidget = StorageUi->ConstructWidget(UiRowHandle, *CellWidgetConstructor, WidgetMetaData);
		}

		return RowWidget;
	}

	SHeaderRow::FColumn::FArguments FTedsTableViewerColumn::ConstructHeaderRowColumn() const
	{
		const TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes = GetMatchedColumns();
		
		FString TooltipText = TEXT("Data Storage columns:");
		for (const TWeakObjectPtr<const UScriptStruct>& ColumnType : ColumnTypes)
		{
			if (ColumnType.IsValid())
			{
				TooltipText += TEXT("\n    ");
				ColumnType->AppendName(TooltipText);
			}
		}

		TSharedPtr<SWidget> Widget;
		RowHandle UiRowHandle = InvalidRowHandle;
		if (HeaderWidgetConstructor)
		{
			UiRowHandle = Storage->AddRow(Storage->FindTable(FName(TEXT("Editor_WidgetTable"))));

			// TEDS UI TODO: We can't do this from the Widget Constructor because it is a UStruct and does not have access to AsShared(), so we would
			// be forced to store a raw pointer instead of a weak pointer which is unsafe. Once the widget construction pipleline is improved this can
			// probably be moved to a better place
			if(FTypedElementSlateWidgetReferenceColumn* WidgetReferenceColumn = Storage->GetColumn<FTypedElementSlateWidgetReferenceColumn>(UiRowHandle))
			{
				WidgetReferenceColumn->WidgetConstructor = HeaderWidgetConstructor;
			}
			
			Widget = StorageUi->ConstructWidget(UiRowHandle, *HeaderWidgetConstructor, 
				WidgetMetaData);
		}
		if (!Widget.IsValid())
		{
			Widget = SNew(STextBlock)
				.Text(FText::FromString(ColumnName.ToString()));
		}
		
		SHeaderRow::FColumn::FArguments Column = SHeaderRow::Column(ColumnName)
			.FillWidth(1)
			.HeaderComboVisibility(EHeaderComboVisibility::OnHover)
			.DefaultTooltip(FText::FromString(MoveTemp(TooltipText)))
			.DefaultLabel(FText::FromName(ColumnName))
			.HeaderContent()
			[
				SNew(SBox)
					.MinDesiredHeight(20.0f)
					.VAlign(VAlign_Center)
					[
						Widget.ToSharedRef()
					]
			];
		
		if (const FUIHeaderPropertiesColumn* HeaderProperties = Storage->GetColumn<FUIHeaderPropertiesColumn>(UiRowHandle))
		{
			float Width = HeaderProperties->Width;
			switch (HeaderProperties->ColumnSizeMode)
			{
				case EColumnSizeMode::Fill: Column.FillWidth(Width); break;
				case EColumnSizeMode::Fixed: Column.FixedWidth(Width); break;
				case EColumnSizeMode::Manual: Column.ManualWidth(Width); break;
				case EColumnSizeMode::FillSized: Column.FillSized(Width); break;
			}
		}
		return Column;
	}

	void FTedsTableViewerColumn::Tick()
	{
		// Update any rows that could need widget updates
		if(!RowsToUpdate.IsEmpty())
		{
			UpdateWidgets();
			RowsToUpdate.Empty();
		}
	}

	void FTedsTableViewerColumn::SetIsRowVisibleDelegate(const FIsRowVisible& InIsRowVisibleDelegate)
	{
		IsRowVisibleDelegate = InIsRowVisibleDelegate;
	}

	void FTedsTableViewerColumn::RegisterQueries()
	{
		using namespace UE::Editor::DataStorage::Queries;

		const TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes = GetMatchedColumns();
		
		// For each TEDS column this column is matched with, we'll add observers to track addition/removal to update any widgets
		for(const TWeakObjectPtr<const UScriptStruct>& ColumnType : ColumnTypes)
		{
			const FName ColumnAddObserverName = *(FString::Printf(TEXT("Column Add Monitor for %s Table Viewer Column Column, %s TEDS Column"), *ColumnName.ToString(), *ColumnType->GetName()));
			FObserver AddObserver(FObserver::EEvent::Add, ColumnType.Get());
			AddObserver.SetExecutionMode(EExecutionMode::GameThread);

			// TEDS-Outliner TODO: Long term if we move this into TypedElementOutlinerMode or similar we can get access to the exact
			// types the Outliner is looking at and specify them on .Where() to cut down on the things we are observing
			QueryHandle AddQueryHandle = Storage->RegisterQuery(
				Select(
					ColumnAddObserverName,
					AddObserver,
					[this](IQueryContext& Context, RowHandle Row)
						{
							RowsToUpdate.Add(TPair<RowHandle, bool>(Row, true));
						})
				.Where()
					.All(ColumnType.Get())
				.Compile());
			
			InternalObserverQueries.Add(AddQueryHandle);

			const FName ColumnRemoveObserverName = *(FString::Printf(TEXT("Column Remove Monitor for %s Table Viewer Column Column, %s TEDS Column"), *ColumnName.ToString(), *ColumnType->GetName()));
			FObserver RemoveObserver(FObserver::EEvent::Remove, ColumnType.Get());
			RemoveObserver.SetExecutionMode(EExecutionMode::GameThread);

			// Table Viewer TODO: We might be able to cut down on the rows we are querying for in the future by getting the rows from the query stack
			// but we currently have to use a generic query so we can support the TEDS-Outliner as well
			QueryHandle RemoveQueryHandle = Storage->RegisterQuery(
				Select(
					ColumnRemoveObserverName,
					RemoveObserver,
					[this](IQueryContext& Context, RowHandle Row)
						{
							RowsToUpdate.Add(TPair<RowHandle, bool>(Row, false));
						})
				.Where()
					.All(ColumnType.Get())
				.Compile());
			
			InternalObserverQueries.Add(RemoveQueryHandle);
		}

		// We are looking for widgets that have a row reference
		TArray<const UScriptStruct*> SelectionColumns({FTypedElementSlateWidgetReferenceColumn::StaticStruct(), FTypedElementRowReferenceColumn::StaticStruct()});

		// We need to remove duplicates because TEDS/Mass does not handle having the same column in Select() and Where()
		TArray<const UScriptStruct*> AdditionalWidgetColumns = CellWidgetConstructor->GetAdditionalColumnsList().FilterByPredicate([&SelectionColumns](const UScriptStruct* Column)
		{
			return !SelectionColumns.Contains(Column);
		});
		
		// Query to get all widgets that were created by this column
		WidgetQuery = Storage->RegisterQuery(
			Select()
				.ReadOnly(SelectionColumns)
			.Where()
				.All(AdditionalWidgetColumns)
			.Compile());
	}

	void FTedsTableViewerColumn::UnRegisterQueries()
	{
		for(const QueryHandle Query : InternalObserverQueries)
		{
			Storage->UnregisterQuery(Query);
		}
		
		Storage->UnregisterQuery(WidgetQuery);

	}

	bool FTedsTableViewerColumn::IsRowVisible(const RowHandle InRowHandle) const
	{
		if(IsRowVisibleDelegate.IsBound())
		{
			return IsRowVisibleDelegate.Execute(InRowHandle);
		}

		// If we don't have a delegate bound we just return true since in the worst case we will just spend time trying to update
		// rows that aren't visible and therefore don't have widgets due to virtualization
		return true; 
	}

	void FTedsTableViewerColumn::UpdateWidgets()
	{
		// Remove any widget rows that don't actually need an update
		RowsToUpdate = RowsToUpdate.FilterByPredicate([this](const TPair<RowHandle, bool>& Pair) -> bool
		{
			// We don't have a widget for this item visible, so there is nothing to update
			if(!IsRowVisible(Pair.Key))
			{
				return false;
			}
			
			// Check if the row now matches the query conditions for this widget
			bool bMatchesQueryConditions = true;

			// First we try to match against the conditions provided by the widget constructor if possible
			const Queries::FConditions* WidgetConstructorConditions = CellWidgetConstructor->GetQueryConditions();
			if(WidgetConstructorConditions && WidgetConstructorConditions->IsCompiled())
			{
				bMatchesQueryConditions = bMatchesQueryConditions && Storage->MatchesColumns(Pair.Key, *WidgetConstructorConditions);
			}
			// If the widget constructor didn't provide any conditions, try to match against the columns we were provided on init
			else
			{
				bMatchesQueryConditions = bMatchesQueryConditions && Storage->MatchesColumns(Pair.Key, MatchedColumnConditions);
			}
			
			// If we are adding a column that we are monitoring and it now matches, or if we are removing a column that we are monitoring and it now
			// stops matching, there is a potential need for widget update
			return (bMatchesQueryConditions && Pair.Value) || (!bMatchesQueryConditions && !Pair.Value);
		});

		using namespace UE::Editor::DataStorage::Queries;

		// Query to find all widgets that belong to the row handles that need updates
		DirectQueryCallback RowCollector = CreateDirectQueryCallbackBinding(
		[this](const IDirectQueryContext& Context, const FTypedElementSlateWidgetReferenceColumn* ContainerWidgetReferenceColumns, const FTypedElementRowReferenceColumn* RowReferenceColumns)
		{
			const FTypedElementSlateWidgetReferenceColumn* WidgetsIt = ContainerWidgetReferenceColumns;
			const FTypedElementRowReferenceColumn* RowRefsIt = RowReferenceColumns;
			const TConstArrayView<RowHandle> Rows = Context.GetRowHandles();

			for(unsigned RowIndex = 0; RowIndex < Context.GetRowCount(); ++RowIndex, ++WidgetsIt, ++RowRefsIt)
			{
				// Check if this widget's owning row is in our rows to update
				bool* bColumnAddedPtr = RowsToUpdate.Find(RowRefsIt->Row);
				// If not, skip it
				if(!bColumnAddedPtr)
				{
					continue;
				}

				// Check if the container TEDSWidget exists, if not we cannot update this widget
				const TSharedPtr<STedsWidget> TedsWidget = WidgetsIt->TedsWidget.Pin();
				if(!TedsWidget)
				{
					continue;
				}

				// A row has numerous widgets, make sure we only update the one that was created by our column by checking the constructor
				if(WidgetsIt->WidgetConstructor != CellWidgetConstructor)
				{
					continue;
				}

				// If a column was added and we are here, we need to re-create the widget
				// TEDS-Outliner TODO: Do we need to create the widget only if it doesn't exist? Or should we also update it to automatically respond
				// to column changes even if it was already created
				if(*bColumnAddedPtr)
				{
					const TSharedPtr<SWidget> RowWidget = CellWidgetConstructor->Construct(Rows[RowIndex], Storage, StorageUi, WidgetMetaData);

					if(RowWidget)
					{
						TedsWidget->SetContent(RowWidget.ToSharedRef());
					}
				}
				// If a column was removed (and we don't match anymore) delete the internal widget
				else
				{
					TedsWidget->SetContent(SNullWidget::NullWidget);
				}
			}
		});

		Storage->RunQuery(WidgetQuery, RowCollector);
	}
	
	FName FTedsTableViewerColumn::GetColumnName() const
	{
		return ColumnName;
	}
	
	TConstArrayView<TWeakObjectPtr<const UScriptStruct>> FTedsTableViewerColumn::GetMatchedColumns() const
	{
		return MatchedColumns;
	}
}

