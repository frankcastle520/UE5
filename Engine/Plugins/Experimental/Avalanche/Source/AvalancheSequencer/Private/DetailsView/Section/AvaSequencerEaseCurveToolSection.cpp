// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaSequencerEaseCurveToolSection.h"
#include "AvaSequence.h"
#include "AvaSequencer.h"
#include "EaseCurveTool/AvaEaseCurveTool.h"
#include "Widgets/SCompoundWidget.h"

#define LOCTEXT_NAMESPACE "AvaSequencerEaseCurveToolSection"

const FName FAvaSequencerEaseCurveToolSection::UniqueId = TEXT("AvaSequencerEaseCurveToolSection");

FAvaSequencerEaseCurveToolSection::FAvaSequencerEaseCurveToolSection(const TSharedRef<FAvaSequencer>& InAvaSequencer)
	: AvaSequencerWeak(InAvaSequencer)
{
}

FName FAvaSequencerEaseCurveToolSection::GetUniqueId() const
{
	return UniqueId;
}

FName FAvaSequencerEaseCurveToolSection::GetSectionId() const
{
	return TEXT("Selection");
}

FText FAvaSequencerEaseCurveToolSection::GetSectionDisplayText() const
{
	return LOCTEXT("EaseCurveToolLabel", "Ease Curve Tool");
}

bool FAvaSequencerEaseCurveToolSection::ShouldShowSection() const
{
	const TSharedPtr<FAvaSequencer> AvaSequencer = AvaSequencerWeak.Pin();
	return AvaSequencer.IsValid() && IsValid(AvaSequencer->GetViewedSequence());
}

int32 FAvaSequencerEaseCurveToolSection::GetSortOrder() const
{
	return -1;
}

TSharedRef<SWidget> FAvaSequencerEaseCurveToolSection::CreateContentWidget()
{
	if (const TSharedPtr<FAvaSequencer> AvaSequencer = AvaSequencerWeak.Pin())
	{
		return AvaSequencer->GetEaseCurveTool()->GenerateWidget();
	}
	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE
