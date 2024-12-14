// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreaming2AudioSink.h"

namespace UE::PixelStreaming2
{
	class PIXELSTREAMING2_API FAudioSink : public IPixelStreaming2AudioSink
	{
	public:
		// Note: destructor will call destroy on any attached audio consumers
		virtual ~FAudioSink();

		virtual void AddAudioConsumer(IPixelStreaming2AudioConsumer* AudioConsumer) override;
		virtual void RemoveAudioConsumer(IPixelStreaming2AudioConsumer* AudioConsumer) override;

		bool HasAudioConsumers();

		void SetMuted(bool bIsMuted);

		void OnAudioData(int16_t* AudioData, uint32 NumFrames, uint32 NumChannels, uint32 SampleRate);

	protected:
		TSet<IPixelStreaming2AudioConsumer*> AudioConsumers;

	private:
		FCriticalSection AudioConsumersCS;
		bool			 bIsMuted = false;
	};
} // namespace UE::PixelStreaming2