// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorUndoClient.h"
#include "Widgets/SCompoundWidget.h"

class FReply;
struct FSlateBrush;
struct FSlateColor;


namespace UE::DMX::Private
{
	class FDMXControlConsoleCueStackModel;
	class SDMXControlConsoleEditorCueList;

	/** View for displaying the cue stack of the edited Control Console */
	class SDMXControlConsoleEditorCueStackView
		: public SCompoundWidget
		, public FSelfRegisteringEditorUndoClient
	{
	public:
		SLATE_BEGIN_ARGS(SDMXControlConsoleEditorCueStackView)
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
		/** Generates a toolbar for the Cue List this view displays */
		TSharedRef<SWidget> GenerateCueListToolbar();

		/** Generates the content of a Cue List toolbar button with the given parameters */
		TSharedRef<SWidget> GenerateCueListToolbarButtonContent(const FText& Label, const FText& ToolTip, const FSlateBrush* IconBrush, const FSlateColor IconColor);

		/** True if there's at least one fader group in the current control console */
		bool IsAddNewCueButtonEnabled() const;

		/** True if there's at least one selected cue item */
		bool IsStoreCueButtonEnabled() const;

		/** True if there's at least one selected cue item */
		bool IsRecallCueButtonEnabled() const;

		/** True if at least one cue exists in the current cue stack */
		bool IsClearAllCuesButtonEnabled() const;

		/** Called when the add new cue button is clicked */
		FReply OnAddNewCueClicked();

		/** Called when the store cue button is clicked */
		FReply OnStoreCueClicked();

		/** Called when the recall cue button is clicked */
		FReply OnRecallCueClicked();

		/** Called when the clear cue stack button is clicked */
		FReply OnClearCueStackClicked();

		/** Called when the DMX Library has been changed */
		void OnDMXLibraryChanged();

		/** Reference to the Cue List this view displays */
		TSharedPtr<SDMXControlConsoleEditorCueList> CueList;

		/** Weak reference to the Control Console Cue Stack Model */
		TWeakPtr<FDMXControlConsoleCueStackModel> WeakCueStackModel;
	};
}
