// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioInsightsTraceModule.h"

#include "CoreGlobals.h"
#include "Insights/IUnrealInsightsModule.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Providers/MixerSourceTraceProvider.h"
#include "Providers/VirtualLoopTraceProvider.h"
#include "Templates/SharedPointer.h"
#include "Trace/Trace.h"


namespace UE::Audio::Insights
{
	FTraceModule::FTraceModule()
		: ChannelManager(MakeShared<FTraceChannelManager>())
	{
		// Don't run providers in any commandlet to avoid additional, unnecessary overhead as audio insights is dormant.
		if (!IsRunningCommandlet())
		{
			TSharedPtr<FMixerSourceTraceProvider> SourceProvider = MakeShared<FMixerSourceTraceProvider>(ChannelManager);
			TSharedPtr<FVirtualLoopTraceProvider> VirtualLoopProvider = MakeShared<FVirtualLoopTraceProvider>(ChannelManager);

			TraceProviders.Add(SourceProvider->GetName(), SourceProvider);
			TraceProviders.Add(VirtualLoopProvider->GetName(), VirtualLoopProvider);
		}
	}

	void FTraceModule::GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo)
	{
		OutModuleInfo.Name = GetName();
		OutModuleInfo.DisplayName = TEXT("Audio");
	}

	void FTraceModule::AddTraceProvider(TSharedPtr<FTraceProviderBase> TraceProvider)
	{
		TraceProviders.Add(TraceProvider->GetName(), TraceProvider);
	}

	TSharedRef<FTraceChannelManager> FTraceModule::GetChannelManager()
	{
		return ChannelManager;
	}

	const FName FTraceModule::GetName()
	{
		const FLazyName TraceName = { "TraceModule_AudioTrace" };
		return TraceName.Resolve();
	}

	void FTraceModule::DisableAllTraceChannels()
	{
		UE::Trace::EnumerateChannels([](const ANSICHAR* ChannelName, bool bEnabled, void*)
			{
				if (bEnabled)
				{
					FString ChannelNameFString(ChannelName);
					UE::Trace::ToggleChannel(ChannelNameFString.GetCharArray().GetData(), false);
				}
			}
		, nullptr);
	}

	void FTraceModule::EnableAudioInsightsTraceChannels()
	{
		UE::Trace::ToggleChannel(TEXT("Audio"), true);
		UE::Trace::ToggleChannel(TEXT("AudioMixer"), true);
	}

	void FTraceModule::OnAnalysisBegin(TraceServices::IAnalysisSession& InSession)
	{
		for (const auto& [ProviderName, Provider] : TraceProviders)
		{
#if !WITH_EDITOR
			Provider->InitSessionCachedMessages(InSession);
#endif // !WITH_EDITOR

			InSession.AddProvider(ProviderName, Provider, Provider);
			InSession.AddAnalyzer(Provider->ConstructAnalyzer(InSession));
		}

		FirstTimeStamp = -TNumericLimits<double>::Min();
	}

	void FTraceModule::StartTraceAnalysis() const
	{
		if (!FTraceAuxiliary::IsConnected())
		{
			DisableAllTraceChannels();
			EnableAudioInsightsTraceChannels();

			// Clear all buffered data and prevent data from previous recordings from leaking into the new recording
			FTraceAuxiliary::FOptions Options;
			Options.bExcludeTail = true;

			FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::Network, TEXT("localhost"), TEXT(""), &Options);

			IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
			UnrealInsightsModule.StartAnalysisForLastLiveSession();
		}
	}

	void FTraceModule::StopTraceAnalysis() const
	{
		if (FTraceAuxiliary::IsConnected())
		{
			FTraceAuxiliary::Stop();
		}
	}

	bool FTraceModule::IsTraceAnalysisActive() const
	{
		return FTraceAuxiliary::IsConnected();
	}

	void FTraceModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
	{
		OutLoggers.Add(TEXT("Audio"));
	}

	void FTraceModule::GenerateReports(const TraceServices::IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory)
	{

	}
} // namespace UE::Audio::Insights
