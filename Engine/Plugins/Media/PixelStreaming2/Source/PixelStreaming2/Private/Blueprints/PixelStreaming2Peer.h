// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioSink.h"
#include "Blueprints/PixelStreaming2MediaTexture.h"
#include "Components/SynthComponent.h"
#include "Containers/Utf8String.h"
#include "EpicRtcManager.h"
#include "IPixelStreaming2AudioConsumer.h"
#include "RTCStatsCollector.h"
#include "VideoSink.h"

#include "PixelStreaming2Peer.generated.h"

DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_OneParam(FPixelStreamingStreamerList, UPixelStreaming2Peer, OnStreamerList, const TArray<FString>&, StreamerList);

namespace UE::PixelStreaming2
{
	class FSoundGenerator;
} // namespace UE::PixelStreaming2

/**
 * A blueprint representation of a Pixel Streaming Peer Connection. Will accept video sinks to receive video data.
 * NOTE: This class is not a peer of a streamer. This class represents a peer in its own right (akin to the browser) and will subscribe to a stream
 */
// UCLASS(Blueprintable, ClassGroup = (PixelStreaming2), meta = (BlueprintSpawnableComponent))
UCLASS(Blueprintable, Category = "PixelStreaming2", META = (DisplayName = "PixelStreaming Peer Component", BlueprintSpawnableComponent))
class UPixelStreaming2Peer : public USynthComponent, public IPixelStreaming2AudioConsumer
{
	GENERATED_UCLASS_BODY()
protected:
	// UObject overrides
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// USynthComponent overrides
	virtual void			   OnBeginGenerate() override;
	virtual void			   OnEndGenerate() override;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

public:
	/**
	 * Attempt to connect to a specified signalling server.
	 * @param Url The url of the signalling server. Ignored if this component has a MediaSource. In that case the URL on the media source will be used instead.
	 * @return Returns false if Connect fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "PixelStreaming2")
	bool Connect(const FString& Url);

	/**
	 * Disconnect from the signalling server. No action if no connection exists.
	 * @return Returns false if Disconnect fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "PixelStreaming2")
	bool Disconnect();

	/**
	 * Subscribe this peer to the streams provided by the specified streamer.
	 * @param StreamerId The name of the streamer to subscribe to.
	 * @return Returns false if Subscribe fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "PixelStreaming2")
	bool Subscribe(const FString& StreamerId);

	/**
	 * Fired when the connection the list of available streams from the server.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Components|Activation")
	FPixelStreamingStreamerList OnStreamerList;

	/**
	 * A sink for the video data received once this connection has finished negotiating.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Properties", META = (DisplayName = "Pixel Streaming Video Consumer", AllowPrivateAccess = true))
	TObjectPtr<UPixelStreaming2MediaTexture> VideoConsumer = nullptr;

private:
	FUtf8String		SubscribedStream;
	FUtf8String		PlayerName;
	static uint32_t PlayerId;

	TSharedPtr<UE::PixelStreaming2::FAudioSink>							  AudioSink;
	TSharedPtr<UE::PixelStreaming2::FSoundGenerator, ESPMode::ThreadSafe> SoundGenerator;

	TSharedPtr<UE::PixelStreaming2::FVideoSink> VideoSink;

	TSharedPtr<UE::PixelStreaming2::FEpicRtcManager> EpicRtcManager;

	TSharedPtr<UE::PixelStreaming2::FRTCStatsCollector> StatsCollector;

	bool Disconnect(const FString& OptionalReason);

public:
	// Begin IPixelStreaming2AudioConsumer Callbacks
	virtual void ConsumeRawPCM(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames) override;
	virtual void OnConsumerAdded() override;
	virtual void OnConsumerRemoved() override;
	// End IPixelStreaming2AudioConsumer Callbacks

	// Begin FEpicRtcManager Callbacks
	void OnSessionStateUpdate(const EpicRtcSessionState StateUpdate);
	void OnSessionErrorUpdate(const EpicRtcErrorCode ErrorUpdate);
	void OnSessionRoomsAvailableUpdate(EpicRtcStringArrayInterface* RoomsList);

	void OnRoomStateUpdate(const EpicRtcRoomState State);
	void OnRoomJoinedUpdate(EpicRtcParticipantInterface* Participant);
	void OnRoomLeftUpdate(const EpicRtcStringView ParticipantId);
	void OnAudioTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcAudioTrackInterface* AudioTrack);
	void OnVideoTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcVideoTrackInterface* VideoTrack);
	void OnDataTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcDataTrackInterface* DataTrack);
	void OnLocalSdpUpdate(EpicRtcParticipantInterface* Participant, EpicRtcSdpInterface* Sdp);
	void OnRemoteSdpUpdate(EpicRtcParticipantInterface* Participant, EpicRtcSdpInterface* Sdp);
	void OnRoomErrorUpdate(const EpicRtcErrorCode Error);

	void OnAudioTrackMuted(EpicRtcAudioTrackInterface* AudioTrack, EpicRtcBool bIsMuted);
	void OnAudioTrackFrame(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcAudioFrame& Frame);
	void OnAudioTrackRemoved(EpicRtcAudioTrackInterface* AudioTrack);
	void OnAudioTrackState(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcTrackState State);

	void OnVideoTrackMuted(EpicRtcVideoTrackInterface* VideoTrack, EpicRtcBool bIsMuted);
	void OnVideoTrackFrame(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcVideoFrame& Frame);
	void OnVideoTrackRemoved(EpicRtcVideoTrackInterface* VideoTrack);
	void OnVideoTrackState(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcTrackState State);

	void OnDataTrackRemoved(EpicRtcDataTrackInterface* DataTrack);
	void OnDataTrackState(EpicRtcDataTrackInterface* DataTrack, const EpicRtcTrackState State);
	void OnDataTrackMessage(EpicRtcDataTrackInterface* DataTrack);
	// End FEpicRtcManager Callbacks

	void OnStatsReady(const FString& PeerId, const EpicRtcConnectionStats& ConnectionStats);

private:
	EpicRtcSessionState SessionState = EpicRtcSessionState::Disconnected;
};
