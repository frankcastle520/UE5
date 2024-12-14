// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2VideoComponent.h"

#include "CoreMinimal.h"
#include "IPixelStreaming2Module.h"
#include "IPixelStreaming2Streamer.h"
#include "Logging.h"

/**
 * Component that recieves video from a remote webrtc connection and outputs it into UE using a "synth component".
 */
UPixelStreaming2VideoComponent::UPixelStreaming2VideoComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PlayerToWatch(FString())
	, bAutoFindPeer(true)
	, VideoSink(nullptr)
	, bIsWatchingPlayer(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	SetComponentTickEnabled(true);
	bAutoActivate = true;
};

void UPixelStreaming2VideoComponent::BeginDestroy()
{
	Super::BeginDestroy();
	Reset();
}

bool UPixelStreaming2VideoComponent::Watch(FString PlayerId)
{
	IPixelStreaming2Module& PixelStreaming2Module = IPixelStreaming2Module::Get();
	if (!PixelStreaming2Module.IsReady())
	{
		return false;
	}
	return StreamerWatch(PixelStreaming2Module.GetDefaultStreamerID(), PlayerId);
}

bool UPixelStreaming2VideoComponent::StreamerWatch(FString StreamerId, FString PlayerId)
{
	if (!IPixelStreaming2Module::IsAvailable())
	{
		UE_LOG(LogPixelStreaming2, Verbose, TEXT("Pixel Streaming video component could not watch anything because Pixel Streaming module is not loaded. This is expected on dedicated servers."));
		return false;
	}

	IPixelStreaming2Module& PixelStreaming2Module = IPixelStreaming2Module::Get();
	if (!PixelStreaming2Module.IsReady())
	{
		return false;
	}

	PlayerToWatch = PlayerId;

	if (StreamerId == FString())
	{
		TArray<FString> StreamerIds = PixelStreaming2Module.GetStreamerIds();
		if (StreamerIds.Num() > 0)
		{
			StreamerToWatch = StreamerIds[0];
		}
		else
		{
			StreamerToWatch = PixelStreaming2Module.GetDefaultStreamerID();
		}
	}
	else
	{
		StreamerToWatch = StreamerId;
	}

	TSharedPtr<IPixelStreaming2Streamer> Streamer = PixelStreaming2Module.FindStreamer(StreamerToWatch);
	if (!Streamer)
	{
		return false;
	}

	IPixelStreaming2VideoSink* CandidateSink = WillWatchAnyPlayer() ? Streamer->GetUnwatchedVideoSink() : Streamer->GetPeerVideoSink(FString(PlayerToWatch));

	if (CandidateSink == nullptr)
	{
		return false;
	}

	VideoSink = CandidateSink;

	if (VideoConsumer)
	{
		VideoSink->AddVideoConsumer(VideoConsumer);
		bIsWatchingPlayer = true;
	}

	return true;
}

void UPixelStreaming2VideoComponent::Reset()
{
	PlayerToWatch = FString();
	StreamerToWatch = FString();
	if (VideoSink && VideoConsumer)
	{
		VideoSink->RemoveVideoConsumer(VideoConsumer);
		bIsWatchingPlayer = false;
	}
	VideoSink = nullptr;
}

bool UPixelStreaming2VideoComponent::IsWatchingPlayer()
{
	return bIsWatchingPlayer;
}

bool UPixelStreaming2VideoComponent::WillWatchAnyPlayer()
{
	return PlayerToWatch == FString();
}

void UPixelStreaming2VideoComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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

	// if watching a peer don't auto connect
	if (IsWatchingPlayer())
	{
		return;
	}

	if (StreamerWatch(StreamerToWatch, PlayerToWatch))
	{
		UE_LOG(LogPixelStreaming2, Log, TEXT("PixelStreaming2 video component found a WebRTC peer to watch."));
	}
}
