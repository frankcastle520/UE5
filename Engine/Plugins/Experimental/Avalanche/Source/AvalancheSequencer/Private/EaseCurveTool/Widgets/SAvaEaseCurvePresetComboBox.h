// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EaseCurveTool/AvaEaseCurveTangents.h"
#include "EaseCurveTool/Widgets/SAvaEaseCurvePresetGroup.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"

class FName;
class FText;
class SUniformWrapPanel;
struct FAvaEaseCurvePreset;

DECLARE_DELEGATE_OneParam(FAvaOnPresetChanged, const TSharedPtr<FAvaEaseCurvePreset>&)

class SAvaEaseCurvePresetComboBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAvaEaseCurvePresetComboBox)
		: _DisplayRate(FFrameRate(30, 1))
		, _AllowEditMode(false)
	{}
		/** Display rate used to draw the ease curve preview. */
		SLATE_ATTRIBUTE(FFrameRate, DisplayRate)
		SLATE_ARGUMENT(bool, AllowEditMode)
		SLATE_EVENT(FAvaOnPresetChanged, OnPresetChanged)
		SLATE_EVENT(FAvaOnPresetChanged, OnQuickPresetChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool HasSelection() const;
	
	void ClearSelection();
	
	bool GetSelectedItem(FAvaEaseCurvePreset& OutPreset) const;
	
	bool SetSelectedItem(const FString& InName);
	bool SetSelectedItem(const FAvaEaseCurveTangents& InTangents);
	bool SetSelectedItem(const FAvaEaseCurvePreset& InPreset);

protected:
	TSharedRef<SWidget> GenerateSearchRowWidget();
	TSharedRef<SWidget> GeneratePresetDropdown();

	void GenerateSelectedRowWidget();

	void UpdateGroupsContent();
	void RegenerateGroupWrapBox();
	
	FReply OnDeletePresetClick();

	void OnSearchTextChanged(const FText& InSearchText);

	FReply ReloadJsonPresets();
	FReply ExploreJsonPresetsFolder();
	FReply CreateNewCategory();

	void ToggleEditMode(const ECheckBoxState bInNewState);

	bool HandleCategoryDelete(const FString& InCategoryName);
	bool HandleCategoryRename(const FString& InCategoryName, const FString& InNewName);
	bool HandlePresetDelete(const TSharedPtr<FAvaEaseCurvePreset>& InPreset);
	bool HandlePresetRename(const TSharedPtr<FAvaEaseCurvePreset>& InPreset, const FString& InNewName);
	bool HandleBeginPresetMove(const TSharedPtr<FAvaEaseCurvePreset>& InPreset, const FString& InNewCategoryName);
	bool HandleEndPresetMove(const TSharedPtr<FAvaEaseCurvePreset>& InPreset, const FString& InNewCategoryName);
	bool HandlePresetClick(const TSharedPtr<FAvaEaseCurvePreset>& InPreset);
	bool HandleSetQuickEase(const TSharedPtr<FAvaEaseCurvePreset>& InPreset);

	TAttribute<FFrameRate> DisplayRate;
	bool bAllowEditMode = false;
	FAvaOnPresetChanged OnPresetChanged;
	FAvaOnPresetChanged OnQuickPresetChanged;

	TSharedPtr<SBox> SelectedRowContainer;
	TSharedPtr<SBox> GroupWidgetsParent;
	TSharedPtr<SUniformWrapPanel> GroupWrapBox;
	TArray<TSharedPtr<SAvaEaseCurvePresetGroup>> GroupWidgets;

	TSharedPtr<FAvaEaseCurvePreset> SelectedItem;

	FText SearchText;

	TAttribute<bool> bEditMode;
};
