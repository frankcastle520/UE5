// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Widgets/SCompoundWidget.h"

class ISceneOutliner;
class SSceneOutliner;
class SHorizontalBox;
struct FTypedElementWidgetConstructor;

namespace UE::Editor::DataStorage
{
	class STedsTableViewer;
	class FQueryStackNode_RowView;
	class FTedsTableViewerColumn;
	class SRowDetails;

	namespace Debug::QueryEditor
	{
		class FTedsQueryEditorModel;

		class SResultsView : public SCompoundWidget
		{
		public:
			SLATE_BEGIN_ARGS( SResultsView ){}
			SLATE_END_ARGS()

			~SResultsView() override;
			void Construct(const FArguments& InArgs, FTedsQueryEditorModel& InModel);
			void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

		private:

			void OnModelChanged();
			void CreateRowHandleColumn();
		
			FTedsQueryEditorModel* Model = nullptr;
			FDelegateHandle ModelChangedDelegateHandle;
			bool bModelDirty = true;


			QueryHandle CountQueryHandle = InvalidQueryHandle;
			QueryHandle TableViewerQueryHandle = InvalidQueryHandle;

			TArray<RowHandle> TableViewerRows;
			// We have to keep a TSet copy because queries return duplicate rows sometimes and to have some form of sorted order for the rows for now
			TSet<RowHandle> TableViewerRows_Set;
			TSharedPtr<STedsTableViewer> TableViewer;
			TSharedPtr<FQueryStackNode_RowView> RowQueryStack;

			// Custom column for the table viewer to display row handles
			TSharedPtr<FTedsTableViewerColumn> RowHandleColumn;

			// Widget that displays details of a row
			TSharedPtr<SRowDetails> RowDetailsWidget;
		};

	} // namespace Debug::QueryEditor
} // namespace UE::Editor::DataStorage
