// Copyright Epic Games, Inc. All Rights Reserved.
#include "IAudioInsightsModule.h"

#include "AudioInsightsModule.h"

IAudioInsightsTraceModule& IAudioInsightsModule::GetTraceModule()
{
	IAudioInsightsModule& AudioInsightsModule = static_cast<IAudioInsightsModule&>(UE::Audio::Insights::FAudioInsightsModule::GetChecked());
	return AudioInsightsModule.GetTraceModule();
}

IAudioInsightsModule& IAudioInsightsModule::GetChecked()
{
	return static_cast<IAudioInsightsModule&>(UE::Audio::Insights::FAudioInsightsModule::GetChecked());
}

#ifdef WITH_EDITOR
IAudioInsightsModule& IAudioInsightsModule::GetEditorChecked()
{
	return static_cast<IAudioInsightsModule&>(FModuleManager::GetModuleChecked<IAudioInsightsModule>("AudioInsightsEditor"));
}
#endif
