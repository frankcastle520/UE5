// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Sound/SoundGenerator.h"

#include <atomic>

namespace UE::PixelStreaming2
{
	/**
	 * An `ISoundGenerator` implementation to pump some audio from EpicRtc into this synth component
	 */
	class PIXELSTREAMING2_API FSoundGenerator : public ::ISoundGenerator
	{
	public:
		FSoundGenerator();
		virtual ~FSoundGenerator() = default;

		// Called when a new buffer is required.
		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

		// Returns the number of samples to render per callback
		virtual int32 GetDesiredNumSamplesToRenderPerCallback() const;

		// Optional. Called on audio generator thread right when the generator begins generating.
		virtual void OnBeginGenerate() { bGeneratingAudio = true; };

		// Optional. Called on audio generator thread right when the generator ends generating.
		virtual void OnEndGenerate() { bGeneratingAudio = false; };

		// Optional. Can be overridden to end the sound when generating is finished.
		virtual bool IsFinished() const { return false; };

		void AddAudio(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames);

		int32 GetSampleRate() { return Params.SampleRate; }
		int32 GetNumChannels() { return Params.NumChannels; }
		void  EmptyBuffers();
		void  SetParameters(const FSoundGeneratorInitParams& InitParams);

	private:
		FSoundGeneratorInitParams Params;
		TArray<int16_t>			  Buffer;
		FCriticalSection		  CriticalSection;

	public:
		std::atomic<bool> bGeneratingAudio = false;
		std::atomic<bool> bShouldGenerateAudio = false;
	};

} // namespace UE::PixelStreaming2
