// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EaseCurveTool/AvaEaseCurveTangents.h"
#include "EaseCurveTool/Widgets/SAvaEaseCurvePresetComboBox.h"
#include "EaseCurveTool/Widgets/SAvaEaseCurvePresetGroup.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"

class FName;
class FText;
class SUniformWrapPanel;
struct FAvaEaseCurvePreset;

DECLARE_DELEGATE_RetVal_OneParam(bool, FAvaOnGetNewPresetTangents, FAvaEaseCurveTangents& /*InTangents*/)

class SAvaEaseCurvePreset : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAvaEaseCurvePreset)
		: _DisplayRate(FFrameRate(30, 1))
	{}
		/** Display rate used to draw the ease curve preview. */
		SLATE_ATTRIBUTE(FFrameRate, DisplayRate)
		SLATE_EVENT(FAvaOnPresetChanged, OnPresetChanged)
		SLATE_EVENT(FAvaOnPresetChanged, OnQuickPresetChanged)
		SLATE_EVENT(FAvaOnGetNewPresetTangents, OnGetNewPresetTangents)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void ClearSelection();
	
	bool SetSelectedItem(const FString& InName);
	bool SetSelectedItem(const FAvaEaseCurveTangents& InTangents);
	bool SetSelectedItem(const FAvaEaseCurvePreset& InPreset);

protected:
	FReply OnCreateNewPresetClick();
	FReply OnCancelNewPresetClick();
	
	FReply OnNewPresetKeyDownHandler(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent);
	void OnNewPresetTextCommitted(const FText& InNewText, ETextCommit::Type InCommitType);

	TAttribute<FFrameRate> DisplayRate;
	FAvaOnPresetChanged OnPresetChanged;
	FAvaOnPresetChanged OnQuickPresetChanged;
	FAvaOnGetNewPresetTangents OnGetNewPresetTangents;
	
	TSharedPtr<SAvaEaseCurvePresetComboBox> PresetComboBox;
	TSharedPtr<SEditableTextBox> NewPresetNameTextBox;

	bool bIsCreatingNewPreset = false;
};
