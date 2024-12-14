// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAvaKeyFrameEdit.h"
#include "AvaSequencer.h"
#include "EaseCurveTool/AvaEaseCurveTool.h"
#include "MVVM/Selection/Selection.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"

using namespace UE::Sequencer;

#define LOCTEXT_NAMESPACE "SAvaKeyFrameEdit"

void SAvaKeyFrameEdit::Construct(const FArguments& InArgs, const TSharedRef<FAvaSequencer>& InSequencer)
{
	AvaSequencerWeak = InSequencer;

	KeyEditData = InArgs._KeyEditData;

	const TSharedRef<ISequencer> Sequencer = InSequencer->GetSequencer();

	if (const TSharedPtr<FSequencerEditorViewModel> SequencerViewModel = Sequencer->GetViewModel())
	{
		SequencerSelectionWeak = SequencerViewModel->GetSelection();
	}

	if (!AvaSequencerWeak.IsValid())
	{
		return;
	}

	const TSharedPtr<FAvaSequencer> AvaSequencer = AvaSequencerWeak.Pin();

	ChildSlot
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.AutoSize()
			.HAlign(HAlign_Fill)
			.Padding(1.f, 1.f, 1.f, 3.f)
			[
				SNew(SScaleBox)
				.Stretch(EStretch::Fill)
				[
					InSequencer->GetEaseCurveTool()->GenerateWidget()
				]
			]
			+ SScrollBox::Slot()
			.AutoSize()
			.HAlign(HAlign_Fill)
			[
				SNew(SKeyEditInterface, AvaSequencer->GetSequencer())
				.EditData(KeyEditData)
			]
		];
}

#undef LOCTEXT_NAMESPACE
