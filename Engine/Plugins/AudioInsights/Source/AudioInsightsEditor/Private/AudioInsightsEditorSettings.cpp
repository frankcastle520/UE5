// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioInsightsEditorSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AudioInsightsEditorSettings)

#define LOCTEXT_NAMESPACE "UAudioInsightsEditorSettings"

FName UAudioInsightsEditorSettings::GetCategoryName() const
{
	return "Plugins";
}

FText UAudioInsightsEditorSettings::GetSectionText() const
{
	return LOCTEXT("AudioInsightsEditorSettings_SectionText", "Audio Insights");
}

FText UAudioInsightsEditorSettings::GetSectionDescription() const
{
	return LOCTEXT("AudioInsightsEditorSettings_SectionDesc", "Configure Audio Insights editor settings.");
}

#undef LOCTEXT_NAMESPACE
