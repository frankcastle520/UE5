// Copyright Epic Games, Inc. All Rights Reserved.

#include "QueryEditor/TedsQueryEditor.h"

#include "QueryEditor/TedsQueryEditorModel.h"
#include "Components/VerticalBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableComboBox.h"
#include "Widgets/QueryEditor/TedsConditionSelectionComboWidget.h"
#include "Widgets/QueryEditor/TedsConditionCollectionViewWidget.h"
#include "Widgets/QueryEditor/TedsQueryEditorResultsView.h"

#define LOCTEXT_NAMESPACE "TedsQueryEditor"

namespace UE::Editor::DataStorage::Debug::QueryEditor
{
	struct SQueryEditorWidget::ColumnComboItem
	{
		const FConditionEntry* Entry = nullptr;

		bool operator==(const ColumnComboItem& Rhs) const
		{
			return Entry == Rhs.Entry;
		}
	};

	void SQueryEditorWidget::Construct(const FArguments& InArgs, FTedsQueryEditorModel& QueryEditorModel)
	{	
		ComboItems.Reset();
		Model = &QueryEditorModel;
	
		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Background"))
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					[
						SNew(SConditionCollectionViewWidget, *Model, EOperatorType::Select)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SConditionComboWidget, *Model, EOperatorType::Select)
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					[
						SNew(SConditionCollectionViewWidget, *Model, EOperatorType::All)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SConditionComboWidget, *Model, EOperatorType::All)
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					[
					SNew(SConditionCollectionViewWidget, *Model, EOperatorType::Any)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
					SNew(SConditionComboWidget, *Model, EOperatorType::Any)
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					[
					SNew(SConditionCollectionViewWidget, *Model, EOperatorType::None)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
					SNew(SConditionComboWidget, *Model, EOperatorType::None)
					]
				]
				+SVerticalBox::Slot()
				[
					SNew(SResultsView, *Model)
				]
			]
			
		];
	}
} // namespace UE::Editor::DataStorage::Debug::QueryEditor

#undef LOCTEXT_NAMESPACE