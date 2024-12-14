// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Elements/Common/TypedElementHandles.h"
#include "QueryEditor/TedsQueryEditorModel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SCompoundWidget.h"

class ISceneOutliner;

namespace UE::Editor::DataStorage
{
	namespace Debug
	{
		class STedsDebugger : public SCompoundWidget
		{
		public:

			SLATE_BEGIN_ARGS(STedsDebugger) 
			{
			}
			SLATE_END_ARGS()

		public:
			STedsDebugger() = default;
			virtual ~STedsDebugger() override;

			/**
			* Constructs the debugger.
			*
			* @param InArgs The Slate argument list.
			* @param ConstructUnderMajorTab The major tab which will contain the session front-end.
			* @param ConstructUnderWindow The window in which this widget is being constructed.
			*/
			void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow);
		private:
			TSharedRef<SDockTab> SpawnToolbar(const FSpawnTabArgs& Args);
			TSharedRef<SDockTab> SpawnQueryEditorTab(const FSpawnTabArgs& Args);
			void FillWindowMenu( FMenuBuilder& MenuBuilder);

			void RegisterTabSpawners();

		private:
	
			// Holds the tab manager that manages the front-end's tabs.
			TSharedPtr<FTabManager> TabManager;

			// Table Viewer
			QueryHandle TableViewerQuery;

			// Query Editor
			TUniquePtr<QueryEditor::FTedsQueryEditorModel> QueryEditorModel;
		};
	} // namespace Debug
} // namespace UE::Editor::DataStorage
