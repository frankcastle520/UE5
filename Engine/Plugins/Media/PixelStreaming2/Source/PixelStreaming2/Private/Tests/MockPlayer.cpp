// Copyright Epic Games, Inc. All Rights Reserved.

#include "MockPlayer.h"

#include "DefaultDataProtocol.h"
#include "EpicRtcVideoEncoderInitializer.h"
#include "EpicRtcVideoDecoderInitializer.h"
#include "EpicRtcWebsocketFactory.h"
#include "Logging.h"
#include "UtilsString.h"

#include "epic_rtc/core/platform.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::PixelStreaming2
{
	uint32_t FMockPlayer::PlayerId = 0;

	void FMockVideoSink::OnFrame(const EpicRtcVideoFrame& Frame)
	{
		if (!bReceivedFrame)
		{
			VideoBuffer = Frame._buffer;
			bReceivedFrame = true;
		}
	}

	void FMockVideoSink::ResetReceivedFrame()
	{
		bReceivedFrame = false;
		VideoBuffer.SafeRelease();
	}

	FMockPlayer::FMockPlayer()
	{
		PlayerName = FUtf8String::Printf("MockPlayer%d", PlayerId++);
		FUtf8String ConferenceId = "test_conference";

		EpicRtcVideoEncoderInitializers = { new FEpicRtcVideoEncoderInitializer() };
		EpicRtcVideoDecoderInitializers = { new FEpicRtcVideoDecoderInitializer() };

		EpicRtcManager = MakeShared<FEpicRtcManager>();

		ToStreamerProtocol = UE::PixelStreaming2Input::GetDefaultToStreamerProtocol();

		EpicRtcManager->OnSessionRoomsAvailableUpdate.AddRaw(this, &FMockPlayer::OnSessionRoomsAvailableUpdate);
		EpicRtcManager->OnSessionErrorUpdate.AddRaw(this, &FMockPlayer::OnSessionErrorUpdate);
		EpicRtcManager->OnRoomStateUpdate.AddRaw(this, &FMockPlayer::OnRoomStateUpdate);
		EpicRtcManager->OnRoomJoinedUpdate.AddRaw(this, &FMockPlayer::OnRoomJoinedUpdate);
		EpicRtcManager->OnRoomLeftUpdate.AddRaw(this, &FMockPlayer::OnRoomLeftUpdate);
		EpicRtcManager->OnRoomErrorUpdate.AddRaw(this, &FMockPlayer::OnRoomErrorUpdate);
		EpicRtcManager->OnSessionStateUpdate.AddRaw(this, &FMockPlayer::OnSessionStateUpdate);
		EpicRtcManager->OnVideoTrackUpdate.AddRaw(this, &FMockPlayer::OnVideoTrackUpdate);
		EpicRtcManager->OnVideoTrackFrame.AddRaw(this, &FMockPlayer::OnVideoTrackFrame);
		EpicRtcManager->OnVideoTrackMuted.AddRaw(this, &FMockPlayer::OnVideoTrackMuted);
		EpicRtcManager->OnVideoTrackRemoved.AddRaw(this, &FMockPlayer::OnVideoTrackRemoved);
		EpicRtcManager->OnVideoTrackState.AddRaw(this, &FMockPlayer::OnVideoTrackState);

		EpicRtcManager->OnDataTrackMessage.AddRaw(this, &FMockPlayer::OnDataTrackMessage);
		EpicRtcManager->OnDataTrackRemoved.AddRaw(this, &FMockPlayer::OnDataTrackRemoved);
		EpicRtcManager->OnDataTrackState.AddRaw(this, &FMockPlayer::OnDataTrackState);
		EpicRtcManager->OnDataTrackUpdate.AddRaw(this, &FMockPlayer::OnDataTrackUpdate);

		EpicRtcErrorCode Result = GetOrCreatePlatform({}, Platform.GetInitReference());

		TRefCountPtr<FEpicRtcWebsocketFactory> WebsocketFactory = MakeRefCount<FEpicRtcWebsocketFactory>(false);

		Result = Platform->CreateConference(ToEpicRtcStringView(ConferenceId),
			{ ._websocketFactory = WebsocketFactory.GetReference(),
				._signallingType = EpicRtcSignallingType::PixelStreaming,
				._signingPlugin = nullptr,
				._migrationPlugin = nullptr,
				._audioDevicePlugin = nullptr,
				._audioConfig = {
					._tickAdm = true,
					._audioEncoderInitializers = {},
					._audioDecoderInitializers = {},
					._enableBuiltInAudioCodecs = true,
				},
				._videoConfig = { ._videoEncoderInitializers = { ._ptr = const_cast<const EpicRtcVideoEncoderInitializerInterface**>(EpicRtcVideoEncoderInitializers.GetData()), ._size = (uint64_t)EpicRtcVideoEncoderInitializers.Num() }, ._videoDecoderInitializers = { ._ptr = const_cast<const EpicRtcVideoDecoderInitializerInterface**>(EpicRtcVideoDecoderInitializers.GetData()), ._size = (uint64_t)EpicRtcVideoDecoderInitializers.Num() }, ._enableBuiltInVideoCodecs = false },
				._fieldTrials = { ._fieldTrials = EpicRtcStringView{ ._ptr = nullptr, ._length = 0 }, ._isGlobal = 0 } },
			EpicRtcManager->EpicRtcConference.GetInitReference());

		TickConferenceTask = FEpicRtcTickableTask::Create<FEpicRtcTickConferenceTask>(EpicRtcManager->EpicRtcConference, TEXT("FMockPlayer TickConferenceTask"));

		EpicRtcManager->SessionObserver = new FEpicRtcSessionObserver(EpicRtcManager);
		EpicRtcManager->RoomObserver = new FEpicRtcRoomObserver(EpicRtcManager);

		EpicRtcManager->AudioTrackObserverFactory = new FEpicRtcAudioTrackObserverFactory(EpicRtcManager);
		EpicRtcManager->VideoTrackObserverFactory = new FEpicRtcVideoTrackObserverFactory(EpicRtcManager);
		EpicRtcManager->DataTrackObserverFactory = new FEpicRtcDataTrackObserverFactory(EpicRtcManager);

		VideoSink = MakeShared<FMockVideoSink>();
	}

	FMockPlayer::~FMockPlayer()
	{
		Disconnect(TEXT("Mock player being destroyed"));

		if (EpicRtcManager->EpicRtcConference)
		{
			EpicRtcManager->EpicRtcConference->RemoveSession(ToEpicRtcStringView(PlayerName));

			Platform->ReleaseConference(EpicRtcManager->EpicRtcConference->GetId());
		}
	}

	void FMockPlayer::Connect(int StreamerPort)
	{
		FUtf8String Url(FString::Printf(TEXT("ws://127.0.0.1:%d/"), StreamerPort));
		FUtf8String ConnectionUrl = Url + +(Url.Contains(TEXT("?")) ? TEXT("&") : TEXT("?")) + TEXT("isStreamer=false");

		EpicRtcSessionConfig SessionConfig = {
			._id = ToEpicRtcStringView(PlayerName),
			._url = ToEpicRtcStringView(ConnectionUrl),
			._observer = EpicRtcManager->SessionObserver.GetReference()
		};

		EpicRtcErrorCode Result = EpicRtcManager->EpicRtcConference->CreateSession(SessionConfig, EpicRtcManager->EpicRtcSession.GetInitReference());
		if (Result != EpicRtcErrorCode::Ok)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("FMockPlayer Failed to create EpicRtc session"));
			return;
		}

		Result = EpicRtcManager->EpicRtcSession->Connect();
		if (Result != EpicRtcErrorCode::Ok)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("FMockPlayer Failed to connect EpicRtcSession"));
		}
		else
		{
			UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("FMockPlayer Connected to EpicRtcSession"));
		}
	}

	bool FMockPlayer::Subscribe(const FString& StreamerId)
	{
		if (SessionState != EpicRtcSessionState::Connected)
		{
			// Sessions state can take several ticks to returning false tells latent test to run again next tick.
			return false;
		}

		EpicRtcConnectionConfig ConnectionConfig = {
			._iceServers = EpicRtcIceServerSpan{ ._ptr = nullptr, ._size = 0 },
			._iceConnectionPolicy = EpicRtcIcePolicy::All,
			._disableTcpCandidates = false
		};

		SubscribedStream = *StreamerId;

		EpicRtcRoomConfig RoomConfig = {
			._id = ToEpicRtcStringView(SubscribedStream),
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
			UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to create EpicRtc room"));
			return false;
		}

		EpicRtcManager->EpicRtcRoom->Join();

		return true;
	}

	void FMockPlayer::OnVideoTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcVideoTrackInterface* VideoTrack)
	{
		FString ParticipantId{ (int32)Participant->GetId()._length, Participant->GetId()._ptr };
		FString VideoTrackId{ (int32)VideoTrack->GetId()._length, VideoTrack->GetId()._ptr };
		UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("UPixelStreaming2Peer::OnVideoTrackUpdate(Participant [%s], VideoTrack [%s])"), *ParticipantId, *VideoTrackId);
	}

	void FMockPlayer::OnVideoTrackFrame(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcVideoFrame& Frame)
	{
		UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("FMockPlayer::OnVideoTrackFrame received a video frame."));

		VideoSink->OnFrame(Frame);
	}

	void FMockPlayer::OnVideoTrackMuted(EpicRtcVideoTrackInterface* VideoTrack, EpicRtcBool bIsMuted)
	{
	}

	void FMockPlayer::OnVideoTrackRemoved(EpicRtcVideoTrackInterface* VideoTrack)
	{
	}

	void FMockPlayer::OnVideoTrackState(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcTrackState State)
	{
	}

	void FMockPlayer::OnSessionRoomsAvailableUpdate(EpicRtcStringArrayInterface* RoomsList)
	{
	}

	void FMockPlayer::OnSessionErrorUpdate(const EpicRtcErrorCode ErrorUpdate)
	{
		UE_LOG(LogPixelStreaming2, Log, TEXT("OnSessionErrorUpdate: "));
	}

	void FMockPlayer::OnRoomStateUpdate(const EpicRtcRoomState State)
	{
		UE_LOG(LogPixelStreaming2, Log, TEXT("OnRoomStateUpdate: "));
	}

	void FMockPlayer::OnRoomJoinedUpdate(EpicRtcParticipantInterface* Participant)
	{
		FString ParticipantId{ (int32)Participant->GetId()._length, Participant->GetId()._ptr };
		UE_LOG(LogPixelStreaming2, Log, TEXT("OnRoomJoinedUpdate: Player (%s) joined"), *ParticipantId);
	}

	void FMockPlayer::OnRoomLeftUpdate(const EpicRtcStringView ParticipantId)
	{
		UE_LOG(LogPixelStreaming2, Log, TEXT("OnRoomLeftUpdate"));
	}

	void FMockPlayer::OnRoomErrorUpdate(const EpicRtcErrorCode Error)
	{
		UE_LOG(LogPixelStreaming2, Log, TEXT("OnRoomErrorUpdate"));
	}

	void FMockPlayer::OnSessionStateUpdate(const EpicRtcSessionState StateUpdate)
	{
		switch (StateUpdate)
		{
			case EpicRtcSessionState::New:
			case EpicRtcSessionState::Pending:
			case EpicRtcSessionState::Connected:
			case EpicRtcSessionState::Disconnected:
			case EpicRtcSessionState::Failed:
			case EpicRtcSessionState::Exiting:
				SessionState = StateUpdate;
				break;
			default:
				break;
		}
	}

	void FMockPlayer::OnDataTrackMessage(EpicRtcDataTrackInterface* InDataTrack)
	{
		TRefCountPtr<EpicRtcDataFrameInterface> DataFrame;
		if (!InDataTrack->PopFrame(DataFrame.GetInitReference()))
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("FMockPlayer::OnDataTrackMessage Failed to PopFrame"));
			return;
		}

		const TArray<uint8> Data(DataFrame->Data(), DataFrame->Size());

		OnMessageReceived.Broadcast(Data);
	}

	void FMockPlayer::OnDataTrackRemoved(EpicRtcDataTrackInterface*) {}

	void FMockPlayer::OnDataTrackState(EpicRtcDataTrackInterface*, const EpicRtcTrackState) {}

	void FMockPlayer::OnDataTrackUpdate(EpicRtcParticipantInterface*, EpicRtcDataTrackInterface* InDataTrack)
	{
		DataTrack = FEpicRtcDataTrack::Create(InDataTrack, ToStreamerProtocol);
	}

	void FMockPlayer::Disconnect(const FString& Reason)
	{
		if (!EpicRtcManager->EpicRtcSession)
		{
			return;
		}

		if (EpicRtcManager->EpicRtcRoom)
		{
			EpicRtcManager->EpicRtcRoom->Leave();
			EpicRtcManager->EpicRtcSession->RemoveRoom(ToEpicRtcStringView(SubscribedStream));
		}

		EpicRtcErrorCode Result = EpicRtcManager->EpicRtcSession->Disconnect(ToEpicRtcStringView(*Reason));
		if (Result != EpicRtcErrorCode::Ok)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to disconnect EpicRtcSession"));
		}
	}
} // namespace UE::PixelStreaming2

#endif // WITH_DEV_AUTOMATION_TESTS
