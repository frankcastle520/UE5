// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"

#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Columns/TypedElementUIColumns.h"
#include "Elements/Framework/TypedElementDataStorageWidget.h"

FTypedElementWidgetConstructor::FTypedElementWidgetConstructor(const UScriptStruct* InTypeInfo)
	: TypeInfo(InTypeInfo)
{
}

bool FTypedElementWidgetConstructor::Initialize(const UE::Editor::DataStorage::FMetaDataView& InArguments,
	TArray<TWeakObjectPtr<const UScriptStruct>> InMatchedColumnTypes, const UE::Editor::DataStorage::Queries::FConditions& InQueryConditions)
{
	MatchedColumnTypes = MoveTemp(InMatchedColumnTypes);
	QueryConditions = &InQueryConditions;
	return true;
}

const UScriptStruct* FTypedElementWidgetConstructor::GetTypeInfo() const
{
	return TypeInfo;
}

const TArray<TWeakObjectPtr<const UScriptStruct>>& FTypedElementWidgetConstructor::GetMatchedColumns() const
{
	return MatchedColumnTypes;
}

const UE::Editor::DataStorage::Queries::FConditions* FTypedElementWidgetConstructor::GetQueryConditions() const
{
	return QueryConditions;
}

TConstArrayView<const UScriptStruct*> FTypedElementWidgetConstructor::GetAdditionalColumnsList() const
{
	return {};
}

FString FTypedElementWidgetConstructor::CreateWidgetDisplayName(
	IEditorDataStorageProvider* DataStorage, RowHandle Row) const
{
	switch (MatchedColumnTypes.Num())
	{
	case 0:
		return FString(TEXT("TEDS Column"));
	case 1:
		return DescribeColumnType(MatchedColumnTypes[0].Get());
	default:
	{
		FString LongestMatchString = DescribeColumnType(MatchedColumnTypes[0].Get());
		FStringView LongestMatch = LongestMatchString;
		const TWeakObjectPtr<const UScriptStruct>* It = MatchedColumnTypes.GetData();
		const TWeakObjectPtr<const UScriptStruct>* ItEnd = It + MatchedColumnTypes.Num();
		++It; // Skip the first entry as that's already set.
		for (; It != ItEnd; ++It)
		{
			FString NextMatchText = DescribeColumnType(It->Get());
			FStringView NextMatch = NextMatchText;

			int32 MatchSize = 0;
			auto ItLeft = LongestMatch.begin();
			auto ItLeftEnd = LongestMatch.end();
			auto ItRight = NextMatch.begin();
			auto ItRightEnd = NextMatch.end();
			while (
				ItLeft != ItLeftEnd &&
				ItRight != ItRightEnd &&
				*ItLeft == *ItRight)
			{
				++MatchSize;
				++ItLeft;
				++ItRight;
			}

			// At least 3 letters have to match to avoid single or double letter names which typically mean nothing.
			if (MatchSize > 2)
			{
				LongestMatch.LeftInline(MatchSize);
			}
			else
			{
				// There are not enough characters in the string that match. Just return the name of the first column
				return LongestMatchString;
			}
		}
		return FString(LongestMatch);
	}
	};
}

TSharedPtr<SWidget> FTypedElementWidgetConstructor::ConstructFinalWidget(
	RowHandle Row,
	IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	// Add the additional columns to the UI row
	TSharedPtr<SWidget> Widget = SNullWidget::NullWidget;
	DataStorage->AddColumns(Row, GetAdditionalColumnsList());
	
	if (const FTypedElementRowReferenceColumn* RowReference = DataStorage->GetColumn<FTypedElementRowReferenceColumn>(Row))
	{
		bool bConstructWidget = DataStorage->IsRowAssigned(RowReference->Row);

		// If the original row matches this widgets query conditions currently, create the actual internal widget
		if (const UE::Editor::DataStorage::Queries::FConditions* MatchedQueryConditions = GetQueryConditions())
		{
			bConstructWidget &= DataStorage->MatchesColumns(RowReference->Row, *MatchedQueryConditions);
		}
		
		if (bConstructWidget)
		{
			Widget = Construct(Row, DataStorage, DataStorageUi, Arguments);
		}
	}
	// If we don't have an original row, simply construct the widget
	else
	{
		Widget = Construct(Row, DataStorage, DataStorageUi, Arguments);
	}

	// Create a container widget to hold the content (even if it doesn't exist yet)
	TSharedPtr<STedsWidget> ContainerWidget = SNew(STedsWidget)
	.UiRowHandle(Row)
	[
		Widget.ToSharedRef()
	];
	
	DataStorage->GetColumn<FTypedElementSlateWidgetReferenceColumn>(Row)->TedsWidget = ContainerWidget;
	return ContainerWidget;
}

TSharedPtr<SWidget> FTypedElementWidgetConstructor::Construct(
	RowHandle Row,
	IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	UE::Editor::DataStorage::RowHandle TargetRow = GetTargetRow(DataStorage, Row);

	TSharedPtr<SWidget> Widget = CreateWidget(DataStorage, DataStorageUi, TargetRow, Row, Arguments);
	if (Widget)
	{
		DataStorage->GetColumn<FTypedElementSlateWidgetReferenceColumn>(Row)->Widget = Widget;
		if (SetColumns(DataStorage, Row))
		{
			if (FinalizeWidget(DataStorage, DataStorageUi, Row, Widget))
			{
				AddDefaultWidgetColumns(Row, DataStorage);
				return Widget;
			}
		}
	}
	return nullptr;
}

TSharedPtr<SWidget> FTypedElementWidgetConstructor::CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	return nullptr;
}

TSharedPtr<SWidget> FTypedElementWidgetConstructor::CreateWidget(
	IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi,
	UE::Editor::DataStorage::RowHandle TargetRow,
	UE::Editor::DataStorage::RowHandle UiRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	return CreateWidget(Arguments);
}

bool FTypedElementWidgetConstructor::SetColumns(IEditorDataStorageProvider* DataStorage, RowHandle Row)
{
	return true;
}

FString FTypedElementWidgetConstructor::DescribeColumnType(const UScriptStruct* ColumnType) const
{
	static const FName DisplayNameName(TEXT("DisplayName"));

#if WITH_EDITOR
	if (ColumnType)
	{
		const FString* Name = ColumnType->FindMetaData(DisplayNameName);
		return Name ? *Name : ColumnType->GetDisplayNameText().ToString();
	}
	else
#endif
	{
		return FString(TEXT("<Invalid>"));
	}
}

bool FTypedElementWidgetConstructor::FinalizeWidget(
	IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi,
	RowHandle Row,
	const TSharedPtr<SWidget>& Widget)
{
	return true;
}

void FTypedElementWidgetConstructor::AddDefaultWidgetColumns(RowHandle Row, IEditorDataStorageProvider* DataStorage) const
{
	const FString WidgetLabel(CreateWidgetDisplayName(DataStorage, Row));
	DataStorage->AddColumn(Row, FTypedElementLabelColumn{.Label = WidgetLabel} );

	// We don't want to display any second level widgets (widgets for widgets and so on...) in UI because they will cause the table viewer to
	// infinitely grow as you keep scrolling (which creates new widgets)
	if(DataStorage->HasColumns<FTypedElementSlateWidgetReferenceColumn>(Row))
	{
		if(const FTypedElementRowReferenceColumn* RowReferenceColumn = DataStorage->GetColumn<FTypedElementRowReferenceColumn>(Row))
		{
			if(DataStorage->HasColumns<FTypedElementSlateWidgetReferenceColumn>(RowReferenceColumn->Row))
			{
				DataStorage->AddColumn(Row, FHideRowFromUITag::StaticStruct());
			}
		}
	}
}

FTypedElementWidgetConstructor::RowHandle FTypedElementWidgetConstructor::GetTargetRow(IEditorDataStorageProvider* DataStorage, RowHandle WidgetRow) const
{
	using namespace UE::Editor::DataStorage;
	RowHandle TargetRow = InvalidRowHandle;

	if(const FTypedElementRowReferenceColumn* RowReferenceColumn = DataStorage->GetColumn<FTypedElementRowReferenceColumn>(WidgetRow))
	{
		TargetRow = RowReferenceColumn->Row;
	}

	return TargetRow;
}


// FSimpleWidgetConstructor

FSimpleWidgetConstructor::FSimpleWidgetConstructor(const UScriptStruct* InTypeInfo)
	: FTypedElementWidgetConstructor(InTypeInfo)
{
}

TSharedPtr<SWidget> FSimpleWidgetConstructor::CreateWidget(IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle TargetRow, UE::Editor::DataStorage::RowHandle WidgetRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	return nullptr;
}

bool FSimpleWidgetConstructor::SetColumns(IEditorDataStorageProvider* DataStorage, RowHandle Row)
{
	return FTypedElementWidgetConstructor::SetColumns(DataStorage, Row);
}

TSharedPtr<SWidget> FSimpleWidgetConstructor::CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	// This function is not needed anymore and only exists so derived classes cannot derive from it anymore
	return nullptr;
}

bool FSimpleWidgetConstructor::FinalizeWidget(IEditorDataStorageProvider* DataStorage, IEditorDataStorageUiProvider* DataStorageUi,
	RowHandle Row, const TSharedPtr<SWidget>& Widget)
{
	// This function is not needed anymore and only exists so derived classes cannot derive from it anymore
	return true;
}

TSharedPtr<SWidget> FSimpleWidgetConstructor::Construct(RowHandle WidgetRow, IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi, const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	const UE::Editor::DataStorage::RowHandle TargetRow = GetTargetRow(DataStorage, WidgetRow);

	// Set any required columns on the widget row first
	SetColumns(DataStorage, WidgetRow);

	// Create the actual widget
	TSharedPtr<SWidget> Widget = CreateWidget(DataStorage, DataStorageUi, TargetRow, WidgetRow, Arguments);

	// If the widget was created, add the default columns we want all widget rows to have (e.g Label)
	if (Widget)
	{
		AddDefaultWidgetColumns(WidgetRow, DataStorage);
	}
	
	return Widget;
}

