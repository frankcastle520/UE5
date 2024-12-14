// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Analytics/DMXEditorToolAnalyticsProvider.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"

enum class EDMXControlConsoleStopDMXMode : uint8;
struct FDMXControlConsoleCue;
class FSpawnTabArgs;
class FTabManager;
class SDockableTab;
class UDMXControlConsole;
class UDMXControlConsoleData;
class UDMXControlConsoleEditorData;
class UDMXControlConsoleEditorLayouts;
class UDMXControlConsoleEditorModel;
class UDMXControlConsoleEditorPlayMenuModel;


namespace UE::DMX::Private
{
	class FDMXControlConsoleCueStackModel;
	class FDMXControlConsoleEditorToolbar;
	class SDMXControlConsoleEditorCueStackView;
	class SDMXControlConsoleEditorDetailsView;
	class SDMXControlConsoleEditorDMXLibraryView;
	class SDMXControlConsoleEditorFiltersView;
	class SDMXControlConsoleEditorLayoutView;

	/** Implements an Editor toolkit for Control Console. */
	class FDMXControlConsoleEditorToolkit
		: public FAssetEditorToolkit
		, public FGCObject
	{
		using Super = FAssetEditorToolkit;

	public:
		FDMXControlConsoleEditorToolkit();
		~FDMXControlConsoleEditorToolkit();

		/**
		 * Edits the specified control console object.
		 *
		 * @param Mode The tool kit mode.
		 * @param InitToolkitHost
		 * @param ObjectToEdit The control console object to edit.
		 */
		void InitControlConsoleEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UDMXControlConsole* InControlConsole);

		/** Returns the edited Control Console */
		UDMXControlConsole* GetControlConsole() const { return ControlConsole; }

		/** Returns the edited Control Console Data */
		UDMXControlConsoleData* GetControlConsoleData() const;

		/** Returns the edited Control Console Editor Data */
		UDMXControlConsoleEditorData* GetControlConsoleEditorData() const;

		/** Returns the edited Control Console Layouts */
		UDMXControlConsoleEditorLayouts* GetControlConsoleLayouts() const;

		/** Returns the Control Console Cue Stack Model, if valid */
		TSharedPtr<FDMXControlConsoleCueStackModel> GetControlConsoleCueStackModel() const { return CueStackModel; }

		/** Returns the Control Console Editor Model, if valid */
		UDMXControlConsoleEditorModel* GetControlConsoleEditorModel() const { return EditorModel; }

		/** Removes all selected elements from DMX Control Console */
		void RemoveAllSelectedElements();

		/** Clears the DMX Control Console and all its elements */
		void ClearAll();

		/** Resets all the elements in the Control Console to their default values */
		void ResetToDefault();

		/** Resets all the elements in the Control Console to zero */
		void ResetToZero();

		/** Reloads the Control Console asset from the disk */
		void Reload();

		/** Closes this editor and presents the compact editor instead */
		void ShowCompactEditor();

		/** Name of the DMX Library View Tab */
		static const FName DMXLibraryViewTabID;

		/** Name of the Layout View Tab */
		static const FName LayoutViewTabID;

		/** Name of the Details View Tab */
		static const FName DetailsViewTabID;

		/** Name of the Filters View Tab */
		static const FName FiltersViewTabID;

		/** Name of the Cue Stack View Tab */
		static const FName CueStackViewTabID;

	protected:
		//~ Begin FAssetEditorToolkit Interface
		virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
		virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
		virtual const FSlateBrush* GetDefaultTabIcon() const override;
		//~ End FAssetEditorToolkit Interface

		//~ Begin IToolkit Interface
		virtual FText GetBaseToolkitName() const override;
		virtual FName GetToolkitFName() const override;
		virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.0f, 0.0f, 0.2f, 0.5f); }
		virtual FString GetWorldCentricTabPrefix() const override;
		//~ End IToolkit Interface

		// FGCObject interface
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override;
		// End of FGCObject interface

	private:
		/** Internally initializes the toolkit */
		void InitializeInternal(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, const FGuid& MessageLogGuid);

		/** Generates all the views of the asset toolkit */
		void GenerateInternalViews();

		/** Generates the DMX Library View for this Control Console instance */
		TSharedRef<SDMXControlConsoleEditorDMXLibraryView> GenerateDMXLibraryView();

		/** Generates the Layout View for this Control Console instance */
		TSharedRef<SDMXControlConsoleEditorLayoutView> GenerateLayoutView();

		/** Generates the Details View for this Control Console instance */
		TSharedRef<SDMXControlConsoleEditorDetailsView> GenerateDetailsView();

		/** Generates the Filters View for this Control Console instance */
		TSharedRef<SDMXControlConsoleEditorFiltersView> GenerateFiltersView();

		/** Generates the Cue Stack View for this Control Console instance */
		TSharedRef<SDMXControlConsoleEditorCueStackView> GenerateCueStackView();

		/** Spawns the DMX Library View */
		TSharedRef<SDockTab> SpawnTab_DMXLibraryView(const FSpawnTabArgs& Args);

		/** Spawns the Layout View */
		TSharedRef<SDockTab> SpawnTab_LayoutView(const FSpawnTabArgs& Args);

		/** Spawns the Details View */
		TSharedRef<SDockTab> SpawnTab_DetailsView(const FSpawnTabArgs& Args);

		/** Spawns the Filters View */
		TSharedRef<SDockTab> SpawnTab_FiltersView(const FSpawnTabArgs& Args);

		/** Spawns the Cue Stack View */
		TSharedRef<SDockTab> SpawnTab_CueStackView(const FSpawnTabArgs& Args);

		/** Setups the asset toolkit's commands */
		void SetupCommands();

		/** Extends the asset toolkit's toolbar */
		void ExtendToolbar();

		/** If true, stops the control console when this widget is destructed */
		bool bStopSendingDMXOnDestruct = true;

		/** True while switching to compact editor */
		bool bSwitchingToCompactEditor = false;

		/** The DMX toolbar extension for this toolkit's toolbar */
		TSharedPtr<FDMXControlConsoleEditorToolbar> Toolbar;

		/** The DMX Library View instance */
		TSharedPtr<SDMXControlConsoleEditorDMXLibraryView> DMXLibraryView;

		/** The Layout View instance */
		TSharedPtr<SDMXControlConsoleEditorLayoutView> LayoutView;

		/** The Details View instance */
		TSharedPtr<SDMXControlConsoleEditorDetailsView> DetailsView;

		/** The Filters View instance */
		TSharedPtr<SDMXControlConsoleEditorFiltersView> FiltersView;

		/** The Cue Stack View instance */
		TSharedPtr<SDMXControlConsoleEditorCueStackView> CueStackView;

		/** The Play Menu Model for the Control Console this toolkit is based on */
		TObjectPtr<UDMXControlConsoleEditorPlayMenuModel> PlayMenuModel;

		/** The Cue Stack Model for the Control Console this toolkit is based on */
		TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel;

		/** The Editor Model for the Control Console this toolkit is based on */
		TObjectPtr<UDMXControlConsoleEditorModel> EditorModel;

		/** The Control Console object this toolkit is based on */
		TObjectPtr<UDMXControlConsole> ControlConsole;

		/** The analytics provider for this tool */
		UE::DMX::FDMXEditorToolAnalyticsProvider AnalyticsProvider;
	};
}
