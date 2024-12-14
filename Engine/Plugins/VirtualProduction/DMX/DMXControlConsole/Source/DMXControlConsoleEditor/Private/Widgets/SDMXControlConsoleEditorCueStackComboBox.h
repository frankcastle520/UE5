// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorUndoClient.h"
#include "Widgets/SCompoundWidget.h"

struct FSlateColor;
template <typename OptionType> class SComboBox;


namespace UE::DMX::Private
{
	class FDMXControlConsoleCueStackModel;
	class FDMXControlConsoleEditorCueListItem;

	/** A combo box widget for selecting cues in the Control Console cue stack */
	class SDMXControlConsoleEditorCueStackComboBox
		: public SCompoundWidget
		, public FSelfRegisteringEditorUndoClient
	{
	public:
		SLATE_BEGIN_ARGS(SDMXControlConsoleEditorCueStackComboBox)
			{}

		SLATE_END_ARGS()

		/** Constructs the widget */
		void Construct(const FArguments& InArgs, TSharedPtr<FDMXControlConsoleCueStackModel> InCueStackModel);

	protected:
		//~ Begin FSelfRegisteringEditorUndoClient interface
		virtual bool MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const override;
		virtual void PostUndo(bool bSuccess) override;
		virtual void PostRedo(bool bSuccess) override;
		//~ End FSelfRegisteringEditorUndoClient interface

	private:
		/** Generates the content widget for the cue stack combo box */
		TSharedRef<SWidget> GenerateComboBoxContentWidget();

		/** Generates a widget for each element in the cue stack combo box */
		TSharedRef<SWidget> GenerateComboBoxOptionWidget(const TSharedPtr<FDMXControlConsoleEditorCueListItem> CueItem);

		/** Updates the array of combo box source items */
		void UpdateCueStackComboBoxSource();

		/** Called when selection in the combo box changed */
		void OnCueStackComboBoxSelectionChanged(const TSharedPtr<FDMXControlConsoleEditorCueListItem> NewSelection, ESelectInfo::Type SelectInfo);

		/** True if there's at least one fader group in the current control console */
		bool IsAddNewCueButtonEnabled() const;

		/** True if there's at least one selected cue item */
		bool IsStoreCueButtonEnabled() const;

		/** Called when the add new cue button is clicked */
		FReply OnAddNewCueClicked();

		/** Called when the store cue button is clicked */
		FReply OnStoreCueClicked();

		/** Gets the color of the loaded cue, if valid */
		FSlateColor GetLoadedCueColor() const;

		/** Gets the name label of the loaded cue as text, if valid */
		FText GetLoadedCueNameAsText() const;

		/** Source items for the CueStackComboBox */
		TArray<TSharedPtr<FDMXControlConsoleEditorCueListItem>> ComboBoxSource;

		/** A ComboBox for showing all the cues in the Control Console cue stack */
		TSharedPtr<SComboBox<TSharedPtr<FDMXControlConsoleEditorCueListItem>>> CueStackComboBox;

		/** Weak reference to the Control Console Cue Stack Model */
		TWeakPtr<FDMXControlConsoleCueStackModel> WeakCueStackModel;
	};
}
