// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsStylingFactory.h"

#include "Columns/TedsStylingColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/TypedElementIndexHasher.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "HAL/IConsoleManager.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

namespace UE::TedsStylingFactory::Local
{
	static const FName TableName("Editor_StyleTable");
	
	static FAutoConsoleCommand RegisterStylesCommand
	(
		TEXT("TEDS.Feature.AddSlateStyleRows"),
		TEXT("Add all slate styles found in all registered stylesheets to TEDS"),
		FConsoleCommandDelegate::CreateLambda([]() { UTedsStylingFactory::RegisterAllKnownStyles(); })
	);

}

void UTedsStylingFactory::RegisterTables(IEditorDataStorageProvider& DataStorage)
{
	DataStorage.RegisterTable(TTypedElementColumnTypeList<FNameColumn, FSlateStyleSetColumn, FSlateStyleTag>(), UE::TedsStylingFactory::Local::TableName);
}

void UTedsStylingFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	Super::RegisterQueries(DataStorage);
}

void UTedsStylingFactory::RegisterAllKnownStyles()
{
	using namespace UE::Editor::DataStorage;
	IEditorDataStorageProvider* Interface = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);

	auto RegisterBrushesForStyle = [Interface](const ISlateStyle& Style)
	{
		// Get all styles belonging to this styleset
		TSet<FName> StyleKeys = Style.GetStyleKeys();

		for (const FName& StyleKey : StyleKeys)
		{
			// Since there is no way to check which type of style a specific key is, we just go through each one by one and check for now
			// This is not the most performant and can be improved in the future by exposing more internals from ISlateStyle if needed
			// We also only support brushes and colors currently
			bool bRegisteredSyleKey = false;

			// Check if this style key is a brush
			if (const FSlateBrush* Brush = Style.GetBrush(StyleKey))
			{
				if(Brush != Style.GetDefaultBrush())
				{
					RegisterBrush(Interface, StyleKey, Brush, Style);
					bRegisteredSyleKey = true;
				}
			}

			if(!bRegisteredSyleKey)
			{
				// Check if this style key is a color
				FSlateColor Color = Style.GetSlateColor(StyleKey);
				if (Color != FStyleDefaults::GetSlateColor())
				{
					RegisterColor(Interface, StyleKey, Color, Style);
					bRegisteredSyleKey = true;
				}
			}
		}
	};

	// Iterate all known style sheets and register all their members
	FSlateStyleRegistry::IterateAllStyles(
		[RegisterBrushesForStyle](const ISlateStyle& Style)
		{
			RegisterBrushesForStyle(Style);
			return true;
		}
	);
}

void UTedsStylingFactory::RegisterBrush(IEditorDataStorageProvider* DataStorage, const FName& StyleName, const FSlateBrush* Brush, const ISlateStyle& OwnerStyle)
{
	// We currently don't store the FSlateBrush* itself to avoid storing a raw pointer that we can query on demand anyways in case we want to support
	// swapping brushes or unloading styles on demand in the future
	const UE::Editor::DataStorage::RowHandle Row = AddOrGetStyleRow(DataStorage, StyleName, OwnerStyle);
	
	if (DataStorage->IsRowAssigned(Row))
	{
		const FName ResourceName = Brush->GetResourceName();

		if (ResourceName != NAME_None)
		{
			DataStorage->AddColumn(Row, FSlateStylePathColumn{.StylePath = ResourceName});
		}

		DataStorage->AddColumn<FSlateBrushTag>(Row);
	}
}

void UTedsStylingFactory::RegisterColor(IEditorDataStorageProvider* DataStorage, const FName& StyleName, const FSlateColor& Color,
	const ISlateStyle& OwnerStyle)
{
	const UE::Editor::DataStorage::RowHandle Row = AddOrGetStyleRow(DataStorage, StyleName, OwnerStyle);
	
	if (DataStorage->IsRowAssigned(Row))
	{
		DataStorage->AddColumn(Row, FSlateColorColumn{.Color = Color});
	}

}

UE::Editor::DataStorage::RowHandle UTedsStylingFactory::AddOrGetStyleRow(IEditorDataStorageProvider* DataStorage,
	const FName& StyleName, const ISlateStyle& OwnerStyle)
{
	const FName StyleSetName = OwnerStyle.GetStyleSetName();
	const UE::Editor::DataStorage::IndexHash Index = UE::Editor::DataStorage::GenerateIndexHash(ISlateStyle::Join(StyleSetName, TCHAR_TO_ANSI(*StyleName.ToString())));
	UE::Editor::DataStorage::RowHandle Row = DataStorage->FindIndexedRow(Index);

	if (!DataStorage->IsRowAssigned(Row))
	{
		static UE::Editor::DataStorage::RowHandle Table = DataStorage->FindTable(UE::TedsStylingFactory::Local::TableName);
		Row = DataStorage->AddRow(Table);
		DataStorage->IndexRow(Index, Row);

		DataStorage->GetColumn<FNameColumn>(Row)->Name = StyleName;
		DataStorage->GetColumn<FSlateStyleSetColumn>(Row)->StyleSetName = StyleSetName;
	}

	return Row;

}
