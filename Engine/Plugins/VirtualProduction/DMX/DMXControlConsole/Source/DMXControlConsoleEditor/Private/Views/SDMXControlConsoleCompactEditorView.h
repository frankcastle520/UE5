// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"

class FReply;
class FUICommandList;
class SBorder;
class SCheckBox;
class UDMXControlConsole;
class UDMXControlConsoleEditorModel;
class UDMXControlConsoleEditorPlayMenuModel;
class UToolMenu;


namespace UE::DMX::Private
{
	class FDMXControlConsoleCueStackModel;

	/** Compact view of a control console */
	class SDMXControlConsoleCompactEditorView
		: public SCompoundWidget
		, public FGCObject
	{
	public:
		SLATE_BEGIN_ARGS(SDMXControlConsoleCompactEditorView)
			{}

		SLATE_END_ARGS()

		~SDMXControlConsoleCompactEditorView();

		/** Constructs this widget */
		void Construct(const FArguments& InArgs);

	protected:
		//~ Begin SWidget interface
		FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
		//~ End SWidget interface
		
		//~ Begin FGCObject interface
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override;
		//~ End FGCObject interface

	private:
		/** Sets up commands for this widget */
		void SetupCommands();

		/** Creates a toolbar for this view */
		TSharedRef<SWidget> CreateToolbar();

		/** Generates the toolbar widget for managing the Control Console cue stack */
		TSharedRef<SWidget> GenerateCueStackToolbarWidget();

		/** Populates the toolbar. Useful as it may not be possible to populate at construction, e.g. on engine startup */
		static void PopulateToolbar(UToolMenu* InMenu);

		/** Called when the 'Save' button was clicked */
		void OnSaveClicked();

		/** Called when the 'Find In Content Browser' button was clicked */
		void OnFindInContentBrowserClicked();

		/** Called when the 'Show Full Editor' button was clicked */
		FReply OnShowFullEditorButtonClicked();

		/** Returns the name of the asset */
		FText GetAssetNameText() const;

		/** Gets the visibility state of the cue stack */
		EVisibility GetCueStackViewVisibility() const;

		/** The control console editor model this widget uses */
		TObjectPtr<UDMXControlConsole> ControlConsole;

		/** If true, stops sending DMX when this widget is destructed */
		bool bStopSendingDMXOnDestruct = true;

		/** The control console editor model this widget uses */
		TObjectPtr<UDMXControlConsoleEditorModel> EditorModel;

		/** The Cue Stack Model for the Control Console this toolkit is based on */
		TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel;

		/** The play menu model this widget uses */
		TObjectPtr<UDMXControlConsoleEditorPlayMenuModel> PlayMenuModel;

		/** The check box for showing the cue stack */
		TSharedPtr<SCheckBox> CueStackCheckBox;

		/** The command list this widget uses */
		TSharedPtr<FUICommandList> CommandList;

		/** The menu name of the toolbar in this view */
		static const FName ToolbarMenuName;
	};
}
