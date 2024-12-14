// Copyright Epic Games, Inc. All Rights Reserved.
#include "IAudioInsightsEditorModule.h"

#include "AudioInsightsEditorModule.h"

IAudioInsightsTraceModule& IAudioInsightsEditorModule::GetTraceModule()
{
	IAudioInsightsEditorModule& AudioInsightsModule = static_cast<IAudioInsightsEditorModule&>(UE::Audio::Insights::FAudioInsightsEditorModule::GetChecked());
	return AudioInsightsModule.GetTraceModule();
}

IAudioInsightsEditorModule& IAudioInsightsEditorModule::GetChecked()
{
	return static_cast<IAudioInsightsEditorModule&>(UE::Audio::Insights::FAudioInsightsEditorModule::GetChecked());
}
