// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcAudioProducer.h"
#include "EpicRtcAudioPatchMixer.h"
#include "EpicRtcTickableTask.h"
#include "ISubmixBufferListener.h"
#include "Logging.h"
#include "SampleBuffer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcPatchInputProxy : public IPixelStreaming2AudioProducer
	{
	public:
		FEpicRtcPatchInputProxy(TSharedPtr<FEpicRtcAudioPatchMixer> InMixer)
			: Mixer(InMixer)
			// We don't want the patch input to handle gain as the capturer handles that at the end
			, PatchInput(Mixer->AddNewInput(Mixer->GetMaxBufferSize(), 1.0f))
			, NumChannels(Mixer->GetNumChannels())
			, SampleRate(Mixer->GetSampleRate())
		{
		}

		~FEpicRtcPatchInputProxy()
		{
			Mixer->RemovePatch(PatchInput);
		}

		virtual void PushAudio(const float* AudioData, int32 InNumSamples, int32 InNumChannels, int32 InSampleRate) override
		{
			TArray<float> AudioBuffer;

			if (SampleRate != InSampleRate)
			{
				float SampleRateConversionRatio = static_cast<float>(SampleRate) / static_cast<float>(InSampleRate);
				Resampler.Init(Audio::EResamplingMethod::Linear, SampleRateConversionRatio, InNumChannels);

				int32 NumConvertedSamples = InSampleRate / SampleRateConversionRatio;
				int32 OutputSamples = INDEX_NONE;
				AudioBuffer.AddZeroed(NumConvertedSamples);
				// Perform the sample rate conversion
				int32 ErrorCode = Resampler.ProcessAudio(const_cast<float*>(AudioData), InNumSamples, false, AudioBuffer.GetData(), AudioBuffer.Num(), OutputSamples);
				verifyf(OutputSamples <= NumConvertedSamples, TEXT("OutputSamples > NumConvertedSamples"));
				if (ErrorCode != 0)
				{
					UE_LOG(LogPixelStreaming2, Warning, TEXT("(FAudioInput) Problem occured resampling audio data. Code: %d"), ErrorCode);
					return;
				}
			}
			else
			{
				AudioBuffer.AddZeroed(InNumSamples);
				FMemory::Memcpy(AudioBuffer.GetData(), AudioData, AudioBuffer.Num() * sizeof(float));
			}

			Audio::TSampleBuffer<float> Buffer(AudioBuffer.GetData(), AudioBuffer.Num(), InNumChannels, InSampleRate);
			// Mix to stereo if required, since PixelStreaming2 only accepts stereo at the moment
			if (NumChannels != Buffer.GetNumChannels())
			{
				Buffer.MixBufferToChannels(NumChannels);
			}

			PatchInput.PushAudio(Buffer.GetData(), Buffer.GetNumSamples());
		}

	private:
		TSharedPtr<FEpicRtcAudioPatchMixer> Mixer;
		Audio::FResampler					Resampler;
		Audio::FPatchInput					PatchInput;
		uint8								NumChannels;
		uint32								SampleRate;
	};

	class FEpicRtcMixAudioTask : public FEpicRtcTickableTask
	{
	public:
		FEpicRtcMixAudioTask(IPixelStreaming2AudioProducer* Parent, TSharedPtr<FEpicRtcAudioPatchMixer> Mixer)
			: Parent(Parent)
			, Mixer(Mixer)
		{
			MixingBuffer.SetNumUninitialized(Mixer->GetMaxBufferSize());
		}

		virtual ~FEpicRtcMixAudioTask() = default;

		// Begin FEpicRtcMixAudioTask
		virtual void Tick(float DeltaMs) override
		{
			if (!Mixer)
			{
				return;
			}

			// 4 samples is the absolute minimum required for mixing
			if (MixingBuffer.Num() < 4)
			{
				return;
			}

			int32 TargetNumSamples = Mixer->MaxNumberOfSamplesThatCanBePopped();
			if (TargetNumSamples < 0)
			{
				return;
			}

			int32 NSamplesPopped = Mixer->PopAudio(MixingBuffer.GetData(), TargetNumSamples, false /* bUseLatestAudio */);
			if (NSamplesPopped == 0)
			{
				return;
			}

			Parent->PushAudio(MixingBuffer.GetData(), NSamplesPopped, Mixer->GetNumChannels(), Mixer->GetSampleRate());
		}

		virtual const FString& GetName() const override
		{
			static FString TaskName = TEXT("EpicRtcMixAudioTask");
			return TaskName;
		}
		// End FEpicRtcMixAudioTask

	private:
		bool								  bIsRunning;
		Audio::VectorOps::FAlignedFloatBuffer MixingBuffer;

		IPixelStreaming2AudioProducer* Parent;
		TSharedPtr<FEpicRtcAudioPatchMixer>	 Mixer;
	};

	class FEpicRtcAudioMixingCapturer : public IPixelStreaming2AudioProducer
	{
	public:
		static TSharedPtr<FEpicRtcAudioMixingCapturer> Create();
		virtual ~FEpicRtcAudioMixingCapturer() = default;

		// Mixed audio input will push its audio to an FEpicRtcPatchInputProxy for mixing
		TSharedPtr<FEpicRtcAudioProducer> CreateAudioProducer();
		void							  CreateAudioProducer(Audio::FDeviceId AudioDeviceId);
		void							  RemoveAudioProducer(Audio::FDeviceId AudioDeviceId);

		virtual void PushAudio(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate) override;

		/**
		 * This is broadcast each time audio is captured. Tracks should bind to this and push the audio into the track
		 */
		DECLARE_EVENT_FourParams(FEpicRtcAudioMixingCapturer, FOnAudioBuffer, int16_t*, int32, int32, const int32);
		FOnAudioBuffer OnAudioBuffer;

	protected:
		FEpicRtcAudioMixingCapturer();

	private:
		void OnDebugDumpAudioChanged(IConsoleVariable* Var);
		void OnEnginePreExit();
		void WriteDebugAudio();

	private:
		TSharedPtr<FEpicRtcAudioPatchMixer>	 Mixer;
		TUniqueTaskPtr<FEpicRtcMixAudioTask> MixerTask;

		TMap<Audio::FDeviceId, TSharedPtr<FEpicRtcAudioProducer>> AudioProducers;

		TArray<int16_t> RecordingBuffer;

		int				  SampleRate;
		int				  NumChannels;
		float			  SampleSizeSeconds;
		Audio::FResampler Resampler;

		Audio::TSampleBuffer<int16_t> DebugDumpAudioBuffer;
	};
} // namespace UE::PixelStreaming2