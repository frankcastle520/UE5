// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once 

#include "AudioInsightsEditorDashboardFactory.h"
#include "AudioInsightsTraceModule.h"
#include "Framework/Docking/TabManager.h"
#include "IAudioInsightsEditorModule.h"
#include "Templates/SharedPointer.h"
#include "Views/DashboardViewFactory.h"
#include "Views/MixerSourceMuteSoloFilter.h"
#include "Views/VirtualLoopsDebugDraw.h"
#include "Widgets/Docking/SDockTab.h"

namespace UE::Audio::Insights
{
	class FAudioInsightsEditorModule final : public IAudioInsightsEditorModule
	{
	public:
		FAudioInsightsEditorModule() = default;

		virtual ~FAudioInsightsEditorModule() = default;

		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
		virtual void RegisterDashboardViewFactory(TSharedRef<IDashboardViewFactory> InDashboardFactory) override;
		virtual void UnregisterDashboardViewFactory(FName InName) override;
		virtual ::Audio::FDeviceId GetDeviceId() const override;

		TSharedRef<FEditorDashboardFactory> GetDashboardFactory();
		const TSharedRef<FEditorDashboardFactory> GetDashboardFactory() const;

		static FAudioInsightsEditorModule& GetChecked();
		virtual IAudioInsightsTraceModule& GetTraceModule() override;

	private:
		void RegisterMenus();
		TSharedRef<SDockTab> CreateDashboardTabWidget(const FSpawnTabArgs& Args);
		
		TSharedPtr<FEditorDashboardFactory> DashboardFactory;
		
		FVirtualLoopsDebugDraw VirtualLoopsDebugDraw;
		FMuteSoloFilter MuteSoloFilter;
	};
} // namespace UE::Audio::Insights
