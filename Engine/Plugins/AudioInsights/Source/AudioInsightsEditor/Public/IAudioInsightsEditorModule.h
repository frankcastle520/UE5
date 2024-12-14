// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDefines.h"
#include "Framework/Docking/TabManager.h"
#include "IAudioInsightsModuleInterface.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"
#include "Widgets/Docking/SDockTab.h"

class IAudioInsightsTraceModule;
class SDockTab;

namespace UE::Audio::Insights
{
	class IDashboardViewFactory;
	class FTraceProviderBase;
} // namespace UE::Audio::Insights

class IAudioInsightsEditorModule : public IAudioInsightsModuleInterface
{
public:
	AUDIOINSIGHTSEDITOR_API virtual void RegisterDashboardViewFactory(TSharedRef<UE::Audio::Insights::IDashboardViewFactory> InDashboardFactory) = 0;
	AUDIOINSIGHTSEDITOR_API virtual void UnregisterDashboardViewFactory(FName InName) = 0;

	AUDIOINSIGHTSEDITOR_API virtual ::Audio::FDeviceId GetDeviceId() const = 0;

	AUDIOINSIGHTSEDITOR_API virtual IAudioInsightsTraceModule& GetTraceModule() override;

	AUDIOINSIGHTSEDITOR_API virtual TSharedRef<SDockTab> CreateDashboardTabWidget(const FSpawnTabArgs& Args) = 0;

	AUDIOINSIGHTSEDITOR_API static IAudioInsightsEditorModule& GetChecked();
};
