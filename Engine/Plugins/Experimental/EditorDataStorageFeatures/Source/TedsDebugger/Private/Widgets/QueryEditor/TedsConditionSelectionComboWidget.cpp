// Copyright Epic Games, Inc. All Rights Reserved.
#include "TedsConditionSelectionComboWidget.h"

#include "QueryEditor/TedsQueryEditorModel.h"

#include "Algo/FindSortedStringCaseInsensitive.h"
#include "Elements/Common/TypedElementDataStorageLog.h"
#include "Filters/SBasicFilterBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SComboBox.h"

namespace UE::Editor::DataStorage::Debug::QueryEditor
{
	struct SConditionComboWidget::FComboItem
	{
		FConditionEntryHandle Handle;
		
		bool IsValid() const { return Handle.IsValid(); }
		void Reset() { Handle.Reset(); }
	};

	void SConditionComboWidget::OnConditionCollectionChanged()
	{		
		PopulateComboItems();
	}

	SConditionComboWidget::~SConditionComboWidget()
	{
		Model->GetModelChangedDelegate().Remove(ConditionCollectionChangedHandle);
	}

	void SConditionComboWidget::Construct(const FArguments& InArgs, FTedsQueryEditorModel& InModel, QueryEditor::EOperatorType InConditionType)
	{
		Model = &InModel;

		check(InConditionType != EOperatorType::Invalid);
		ConditionType = InConditionType;
		
		ConditionCollectionChangedHandle = Model->GetModelChangedDelegate().AddRaw(this, &SConditionComboWidget::OnConditionCollectionChanged);

		
		
		ComboBox = SNew(SComboBox<TSharedPtr<FComboItem>>)
			.OptionsSource(&ComboItems)
			.OnSelectionChanged(this, &SConditionComboWidget::OnSelectionChanged)
			.OnGenerateWidget(this, &SConditionComboWidget::OnGenerateWidget)
			.IsEnabled_Lambda([this](){ return !ComboItems.IsEmpty(); })
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([]() { return FText::FromString("+"); })
			];

		PopulateComboItems();

		ChildSlot
		[
		SNew( SVerticalBox )
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			ComboBox.ToSharedRef()
		]
			
		];
	}

	void SConditionComboWidget::PopulateComboItems()
	{
		ComboItems.Reset();
		Model->GenerateValidOperatorChoices(ConditionType, [this](const FTedsQueryEditorModel&, const QueryEditor::FConditionEntryHandle& Handle)
		{
			FComboItem ComboItem;
			ComboItem.Handle = Handle;
			ComboItems.Emplace(MakeShared<FComboItem>(ComboItem));
			
		});

		// Sort the ComboItems - this will make it only slightly easier to find them...
		Algo::Sort(ComboItems, [this](TSharedPtr<FComboItem>& A, TSharedPtr<FComboItem>& B)->bool
		{
			const UScriptStruct* AStruct = Model->GetColumnScriptStruct(A->Handle);
			const UScriptStruct* BStruct = Model->GetColumnScriptStruct(B->Handle);
			return AStruct->GetFName().Compare(BStruct->GetFName()) < 0;
		});
		
		ComboBox->RefreshOptions();
	}

	void SConditionComboWidget::OnSelectionChanged(TSharedPtr<FComboItem> NewSelection, ESelectInfo::Type SelectInfo)
	{
		if (NewSelection)
		{
			EErrorCode ErrorCode = Model->SetOperatorType(NewSelection->Handle, ConditionType);
			if (ErrorCode != EErrorCode::Success)
			{
				UE_LOG(LogEditorDataStorage, Error, TEXT("Could not set model condition: [%d]"), static_cast<int>(ErrorCode));
			}
			ComboBox->ClearSelection();
		}
	}

	TSharedRef<SWidget> SConditionComboWidget::OnGenerateWidget(TSharedPtr<FComboItem> Item)
	{
		const UScriptStruct* ColumnType = Model->GetColumnScriptStruct(Item->Handle);
		const FText Label = ColumnType ? FText::FromString(ColumnType->GetName()) : FText::FromString(TEXT("Null Column Type"));

		return SNew(STextBlock)
			.Text(Label);
	}
}
