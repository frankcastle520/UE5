// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ConstantQ.h"
#include "SAudioSpectrumPlot.h"
#include "Sound/AudioBus.h"
#include "SynesthesiaSpectrumAnalysis.h"
#include "UObject/StrongObjectPtr.h"
#include "AudioSpectrumAnalyzer.generated.h"

class UWorld;

UENUM(BlueprintType)
enum class EAudioSpectrumAnalyzerBallistics : uint8
{
	Analog,
	Digital,
};

UENUM(BlueprintType)
enum class EAudioSpectrumAnalyzerType : uint8
{
	FFT UMETA(ToolTip = "Fast Fourier Transform"),
	CQT UMETA(ToolTip = "Constant-Q Transform"),
};

DECLARE_DELEGATE_OneParam(FOnBallisticsMenuEntryClicked, EAudioSpectrumAnalyzerBallistics);
DECLARE_DELEGATE_OneParam(FOnAnalyzerTypeMenuEntryClicked, EAudioSpectrumAnalyzerType);
DECLARE_DELEGATE_OneParam(FOnFFTAnalyzerFFTSizeMenuEntryClicked, EFFTSize);
DECLARE_DELEGATE_OneParam(FOnCQTAnalyzerFFTSizeMenuEntryClicked, EConstantQFFTSizeEnum);

namespace AudioWidgets
{
	/**
	 * Constructor parameters for the analyzer.
	 */
	struct AUDIOWIDGETS_API FAudioSpectrumAnalyzerParams
	{
		int32 NumChannels = 1;
		Audio::FDeviceId AudioDeviceId = INDEX_NONE;
		TObjectPtr<UAudioBus> ExternalAudioBus = nullptr;

		TAttribute<EAudioSpectrumAnalyzerBallistics> Ballistics = EAudioSpectrumAnalyzerBallistics::Digital;
		TAttribute<EAudioSpectrumAnalyzerType> AnalyzerType = EAudioSpectrumAnalyzerType::CQT;
		TAttribute<EFFTSize> FFTAnalyzerFFTSize = EFFTSize::Max;
		TAttribute<EConstantQFFTSizeEnum> CQTAnalyzerFFTSize = EConstantQFFTSizeEnum::XXLarge;
		TAttribute<float> TiltExponent = 0.0f;
		TAttribute<EAudioSpectrumPlotFrequencyAxisPixelBucketMode> FrequencyAxisPixelBucketMode = EAudioSpectrumPlotFrequencyAxisPixelBucketMode::Average;
		TAttribute<EAudioSpectrumPlotFrequencyAxisScale> FrequencyAxisScale = EAudioSpectrumPlotFrequencyAxisScale::Logarithmic;
		TAttribute<bool> bDisplayFrequencyAxisLabels = false;
		TAttribute<bool> bDisplaySoundLevelAxisLabels = false;
		
		FOnBallisticsMenuEntryClicked OnBallisticsMenuEntryClicked;
		FOnAnalyzerTypeMenuEntryClicked OnAnalyzerTypeMenuEntryClicked;
		FOnFFTAnalyzerFFTSizeMenuEntryClicked OnFFTAnalyzerFFTSizeMenuEntryClicked;
		FOnCQTAnalyzerFFTSizeMenuEntryClicked OnCQTAnalyzerFFTSizeMenuEntryClicked;
		FOnTiltSpectrumMenuEntryClicked OnTiltSpectrumMenuEntryClicked;
		FOnFrequencyAxisPixelBucketModeMenuEntryClicked OnFrequencyAxisPixelBucketModeMenuEntryClicked;
		FOnFrequencyAxisScaleMenuEntryClicked OnFrequencyAxisScaleMenuEntryClicked;
		FOnDisplayAxisLabelsButtonToggled OnDisplayFrequencyAxisLabelsButtonToggled;
		FOnDisplayAxisLabelsButtonToggled OnDisplaySoundLevelAxisLabelsButtonToggled;

		const FAudioSpectrumPlotStyle* PlotStyle = nullptr;
	};

	/**
	 * Owns an analyzer and a corresponding Slate widget for displaying the resulting spectrum.
	 * Exponential time-smoothing is applied to the spectrum.
	 * Can either create an Audio Bus to analyze, or analyze the given Bus.
	 */
	class AUDIOWIDGETS_API FAudioSpectrumAnalyzer : public TSharedFromThis<FAudioSpectrumAnalyzer>
	{
	public:
		FAudioSpectrumAnalyzer(const FAudioSpectrumAnalyzerParams& Params);
		FAudioSpectrumAnalyzer(int32 InNumChannels, Audio::FDeviceId InAudioDeviceId, TObjectPtr<UAudioBus> InExternalAudioBus = nullptr);
		~FAudioSpectrumAnalyzer();

		UAudioBus* GetAudioBus() const;

		TSharedRef<SWidget> GetWidget() const;

		void Init(int32 InNumChannels, Audio::FDeviceId InAudioDeviceId, TObjectPtr<UAudioBus> InExternalAudioBus = nullptr);

	protected:
		void StartAnalyzing(const EAudioSpectrumAnalyzerType InAnalyzerType);
		void StopAnalyzing();

		void OnSpectrumResults(USynesthesiaSpectrumAnalyzer* InSpectrumAnalyzer, int32 ChannelIndex, const TArray<FSynesthesiaSpectrumResults>& InSpectrumResultsArray);
		void OnConstantQResults(UConstantQAnalyzer* InSpectrumAnalyzer, int32 ChannelIndex, const TArray<FConstantQResults>& InSpectrumResultsArray);
		void UpdateARSmoothing(const float TimeStamp, TConstArrayView<float> SquaredMagnitudes);

		FAudioPowerSpectrumData GetAudioSpectrumData();

		void ExtendSpectrumPlotContextMenu(FMenuBuilder& MenuBuilder);
		void BuildBallisticsSubMenu(FMenuBuilder& SubMenu);
		void BuildAnalyzerTypeSubMenu(FMenuBuilder& SubMenu);
		void BuildFFTSizeSubMenu(FMenuBuilder& SubMenu);

		void UpdateAnalyzerSettings();

	private:
		void CreateSynesthesiaSpectrumAnalyzer();
		void ReleaseSynesthesiaSpectrumAnalyzer();
			
		void CreateConstantQAnalyzer();
		void ReleaseConstantQAnalyzer();

		void Teardown();

		/** Audio analyzer objects. */
		TStrongObjectPtr<USynesthesiaSpectrumAnalyzer> SpectrumAnalyzer;
		TStrongObjectPtr<UConstantQAnalyzer> ConstantQAnalyzer;

		/** The audio bus used for analysis. */
		TStrongObjectPtr<UAudioBus> AudioBus;

		/** Meaning of spectrum data. */
		TArray<float> CenterFrequencies;

		/** Cached spectrum data, with AR smoothing applied. */
		TArray<float> ARSmoothedSquaredMagnitudes;

		/** Handles for results delegate for analyzers. */
		FDelegateHandle SpectrumResultsDelegateHandle;
		FDelegateHandle ConstantQResultsDelegateHandle;

		/** Analyzer settings. */
		TStrongObjectPtr<USynesthesiaSpectrumAnalysisSettings> SpectrumAnalysisSettings;
		TStrongObjectPtr<UConstantQSettings> ConstantQSettings;

		/** Slate widget for spectrum display */
		TSharedPtr<SAudioSpectrumPlot> Widget;
		TSharedPtr<const FExtensionBase> ContextMenuExtension;

		Audio::FDeviceId AudioDeviceId = INDEX_NONE;
		bool bUseExternalAudioBus = false;

		TOptional<EAudioSpectrumAnalyzerType> ActiveAnalyzerType;
		TOptional<float> PrevTimeStamp;
		float WindowCompensationPowerGain = 1.0f;
		float AttackTimeMsec = 300.0f;
		float ReleaseTimeMsec = 300.0f;
		TAttribute<EAudioSpectrumAnalyzerBallistics> Ballistics;
		TAttribute<EAudioSpectrumAnalyzerType> AnalyzerType;
		TAttribute<EFFTSize> FFTAnalyzerFFTSize;
		TAttribute<EConstantQFFTSizeEnum> CQTAnalyzerFFTSize;

		FOnBallisticsMenuEntryClicked OnBallisticsMenuEntryClicked;
		FOnAnalyzerTypeMenuEntryClicked OnAnalyzerTypeMenuEntryClicked;
		FOnFFTAnalyzerFFTSizeMenuEntryClicked OnFFTAnalyzerFFTSizeMenuEntryClicked;
		FOnCQTAnalyzerFFTSizeMenuEntryClicked OnCQTAnalyzerFFTSizeMenuEntryClicked;
	};
} // namespace AudioWidgets
