// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once 

#include "AudioInsightsDashboardFactory.h"
#include "AudioInsightsTraceModule.h"
#include "Framework/Docking/TabManager.h"
#include "IAudioInsightsModule.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Views/DashboardViewFactory.h"
#include "Widgets/Docking/SDockTab.h"

#if !WITH_EDITOR
#include "AudioInsightsTimingViewExtender.h"
#endif // !WITH_EDITOR

namespace UE::Audio::Insights
{
#if !WITH_EDITOR
	class FAudioInsightsComponent;
#endif // !WITH_EDITOR

	class FAudioInsightsModule final : public IAudioInsightsModule
	{
	public:
		FAudioInsightsModule() = default;

		virtual ~FAudioInsightsModule() = default;

		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
		virtual void RegisterDashboardViewFactory(TSharedRef<IDashboardViewFactory> InDashboardFactory) override;
		virtual void UnregisterDashboardViewFactory(FName InName) override;
		virtual ::Audio::FDeviceId GetDeviceId() const override;

		static FAudioInsightsModule& GetChecked();
		virtual IAudioInsightsTraceModule& GetTraceModule() override;

#if !WITH_EDITOR
		TSharedPtr<FAudioInsightsComponent> GetAudioInsightsComponent() { return AudioInsightsComponent; };
		FAudioInsightsTimingViewExtender& GetTimingViewExtender() { return AudioInsightsTimingViewExtender; };
#endif // !WITH_EDITOR

		TSharedRef<FDashboardFactory> GetDashboardFactory();
		const TSharedRef<FDashboardFactory> GetDashboardFactory() const;

		virtual TSharedRef<SDockTab> CreateDashboardTabWidget(const FSpawnTabArgs& Args) override;

	private:
		TSharedPtr<FDashboardFactory> DashboardFactory;
		TUniquePtr<FTraceModule> TraceModule;

#if !WITH_EDITOR
		TSharedPtr<FAudioInsightsComponent> AudioInsightsComponent;
		FAudioInsightsTimingViewExtender AudioInsightsTimingViewExtender;
#endif // !WITH_EDITOR
	};
} // namespace UE::Audio::Insights
