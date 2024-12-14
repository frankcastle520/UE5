// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2AudioComponent.h"

#include "CoreMinimal.h"
#include "IPixelStreaming2Module.h"
#include "IPixelStreaming2Streamer.h"
#include "Logging.h"
#include "SampleBuffer.h"
#include "SoundGenerator.h"

/**
 * Component that recieves audio from a remote webrtc connection and outputs it into UE using a "synth component".
 */
UPixelStreaming2AudioComponent::UPixelStreaming2AudioComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PlayerToHear(FString())
	, bAutoFindPeer(true)
	, AudioSink(nullptr)
	, SoundGenerator(MakeShared<UE::PixelStreaming2::FSoundGenerator, ESPMode::ThreadSafe>())
{
	PreferredBufferLength = 512u;
	NumChannels = 2;
	PrimaryComponentTick.bCanEverTick = true;
	SetComponentTickEnabled(true);
	bAutoActivate = true;
};

ISoundGeneratorPtr UPixelStreaming2AudioComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	SoundGenerator->SetParameters(InParams);
	Initialize(InParams.SampleRate);
	return SoundGenerator;
}

void UPixelStreaming2AudioComponent::OnBeginGenerate()
{
	SoundGenerator->bGeneratingAudio = true;
}

void UPixelStreaming2AudioComponent::OnEndGenerate()
{
	SoundGenerator->bGeneratingAudio = false;
}

void UPixelStreaming2AudioComponent::BeginDestroy()
{
	Super::BeginDestroy();
	Reset();
}

bool UPixelStreaming2AudioComponent::ListenTo(FString PlayerToListenTo)
{
	IPixelStreaming2Module& PixelStreaming2Module = IPixelStreaming2Module::Get();
	if (!PixelStreaming2Module.IsReady())
	{
		return false;
	}
	return StreamerListenTo(PixelStreaming2Module.GetDefaultStreamerID(), PlayerToListenTo);
}

bool UPixelStreaming2AudioComponent::StreamerListenTo(FString StreamerId, FString PlayerToListenTo)
{
	if (!IPixelStreaming2Module::IsAvailable())
	{
		UE_LOG(LogPixelStreaming2, Verbose, TEXT("Pixel Streaming audio component could not listen to anything because Pixel Streaming module is not loaded. This is expected on dedicated servers."));
		return false;
	}

	IPixelStreaming2Module& PixelStreaming2Module = IPixelStreaming2Module::Get();
	if (!PixelStreaming2Module.IsReady())
	{
		return false;
	}

	PlayerToHear = PlayerToListenTo;

	if (StreamerId == FString())
	{
		TArray<FString> StreamerIds = PixelStreaming2Module.GetStreamerIds();
		if (StreamerIds.Num() > 0)
		{
			StreamerToHear = StreamerIds[0];
		}
		else
		{
			StreamerToHear = PixelStreaming2Module.GetDefaultStreamerID();
		}
	}
	else
	{
		StreamerToHear = StreamerId;
	}

	TSharedPtr<IPixelStreaming2Streamer> Streamer = PixelStreaming2Module.FindStreamer(StreamerToHear);
	if (!Streamer)
	{
		return false;
	}
	IPixelStreaming2AudioSink* CandidateSink = WillListenToAnyPlayer() ? Streamer->GetUnlistenedAudioSink() : Streamer->GetPeerAudioSink(FString(PlayerToHear));

	if (CandidateSink == nullptr)
	{
		return false;
	}

	AudioSink = CandidateSink;
	AudioSink->AddAudioConsumer(this);

	return true;
}

void UPixelStreaming2AudioComponent::Reset()
{
	PlayerToHear = FString();
	StreamerToHear = FString();
	SoundGenerator->bShouldGenerateAudio = false;
	if (AudioSink)
	{
		AudioSink->RemoveAudioConsumer(this);
	}
	AudioSink = nullptr;
	SoundGenerator->EmptyBuffers();
}

bool UPixelStreaming2AudioComponent::IsListeningToPlayer()
{
	return SoundGenerator->bShouldGenerateAudio;
}

bool UPixelStreaming2AudioComponent::WillListenToAnyPlayer()
{
	return PlayerToHear == FString();
}

void UPixelStreaming2AudioComponent::ConsumeRawPCM(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames)
{
	// Sound generator has not been initialized yet.
	if (SoundGenerator->GetSampleRate() == 0 || GetAudioComponent() == nullptr)
	{
		return;
	}

	// Set pitch multiplier as a way to handle mismatched sample rates
	if (InSampleRate != SoundGenerator->GetSampleRate())
	{
		GetAudioComponent()->SetPitchMultiplier((float)InSampleRate / SoundGenerator->GetSampleRate());
	}
	else if (GetAudioComponent()->PitchMultiplier != 1.0f)
	{
		GetAudioComponent()->SetPitchMultiplier(1.0f);
	}

	//                                   Data       Samples              Channels   SampleRate
	Audio::TSampleBuffer<int16_t> Buffer(AudioData, NFrames * NChannels, NChannels, InSampleRate);
	int32						  TargetNumChannels = SoundGenerator->GetNumChannels();
	if (NChannels != TargetNumChannels)
	{
		if (NChannels < TargetNumChannels)
		{
			// Up mix by duplicating the mono source to each channel
			TArrayView<int16> SourceBuffer = Buffer.GetArrayView();
			TArray<int16>	  MixedBuffer;
			MixedBuffer.SetNumZeroed(NFrames * TargetNumChannels);
			for (int32 SrcSampleIdx = 0; SrcSampleIdx < Buffer.GetNumSamples(); SrcSampleIdx++)
			{
				int32 DestSampleIdx = SrcSampleIdx * TargetNumChannels;
				for (int32 Channel = 0; Channel < TargetNumChannels; Channel++)
				{
					MixedBuffer[DestSampleIdx + Channel] = SourceBuffer[SrcSampleIdx];
				}
			}

			Buffer.CopyFrom(MixedBuffer, TargetNumChannels, InSampleRate);
		}
		else
		{
			// Down mix using inbuilt method
			Buffer.MixBufferToChannels(TargetNumChannels);
		}
	}

	SoundGenerator->AddAudio(Buffer.GetData(), InSampleRate, Buffer.GetNumChannels(), Buffer.GetNumFrames());
}

void UPixelStreaming2AudioComponent::OnConsumerAdded()
{
	SoundGenerator->bShouldGenerateAudio = true;
	Start();
}

void UPixelStreaming2AudioComponent::OnConsumerRemoved()
{
	Reset();
}

void UPixelStreaming2AudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{

	bool bPixelStreaming2Loaded = IPixelStreaming2Module::IsAvailable();

	// Early out if running in commandlet
	if (IsRunningCommandlet())
	{
		return;
	}

	// if auto connect turned off don't bother
	if (!bAutoFindPeer)
	{
		return;
	}

	// if listening to a peer don't auto connect
	if (IsListeningToPlayer())
	{
		return;
	}

	if (StreamerListenTo(StreamerToHear, PlayerToHear))
	{
		UE_LOG(LogPixelStreaming2, Log, TEXT("PixelStreaming2 audio component found a WebRTC peer to listen to."));
	}
}
