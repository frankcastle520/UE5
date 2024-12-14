// Copyright Epic Games, Inc. All Rights Reserved.

#include "Streamer.h"

#include "IPixelStreaming2Module.h"
#include "PixelStreaming2PluginSettings.h"
#include "PixelStreaming2Delegates.h"
#include "EpicRtcWebsocket.h"
#include "EpicRtcWebsocketFactory.h"
#include "TextureResource.h"
#include "WebSocketsModule.h"
#include "Logging.h"
#include "Stats.h"
#include "PixelStreaming2StatNames.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "RTCStatsCollector.h"
#include "Framework/Application/SlateApplication.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "PixelStreaming2Module.h"
#include "EpicRtcAudioTrack.h"
#include "EpicRtcDataTrack.h"
#include "IPixelStreaming2Module.h"
#include "IPixelStreaming2InputModule.h"
#include "CoreGlobals.h"
#include "UtilsString.h"
#include "UtilsVideo.h"

namespace UE::PixelStreaming2
{
	TSharedPtr<FStreamer> FStreamer::Create(const FString& StreamerId, TRefCountPtr<EpicRtcConferenceInterface> Conference)
	{
		TSharedPtr<FStreamer> Streamer = TSharedPtr<FStreamer>(new FStreamer(StreamerId, Conference));

		if (TSharedPtr<IPixelStreaming2InputHandler> InputHandler = Streamer->GetInputHandler().Pin())
		{
			InputHandler->GetToStreamerProtocol()->OnProtocolUpdated().AddSP(Streamer.ToSharedRef(), &FStreamer::OnProtocolUpdated);
			InputHandler->GetFromStreamerProtocol()->OnProtocolUpdated().AddSP(Streamer.ToSharedRef(), &FStreamer::OnProtocolUpdated);
		}

		Streamer->EpicRtcManager->OnSessionStateUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnSessionStateUpdate);
		Streamer->EpicRtcManager->OnSessionErrorUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnSessionErrorUpdate);
		Streamer->EpicRtcManager->OnSessionRoomsAvailableUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnSessionRoomsAvailableUpdate);

		Streamer->EpicRtcManager->OnRoomStateUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnRoomStateUpdate);
		Streamer->EpicRtcManager->OnRoomJoinedUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnRoomJoinedUpdate);
		Streamer->EpicRtcManager->OnRoomLeftUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnRoomLeftUpdate);
		Streamer->EpicRtcManager->OnAudioTrackUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnAudioTrackUpdate);
		Streamer->EpicRtcManager->OnVideoTrackUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnVideoTrackUpdate);
		Streamer->EpicRtcManager->OnDataTrackUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnDataTrackUpdate);
		Streamer->EpicRtcManager->OnLocalSdpUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnLocalSdpUpdate);
		Streamer->EpicRtcManager->OnRemoteSdpUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnRemoteSdpUpdate);
		Streamer->EpicRtcManager->OnRoomErrorUpdate.AddSP(Streamer.ToSharedRef(), &FStreamer::OnRoomErrorUpdate);

		Streamer->EpicRtcManager->OnAudioTrackMuted.AddSP(Streamer.ToSharedRef(), &FStreamer::OnAudioTrackMuted);
		Streamer->EpicRtcManager->OnAudioTrackFrame.AddSP(Streamer.ToSharedRef(), &FStreamer::OnAudioTrackFrame);
		Streamer->EpicRtcManager->OnAudioTrackRemoved.AddSP(Streamer.ToSharedRef(), &FStreamer::OnAudioTrackRemoved);
		Streamer->EpicRtcManager->OnAudioTrackState.AddSP(Streamer.ToSharedRef(), &FStreamer::OnAudioTrackState);

		Streamer->EpicRtcManager->OnVideoTrackMuted.AddSP(Streamer.ToSharedRef(), &FStreamer::OnVideoTrackMuted);
		Streamer->EpicRtcManager->OnVideoTrackFrame.AddSP(Streamer.ToSharedRef(), &FStreamer::OnVideoTrackFrame);
		Streamer->EpicRtcManager->OnVideoTrackRemoved.AddSP(Streamer.ToSharedRef(), &FStreamer::OnVideoTrackRemoved);
		Streamer->EpicRtcManager->OnVideoTrackState.AddSP(Streamer.ToSharedRef(), &FStreamer::OnVideoTrackState);

		Streamer->EpicRtcManager->OnDataTrackRemoved.AddSP(Streamer.ToSharedRef(), &FStreamer::OnDataTrackRemoved);
		Streamer->EpicRtcManager->OnDataTrackState.AddSP(Streamer.ToSharedRef(), &FStreamer::OnDataTrackState);
		Streamer->EpicRtcManager->OnDataTrackMessage.AddSP(Streamer.ToSharedRef(), &FStreamer::OnDataTrackMessage);

		FPixelStreaming2Module::GetModule()->GetStatsCollector()->OnStatsReady.AddSP(Streamer.ToSharedRef(), &FStreamer::OnStatsReady);

		return Streamer;
	}

	FStreamer::FStreamer(const FString& InStreamerId, TRefCountPtr<EpicRtcConferenceInterface> Conference)
		: StreamerId(InStreamerId)
		, InputHandler(IPixelStreaming2InputModule::Get().CreateInputHandler())
		, Players(new TThreadSafeMap<FString, FPlayerContext>())
		, VideoCapturer(FVideoCapturer::Create())
		, VideoSourceGroup(FVideoSourceGroup::Create(VideoCapturer))
		, FreezeFrame(FFreezeFrame::Create(Players, VideoCapturer, InputHandler))
		, EpicRtcManager(MakeShared<FEpicRtcManager>())
	{
		ReconnectTimer = MakeShared<FStreamerReconnectTimer>();

		InputHandler->SetElevatedCheck([this](FString PlayerId) {
			return GetEnumFromCVar<EInputControllerMode>(UPixelStreaming2PluginSettings::CVarInputController) == EInputControllerMode::Any
				|| InputControllingId == INVALID_PLAYER_ID
				|| PlayerId == InputControllingId;
		});

		EpicRtcManager->EpicRtcConference = Conference;

		EpicRtcManager->SessionObserver = MakeRefCount<FEpicRtcSessionObserver>(EpicRtcManager);
		EpicRtcManager->RoomObserver = MakeRefCount<FEpicRtcRoomObserver>(EpicRtcManager);

		EpicRtcManager->AudioTrackObserverFactory = MakeRefCount<FEpicRtcAudioTrackObserverFactory>(EpicRtcManager);
		EpicRtcManager->VideoTrackObserverFactory = MakeRefCount<FEpicRtcVideoTrackObserverFactory>(EpicRtcManager);
		EpicRtcManager->DataTrackObserverFactory = MakeRefCount<FEpicRtcDataTrackObserverFactory>(EpicRtcManager);
	}

	FStreamer::~FStreamer()
	{
		StopStreaming();
	}

	void FStreamer::OnProtocolUpdated()
	{
		Players->Apply([this](FString DataPlayerId, FPlayerContext& PlayerContext) {
			SendProtocol(DataPlayerId);
		});
	}

	void FStreamer::SetStreamFPS(int32 InFramesPerSecond)
	{
		VideoSourceGroup->SetFPS(InFramesPerSecond);
	}

	int32 FStreamer::GetStreamFPS()
	{
		return VideoSourceGroup->GetFPS();
	}

	void FStreamer::SetCoupleFramerate(bool bCouple)
	{
		VideoSourceGroup->SetDecoupleFramerate(!bCouple);
	}

	void FStreamer::SetVideoProducer(TSharedPtr<IPixelStreaming2VideoProducer> Producer)
	{
		VideoCapturer->SetVideoProducer(StaticCastSharedPtr<FVideoProducer>(Producer));
	}

	TWeakPtr<IPixelStreaming2VideoProducer> FStreamer::GetVideoProducer()
	{
		return VideoCapturer->GetVideoProducer();
	}

	void FStreamer::SetSignallingServerURL(const FString& InSignallingServerURL)
	{
		CurrentSignallingServerURL = InSignallingServerURL;
	}

	FString FStreamer::GetSignallingServerURL()
	{
		return CurrentSignallingServerURL;
	}

	void FStreamer::StartStreaming()
	{
		if (CurrentSignallingServerURL.IsEmpty())
		{
			UE_LOG(LogPixelStreaming2, Log, TEXT("Attempted to start streamer (%s) but no signalling server URL has been set. Use Streamer->SetSignallingServerURL(URL) or -PixelStreaming2URL="), *StreamerId);
			return;
		}

		StopStreaming();
		ReconnectTimer->Stop();

		if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
		{
			ConsumeStatsHandle = Delegates->OnStatChangedNative.AddSP(AsShared(), &FStreamer::ConsumeStats);
			AllConnectionsClosedHandle = Delegates->OnAllConnectionsClosedNative.AddSP(AsShared(), &FStreamer::TriggerMouseLeave);
		}

		// Broadcast the preconnection event just before we do `TryConnect`
		StreamingPreConnectionEvent.Broadcast(this);

		VideoSourceGroup->Start();

		FUtf8String Utf8StreamerId(StreamerId);
		FUtf8String Utf8CurrentSignallingServerURL(CurrentSignallingServerURL);

		EpicRtcSessionConfig SessionConfig{
			._id = ToEpicRtcStringView(Utf8StreamerId),
			._url = ToEpicRtcStringView(Utf8CurrentSignallingServerURL),
			._observer = EpicRtcManager->SessionObserver
		};

		EpicRtcErrorCode Result = EpicRtcManager->EpicRtcConference->CreateSession(SessionConfig, EpicRtcManager->EpicRtcSession.GetInitReference());
		if (Result != EpicRtcErrorCode::Ok)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to create EpicRtc session. CreateSession returned %s"), *ToString(Result));
			StopStreaming();
			return;
		}

		Result = EpicRtcManager->EpicRtcSession->Connect();
		if (Result != EpicRtcErrorCode::Ok)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to connect EpicRtcSession. Connect returned %s"), *ToString(Result));
			StopStreaming();
			return;
		}

		bStreamingStarted = true;
	}

	void FStreamer::StopStreaming()
	{
		if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
		{
			Delegates->OnStatChangedNative.Remove(ConsumeStatsHandle);
			Delegates->OnAllConnectionsClosedNative.Remove(AllConnectionsClosedHandle);
		}

		RemoveSession(true);

		VideoSourceGroup->Stop();
		TriggerMouseLeave(StreamerId);

		if (bStreamingStarted)
		{
			OnStreamingStopped().Broadcast(this);
		}

		DeleteAllPlayerSessions();
		bStreamingStarted = false;
	}

	void FStreamer::RemoveSession(bool bDisconnect)
	{
		if (!EpicRtcManager->EpicRtcSession)
		{
			return;
		}

		RemoveRoom();

		if (bDisconnect)
		{
			EpicRtcErrorCode Result = EpicRtcManager->EpicRtcSession->Disconnect(ToEpicRtcStringView("Streaming Session Removed"));
			if (Result == EpicRtcErrorCode::SessionDisconnected)
			{
				UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("FStreamer::StopStreaming - Session disconnected cleanly."));
			}
			else if (Result != EpicRtcErrorCode::Ok)
			{
				UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to disconnect EpicRtcSession. Disconnect returned %s"), *ToString(Result));
			}
		}

		FUtf8String Utf8StreamerId(StreamerId);
		EpicRtcManager->EpicRtcConference->RemoveSession(ToEpicRtcStringView(Utf8StreamerId));

		EpicRtcManager->EpicRtcSession = nullptr;
	}

	void FStreamer::RemoveRoom()
	{
		if (!EpicRtcManager->EpicRtcRoom)
		{
			return;
		}

		FUtf8String Utf8StreamerId(StreamerId);
		EpicRtcManager->EpicRtcRoom->Leave();
		EpicRtcManager->EpicRtcSession->RemoveRoom(ToEpicRtcStringView(Utf8StreamerId));

		EpicRtcManager->EpicRtcRoom = nullptr;
	}

	void FStreamer::OnStatsReady(const FString& PlayerId, const EpicRtcConnectionStats& ConnectionStats)
	{
		FPlayerContext* PlayerContext = Players->Find(PlayerId);
		if (!PlayerContext)
		{
			return;
		}

		if (!PlayerContext->StatsCollector)
		{
			return;
		}

		PlayerContext->StatsCollector->Process(ConnectionStats);
	}

	IPixelStreaming2Streamer::FPreConnectionEvent& FStreamer::OnPreConnection()
	{
		return StreamingPreConnectionEvent;
	}

	IPixelStreaming2Streamer::FStreamingStartedEvent& FStreamer::OnStreamingStarted()
	{
		return StreamingStartedEvent;
	}

	IPixelStreaming2Streamer::FStreamingStoppedEvent& FStreamer::OnStreamingStopped()
	{
		return StreamingStoppedEvent;
	}

	void FStreamer::ForceKeyFrame()
	{
		VideoSourceGroup->ForceKeyFrame();
	}

	void FStreamer::FreezeStream(UTexture2D* Texture)
	{
		FreezeFrame->StartFreeze(Texture);
	}

	void FStreamer::UnfreezeStream()
	{
		// Force a keyframe so when stream unfreezes if player has never received a frame before they can still connect.
		ForceKeyFrame();
		FreezeFrame->StopFreeze();
	}

	void FStreamer::SendAllPlayersMessage(FString MessageType, const FString& Descriptor)
	{
		Players->Apply([&MessageType, &Descriptor](FString PlayerId, FPlayerContext& PlayerContext) {
			if (PlayerContext.DataTrack && !IsSFU(PlayerId))
			{
				PlayerContext.DataTrack->SendMessage(MessageType, Descriptor);
			}
		});
	}

	void FStreamer::SendPlayerMessage(FString PlayerId, FString MessageType, const FString& Descriptor)
	{
		if (IsSFU(PlayerId))
		{
			return;
		}
		if (FPlayerContext* PlayerContext = Players->Find(PlayerId))
		{
			if (!PlayerContext->DataTrack)
			{
				return;
			}
			PlayerContext->DataTrack->SendMessage(MessageType, Descriptor);
		}
	}

	void FStreamer::SendFileData(const TArray64<uint8>& ByteData, FString& MimeType, FString& FileExtension)
	{
		// TODO this should be dispatched as an async task, but because we lock when we visit the data
		// channels it might be a bad idea. At some point it would be good to take a snapshot of the
		// keys in the map when we start, then one by one get the channel and send the data

		Players->Apply([&ByteData, &MimeType, &FileExtension](FString PlayerId, FPlayerContext& PlayerContext) {
			if (!PlayerContext.DataTrack)
			{
				return;
			}

			// Send the mime type first
			PlayerContext.DataTrack->SendMessage(EPixelStreaming2FromStreamerMessage::FileMimeType, MimeType);

			// Send the extension next
			PlayerContext.DataTrack->SendMessage(EPixelStreaming2FromStreamerMessage::FileExtension, FileExtension);

			// Send the contents of the file. Note to callers: consider running this on its own thread, it can take a while if the file is big.
			PlayerContext.DataTrack->SendArbitraryData(EPixelStreaming2FromStreamerMessage::FileContents, ByteData);
		});
	}

	void FStreamer::KickPlayer(FString PlayerId)
	{
		if (FPlayerContext* PlayerContext = Players->Find(PlayerId))
		{
			PlayerContext->ParticipantInterface->Kick();
		}
	}

	TArray<FString> FStreamer::GetConnectedPlayers()
	{
		TArray<FString> ConnectedPlayerIds;
		Players->Apply([&ConnectedPlayerIds, this](FString PlayerId, FPlayerContext& PlayerContext) {
			ConnectedPlayerIds.Add(PlayerId);
		});
		return ConnectedPlayerIds;
	}

	IPixelStreaming2AudioSink* FStreamer::GetPeerAudioSink(FString PlayerId)
	{
		if (FPlayerContext* PlayerContext = Players->Find(PlayerId))
		{
			if (PlayerContext->AudioSink)
			{
				return PlayerContext->AudioSink.Get();
			}
		}
		return nullptr;
	}

	IPixelStreaming2AudioSink* FStreamer::GetUnlistenedAudioSink()
	{
		IPixelStreaming2AudioSink* Result = nullptr;
		Players->ApplyUntil([&Result](FString PlayerId, FPlayerContext& PlayerContext) {
			if (PlayerContext.AudioSink)
			{
				if (!PlayerContext.AudioSink->HasAudioConsumers())
				{
					Result = PlayerContext.AudioSink.Get();
					return true;
				}
			}
			return false;
		});
		return Result;
	}

	IPixelStreaming2VideoSink* FStreamer::GetPeerVideoSink(FString PlayerId)
	{
		if (FPlayerContext* PlayerContext = Players->Find(PlayerId))
		{
			if (PlayerContext->VideoSink)
			{
				return PlayerContext->VideoSink.Get();
			}
		}
		return nullptr;
	}

	IPixelStreaming2VideoSink* FStreamer::GetUnwatchedVideoSink()
	{
		IPixelStreaming2VideoSink* Result = nullptr;
		Players->ApplyUntil([&Result](FString PlayerId, FPlayerContext& PlayerContext) {
			if (PlayerContext.VideoSink)
			{
				if (!PlayerContext.VideoSink->HasVideoConsumers())
				{
					Result = PlayerContext.VideoSink.Get();
					return true;
				}
			}
			return false;
		});
		return Result;
	}

	void FStreamer::SetConfigOption(const FName& OptionName, const FString& Value)
	{
		if (Value.IsEmpty())
		{
			ConfigOptions.Remove(OptionName);
		}
		else
		{
			ConfigOptions.Add(OptionName, Value);
		}
	}

	bool FStreamer::GetConfigOption(const FName& OptionName, FString& OutValue)
	{
		FString* OptionValue = ConfigOptions.Find(OptionName);
		if (OptionValue)
		{
			OutValue = *OptionValue;
			return true;
		}
		else
		{
			return false;
		}
	}

	void FStreamer::PlayerRequestsBitrate(FString PlayerId, int MinBitrate, int MaxBitrate)
	{
		UPixelStreaming2PluginSettings::CVarWebRTCMinBitrate.AsVariable()->Set(MinBitrate);
		UPixelStreaming2PluginSettings::CVarWebRTCMaxBitrate.AsVariable()->Set(MaxBitrate);
	}

	void FStreamer::RefreshStreamBitrate()
	{
		Players->Apply([this](FString PlayerId, FPlayerContext& PlayerContext) {
			if (!PlayerContext.ParticipantInterface)
			{
				return;
			}

			TRefCountPtr<EpicRtcConnectionInterface> ConnectionInterface = PlayerContext.ParticipantInterface->GetConnection();
			if (!ConnectionInterface)
			{
				return;
			}

			EpicRtcBitrate Bitrates = {
				._minBitrateBps = UPixelStreaming2PluginSettings::CVarWebRTCMinBitrate.GetValueOnAnyThread(),
				._hasMinBitrateBps = true,
				._maxBitrateBps = UPixelStreaming2PluginSettings::CVarWebRTCMaxBitrate.GetValueOnAnyThread(),
				._hasMaxBitrateBps = true,
				._startBitrateBps = UPixelStreaming2PluginSettings::CVarWebRTCStartBitrate.GetValueOnAnyThread(),
				._hasStartBitrateBps = true
			};

			ConnectionInterface->SetConnectionRates(Bitrates);
		});
	}

	void FStreamer::ForEachPlayer(const TFunction<void(FString, FPlayerContext)>& Func)
	{
		Players->Apply(Func);
	}

	void FStreamer::ConsumeStats(FString PlayerId, FName StatName, float StatValue)
	{
		if (IsSFU(PlayerId))
		{
			return;
		}

		if (StatName != PixelStreaming2StatNames::MeanQPPerSecond)
		{
			return;
		}

		FPlayerContext* PlayerContext = Players->Find(PlayerId);

		if (!PlayerContext)
		{
			return;
		}

		if (!PlayerContext->DataTrack)
		{
			return;
		}

		PlayerContext->DataTrack->SendMessage(EPixelStreaming2FromStreamerMessage::VideoEncoderAvgQP, FString::FromInt((int)StatValue));
	}

	void FStreamer::DeletePlayerSession(FString PlayerId)
	{
		// We dont want to allow this to be deleted within Players.Remove because
		// we lock the players map and the delete could dispatch a webrtc object
		// delete on the signalling thread which might be waiting for the players
		// lock.
		FPlayerContext PendingDeletePlayer;
		if (FPlayerContext* PlayerContext = Players->Find(PlayerId))
		{
			// when a sfu is connected we only get disconnect messages.
			// we dont get connect messages but we might get datachannel requests which can result
			// in players with no PeerConnection but a datachannel
			if (PlayerContext->VideoSource)
			{
				VideoSourceGroup->RemoveVideoSource(PlayerContext->VideoSource.Get());
			}

			// Close any data track related things (we do this here because RoomLeft happens before DataTrack stopped fires)
			// So if we only did this in DataTrack stooped the playerId would already be removed
			OnDataChannelClosed(PlayerId);

			PendingDeletePlayer = *PlayerContext;
		}

		Players->Remove(PlayerId);

		if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
		{
			Delegates->OnClosedConnection.Broadcast(StreamerId, PlayerId);
			Delegates->OnClosedConnectionNative.Broadcast(StreamerId, PlayerId);
			if (Players->IsEmpty())
			{
				Delegates->OnAllConnectionsClosed.Broadcast(StreamerId);
				Delegates->OnAllConnectionsClosedNative.Broadcast(StreamerId);
			}
		}

		if (FStats* PSStats = FStats::Get())
		{
			PSStats->RemovePeerStats(PlayerId);
		}
	}

	void FStreamer::DeleteAllPlayerSessions()
	{
		if (FStats* PSStats = FStats::Get())
		{
			PSStats->RemoveAllPeerStats();
		}

		VideoSourceGroup->RemoveAllVideoSources();
		Players->Empty();
		InputControllingId = INVALID_PLAYER_ID;
		if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
		{
			Delegates->OnAllConnectionsClosed.Broadcast(StreamerId);
			Delegates->OnAllConnectionsClosedNative.Broadcast(StreamerId);
		}
	}

	void FStreamer::OnDataChannelOpen(FString PlayerId)
	{
		// Only time we automatically make a new peer the input controlling host is if they are the first peer (and not the SFU).
		bool HostControlsInput = GetEnumFromCVar<EInputControllerMode>(UPixelStreaming2PluginSettings::CVarInputController) == EInputControllerMode::Host;
		if (HostControlsInput && !IsSFU(PlayerId) && InputControllingId == INVALID_PLAYER_ID)
		{
			InputControllingId = PlayerId;
		}

		if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
		{
			Delegates->OnDataTrackOpen.Broadcast(StreamerId, PlayerId);
			Delegates->OnDataTrackOpenNative.Broadcast(StreamerId, PlayerId);
		}

		// When data channel is open
		SendProtocol(PlayerId);
		// Try to send cached freeze frame (if we have one)
		FreezeFrame->SendCachedFreezeFrameTo(PlayerId);
		SendInitialSettings(PlayerId);
		SendPeerControllerMessages(PlayerId);
	}

	void FStreamer::OnDataChannelClosed(FString PlayerId)
	{
		if (FPlayerContext* PlayerContext = Players->Find(PlayerId))
		{
			PlayerContext->DataTrack = nullptr;

			if (InputControllingId == PlayerId)
			{
				InputControllingId = INVALID_PLAYER_ID;
				// just get the first channel we have and give it input control.
				Players->ApplyUntil([this](FString PlayerId, FPlayerContext& PlayerContext) {
					if (!PlayerContext.DataTrack)
					{
						return false;
					}
					if (IsSFU(PlayerId))
					{
						return false;
					}
					InputControllingId = PlayerId;
					PlayerContext.DataTrack->SendMessage(EPixelStreaming2FromStreamerMessage::InputControlOwnership, 1 /* ControlsInput */);
					return true;
				});
			}

			if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
			{
				Delegates->OnDataTrackClosed.Broadcast(StreamerId, PlayerId);
				Delegates->OnDataTrackClosedNative.Broadcast(StreamerId, PlayerId);
			}
		}
	}

	void FStreamer::SendInitialSettings(FString PlayerId) const
	{
		const FString PixelStreaming2Payload = FString::Printf(TEXT("{ \"AllowPixelStreamingCommands\": %s, \"DisableLatencyTest\": %s }"),
			UPixelStreaming2PluginSettings::CVarInputAllowConsoleCommands.GetValueOnAnyThread() ? TEXT("true") : TEXT("false"),
			UPixelStreaming2PluginSettings::CVarDisableLatencyTester.GetValueOnAnyThread() ? TEXT("true") : TEXT("false"));

		const FString WebRTCPayload = FString::Printf(TEXT("{ \"FPS\": %d, \"MinBitrate\": %d, \"MaxBitrate\": %d }"),
			UPixelStreaming2PluginSettings::CVarWebRTCFps.GetValueOnAnyThread(),
			UPixelStreaming2PluginSettings::CVarWebRTCMinBitrate.GetValueOnAnyThread(),
			UPixelStreaming2PluginSettings::CVarWebRTCMaxBitrate.GetValueOnAnyThread());

		const FString EncoderPayload = FString::Printf(TEXT("{ \"TargetBitrate\": %d, \"MinQuality\": %d, \"MaxQuality\": %d }"),
			UPixelStreaming2PluginSettings::CVarEncoderTargetBitrate.GetValueOnAnyThread(),
			UPixelStreaming2PluginSettings::CVarEncoderMinQuality.GetValueOnAnyThread(),
			UPixelStreaming2PluginSettings::CVarEncoderMaxQuality.GetValueOnAnyThread());

		FString ConfigPayload = TEXT("{ ");
		bool	bComma = false; // Simplest way to avoid complaints from pedantic JSON parsers
		for (const TPair<FName, FString>& Option : ConfigOptions)
		{
			if (bComma)
			{
				ConfigPayload.Append(TEXT(", "));
			}
			ConfigPayload.Append(FString::Printf(TEXT("\"%s\": \"%s\""), *Option.Key.ToString(), *Option.Value));
			bComma = true;
		}
		ConfigPayload.Append(TEXT("}"));

		const FString FullPayload = FString::Printf(TEXT("{ \"PixelStreaming\": %s, \"Encoder\": %s, \"WebRTC\": %s, \"ConfigOptions\": %s }"), *PixelStreaming2Payload, *EncoderPayload, *WebRTCPayload, *ConfigPayload);

		if (const FPlayerContext* PlayerContext = Players->Find(PlayerId))
		{
			if (!PlayerContext->DataTrack)
			{
				return;
			}
			PlayerContext->DataTrack->SendMessage(EPixelStreaming2FromStreamerMessage::InitialSettings, FullPayload);
		}
	}

	void FStreamer::SendProtocol(FString PlayerId) const
	{
		const TArray<TSharedPtr<IPixelStreaming2DataProtocol>> Protocols = { InputHandler->GetToStreamerProtocol(), InputHandler->GetFromStreamerProtocol() };
		for (TSharedPtr<IPixelStreaming2DataProtocol> Protocol : Protocols)
		{
			TSharedPtr<FJsonObject>	  ProtocolJson = Protocol->ToJson();
			FString					  Body;
			TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&Body);
			if (!ensure(FJsonSerializer::Serialize(ProtocolJson.ToSharedRef(), JsonWriter)))
			{
				UE_LOG(LogPixelStreaming2, Warning, TEXT("Cannot serialize protocol json object"));
				return;
			}

			if (const FPlayerContext* PlayerContext = Players->Find(PlayerId))
			{
				if (!PlayerContext->DataTrack)
				{
					return;
				}
				PlayerContext->DataTrack->SendMessage(EPixelStreaming2FromStreamerMessage::Protocol, Body);
			}
		}
	}

	void FStreamer::SendPeerControllerMessages(FString PlayerId) const
	{
		if (const FPlayerContext* PlayerContext = Players->Find(PlayerId))
		{
			if (!PlayerContext->DataTrack)
			{
				return;
			}
			const uint8 ControlsInput = (GetEnumFromCVar<EInputControllerMode>(UPixelStreaming2PluginSettings::CVarInputController) == EInputControllerMode::Host) ? (PlayerId == InputControllingId) : 1;
			// Even though the QualityController feature is removed we send it for backwards compatibility with older frontends (can probably remove 2 versions after 5.5)
			PlayerContext->DataTrack->SendMessage(EPixelStreaming2FromStreamerMessage::InputControlOwnership, ControlsInput);
			PlayerContext->DataTrack->SendMessage(EPixelStreaming2FromStreamerMessage::QualityControlOwnership, 1 /* True */);
		}
	}

	void FStreamer::SendLatencyReport(FString PlayerId) const
	{
		if (UPixelStreaming2PluginSettings::CVarDisableLatencyTester.GetValueOnAnyThread())
		{
			return;
		}

		double ReceiptTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());

		AsyncTask(ENamedThreads::GameThread, [this, PlayerId, ReceiptTimeMs]() {
			FString ReportToTransmitJSON;

			if (!UPixelStreaming2PluginSettings::CVarWebRTCDisableStats.GetValueOnAnyThread())
			{
				double EncodeMs = -1.0;
				double CaptureToSendMs = 0.0;

				FStats* Stats = FStats::Get();
				if (Stats)
				{
					Stats->QueryPeerStat(PlayerId, RTCStatCategories::LocalVideoTrack, PixelStreaming2StatNames::MeanEncodeTime, EncodeMs);
					Stats->QueryPeerStat(PlayerId, RTCStatCategories::LocalVideoTrack, PixelStreaming2StatNames::AvgSendDelay, CaptureToSendMs);
				}

				double TransmissionTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());
				ReportToTransmitJSON = FString::Printf(
					TEXT("{ \"ReceiptTimeMs\": %.2f, \"EncodeMs\": %.2f, \"CaptureToSendMs\": %.2f, \"TransmissionTimeMs\": %.2f }"),
					ReceiptTimeMs,
					EncodeMs,
					CaptureToSendMs,
					TransmissionTimeMs);
			}
			else
			{
				double TransmissionTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());
				ReportToTransmitJSON = FString::Printf(
					TEXT("{ \"ReceiptTimeMs\": %.2f, \"EncodeMs\": \"Pixel Streaming stats are disabled\", \"CaptureToSendMs\": \"Pixel Streaming stats are disabled\", \"TransmissionTimeMs\": %.2f }"),
					ReceiptTimeMs,
					TransmissionTimeMs);
			}

			if (const FPlayerContext* PlayerContext = Players->Find(PlayerId))
			{
				if (PlayerContext->DataTrack)
				{
					PlayerContext->DataTrack->SendMessage(EPixelStreaming2FromStreamerMessage::LatencyTest, ReportToTransmitJSON);
				}
			}
		});
	}

	void FStreamer::HandleRelayStatusMessage(const uint8_t* Data, uint32_t Size, EpicRtcDataTrackInterface* DataTrack)
	{
		//skip type
		Data++;
		Size--;
		FString PlayerId = ReadString(Data, Size);
		checkf(Size > 0, TEXT("Malformed relay status message!"))
		bool bIsOn = static_cast<bool>(Data[0]);

		FString DataTrackId = ToString(DataTrack->GetId());
		if (bIsOn)
		{
			UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::HandleRelayStatusMessage() Adding new PlayerId [%s] with DataTrackId [%s]"), *PlayerId, *DataTrackId);
			
			FString SFUId;
			if (FindPlayerByDataTrack(DataTrack, SFUId))
			{
				FPlayerContext* SFUContext = Players->Find(SFUId);
				FPlayerContext& PlayerContext = Players->FindOrAdd(PlayerId);
				PlayerContext.DataTrack = FEpicRtcMutliplexDataTrack::Create(SFUContext->DataTrack, InputHandler->GetFromStreamerProtocol(), PlayerId);
				OnDataChannelOpen(PlayerId);
			}
			else
			{
				UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::HandleRelayStatusMessage() Failed to find SFU PlayerContext"));
			}
		}
		else
		{
			UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::HandleRelayStatusMessage() Removing PlayerId [%s] with DataTrackId [%s]"), *PlayerId, *DataTrackId);

			OnDataChannelClosed(PlayerId);
			Players->Remove(PlayerId);
		}
	}

	void FStreamer::TriggerMouseLeave(FString InStreamerId)
	{
		if (!IsEngineExitRequested() && StreamerId == InStreamerId)
		{
			TSharedPtr<IPixelStreaming2InputHandler> SharedInputHandler = InputHandler;

			// Force a MouseLeave event. This prevents the PixelStreaming2ApplicationWrapper from
			// still wrapping the base FSlateApplication after we stop streaming
			const auto MouseLeaveFunction = [SharedInputHandler]() {
				if (SharedInputHandler.IsValid())
				{
					TArray<uint8>							EmptyArray;
					TFunction<void(FString, FMemoryReader)> MouseLeaveHandler = SharedInputHandler->FindMessageHandler("MouseLeave");
					MouseLeaveHandler("", FMemoryReader(EmptyArray));
				}
			};

			if (IsInGameThread())
			{
				MouseLeaveFunction();
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, [MouseLeaveFunction]() {
					MouseLeaveFunction();
				});
			}
		}
	}

	void FStreamer::OnSessionStateUpdate(const EpicRtcSessionState State)
	{
		switch (State)
		{
			case EpicRtcSessionState::Connected:
			{
				bSignallingConnected = true;
				if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
				{
					Delegates->OnConnectedToSignallingServer.Broadcast(StreamerId);
					Delegates->OnConnectedToSignallingServerNative.Broadcast(StreamerId);
				}

				UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("FStreamer::OnSessionStateUpdate State=Connected"));
				EpicRtcBitrate Bitrate = {
					._minBitrateBps = UPixelStreaming2PluginSettings::CVarWebRTCMinBitrate.GetValueOnAnyThread(),
					._hasMinBitrateBps = true,
					._maxBitrateBps = UPixelStreaming2PluginSettings::CVarWebRTCMaxBitrate.GetValueOnAnyThread(),
					._hasMaxBitrateBps = true,
					._startBitrateBps = UPixelStreaming2PluginSettings::CVarWebRTCStartBitrate.GetValueOnAnyThread(),
					._hasStartBitrateBps = true
				};

				EpicRtcPortAllocator PortAllocator = {
					._minPort = UPixelStreaming2PluginSettings::CVarWebRTCMinPort.GetValueOnAnyThread(),
					._hasMinPort = true,
					._maxPort = UPixelStreaming2PluginSettings::CVarWebRTCMaxPort.GetValueOnAnyThread(),
					._hasMaxPort = true,
					._portAllocation = static_cast<EpicRtcPortAllocatorOptions>(UPixelStreaming2PluginSettings::GetPortAllocationFlags())
				};

				EpicRtcConnectionConfig ConnectionConfig = {
					._iceServers = { ._ptr = nullptr, ._size = 0 }, // This can stay empty because EpicRtc handles the ice servers internally
					._portAllocator = PortAllocator,
					._bitrate = Bitrate,
					._iceConnectionPolicy = EpicRtcIcePolicy::All,
					._disableTcpCandidates = false
				};

				FUtf8String		  Utf8StreamerId(StreamerId);
				EpicRtcRoomConfig RoomConfig = {
					._id = ToEpicRtcStringView(Utf8StreamerId),
					._connectionConfig = ConnectionConfig,
					._ticket = EpicRtcStringView{ ._ptr = nullptr, ._length = 0 },
					._observer = EpicRtcManager->RoomObserver,
					._audioTrackObserverFactory = EpicRtcManager->AudioTrackObserverFactory,
					._dataTrackObserverFactory = EpicRtcManager->DataTrackObserverFactory,
					._videoTrackObserverFactory = EpicRtcManager->VideoTrackObserverFactory
				};

				EpicRtcErrorCode Result = EpicRtcManager->EpicRtcSession->CreateRoom(RoomConfig, EpicRtcManager->EpicRtcRoom.GetInitReference());
				if (Result != EpicRtcErrorCode::Ok)
				{
					UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to create EpicRtc room. CreateRoom returned %s"), *ToString(Result));
					break;
				}

				EpicRtcManager->EpicRtcRoom->Join();

				// Would be better renamed to OnSessionConnected
				OnStreamingStarted().Broadcast(this);
			}
			break;
			case EpicRtcSessionState::New:
				// Do something on `new` Session here
				UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("FStreamer::OnSessionStateUpdate State=New"));
				break;
			case EpicRtcSessionState::Pending:
				// Do something on `pending` session here
				UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("FStreamer::OnSessionStateUpdate State=Pending"));
				break;
			case EpicRtcSessionState::Disconnected:
				// Do something on `disconnected` session here
				bSignallingConnected = false;
				if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
				{
					Delegates->OnDisconnectedFromSignallingServer.Broadcast(StreamerId);
					Delegates->OnDisconnectedFromSignallingServerNative.Broadcast(StreamerId);
				}
				UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("FStreamer::OnSessionStateUpdate State=Disconnected"));
				RemoveSession(false);
				StopStreaming();
				ReconnectTimer->Start(AsShared());
				break;
			case EpicRtcSessionState::Failed:
				// Do something on `failed` session here
				bSignallingConnected = false;
				UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("FStreamer::OnSessionStateUpdate State=Failed"));
				break;
			case EpicRtcSessionState::Exiting:
				// Do something on `exiting` session here
				UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("FStreamer::OnSessionStateUpdate State=Exiting"));
				break;
			default:
				UE_LOG(LogPixelStreaming2, Error, TEXT("FStreamer::OnSessionStateUpdate An unhandled session state was encountered. This switch might be missing a case."));
				break;
		}
	}

	void FStreamer::OnSessionErrorUpdate(const EpicRtcErrorCode ErrorUpdate)
	{
		UE_LOGFMT(LogPixelStreaming2, VeryVerbose, "FStreamer::OnSessionErrorUpdate does nothing");
	}

	void FStreamer::OnSessionRoomsAvailableUpdate(EpicRtcStringArrayInterface* RoomsList)
	{
		UE_LOGFMT(LogPixelStreaming2, VeryVerbose, "FStreamer::OnSessionRoomsAvailableUpdate does nothing");
	}

	void FStreamer::OnRoomStateUpdate(const EpicRtcRoomState State)
	{
		UE_LOGFMT(LogPixelStreaming2, VeryVerbose, "FStreamer::OnRoomStateUpdate does nothing");
	}

	FUtf8String GetAudioStreamID()
	{
		const bool bSyncVideoAndAudio = !UPixelStreaming2PluginSettings::CVarWebRTCDisableAudioSync.GetValueOnAnyThread();
		return bSyncVideoAndAudio ? "pixelstreaming_av_stream_id" : "pixelstreaming_audio_stream_id";
	}

	FUtf8String GetVideoStreamID()
	{
		const bool bSyncVideoAndAudio = !UPixelStreaming2PluginSettings::CVarWebRTCDisableAudioSync.GetValueOnAnyThread();
		return bSyncVideoAndAudio ? "pixelstreaming_av_stream_id" : "pixelstreaming_video_stream_id";
	}

	void FStreamer::OnRoomJoinedUpdate(EpicRtcParticipantInterface* Participant)
	{
		FString ParticipantId = ToString(Participant->GetId());
		UE_LOG(LogPixelStreaming2, Log, TEXT("Player (%s) joined"), *ParticipantId);

		if (ParticipantId == StreamerId)
		{
			return;
		}

		if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
		{
			Delegates->OnNewConnection.Broadcast(StreamerId, ParticipantId);
			Delegates->OnNewConnectionNative.Broadcast(StreamerId, ParticipantId);
		}

		FPlayerContext& PlayerContext = Players->FindOrAdd(ParticipantId);
		PlayerContext.ParticipantInterface = Participant;
		PlayerContext.StatsCollector = FRTCStatsCollector::Create(ParticipantId);

		TRefCountPtr<EpicRtcConnectionInterface> ParticipantConnection = Participant->GetConnection();
		ParticipantConnection->SetManualNegotiation(true);

		const EVideoCodec SelectedCodec = GetEnumFromCVar<EVideoCodec>(UPixelStreaming2PluginSettings::CVarEncoderCodec);
		const bool		  bNegotiateCodecs = UPixelStreaming2PluginSettings::CVarWebRTCNegotiateCodecs.GetValueOnAnyThread();
		const bool		  bTransmitUEVideo = !UPixelStreaming2PluginSettings::CVarWebRTCDisableTransmitVideo.GetValueOnAnyThread();
		bool			  bReceiveBrowserVideo = !UPixelStreaming2PluginSettings::CVarWebRTCDisableReceiveVideo.GetValueOnAnyThread();

		// Check if the user has selected only H.264 on a AMD gpu and disable receiving video.
		// WebRTC does not support using SendRecv if the encoding and decoding do not support the same codec.
		// AMD GPUs currently have decoding disabled so WebRTC fails to create SDP codecs with SendRecv.
		if (IsRHIDeviceAMD() && !bNegotiateCodecs && SelectedCodec == EVideoCodec::H264)
		{
			if (bReceiveBrowserVideo)
			{
				bReceiveBrowserVideo = false;
				UE_LOGFMT(LogPixelStreaming2, Warning, "AMD GPUs do not support receiving H.264 video.");
			}
		}

		if (bTransmitUEVideo || bReceiveBrowserVideo)
		{
			TArray<EpicRtcVideoEncodingConfig> VideoEncodingConfigs;
			// We need ensure the Rids have the same lifetime as the VideoEncodingConfigs
			// to ensure the contents don't get deleted before we can call AddVideoSource
			TArray<FUtf8String> Rids;

			int MaxFramerate = UPixelStreaming2PluginSettings::CVarWebRTCFps.GetValueOnAnyThread();

			TArray<FPixelStreaming2SimulcastLayer> SimulcastParams = UE::PixelStreaming2::GetSimulcastParameters();
			for (int i = 0; i < SimulcastParams.Num(); ++i)
			{
				const FPixelStreaming2SimulcastLayer& SpatialLayer = SimulcastParams[i];

				if (SimulcastParams.Num() > 1)
				{
					Rids.Add(FUtf8String("simulcast") + FUtf8String::FromInt(SimulcastParams.Num() - i));
				}

				EpicRtcVideoEncodingConfig VideoEncodingConfig = {
					// clang-format off
					// TODO (Migration): RTCP-7027 Maybe bug in EpicRtc? Setting an rid if there's only one config results in no video
					._rid = SimulcastParams.Num() > 1
						? EpicRtcStringView{._ptr = (const char*)(*(Rids[i])), ._length = static_cast<uint64>(Rids[i].Len()) }
						: EpicRtcStringView{._ptr = nullptr, ._length = 0 },
					// clang-format on
					._scaleResolutionDownBy = SpatialLayer.Scaling,
					._scalabilityMode = static_cast<EpicRtcVideoScalabilityMode>(GetEnumFromCVar<EScalabilityMode>(UPixelStreaming2PluginSettings::CVarEncoderScalabilityMode)), // HACK if the Enums become un-aligned
					._minBitrate = (uint32_t)SpatialLayer.MinBitrate,
					._maxBitrate = (uint32_t)SpatialLayer.MaxBitrate,
					._maxFrameRate = (uint8_t)MaxFramerate
				};

				VideoEncodingConfigs.Add(VideoEncodingConfig);
			}

			EpicRtcVideoEncodingConfigSpan VideoEncodingConfigSpan = {
				._ptr = VideoEncodingConfigs.GetData(),
				._size = (uint64_t)VideoEncodingConfigs.Num()
			};

			EpicRtcMediaSourceDirection VideoDirection;
			if (bTransmitUEVideo && bReceiveBrowserVideo)
			{
				VideoDirection = EpicRtcMediaSourceDirection::SendRecv;
			}
			else if (bTransmitUEVideo)
			{
				VideoDirection = EpicRtcMediaSourceDirection::SendOnly;
			}
			else if (bReceiveBrowserVideo)
			{
				VideoDirection = EpicRtcMediaSourceDirection::RecvOnly;
			}
			else
			{
				VideoDirection = EpicRtcMediaSourceDirection::RecvOnly;
			}

			FUtf8String		   VideoStreamID = GetVideoStreamID();
			EpicRtcVideoSource VideoSource = {
				._streamId = ToEpicRtcStringView(VideoStreamID),
				._encodings = VideoEncodingConfigSpan,
				._direction = VideoDirection
			};

			ParticipantConnection->AddVideoSource(VideoSource);
		}

		const bool bTransmitUEAudio = !UPixelStreaming2PluginSettings::CVarWebRTCDisableTransmitAudio.GetValueOnAnyThread();
		const bool bReceiveBrowserAudio = !UPixelStreaming2PluginSettings::CVarWebRTCDisableReceiveAudio.GetValueOnAnyThread();
		if (bTransmitUEAudio || bReceiveBrowserAudio)
		{
			EpicRtcMediaSourceDirection AudioDirection;
			if (bTransmitUEAudio && bReceiveBrowserAudio)
			{
				AudioDirection = EpicRtcMediaSourceDirection::SendRecv;
			}
			else if (bTransmitUEAudio)
			{
				AudioDirection = EpicRtcMediaSourceDirection::SendOnly;
			}
			else if (bReceiveBrowserAudio)
			{
				AudioDirection = EpicRtcMediaSourceDirection::RecvOnly;
			}
			else
			{
				AudioDirection = EpicRtcMediaSourceDirection::RecvOnly;
			}

			FUtf8String		   AudioStreamID = GetAudioStreamID();
			EpicRtcAudioSource AudioSource = {
				._streamId = ToEpicRtcStringView(AudioStreamID),
				._bitrate = 510000,
				._channels = 2,
				._direction = AudioDirection
			};

			ParticipantConnection->AddAudioSource(AudioSource);
		}

		if (IsSFU(ParticipantId))
		{
			FString RecvLabel(TEXT("recv-datachannel"));
			FUtf8String Utf8RecvLabel = *RecvLabel;
			EpicRtcDataSource RecvDataSource = {
				._label = ToEpicRtcStringView(Utf8RecvLabel),
				._maxRetransmitTime = 0,
				._maxRetransmits = 0,
				._isOrdered = true,
				._protocol = EpicRtcDataSourceProtocol::Sctp,
				._negotiated = true,
				._transportChannelId = 1
			};
			ParticipantConnection->AddDataSource(RecvDataSource);

			FString SendLabel(TEXT("send-datachannel"));
			FUtf8String Utf8SendLabel = *SendLabel;
			EpicRtcDataSource SendDataSource = {
				._label = ToEpicRtcStringView(Utf8RecvLabel),
				._maxRetransmitTime = 0,
				._maxRetransmits = 0,
				._isOrdered = true,
				._protocol = EpicRtcDataSourceProtocol::Sctp,
				._negotiated = true,
				._transportChannelId = 0
			};
			ParticipantConnection->AddDataSource(SendDataSource);
		}
		else
		{
			EpicRtcDataSource DataSource = {
				._label = Participant->GetId(),
				._maxRetransmitTime = 0,
				._maxRetransmits = 0,
				._isOrdered = true,
				._protocol = EpicRtcDataSourceProtocol::Sctp
			};
			ParticipantConnection->AddDataSource(DataSource);
		}

		EpicRtcBitrate Bitrates = {
			._minBitrateBps = UPixelStreaming2PluginSettings::CVarWebRTCMinBitrate.GetValueOnAnyThread(),
			._hasMinBitrateBps = true,
			._maxBitrateBps = UPixelStreaming2PluginSettings::CVarWebRTCMaxBitrate.GetValueOnAnyThread(),
			._hasMaxBitrateBps = true,
			._startBitrateBps = UPixelStreaming2PluginSettings::CVarWebRTCStartBitrate.GetValueOnAnyThread(),
			._hasStartBitrateBps = true
		};

		ParticipantConnection->SetConnectionRates(Bitrates);

		ParticipantConnection->StartNegotiation();
	}

	void FStreamer::OnRoomLeftUpdate(const EpicRtcStringView Participant)
	{
		FString ParticipantId = ToString(Participant);
		UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnRoomLeftUpdate(Participant [%s] left the room.)"), *ParticipantId);

		// Remove the player
		DeletePlayerSession(ParticipantId);
	}

	void FStreamer::OnAudioTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcAudioTrackInterface* AudioTrack)
	{
		FString ParticipantId = ToString(Participant->GetId());
		FString AudioTrackId = ToString(AudioTrack->GetId());
		UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnAudioTrackUpdate(Participant [%s], AudioTrack [%s])"), *ParticipantId, *AudioTrackId);

		if (FPlayerContext* PlayerContext = Players->Find(ParticipantId))
		{
			if (AudioTrack->IsRemote())
			{
				PlayerContext->AudioSink = FEpicRtcAudioSink::Create(AudioTrack);
			}
			else
			{
				PlayerContext->AudioSource = FEpicRtcAudioSource::Create(AudioTrack);
			}

			if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
			{
				Delegates->OnAudioTrackOpenNative.Broadcast(StreamerId, ParticipantId, (bool)AudioTrack->IsRemote());
			}
		}
	}

	void FStreamer::OnVideoTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcVideoTrackInterface* VideoTrack)
	{
		FString ParticipantId = ToString(Participant->GetId());
		FString VideoTrackId = ToString(VideoTrack->GetId());
		UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnVideoTrackUpdate(Participant [%s], VideoTrack [%s])"), *ParticipantId, *VideoTrackId);

		if (FPlayerContext* PlayerContext = Players->Find(ParticipantId))
		{
			if (VideoTrack->IsRemote())
			{
				PlayerContext->VideoSink = FEpicRtcVideoSink::Create(VideoTrack);
			}
			else
			{
				PlayerContext->VideoSource = FEpicRtcVideoSource::Create(VideoTrack, VideoCapturer, VideoSourceGroup);
			}

			if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
			{
				Delegates->OnVideoTrackOpenNative.Broadcast(StreamerId, ParticipantId, (bool)VideoTrack->IsRemote());
			}
		}
	}

	void FStreamer::OnDataTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcDataTrackInterface* DataTrack)
	{
		FString ParticipantId = ToString(Participant->GetId());
		FString DataTrackId = ToString(DataTrack->GetId());
		UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnDataTrackUpdate(Participant [%s], DataTrack [%s])"), *ParticipantId, *DataTrackId);

		if (FPlayerContext* PlayerContext = Players->Find(ParticipantId))
		{
			if (!PlayerContext->DataTrack)
			{
				PlayerContext->DataTrack = FEpicRtcDataTrack::Create(DataTrack, InputHandler->GetFromStreamerProtocol());
			}
			else
			{
				PlayerContext->DataTrack->SetSendTrack(DataTrack);
			}
		}
	}

	void FStreamer::OnLocalSdpUpdate(EpicRtcParticipantInterface* Participant, EpicRtcSdpInterface* Sdp)
	{
		FString ParticipantId = ToString(Participant->GetId());
		FString SdpType = TEXT("");
		switch (Sdp->GetType())
		{
			case EpicRtcSdpType::Offer:
				SdpType = TEXT("Offer");
				break;
			case EpicRtcSdpType::Answer:
				SdpType = TEXT("Answer");
				break;
		}
		UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnLocalSdpUpdate(Participant [%s], Type [%s])"), *ParticipantId, *SdpType);
	}

	void FStreamer::OnRemoteSdpUpdate(EpicRtcParticipantInterface* Participant, EpicRtcSdpInterface* Sdp)
	{
		FString ParticipantId = ToString(Participant->GetId());
		FString SdpType = TEXT("");
		switch (Sdp->GetType())
		{
			case EpicRtcSdpType::Offer:
				SdpType = TEXT("Offer");
				break;
			case EpicRtcSdpType::Answer:
				SdpType = TEXT("Answer");
				break;
		}
		UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnRemoteSdpUpdate(Participant [%s], Type [%s])"), *ParticipantId, *SdpType);
	}

	void FStreamer::OnRoomErrorUpdate(const EpicRtcErrorCode Error)
	{
		UE_LOGFMT(LogPixelStreaming2, VeryVerbose, "FStreamer::OnRoomErrorUpdate does nothing");
	}

	void FStreamer::OnAudioTrackMuted(EpicRtcAudioTrackInterface* AudioTrack, EpicRtcBool bIsMuted)
	{
		FString PlayerId;
		bool	bFoundPlayer = FindPlayerByAudioTrack(AudioTrack, PlayerId);
		FString AudioTrackId = ToString(AudioTrack->GetId());
		UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnAudioTrackMuted(AudioTrack [%s], bIsMuted[%s], PlayerId[%s])"), *AudioTrackId, bIsMuted ? TEXT("True") : TEXT("False"), *PlayerId);

		if (!bFoundPlayer)
		{
			UE_LOG(LogPixelStreaming2, Warning, TEXT("FStreamer::OnAudioTrackMuted(Failed to find a player for audio track [%s])"), *AudioTrackId);
			return;
		}

		FPlayerContext* PlayerContext = Players->Find(PlayerId);
		if (AudioTrack->IsRemote())
		{
			PlayerContext->AudioSink->SetMuted((bool)bIsMuted);
		}
		else
		{
			PlayerContext->AudioSource->SetMuted((bool)bIsMuted);
		}
	}

	void FStreamer::OnAudioTrackFrame(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcAudioFrame& Frame)
	{
		FString PlayerId;
		bool	bFoundPlayer = FindPlayerByAudioTrack(AudioTrack, PlayerId);
		FString AudioTrackId = ToString(AudioTrack->GetId());

		if (!bFoundPlayer)
		{
			UE_LOG(LogPixelStreaming2, Warning, TEXT("FStreamer::OnAudioTrackFrame(Failed to find a player for audio track [%s])"), *AudioTrackId);
			return;
		}

		FPlayerContext* PlayerContext = Players->Find(PlayerId);
		if (PlayerContext->AudioSink)
		{
			PlayerContext->AudioSink->OnAudioData(Frame._data, Frame._length, Frame._format._numChannels, Frame._format._sampleRate);
		}
	}

	void FStreamer::OnAudioTrackRemoved(EpicRtcAudioTrackInterface* AudioTrack)
	{
		FString PlayerId;
		bool	bFoundPlayer = FindPlayerByAudioTrack(AudioTrack, PlayerId);
		FString AudioTrackId = ToString(AudioTrack->GetId());
		UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnAudioTrackRemoved(AudioTrack [%s], PlayerId[%s])"), *AudioTrackId, *PlayerId);

		if (!bFoundPlayer)
		{
			UE_LOG(LogPixelStreaming2, Warning, TEXT("FStreamer::OnAudioTrackFrame(Failed to find a player for audio track [%s])"), *AudioTrackId);
			return;
		}

		FPlayerContext* PlayerContext = Players->Find(PlayerId);
		if (AudioTrack->IsRemote())
		{
			PlayerContext->AudioSink = nullptr;
		}
		else
		{
			PlayerContext->AudioSource = nullptr;
		}

		if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
		{
			Delegates->OnAudioTrackClosedNative.Broadcast(StreamerId, PlayerId, (bool)AudioTrack->IsRemote());
		}
	}

	void FStreamer::OnAudioTrackState(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcTrackState State)
	{
		UE_LOGFMT(LogPixelStreaming2, VeryVerbose, "FStreamer::OnAudioTrackState does nothing");
	}

	void FStreamer::OnVideoTrackMuted(EpicRtcVideoTrackInterface* VideoTrack, EpicRtcBool bIsMuted)
	{
		FString PlayerId;
		bool	bFoundPlayer = FindPlayerByVideoTrack(VideoTrack, PlayerId);
		FString VideoTrackId = ToString(VideoTrack->GetId());
		UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnVideoTrackMuted(VideoTrack [%s], bIsMuted[%s], PlayerId[%s])"), *VideoTrackId, bIsMuted ? TEXT("True") : TEXT("False"), *PlayerId);

		if (!bFoundPlayer)
		{
			UE_LOG(LogPixelStreaming2, Warning, TEXT("FStreamer::OnVideoTrackMuted(Failed to find a player for video track [%s])"), *VideoTrackId);
			return;
		}

		FPlayerContext* PlayerContext = Players->Find(PlayerId);
		if (VideoTrack->IsRemote())
		{
			PlayerContext->VideoSink->SetMuted((bool)bIsMuted);
		}
		else
		{
			PlayerContext->VideoSource->SetMuted((bool)bIsMuted);
		}
	}

	void FStreamer::OnVideoTrackFrame(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcVideoFrame& Frame)
	{
		FString PlayerId;
		bool	bFoundPlayer = FindPlayerByVideoTrack(VideoTrack, PlayerId);
		FString VideoTrackId = ToString(VideoTrack->GetId());

		if (!bFoundPlayer)
		{
			UE_LOG(LogPixelStreaming2, Warning, TEXT("FStreamer::OnVideoTrackFrame(Failed to find a player for video track [%s])"), *VideoTrackId);
			return;
		}

		FPlayerContext* PlayerContext = Players->Find(PlayerId);
		if (PlayerContext->VideoSink)
		{
			PlayerContext->VideoSink->OnVideoData(Frame);
		}
	}

	void FStreamer::OnVideoTrackRemoved(EpicRtcVideoTrackInterface* VideoTrack)
	{
		FString PlayerId;
		bool	bFoundPlayer = FindPlayerByVideoTrack(VideoTrack, PlayerId);
		FString VideoTrackId = ToString(VideoTrack->GetId());

		if (!bFoundPlayer)
		{
			UE_LOG(LogPixelStreaming2, Warning, TEXT("FStreamer::OnVideoTrackRemoved(Failed to find a player for video track [%s])"), *VideoTrackId);
			return;
		}

		UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnVideoTrackRemoved(VideoTrack=[%s], Player=[%s])"), *VideoTrackId, *PlayerId);

		// If we did find a player, clear its video sink/source
		FPlayerContext* PlayerContext = Players->Find(PlayerId);
		if (VideoTrack->IsRemote())
		{
			PlayerContext->VideoSink = nullptr;
		}
		else
		{
			PlayerContext->VideoSource = nullptr;
		}

		if (UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get())
		{
			Delegates->OnVideoTrackClosedNative.Broadcast(StreamerId, PlayerId, (bool)VideoTrack->IsRemote());
		}
	}

	void FStreamer::OnVideoTrackState(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcTrackState State)
	{
		FString PlayerId;
		bool	bFoundPlayer = FindPlayerByVideoTrack(VideoTrack, PlayerId);
		FString VideoTrackId = ToString(VideoTrack->GetId());

		// Note: It is acceptable to not have a found a player for track state changes, as these can trigger before we have added a participant

		if (State == EpicRtcTrackState::Active)
		{
			UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnVideoTrackState(VideoTrack=[%s], Player=[%s], State=Active)"), *VideoTrackId, *PlayerId);
		}
		else if (State == EpicRtcTrackState::Stopped)
		{
			UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnVideoTrackState(VideoTrack=[%s], Player=[%s], State=Stopped)"), *VideoTrackId, *PlayerId);
		}
	}

	void FStreamer::OnDataTrackRemoved(EpicRtcDataTrackInterface* DataTrack)
	{
		// As long as DataTrack emits `stopped` state when it removed, this should be enough.
		// because OnDataTrackState(DataTrack, EpicRtcTrackState::Stopped) already calls `OnDataChannelClosed`.
	}

	void FStreamer::OnDataTrackState(EpicRtcDataTrackInterface* DataTrack, const EpicRtcTrackState State)
	{
		FString PlayerId;
		bool	bFoundPlayer = FindPlayerByDataTrack(DataTrack, PlayerId);
		FString DataTrackId = ToString(DataTrack->GetId());

		if (State == EpicRtcTrackState::Active)
		{
			if (!bFoundPlayer)
			{
				UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnDataTrackState(Message was State=Active. Failed to find a player for data track [%s])"), *DataTrackId);
				return;
			}
			UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnDataTrackState(Message was State=Active. Player [%s])"), *DataTrackId);
			OnDataChannelOpen(PlayerId);
		}
		else if (State == EpicRtcTrackState::Stopped)
		{
			UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnDataTrackState(Message was State=Stopped. Player [%s])"), *DataTrackId);
			OnDataChannelClosed(PlayerId);
		}
	}

	void FStreamer::OnDataTrackMessage(EpicRtcDataTrackInterface* DataTrack)
	{
		FString DataTrackId = ToString(DataTrack->GetId());
		TRefCountPtr<EpicRtcDataFrameInterface> DataFrame;
		if (!DataTrack->PopFrame(DataFrame.GetInitReference()))
		{
			UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnDataTrackMessage(Failed to PopFrame [%s])"), *DataTrackId);
			return;
		}
		FString PlayerId;
		const uint8_t* Data = DataFrame->Data();
		uint32_t DataSize = DataFrame->Size();
		uint8 Type = Data[0];
		TSharedPtr<IPixelStreaming2DataProtocol> ToStreamerProtocol = InputHandler->GetToStreamerProtocol();
		if (Type == ToStreamerProtocol->Find(EPixelStreaming2ToStreamerMessage::Multiplexed)->GetID())
		{
			//skip type
			Data++;
			DataSize--;
			PlayerId = ReadString(Data, DataSize); 
			Type = Data[0];
			UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("FStreamer::OnDataTrackMessage(Received multiplexed message of type [%d] with PlayerId [%s])"), Type, *PlayerId);
		}
		else if (Type == ToStreamerProtocol->Find(EPixelStreaming2ToStreamerMessage::ChannelRelayStatus)->GetID())
		{
			HandleRelayStatusMessage(Data, DataSize, DataTrack);
			return;
		}
		else if (!FindPlayerByDataTrack(DataTrack, PlayerId))
		{
			UE_LOG(LogPixelStreaming2, Log, TEXT("FStreamer::OnDataTrackMessage(Failed to find a player for data track [%s])"), *DataTrackId);
			return;
		}
		
		if (Type == ToStreamerProtocol->Find(EPixelStreaming2ToStreamerMessage::LatencyTest)->GetID())
		{
			SendLatencyReport(PlayerId);
		}
		else if (Type == ToStreamerProtocol->Find(EPixelStreaming2ToStreamerMessage::RequestInitialSettings)->GetID())
		{
			SendInitialSettings(PlayerId);
		}
		else if (Type == ToStreamerProtocol->Find(EPixelStreaming2ToStreamerMessage::IFrameRequest)->GetID())
		{
			ForceKeyFrame();
		}
		else if (Type == ToStreamerProtocol->Find(EPixelStreaming2ToStreamerMessage::TestEcho)->GetID())
		{
			if (FPlayerContext* PlayerContext = Players->Find(PlayerId))
			{
				if (PlayerContext->DataTrack)
				{
					const size_t  DescriptorSize = (DataSize - 1) / sizeof(TCHAR);
					const TCHAR*  DescPtr = reinterpret_cast<const TCHAR*>(Data + 1);
					const FString Message(DescriptorSize, DescPtr);
					PlayerContext->DataTrack->SendMessage(EPixelStreaming2FromStreamerMessage::TestEcho, Message);
				}
			}
		}
		else if (!IsEngineExitRequested())
		{
			// If we are in "Host" mode and the current peer is not the host, then discard this input.
			if (GetEnumFromCVar<EInputControllerMode>(UPixelStreaming2PluginSettings::CVarInputController) == EInputControllerMode::Host && InputControllingId != PlayerId)
			{
				return;
			}
			
			TArray<uint8> MessageData(Data, DataSize);
			if (InputHandler)
			{
				InputHandler->OnMessage(PlayerId, MessageData);
			}
		}
	}

	bool FStreamer::FindPlayerByAudioTrack(EpicRtcAudioTrackInterface* AudioTrack, FString& OutPlayerId)
	{
		OutPlayerId = "";
		// Find the player corresponding to this data track
		FString AudioTrackId = ToString(AudioTrack->GetId());
		Players->ApplyUntil([this, AudioTrackId, &OutPlayerId](FString DataPlayerId, FPlayerContext& PlayerContext) {
			if (PlayerContext.AudioSource)
			{
				FString TrackId = ToString(PlayerContext.AudioSource->GetTrackId());
				if (TrackId == AudioTrackId)
				{
					OutPlayerId = DataPlayerId;
					return true;
				}
			}
			if (PlayerContext.AudioSink)
			{
				FString TrackId = ToString(PlayerContext.AudioSink->GetTrackId());
				if (TrackId == AudioTrackId)
				{
					OutPlayerId = DataPlayerId;
					return true;
				}
			}
			return false;
		});

		return !OutPlayerId.IsEmpty();
	}

	bool FStreamer::FindPlayerByVideoTrack(EpicRtcVideoTrackInterface* VideoTrack, FString& OutPlayerId)
	{
		OutPlayerId = "";
		// Find the player corresponding to this data track
		FString VideoTrackId = ToString(VideoTrack->GetId());
		Players->ApplyUntil([this, VideoTrackId, &OutPlayerId](FString DataPlayerId, FPlayerContext& PlayerContext) {
			if (PlayerContext.VideoSource)
			{
				FString TrackId = ToString(PlayerContext.VideoSource->GetTrackId());
				if (TrackId == VideoTrackId)
				{
					OutPlayerId = DataPlayerId;
					return true;
				}
			}
			if (PlayerContext.VideoSink)
			{
				FString TrackId = ToString(PlayerContext.VideoSink->GetTrackId());
				if (TrackId == VideoTrackId)
				{
					OutPlayerId = DataPlayerId;
					return true;
				}
			}
			return false;
		});

		return !OutPlayerId.IsEmpty();
	}

	bool FStreamer::FindPlayerByDataTrack(EpicRtcDataTrackInterface* DataTrack, FString& OutPlayerId)
	{
		OutPlayerId = "";
		// Find the player corresponding to this data track
		FString DataTrackId = ToString(DataTrack->GetId());
		Players->ApplyUntil([this, DataTrackId, &OutPlayerId](FString DataPlayerId, FPlayerContext& PlayerContext) {
			if (PlayerContext.DataTrack)
			{
				FString TrackId = ToString(PlayerContext.DataTrack->GetId());
				if (TrackId == DataTrackId)
				{
					OutPlayerId = DataPlayerId;
					return true;
				}
			}
			return false;
		});

		return !OutPlayerId.IsEmpty();
	}

} // namespace UE::PixelStreaming2
