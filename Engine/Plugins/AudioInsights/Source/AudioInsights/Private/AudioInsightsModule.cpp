// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioInsightsModule.h"

#include "AudioInsightsDashboardAssetCommands.h"
#include "AudioInsightsDashboardFactory.h"
#include "AudioInsightsLog.h"
#include "AudioInsightsTraceModule.h"
#include "Features/IModularFeatures.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"
#include "TraceServices/ModuleService.h"
#include "UObject/NameTypes.h"

#if !WITH_EDITOR
#include "AudioInsightsComponent.h"
#include "Views/MixerSourceDashboardViewFactory.h"
#include "Views/VirtualLoopDashboardViewFactory.h"
#endif // !WITH_EDITOR

#define LOCTEXT_NAMESPACE "AudioInsights"
DEFINE_LOG_CATEGORY(LogAudioInsights);


namespace UE::Audio::Insights
{
	void FAudioInsightsModule::StartupModule()
	{
		// Don't run providers in any commandlet to avoid additional, unnecessary overhead as audio insights is dormant.
		if (!IsRunningCommandlet())
		{
			TraceModule = MakeUnique<FTraceModule>();
			IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, TraceModule.Get());

			DashboardFactory = MakeShared<FDashboardFactory>();
			
			FDashboardAssetCommands::Register();

#if !WITH_EDITOR
			IModularFeatures::Get().RegisterModularFeature(UE::Insights::Timing::TimingViewExtenderFeatureName, &AudioInsightsTimingViewExtender);

			DashboardFactory->RegisterViewFactory(MakeShared<FMixerSourceDashboardViewFactory>());
			DashboardFactory->RegisterViewFactory(MakeShared<FVirtualLoopDashboardViewFactory>());
			
			AudioInsightsComponent = FAudioInsightsComponent::CreateInstance();

			IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
			UnrealInsightsModule.RegisterComponent(AudioInsightsComponent);
#endif // !WITH_EDITOR

			FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]
			{
				LLM_SCOPE_BYNAME(TEXT("Insights/AudioInsights"));
				IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
				if (!UnrealInsightsModule.GetStoreClient())
				{
					UE_LOG(LogCore, Display, TEXT("AudioInsights module auto-connecting to local trace server..."));
					UnrealInsightsModule.ConnectToStore(TEXT("127.0.0.1"));
					UnrealInsightsModule.CreateSessionViewer(false);
				}
			});
		}
	}

	void FAudioInsightsModule::ShutdownModule()
	{
		if (!IsRunningCommandlet())
		{
#if !WITH_EDITOR
			IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
			UnrealInsightsModule.UnregisterComponent(AudioInsightsComponent);

			AudioInsightsComponent.Reset();

			IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &AudioInsightsTimingViewExtender);
#endif // !WITH_EDITOR

			FDashboardAssetCommands::Unregister();

			DashboardFactory.Reset();

			IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, TraceModule.Get());
		}
	}

	void FAudioInsightsModule::RegisterDashboardViewFactory(TSharedRef<IDashboardViewFactory> InDashboardFactory)
	{
		DashboardFactory->RegisterViewFactory(InDashboardFactory);
	}

	void FAudioInsightsModule::UnregisterDashboardViewFactory(FName InName)
	{
		DashboardFactory->UnregisterViewFactory(InName);
	}

	::Audio::FDeviceId FAudioInsightsModule::GetDeviceId() const
	{
		return DashboardFactory->GetDeviceId();
	}

	FAudioInsightsModule& FAudioInsightsModule::GetChecked()
	{
		return static_cast<FAudioInsightsModule&>(FModuleManager::GetModuleChecked<IAudioInsightsModule>("AudioInsights"));
	}

	TSharedRef<FDashboardFactory> FAudioInsightsModule::GetDashboardFactory()
	{
		return DashboardFactory->AsShared();
	}

	const TSharedRef<FDashboardFactory> FAudioInsightsModule::GetDashboardFactory() const
	{
		return DashboardFactory->AsShared();
	}

	IAudioInsightsTraceModule& FAudioInsightsModule::GetTraceModule()
	{
		return *TraceModule;
	}

	TSharedRef<SDockTab> FAudioInsightsModule::CreateDashboardTabWidget(const FSpawnTabArgs& Args)
	{
		return DashboardFactory->MakeDockTabWidget(Args);
	}
} // namespace UE::Audio::Insights
#undef LOCTEXT_NAMESPACE // AudioInsights

IMPLEMENT_MODULE(UE::Audio::Insights::FAudioInsightsModule, AudioInsights)
