// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioInsightsTraceChannelHandle.h"
#include "AudioInsightsTraceProviderBase.h"
#include "Insights/Messages/ControlBusTraceMessages.h"


namespace AudioModulationEditor
{
	class FControlBusTraceProvider
		: public UE::Audio::Insights::TDeviceDataMapTraceProvider<uint32, TSharedPtr<FControlBusDashboardEntry>>
		, public TSharedFromThis<FControlBusTraceProvider>
	{
	public:
		FControlBusTraceProvider(TSharedRef<UE::Audio::Insights::FTraceChannelManager> InManager)
			: UE::Audio::Insights::TDeviceDataMapTraceProvider<uint32, TSharedPtr<FControlBusDashboardEntry>>(GetName_Static())
		{
			Channels.Add(InManager->CreateHandle({ TEXT("AudioChannel") }));
		}

		virtual ~FControlBusTraceProvider() = default;
		virtual UE::Trace::IAnalyzer* ConstructAnalyzer(TraceServices::IAnalysisSession& InSession) override;

		virtual bool ProcessMessages() override;
		static FName GetName_Static();

	private:

		FControlBusMessages TraceMessages;
		TSet<UE::Audio::Insights::FTraceChannelHandle> Channels;
	};
} // namespace AudioModulationEditor
