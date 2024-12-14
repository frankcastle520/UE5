// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioVectorscope.h"

#include "SAudioVectorscopePanelWidget.h"

namespace AudioWidgets
{
	FAudioVectorscope::FAudioVectorscope(Audio::FDeviceId InAudioDeviceId,
		const uint32 InNumChannels,
		const float InTimeWindowMs,
		const float InMaxTimeWindowMs,
		const float InAnalysisPeriodMs,
		const EAudioPanelLayoutType InPanelLayoutType,
		const FAudioVectorscopePanelStyle* InPanelStyle)
	{
		VectorscopePanelStyle = InPanelStyle ? *InPanelStyle : FAudioVectorscopePanelStyle::GetDefault();
		CreateAudioBus(InNumChannels);
		CreateDataProvider(InAudioDeviceId, InTimeWindowMs, InMaxTimeWindowMs, InAnalysisPeriodMs);
		CreateVectorscopeWidget(InPanelLayoutType);
	}

	void FAudioVectorscope::CreateAudioBus(const uint32 InNumChannels)
	{
		AudioBus = TStrongObjectPtr(NewObject<UAudioBus>());
		AudioBus->AudioBusChannels = AudioBusUtils::ConvertIntToEAudioBusChannels(InNumChannels);
	}

	void FAudioVectorscope::CreateDataProvider(Audio::FDeviceId InAudioDeviceId, const float InTimeWindowMs,	const float InMaxTimeWindowMs, const float InAnalysisPeriodMs)
	{
		check(AudioBus);

		AudioSamplesDataProvider = MakeShared<FWaveformAudioSamplesDataProvider>(InAudioDeviceId, AudioBus.Get(), AudioBus->GetNumChannels(), InTimeWindowMs, InMaxTimeWindowMs, InAnalysisPeriodMs);
	}

	void FAudioVectorscope::CreateVectorscopeWidget(const EAudioPanelLayoutType InPanelLayoutType, const FAudioVectorscopePanelStyle* PanelStyle)
	{
		check(AudioSamplesDataProvider);

		const FFixedSampledSequenceView SequenceView = AudioSamplesDataProvider->GetDataView();

		if (PanelStyle)
		{
			VectorscopePanelStyle = *PanelStyle;
		}

		if (!VectorscopePanelWidget.IsValid())
		{
			VectorscopePanelWidget = SNew(SAudioVectorscopePanelWidget, SequenceView)
				.PanelStyle(PanelStyle ? PanelStyle : &FAudioVectorscopePanelStyle::GetDefault())
				.PanelLayoutType(InPanelLayoutType)
				.PanelStyle(&VectorscopePanelStyle);
		}
		else
		{
			VectorscopePanelWidget->BuildWidget(SequenceView, InPanelLayoutType);
		}

		// Interconnect data provider and widget
		AudioSamplesDataProvider->OnDataViewGenerated.AddSP(VectorscopePanelWidget.Get(), &SAudioVectorscopePanelWidget::ReceiveSequenceView);

		if (InPanelLayoutType == EAudioPanelLayoutType::Advanced)
		{
			VectorscopePanelWidget->OnDisplayPersistenceValueChanged.AddSP(AudioSamplesDataProvider.Get(), &FWaveformAudioSamplesDataProvider::SetTimeWindow);
		}
	}

	void FAudioVectorscope::StartProcessing()
	{
		AudioSamplesDataProvider->StartProcessing();
	}

	void FAudioVectorscope::StopProcessing()
	{
		AudioSamplesDataProvider->StopProcessing();
	}

	UAudioBus* FAudioVectorscope::GetAudioBus() const
	{
		return AudioBus.Get();
	}

	TSharedRef<SWidget> FAudioVectorscope::GetPanelWidget() const
	{
		return VectorscopePanelWidget.ToSharedRef();
	}
} // namespace AudioWidgets
