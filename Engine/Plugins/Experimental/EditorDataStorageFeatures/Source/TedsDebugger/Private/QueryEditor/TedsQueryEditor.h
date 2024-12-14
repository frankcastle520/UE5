// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Widgets/SCompoundWidget.h"
#include "Templates/SharedPointerFwd.h"

class SDockTab;
class SWindow;

namespace UE::Editor::DataStorage::Debug::QueryEditor
{
	class FTedsQueryEditorModel;
	class SQueryEditorWidget : public SCompoundWidget
	{
	public:

		SLATE_BEGIN_ARGS(SQueryEditorWidget) 
		{
		}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, FTedsQueryEditorModel& QueryEditorModel);

		/**
		 * Wrapper class for a combo item in the dropdown selection
		 */
		struct ColumnComboItem;
	private:
		FTedsQueryEditorModel* Model = nullptr;
		TArray<TSharedPtr<ColumnComboItem>> ComboItems;
	};
}

