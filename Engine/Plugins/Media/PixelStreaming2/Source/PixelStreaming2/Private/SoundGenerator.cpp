// Copyright Epic Games, Inc. All Rights Reserved.

#include "SoundGenerator.h"
#include "DSP/FloatArrayMath.h"

namespace UE::PixelStreaming2
{

	FSoundGenerator::FSoundGenerator()
		: Params()
		, Buffer()
		, CriticalSection()
	{
	}

	void FSoundGenerator::SetParameters(const FSoundGeneratorInitParams& InitParams)
	{
		Params = InitParams;
	}

	int32 FSoundGenerator::GetDesiredNumSamplesToRenderPerCallback() const
	{
		return Params.NumFramesPerCallback * Params.NumChannels;
	}

	void FSoundGenerator::EmptyBuffers()
	{
		FScopeLock Lock(&CriticalSection);
		Buffer.Empty();
	}

	void FSoundGenerator::AddAudio(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames)
	{
		if (!bGeneratingAudio)
		{
			return;
		}

		int NSamples = NFrames * NChannels;

		// Critical Section as we are writing into the `Buffer` that `ISoundGenerator` is using on another thread.
		FScopeLock Lock(&CriticalSection);

		Buffer.Append(AudioData, NSamples);
	}

	// Called when a new buffer is required.
	int32 FSoundGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamples)
	{
		// Not listening to peer, return zero'd buffer.
		if (!bShouldGenerateAudio || Buffer.Num() == 0)
		{
			FMemory::Memzero(OutAudio, NumSamples * sizeof(float));
			return NumSamples;
		}

		int32 NumSamplesToCopy = NumSamples;

		// Critical section
		{
			FScopeLock Lock(&CriticalSection);

			NumSamplesToCopy = FGenericPlatformMath::Min(NumSamples, Buffer.Num());

			// Copy from local buffer into OutAudio if we have enough samples
			Audio::ArrayPcm16ToFloat(MakeArrayView(Buffer.GetData(), NumSamplesToCopy), MakeArrayView(OutAudio, NumSamplesToCopy));

			// Remove front NumSamples from the local buffer
			Buffer.RemoveAt(0, NumSamplesToCopy, EAllowShrinking::No);
		}

		if (NumSamplesToCopy < NumSamples)
		{
			FMemory::Memzero(&OutAudio[NumSamplesToCopy], (NumSamples - NumSamplesToCopy) * sizeof(float));
		}

		return NumSamplesToCopy;
	}

} // namespace UE::PixelStreaming2
