// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Filters/GenericFilter.h"
#include "Filters/SFilterBar.h"
#include "QueryEditor/TedsQueryEditorModel.h"
#include "Widgets/SCompoundWidget.h"

template<typename>
class SComboBox;

namespace UE::Editor::DataStorage::Debug::QueryEditor
{
	class FTedsQueryEditorModel;
	struct FConditionEntry;

	class SConditionComboWidget : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS( SConditionComboWidget ){}
		SLATE_END_ARGS()

		~SConditionComboWidget() override;
		void OnConditionCollectionChanged();
		void Construct(const FArguments& InArgs, FTedsQueryEditorModel& InModel, QueryEditor::EOperatorType InConditionType);
	private:

		struct FComboItem;
		void PopulateComboItems();
		void OnSelectionChanged(TSharedPtr<FComboItem> NewSelection, ESelectInfo::Type SelectInfo);
		TSharedRef<SWidget> OnGenerateWidget(TSharedPtr<FComboItem> Item);

		FTedsQueryEditorModel* Model = nullptr;
		QueryEditor::EOperatorType ConditionType = QueryEditor::EOperatorType::Invalid;

		FDelegateHandle ConditionCollectionChangedHandle;
		bool bConditionCollectionDirty = true;

		TArray<TSharedPtr<FComboItem>> ComboItems;
		TSharedPtr<SComboBox<TSharedPtr<FComboItem>>> ComboBox;

		TSharedPtr<SFilterBar<TSharedPtr<FComboItem>>> FilterThing;
	};
} // namespace UE::Editor::DataStorage::Debug::QueryEditor
