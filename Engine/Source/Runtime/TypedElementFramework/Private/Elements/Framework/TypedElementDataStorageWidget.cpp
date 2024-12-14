// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementDataStorageWidget.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Common//EditorDataStorageFeatures.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

STedsWidget::STedsWidget()
	: UiRowHandle(UE::Editor::DataStorage::InvalidRowHandle)
{

}

void STedsWidget::Construct(const FArguments& InArgs)
{
	UiRowHandle = InArgs._UiRowHandle;

	// If the Ui row wasn't already registered externally, register it with Teds
	if(UiRowHandle == UE::Editor::DataStorage::InvalidRowHandle)
	{
		RegisterTedsWidget(InArgs._Content.Widget);
	}

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void STedsWidget::RegisterTedsWidget(const TSharedPtr<SWidget>& InContentWidget)
{
	using namespace UE::Editor::DataStorage;
	IEditorDataStorageProvider* Storage = GetStorageIfAvailable();

	// If TEDS is not enabled, STedsWidget will just behave like a regular widget
	if(!Storage)
	{
		return;
	}
	
	const TableHandle WidgetTable = Storage->FindTable(TEXT("Editor_WidgetTable"));
	if(WidgetTable == InvalidTableHandle)
	{
		return;
	}
	
	UiRowHandle = Storage->AddRow(WidgetTable);
	
	if(FTypedElementSlateWidgetReferenceColumn* WidgetReferenceColumn = Storage->GetColumn<FTypedElementSlateWidgetReferenceColumn>(UiRowHandle))
	{
		WidgetReferenceColumn->TedsWidget = SharedThis(this);
		WidgetReferenceColumn->Widget = InContentWidget;
	}
}

void STedsWidget::SetContent(const TSharedRef< SWidget >& InContent)
{
	if(IEditorDataStorageProvider* Storage = GetStorageIfAvailable())
	{
		if(FTypedElementSlateWidgetReferenceColumn* WidgetReferenceColumn = Storage->GetColumn<FTypedElementSlateWidgetReferenceColumn>(UiRowHandle))
		{
			WidgetReferenceColumn->Widget = InContent;
		}
	}
	
	ChildSlot
	[
		InContent
	];
}

UE::Editor::DataStorage::RowHandle STedsWidget::GetRowHandle() const
{
	return UiRowHandle;
}

IEditorDataStorageProvider* STedsWidget::GetStorageIfAvailable()
{
	using namespace UE::Editor::DataStorage;
	return GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
}
