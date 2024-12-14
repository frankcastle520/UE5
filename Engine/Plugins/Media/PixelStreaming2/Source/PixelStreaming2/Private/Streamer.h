// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreaming2Streamer.h"
#include "VideoSourceGroup.h"
#include "ThreadSafeMap.h"
#include "Dom/JsonObject.h"
#include "IPixelStreaming2InputHandler.h"
#include "Templates/SharedPointer.h"
#include "PlayerContext.h"
#include "FreezeFrame.h"
#include "StreamerReconnectTimer.h"
#include "EpicRtcManager.h"

#include "epic_rtc/core/stats.h"

class IPixelStreaming2Module;

namespace UE::PixelStreaming2
{
	static const FString INVALID_PLAYER_ID = FString(TEXT("Invalid Player Id"));

	class FStreamer : public IPixelStreaming2Streamer, public TSharedFromThis<FStreamer>
	{
	public:
		static TSharedPtr<FStreamer> Create(const FString& StreamerId, TRefCountPtr<EpicRtcConferenceInterface> Conference);
		virtual ~FStreamer();

		virtual void  SetStreamFPS(int32 InFramesPerSecond) override;
		virtual int32 GetStreamFPS() override;
		virtual void  SetCoupleFramerate(bool bCouple) override;

		virtual void									SetVideoProducer(TSharedPtr<IPixelStreaming2VideoProducer> Input) override;
		virtual TWeakPtr<IPixelStreaming2VideoProducer> GetVideoProducer() override;

		virtual void	SetSignallingServerURL(const FString& InSignallingServerURL) override;
		virtual FString GetSignallingServerURL() override;

		virtual FString GetId() override { return StreamerId; };
		virtual bool	IsSignallingConnected() override { return bSignallingConnected; }
		virtual void	StartStreaming() override;
		virtual void	StopStreaming() override;
		virtual bool	IsStreaming() const override { return bStreamingStarted; }

		virtual FPreConnectionEvent&	OnPreConnection() override;
		virtual FStreamingStartedEvent& OnStreamingStarted() override;
		virtual FStreamingStoppedEvent& OnStreamingStopped() override;

		virtual void ForceKeyFrame() override;

		virtual void FreezeStream(UTexture2D* Texture) override;
		virtual void UnfreezeStream() override;

		virtual void			SendAllPlayersMessage(FString MessageType, const FString& Descriptor) override;
		virtual void			SendPlayerMessage(FString PlayerId, FString MessageType, const FString& Descriptor) override;
		virtual void			SendFileData(const TArray64<uint8>& ByteData, FString& MimeType, FString& FileExtension) override;
		virtual void			KickPlayer(FString PlayerId) override;
		virtual TArray<FString> GetConnectedPlayers() override;

		virtual TWeakPtr<IPixelStreaming2InputHandler> GetInputHandler() override { return InputHandler; }

		virtual IPixelStreaming2AudioSink* GetPeerAudioSink(FString PlayerId) override;
		virtual IPixelStreaming2AudioSink* GetUnlistenedAudioSink() override;
		virtual IPixelStreaming2VideoSink* GetPeerVideoSink(FString PlayerId) override;
		virtual IPixelStreaming2VideoSink* GetUnwatchedVideoSink() override;

		virtual void SetConfigOption(const FName& OptionName, const FString& Value) override;
		virtual bool GetConfigOption(const FName& OptionName, FString& OutValue) override;

		virtual void PlayerRequestsBitrate(FString PlayerId, int MinBitrate, int MaxBitrate) override;

		virtual void RefreshStreamBitrate() override;

		void ForEachPlayer(const TFunction<void(FString, FPlayerContext)>& Func);

	private:
		FStreamer(const FString& StreamerId, TRefCountPtr<EpicRtcConferenceInterface> Conference);

		// own methods
		void OnProtocolUpdated();
		void ConsumeStats(FString PlayerId, FName StatName, float StatValue);
		void DeletePlayerSession(FString PlayerId);
		void DeleteAllPlayerSessions();
		void OnDataChannelOpen(FString PlayerId);
		void OnDataChannelClosed(FString PlayerId);
		void SendInitialSettings(FString PlayerId) const;
		void SendProtocol(FString PlayerId) const;
		void SendPeerControllerMessages(FString PlayerId) const;
		void SendLatencyReport(FString PlayerId) const;
		void HandleRelayStatusMessage(const uint8_t* Data, uint32_t Size, EpicRtcDataTrackInterface* DataTrack);
		bool FindPlayerByAudioTrack(EpicRtcAudioTrackInterface* AudioTrack, FString& OutPlayerId);
		bool FindPlayerByVideoTrack(EpicRtcVideoTrackInterface* VideoTrack, FString& OutPlayerId);
		bool FindPlayerByDataTrack(EpicRtcDataTrackInterface* DataTrack, FString& OutPlayerId);
		void TriggerMouseLeave(FString InStreamerId);
		void RemoveSession(bool bDisconnect);
		void RemoveRoom();
		void OnStatsReady(const FString& PlayerId, const EpicRtcConnectionStats& ConnectionStats);

	private:
		FString StreamerId;
		FString CurrentSignallingServerURL;

		TSharedPtr<IPixelStreaming2InputHandler> InputHandler;

		TSharedPtr<TThreadSafeMap<FString, FPlayerContext>> Players;

		FString InputControllingId = INVALID_PLAYER_ID;

		bool bSignallingConnected = false;
		bool bStreamingStarted = false;

		FPreConnectionEvent	   StreamingPreConnectionEvent;
		FStreamingStartedEvent StreamingStartedEvent;
		FStreamingStoppedEvent StreamingStoppedEvent;

		TSharedPtr<FVideoCapturer>	  VideoCapturer;
		TSharedPtr<FVideoSourceGroup> VideoSourceGroup;
		TSharedPtr<FFreezeFrame>	  FreezeFrame;

		FDelegateHandle ConsumeStatsHandle;
		FDelegateHandle AllConnectionsClosedHandle;

		TMap<FName, FString> ConfigOptions;

		TSharedPtr<FStreamerReconnectTimer> ReconnectTimer;

		TSharedPtr<FEpicRtcManager> EpicRtcManager;

	public:
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
	};
} // namespace UE::PixelStreaming2
