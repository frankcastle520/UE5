// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SRowDetails.h"

#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "SRowDetails"

namespace UE::Editor::DataStorage
{
	namespace Widgets::Private
	{
		static const FName NameColumn(TEXT("Name"));
		static const FName DataColumn(TEXT("Data"));
		static TArray<FName> DefaultWidgetPurposes = { FName(TEXT("RowDetails.Cell.Large")),
			FName(TEXT("RowDetails.Cell")), FName(TEXT("General.Cell.Large")), FName(TEXT("General.Cell")) };
	} // namespace Widgets::Private

	//
	// SRowDetails
	//

	void SRowDetails::Construct(const FArguments& InArgs)
	{
		bShowAllDetails = InArgs._ShowAllDetails;
		WidgetPurposes = InArgs._WidgetPurposesOverride;

		if(WidgetPurposes.IsEmpty())
		{
			WidgetPurposes = Widgets::Private::DefaultWidgetPurposes;
		}

		checkf(AreEditorDataStorageFeaturesEnabled(), TEXT("Unable to initialize SRowDetails without the editor data storage interfaces."));

		DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
		DataStorageUi = GetMutableDataStorageFeature<IEditorDataStorageUiProvider>(UiFeatureName);

		
		ChildSlot
		[
			SAssignNew(ListView, SListView<RowDetailsItemPtr>)
				.ListItemsSource(&Items)
				.OnGenerateRow(this, &SRowDetails::CreateRow)
				.Visibility_Lambda([this]()
					{
						return Items.IsEmpty() ? EVisibility::Hidden : EVisibility::Visible;
					})
				.HeaderRow
				(
					SNew(SHeaderRow)
					+SHeaderRow::Column(Widgets::Private::NameColumn)
						.DefaultLabel(FText::FromString(TEXT("Name")))
						.FillWidth(0.3f)
					+SHeaderRow::Column(Widgets::Private::DataColumn)
						.DefaultLabel(FText::FromString(TEXT("Value")))
						.FillWidth(0.7f)
				)
		];
	}
	
	void SRowDetails::SetRow(RowHandle Row)
	{
		if (DataStorage->IsRowAssigned(Row))
		{
			Items.Reset();
			
			TArray<TWeakObjectPtr<const UScriptStruct>> Columns;
			DataStorage->ListColumns(Row, [&Columns](const UScriptStruct& ColumnType)
				{
					Columns.Emplace(&ColumnType);
					return true;
				});
			
			for (const FName& Purpose : WidgetPurposes)
			{
				DataStorageUi->CreateWidgetConstructors(Purpose, IEditorDataStorageUiProvider::EMatchApproach::LongestMatch,
					Columns, {}, [this, Row](
						TUniquePtr<FTypedElementWidgetConstructor> Constructor, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> Columns)
					{
						Items.Add(MakeShared<FRowDetailsItem>(nullptr, MoveTemp(Constructor), Row));
						return true;
					});
			}
			
			if (bShowAllDetails)
			{
				// Create defaults for the remaining widgets.
				for (TWeakObjectPtr<const UScriptStruct> Column : Columns)
				{
					DataStorageUi->CreateWidgetConstructors(FName(TEXT("General.Cell.Default")), {},
						[this, Column, Row](
							TUniquePtr<FTypedElementWidgetConstructor> Constructor, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> Columns)
						{
							Items.Add(MakeShared<FRowDetailsItem>(Column, MoveTemp(Constructor), Row));
							return true;
						});
				}
			}
			
			ListView->RequestListRefresh();
		}
		else
		{
			ClearRow();
		}
	}
	
	void SRowDetails::ClearRow()
	{
		Items.Reset();
		ListView->RequestListRefresh();
	}
	
	TSharedRef<ITableRow> SRowDetails::CreateRow(RowDetailsItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
	{
		TSharedPtr<SRowDetailsRow> Row = SNew(SRowDetailsRow, OwnerTable, DataStorage, DataStorageUi)
			.Item(InItem);
		
		return Row.ToSharedRef();
	}
	
	//
	// SRowDetailsRow
	//
	void SRowDetailsRow::Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView,IEditorDataStorageProvider* InDataStorage,
			IEditorDataStorageUiProvider* InDataStorageUi)
	{
		Item = Args._Item;

		DataStorage = InDataStorage;
		DataStorageUi = InDataStorageUi;
		
		SMultiColumnTableRow<RowDetailsItemPtr>::Construct(FSuperRowType::FArguments(), OwnerTableView);
	}
	
	TSharedRef<SWidget> SRowDetailsRow::GenerateWidgetForColumn(const FName& ColumnName)
	{		
		if (!DataStorage->IsRowAvailable(Item->WidgetRow))
		{
			Item->WidgetRow = DataStorage->AddRow(DataStorage->FindTable(FName("Editor_WidgetTable")));
			
			DataStorage->AddColumn<FTypedElementRowReferenceColumn>(Item->WidgetRow, FTypedElementRowReferenceColumn
				{
					.Row = Item->Row
				});
			
			if (Item->ColumnType.IsValid() &&
				Item->WidgetConstructor->GetAdditionalColumnsList().Contains(FTypedElementScriptStructTypeInfoColumn::StaticStruct()))
			{
				DataStorage->AddColumn(Item->WidgetRow, FTypedElementScriptStructTypeInfoColumn
					{
						.TypeInfo = Item->ColumnType
					});
			}
		}
		if (ColumnName == Widgets::Private::NameColumn)
		{
			return SNew(STextBlock)
					.Text(FText::FromString(Item->WidgetConstructor->CreateWidgetDisplayName(
							DataStorage, Item->WidgetRow)));
		}
		else if (ColumnName == Widgets::Private::DataColumn)
		{
			return DataStorageUi->ConstructWidget(Item->WidgetRow, *(Item->WidgetConstructor), {}).ToSharedRef();
		}
		else
		{
			return SNew(STextBlock)
					.Text(LOCTEXT("InvalidColumnType", "Invalid Column Type")); 
		}
	}

	// FRowDetailsItem
	FRowDetailsItem::FRowDetailsItem(const TWeakObjectPtr<const UScriptStruct>& InColumnType,
		TUniquePtr<FTypedElementWidgetConstructor> InWidgetConstructor, RowHandle InRow)
		: ColumnType(InColumnType)
		, WidgetConstructor(MoveTemp(InWidgetConstructor))
		, Row(InRow)
	{
	
	}
	
} // namespace UE::Editor::DataStorage


#undef LOCTEXT_NAMESPACE // "SRowDetails"
