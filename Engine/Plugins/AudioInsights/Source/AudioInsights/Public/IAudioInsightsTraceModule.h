// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioInsightsTraceChannelHandle.h"
#include "AudioInsightsTraceProviderBase.h"
#include "TraceServices/ModuleService.h"

namespace UE::Audio::Insights
{
	class FTraceProviderBase;
} // namespace UE::Audio::Insights

class AUDIOINSIGHTS_API IAudioInsightsTraceModule : public TraceServices::IModule
{
public:
	virtual void AddTraceProvider(TSharedPtr<UE::Audio::Insights::FTraceProviderBase> TraceProvider) = 0;

	virtual void StartTraceAnalysis() const = 0;
	virtual bool IsTraceAnalysisActive() const = 0;
	virtual void StopTraceAnalysis() const = 0;

	virtual TSharedRef<UE::Audio::Insights::FTraceChannelManager> GetChannelManager() = 0;
};
