// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioInsightsTraceProviderBase.h"

#if !WITH_EDITOR
#include "AudioInsightsModule.h"
#include "AudioInsightsTimingViewExtender.h"
#include "Modules/ModuleManager.h"
#endif // !WITH_EDITOR

namespace UE::Audio::Insights
{
	FTraceProviderBase::FTraceProviderBase(FName InName)
		: Name(InName)
	{
#if !WITH_EDITOR
		FAudioInsightsModule& AudioInsightsModule = FAudioInsightsModule::GetChecked();
		FAudioInsightsTimingViewExtender& AudioInsightsTimingViewExtender = AudioInsightsModule.GetTimingViewExtender();
		AudioInsightsTimingViewExtender.OnTimingViewTimeMarkerChanged.AddRaw(this, &FTraceProviderBase::OnTimingViewTimeMarkerChanged);
#endif // !WITH_EDITOR
	}

	FTraceProviderBase::~FTraceProviderBase()
	{
#if !WITH_EDITOR
		if (FModuleManager::Get().IsModuleLoaded("AudioInsights"))
		{
			FAudioInsightsModule& AudioInsightsModule = FAudioInsightsModule::GetChecked();
			FAudioInsightsTimingViewExtender& AudioInsightsTimingViewExtender = AudioInsightsModule.GetTimingViewExtender();
			AudioInsightsTimingViewExtender.OnTimingViewTimeMarkerChanged.RemoveAll(this);
		}
#endif // !WITH_EDITOR
	}

	FName FTraceProviderBase::GetName() const
	{
		return Name;
	}

	FTraceProviderBase::FTraceAnalyzerBase::FTraceAnalyzerBase(TSharedRef<FTraceProviderBase> InProvider)
		: Provider(InProvider)
	{
	}

	void FTraceProviderBase::FTraceAnalyzerBase::OnAnalysisBegin(const FOnAnalysisContext& Context)
	{
		Provider->Reset();
	}

	bool FTraceProviderBase::FTraceAnalyzerBase::OnEventSuccess(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
	{
		++(Provider->LastMessageId);
		return true;
	}

	bool FTraceProviderBase::FTraceAnalyzerBase::OnEventFailure(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
	{
		++(Provider->LastMessageId);

		const FString ProviderName = Provider->GetName().ToString();
		constexpr bool bEventSucceeded = false;
		ensureAlwaysMsgf(bEventSucceeded, TEXT("'%s' TraceProvider's Analyzer message with RouteId '%u' event not handled"), *ProviderName, RouteId);
		return bEventSucceeded;
	}
} // namespace UE::Audio::Insights
