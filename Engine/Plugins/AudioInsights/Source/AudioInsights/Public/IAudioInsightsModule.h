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

namespace UE::Audio::Insights
{
	class IDashboardViewFactory;
	class FTraceProviderBase;
} // namespace UE::Audio::Insights

class AUDIOINSIGHTS_API IAudioInsightsModule : public IAudioInsightsModuleInterface
{
public:
	virtual void RegisterDashboardViewFactory(TSharedRef<UE::Audio::Insights::IDashboardViewFactory> InDashboardFactory) = 0;
	virtual void UnregisterDashboardViewFactory(FName InName) = 0;

	virtual ::Audio::FDeviceId GetDeviceId() const = 0;

	virtual IAudioInsightsTraceModule& GetTraceModule() override;

	virtual TSharedRef<SDockTab> CreateDashboardTabWidget(const FSpawnTabArgs& Args) = 0;

	static IAudioInsightsModule& GetChecked();

#ifdef WITH_EDITOR
	static IAudioInsightsModule& GetEditorChecked();
#endif

};
