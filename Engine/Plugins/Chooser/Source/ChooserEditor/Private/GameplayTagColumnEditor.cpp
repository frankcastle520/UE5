// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayTagColumnEditor.h"

#include <ChooserColumnHeader.h>

#include "SPropertyAccessChainWidget.h"
#include "GameplayTagColumn.h"
#include "ObjectChooserWidgetFactories.h"
#include "SGameplayTagWidget.h"
#include "SSimpleComboButton.h"
#include "GraphEditorSettings.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "FGameplayTagColumnEditor"

namespace UE::ChooserEditor
{

TSharedRef<SWidget> CreateGameplayTagColumnWidget(UChooserTable* Chooser, FChooserColumnBase* Column, int Row)
{
	FGameplayTagColumn* GameplayTagColumn = static_cast<struct FGameplayTagColumn*>(Column);
	
	if (Row == ColumnWidget_SpecialIndex_Fallback)
	{
		return SNullWidget::NullWidget;
	}
	else if (Row == ColumnWidget_SpecialIndex_Header)
	{
		// create column header widget
		const FSlateBrush* ColumnIcon = FCoreStyle::Get().GetBrush("Icons.Filter");
		const FText ColumnTooltip = LOCTEXT("Gameplay Tag Tooltip", "Gameplay Tag: cells pass if the input gameplay tag collection matches the cell data (accoding to comparison settings in the column properties).");
		const FText ColumnName = LOCTEXT("Gameplay Tag","Gameplay Tag");
		
		TSharedPtr<SWidget> DebugWidget = nullptr;
		if (Chooser->GetEnableDebugTesting())
		{
			DebugWidget = SNew(SSimpleComboButton)
				.IsEnabled_Lambda([Chooser]()
				{
					 return !Chooser->HasDebugTarget();
				})
				.Text_Lambda([GameplayTagColumn]()
				{
					FText Text = FText::FromString(GameplayTagColumn->TestValue.ToStringSimple(false));
					if (Text.IsEmpty())
					{
						Text = LOCTEXT("None", "None");
					}
					return Text;
				})	
				.OnGetMenuContent_Lambda([Chooser, GameplayTagColumn, Row]()
				{
					TArray<SGameplayTagWidget::FEditableGameplayTagContainerDatum> EditableContainers;
					EditableContainers.Emplace(Chooser, &(GameplayTagColumn->TestValue));
					return TSharedRef<SWidget>(SNew(SGameplayTagWidget, EditableContainers));
				});
		}

		return MakeColumnHeaderWidget(Chooser, Column, ColumnName, ColumnTooltip, ColumnIcon, DebugWidget);
	}

	// create cell widget
	return SNew(SSimpleComboButton)
		.Text_Lambda([GameplayTagColumn, Row]()
		{
			if (Row < GameplayTagColumn->RowValues.Num())
			{
				FText Result = FText::FromString(GameplayTagColumn->RowValues[Row].ToStringSimple(false));
				if (Result.IsEmpty())
				{
					Result = LOCTEXT("Any Tag", "[Any]");
				}
				return Result;
			}
			else
			{
				return FText();
			}
		})	
		.OnGetMenuContent_Lambda([Chooser, GameplayTagColumn, Row]()
		{
			if (Row < GameplayTagColumn->RowValues.Num())
			{
				TArray<SGameplayTagWidget::FEditableGameplayTagContainerDatum> EditableContainers;
				EditableContainers.Emplace(Chooser, &(GameplayTagColumn->RowValues[Row]));
				return TSharedRef<SWidget>(SNew(SGameplayTagWidget, EditableContainers));
			}

			return SNullWidget::NullWidget;
		}
	);
}

TSharedRef<SWidget> CreateGameplayTagPropertyWidget(bool bReadOnly, UObject* TransactionObject, void* Value, UClass* ResultBaseClass, FChooserWidgetValueChanged ValueChanged)
{
	IHasContextClass* HasContextClass = Cast<IHasContextClass>(TransactionObject);

	FGameplayTagContextProperty* ContextProperty = reinterpret_cast<FGameplayTagContextProperty*>(Value);

	return SNew(SPropertyAccessChainWidget).ContextClassOwner(HasContextClass).AllowFunctions(false).BindingColor("StructPinTypeColor").TypeFilter("FGameplayTagContainer")
	.PropertyBindingValue(&ContextProperty->Binding)
	.OnValueChanged(ValueChanged);
}
	
void RegisterGameplayTagWidgets()
{
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FGameplayTagContextProperty::StaticStruct(), CreateGameplayTagPropertyWidget);
	FObjectChooserWidgetFactories::RegisterColumnWidgetCreator(FGameplayTagColumn::StaticStruct(), CreateGameplayTagColumnWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
