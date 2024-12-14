// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2Peer.h"

#include "Async/Async.h"
#include "EpicRtcSessionObserver.h"
#include "Logging.h"
#include "PixelStreaming2Module.h"
#include "SampleBuffer.h"
#include "SoundGenerator.h"
#include "UtilsString.h"

uint32_t UPixelStreaming2Peer::PlayerId = 0;

UPixelStreaming2Peer::UPixelStreaming2Peer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SoundGenerator(MakeShared<UE::PixelStreaming2::FSoundGenerator, ESPMode::ThreadSafe>())
{
	PreferredBufferLength = 512u;
	NumChannels = 2;
	PrimaryComponentTick.bCanEverTick = true;
	SetComponentTickEnabled(true);
	bAutoActivate = true;

	PlayerName = FUtf8String::Printf("PixelStreaming2Player%d", PlayerId++);
}

void UPixelStreaming2Peer::BeginPlay()
{
	EpicRtcManager = MakeShared<UE::PixelStreaming2::FEpicRtcManager>();

	EpicRtcManager->OnSessionStateUpdate.AddUObject(this, &UPixelStreaming2Peer::OnSessionStateUpdate);
	EpicRtcManager->OnSessionErrorUpdate.AddUObject(this, &UPixelStreaming2Peer::OnSessionErrorUpdate);
	EpicRtcManager->OnSessionRoomsAvailableUpdate.AddUObject(this, &UPixelStreaming2Peer::OnSessionRoomsAvailableUpdate);

	EpicRtcManager->OnRoomStateUpdate.AddUObject(this, &UPixelStreaming2Peer::OnRoomStateUpdate);
	EpicRtcManager->OnRoomJoinedUpdate.AddUObject(this, &UPixelStreaming2Peer::OnRoomJoinedUpdate);
	EpicRtcManager->OnRoomLeftUpdate.AddUObject(this, &UPixelStreaming2Peer::OnRoomLeftUpdate);
	EpicRtcManager->OnAudioTrackUpdate.AddUObject(this, &UPixelStreaming2Peer::OnAudioTrackUpdate);
	EpicRtcManager->OnVideoTrackUpdate.AddUObject(this, &UPixelStreaming2Peer::OnVideoTrackUpdate);
	EpicRtcManager->OnDataTrackUpdate.AddUObject(this, &UPixelStreaming2Peer::OnDataTrackUpdate);
	EpicRtcManager->OnLocalSdpUpdate.AddUObject(this, &UPixelStreaming2Peer::OnLocalSdpUpdate);
	EpicRtcManager->OnRemoteSdpUpdate.AddUObject(this, &UPixelStreaming2Peer::OnRemoteSdpUpdate);
	EpicRtcManager->OnRoomErrorUpdate.AddUObject(this, &UPixelStreaming2Peer::OnRoomErrorUpdate);

	EpicRtcManager->OnAudioTrackMuted.AddUObject(this, &UPixelStreaming2Peer::OnAudioTrackMuted);
	EpicRtcManager->OnAudioTrackFrame.AddUObject(this, &UPixelStreaming2Peer::OnAudioTrackFrame);
	EpicRtcManager->OnAudioTrackRemoved.AddUObject(this, &UPixelStreaming2Peer::OnAudioTrackRemoved);
	EpicRtcManager->OnAudioTrackState.AddUObject(this, &UPixelStreaming2Peer::OnAudioTrackState);

	EpicRtcManager->OnVideoTrackMuted.AddUObject(this, &UPixelStreaming2Peer::OnVideoTrackMuted);
	EpicRtcManager->OnVideoTrackFrame.AddUObject(this, &UPixelStreaming2Peer::OnVideoTrackFrame);
	EpicRtcManager->OnVideoTrackRemoved.AddUObject(this, &UPixelStreaming2Peer::OnVideoTrackRemoved);
	EpicRtcManager->OnVideoTrackState.AddUObject(this, &UPixelStreaming2Peer::OnVideoTrackState);

	EpicRtcManager->OnDataTrackRemoved.AddUObject(this, &UPixelStreaming2Peer::OnDataTrackRemoved);
	EpicRtcManager->OnDataTrackState.AddUObject(this, &UPixelStreaming2Peer::OnDataTrackState);
	EpicRtcManager->OnDataTrackMessage.AddUObject(this, &UPixelStreaming2Peer::OnDataTrackMessage);

	EpicRtcManager->EpicRtcConference = UE::PixelStreaming2::FPixelStreaming2Module::GetModule()->GetEpicRtcConference();

	EpicRtcManager->SessionObserver = new UE::PixelStreaming2::FEpicRtcSessionObserver(EpicRtcManager);
	EpicRtcManager->RoomObserver = new UE::PixelStreaming2::FEpicRtcRoomObserver(EpicRtcManager);

	EpicRtcManager->AudioTrackObserverFactory = new UE::PixelStreaming2::FEpicRtcAudioTrackObserverFactory(EpicRtcManager);
	EpicRtcManager->VideoTrackObserverFactory = new UE::PixelStreaming2::FEpicRtcVideoTrackObserverFactory(EpicRtcManager);
	EpicRtcManager->DataTrackObserverFactory = new UE::PixelStreaming2::FEpicRtcDataTrackObserverFactory(EpicRtcManager);

	UE::PixelStreaming2::FPixelStreaming2Module::GetModule()->GetStatsCollector()->OnStatsReady.AddUObject(this, &UPixelStreaming2Peer::OnStatsReady);

	Super::BeginPlay();
}

void UPixelStreaming2Peer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Disconnect(FString(TEXT("UPixelStreaming2Peer::EndPlay called with reason: ")) + StaticEnum<EEndPlayReason::Type>()->GetNameStringByValue(EndPlayReason));

	Super::EndPlay(EndPlayReason);
}

bool UPixelStreaming2Peer::Connect(const FString& Url)
{
	if (!EpicRtcManager)
	{
		UE_LOGFMT(LogPixelStreaming2, Warning, "Failed to connect. EpicRtcManager isn't valid!");
		return false;
	}

	FUtf8String Utf8Url = *Url;
	FUtf8String ConnectionUrl = Utf8Url + (Utf8Url.Contains(TEXT("?")) ? TEXT("&") : TEXT("?")) + TEXT("isStreamer=false");

	EpicRtcSessionConfig SessionConfig = {
		._id = { ._ptr = reinterpret_cast<const char*>(*PlayerName), ._length = static_cast<uint64_t>(PlayerName.Len()) },
		._url = { ._ptr = reinterpret_cast<const char*>(*ConnectionUrl), ._length = static_cast<uint64_t>(ConnectionUrl.Len()) },
		._observer = EpicRtcManager->SessionObserver.GetReference()
	};

	EpicRtcErrorCode Result = EpicRtcManager->EpicRtcConference->CreateSession(SessionConfig, EpicRtcManager->EpicRtcSession.GetInitReference());
	if (Result != EpicRtcErrorCode::Ok)
	{
		UE_LOGFMT(LogPixelStreaming2, Error, "Failed to create EpicRtc session, CreateSession returned: {0}", UE::PixelStreaming2::ToString(Result));
		return false;
	}

	Result = EpicRtcManager->EpicRtcSession->Connect();
	if (Result != EpicRtcErrorCode::Ok)
	{
		UE_LOGFMT(LogPixelStreaming2, Error, "Failed to connect EpicRtcSession. Connect returned: {0}", UE::PixelStreaming2::ToString(Result));
		return false;
	}
	return true;
}

bool UPixelStreaming2Peer::Disconnect()
{
	return Disconnect(TEXT("Disconnect called from Blueprint"));
}

bool UPixelStreaming2Peer::Disconnect(const FString& OptionalReason)
{
	if (!EpicRtcManager)
	{
		UE_LOGFMT(LogPixelStreaming2, Warning, "Failed to disconnect. EpicRtcManager isn't valid!");
		return false;
	}

	if (!EpicRtcManager->EpicRtcSession)
	{
		UE_LOGFMT(LogPixelStreaming2, Error, "Failed to disconnect, EpicRtcSession does not exist");
		return false;
	}

	if (EpicRtcManager->EpicRtcRoom)
	{
		EpicRtcManager->EpicRtcRoom->Leave();
		EpicRtcManager->EpicRtcSession->RemoveRoom({ ._ptr = reinterpret_cast<const char*>(*SubscribedStream), ._length = static_cast<uint64_t>(SubscribedStream.Len()) });
	}

	FUtf8String Reason;
	if (OptionalReason.Len())
	{
		Reason = *OptionalReason;
	}
	else
	{
		Reason = "PixelStreaming2Peer Disconnected";
	}

	EpicRtcErrorCode Result = EpicRtcManager->EpicRtcSession->Disconnect(UE::PixelStreaming2::ToEpicRtcStringView(Reason));
	if (Result != EpicRtcErrorCode::Ok)
	{
		UE_LOGFMT(LogPixelStreaming2, Error, "Failed to disconnect EpicRtcSession. Disconnect returned {0}", UE::PixelStreaming2::ToString(Result));
		return false;
	}

	if (AudioSink)
	{
		AudioSink->RemoveAudioConsumer(this);
	}

	if (VideoSink && VideoConsumer)
	{
		VideoSink->RemoveVideoConsumer(VideoConsumer);
	}

	EpicRtcManager->EpicRtcConference->RemoveSession({ ._ptr = reinterpret_cast<const char*>(*PlayerName), ._length = static_cast<uint64_t>(PlayerName.Len()) });

	return true;
}

bool UPixelStreaming2Peer::Subscribe(const FString& StreamerId)
{
	if (!EpicRtcManager)
	{
		UE_LOGFMT(LogPixelStreaming2, Warning, "Failed to subscribe. EpicRtcManager isn't valid!");
		return false;
	}

	if (SessionState != EpicRtcSessionState::Connected)
	{
		UE_LOGFMT(LogPixelStreaming2, Error, "Failed to create subscribe to streamer. EpicRtc session isn't connected");
		return false;
	}

	EpicRtcConnectionConfig ConnectionConfig = {
		// TODO (Migration): RTCP-7032 This info usually comes from the OnConfig signalling message
		._iceServers = EpicRtcIceServerSpan{ ._ptr = nullptr, ._size = 0 },
		._iceConnectionPolicy = EpicRtcIcePolicy::All,
		._disableTcpCandidates = false
	};

	SubscribedStream = *StreamerId;

	EpicRtcRoomConfig RoomConfig = {
		._id = EpicRtcStringView{ ._ptr = reinterpret_cast<const char*>(*SubscribedStream), ._length = static_cast<uint64_t>(SubscribedStream.Len()) },
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
		UE_LOGFMT(LogPixelStreaming2, Error, "Failed to create EpicRtc room. CreateRoom returned: {0}", UE::PixelStreaming2::ToString(Result));
		return false;
	}

	EpicRtcManager->EpicRtcRoom->Join();

	// Create a stats collector so we can receive stats from the subscribed streamer
	StatsCollector = UE::PixelStreaming2::FRTCStatsCollector::Create(StreamerId);

	return true;
}

ISoundGeneratorPtr UPixelStreaming2Peer::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	SoundGenerator->SetParameters(InParams);
	Initialize(InParams.SampleRate);

	return SoundGenerator;
}

void UPixelStreaming2Peer::OnBeginGenerate()
{
	SoundGenerator->bGeneratingAudio = true;
}

void UPixelStreaming2Peer::OnEndGenerate()
{
	SoundGenerator->bGeneratingAudio = false;
}

void UPixelStreaming2Peer::ConsumeRawPCM(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames)
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

	Audio::TSampleBuffer<int16_t> Buffer(AudioData, NFrames, NChannels, InSampleRate);
	if (NChannels != SoundGenerator->GetNumChannels())
	{
		Buffer.MixBufferToChannels(SoundGenerator->GetNumChannels());
	}

	SoundGenerator->AddAudio(Buffer.GetData(), InSampleRate, NChannels, Buffer.GetNumSamples());
}

void UPixelStreaming2Peer::OnConsumerAdded()
{
	SoundGenerator->bShouldGenerateAudio = true;
}

void UPixelStreaming2Peer::OnConsumerRemoved()
{
	SoundGenerator->bShouldGenerateAudio = false;
	SoundGenerator->EmptyBuffers();
}

void UPixelStreaming2Peer::OnSessionStateUpdate(const EpicRtcSessionState StateUpdate)
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
			UE_LOGFMT(LogPixelStreaming2, Warning, "OnSessionStateUpdate received unknown EpicRtcSessionState: {0}", static_cast<int>(StateUpdate));
			break;
	}
}

void UPixelStreaming2Peer::OnSessionRoomsAvailableUpdate(EpicRtcStringArrayInterface* RoomsList)
{
	TArray<FString> Streamers;

	for (uint64_t i = 0; i < RoomsList->Size(); i++)
	{
		EpicRtcStringInterface* RoomName = RoomsList->Get()[i];
		Streamers.Add(FString{ RoomName->Get(), (int32)RoomName->Length() });
	}

	OnStreamerList.Broadcast(Streamers);
}

void UPixelStreaming2Peer::OnSessionErrorUpdate(const EpicRtcErrorCode ErrorUpdate)
{
}

void UPixelStreaming2Peer::OnRoomStateUpdate(const EpicRtcRoomState State)
{
}

void UPixelStreaming2Peer::OnRoomJoinedUpdate(EpicRtcParticipantInterface* Participant)
{
	FString ParticipantId{ (int32)Participant->GetId()._length, Participant->GetId()._ptr };
	UE_LOG(LogPixelStreaming2, Log, TEXT("Player (%s) joined"), *ParticipantId);
}

void UPixelStreaming2Peer::OnRoomLeftUpdate(const EpicRtcStringView ParticipantId)
{
}

void UPixelStreaming2Peer::OnAudioTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcAudioTrackInterface* AudioTrack)
{
	FString ParticipantId{ (int32)Participant->GetId()._length, Participant->GetId()._ptr };
	FString VideoTrackId{ (int32)AudioTrack->GetId()._length, AudioTrack->GetId()._ptr };
	UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("UPixelStreaming2Peer::OnAudioTrackUpdate(Participant [%s], VideoTrack [%s])"), *ParticipantId, *VideoTrackId);

	if (AudioTrack->IsRemote())
	{
		// We received a remote track. We should now generate audio from it
		AudioSink = MakeShared<UE::PixelStreaming2::FAudioSink>();
		AudioSink->AddAudioConsumer(this);
	}
}

void UPixelStreaming2Peer::OnVideoTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcVideoTrackInterface* VideoTrack)
{
	FString ParticipantId{ (int32)Participant->GetId()._length, Participant->GetId()._ptr };
	FString VideoTrackId{ (int32)VideoTrack->GetId()._length, VideoTrack->GetId()._ptr };
	UE_LOG(LogPixelStreaming2, VeryVerbose, TEXT("UPixelStreaming2Peer::OnVideoTrackUpdate(Participant [%s], VideoTrack [%s])"), *ParticipantId, *VideoTrackId);

	if (VideoTrack->IsRemote())
	{
		// We received a remote track. We should now generate video from it
		VideoSink = MakeShared<UE::PixelStreaming2::FVideoSink>();
		if (VideoConsumer)
		{
			VideoSink->AddVideoConsumer(VideoConsumer);
		}
	}
}

void UPixelStreaming2Peer::OnDataTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcDataTrackInterface* DataTrack)
{
}

void UPixelStreaming2Peer::OnLocalSdpUpdate(EpicRtcParticipantInterface* Participant, EpicRtcSdpInterface* Sdp)
{
}

void UPixelStreaming2Peer::OnRemoteSdpUpdate(EpicRtcParticipantInterface* Participant, EpicRtcSdpInterface* Sdp)
{
}

void UPixelStreaming2Peer::OnRoomErrorUpdate(const EpicRtcErrorCode Error)
{
}

void UPixelStreaming2Peer::OnAudioTrackMuted(EpicRtcAudioTrackInterface* AudioTrack, EpicRtcBool bIsMuted)
{
	if (!AudioSink)
	{
		return;
	}

	AudioSink->SetMuted((bool)bIsMuted);
}

void UPixelStreaming2Peer::OnAudioTrackFrame(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcAudioFrame& Frame)
{
	if (!AudioSink)
	{
		return;
	}

	AudioSink->OnAudioData(Frame._data, Frame._length, Frame._format._numChannels, Frame._format._sampleRate);
}

void UPixelStreaming2Peer::OnAudioTrackRemoved(EpicRtcAudioTrackInterface* AudioTrack)
{
}

void UPixelStreaming2Peer::OnAudioTrackState(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcTrackState State)
{
}

void UPixelStreaming2Peer::OnVideoTrackMuted(EpicRtcVideoTrackInterface* VideoTrack, EpicRtcBool bIsMuted)
{
	if (!VideoSink)
	{
		return;
	}

	VideoSink->SetMuted((bool)bIsMuted);
}

void UPixelStreaming2Peer::OnVideoTrackFrame(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcVideoFrame& Frame)
{
	if (!VideoSink)
	{
		return;
	}

	VideoSink->OnVideoData(Frame);
}

void UPixelStreaming2Peer::OnVideoTrackRemoved(EpicRtcVideoTrackInterface* VideoTrack)
{
}

void UPixelStreaming2Peer::OnVideoTrackState(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcTrackState State)
{
}

void UPixelStreaming2Peer::OnDataTrackRemoved(EpicRtcDataTrackInterface* DataTrack)
{
}

void UPixelStreaming2Peer::OnDataTrackState(EpicRtcDataTrackInterface* DataTrack, const EpicRtcTrackState State)
{
}

void UPixelStreaming2Peer::OnDataTrackMessage(EpicRtcDataTrackInterface* DataTrack)
{
}

void UPixelStreaming2Peer::OnStatsReady(const FString& PeerId, const EpicRtcConnectionStats& ConnectionStats)
{
	if (!StatsCollector)
	{
		return;
	}

	FString StreamId = *SubscribedStream;
	if (PeerId != StreamId)
	{
		return;
	}

	StatsCollector->Process(ConnectionStats);
}