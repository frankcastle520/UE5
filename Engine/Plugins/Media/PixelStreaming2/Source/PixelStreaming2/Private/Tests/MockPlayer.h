// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcDataTrack.h"
#include "HAL/ThreadSafeBool.h"
#include "EpicRtcManager.h"
#include "EpicRtcConferenceUtils.h"
#include "IPixelStreaming2DataProtocol.h"
#include "Containers/Utf8String.h"

#if WITH_DEV_AUTOMATION_TESTS

class EpicRtcPlatformInterface;

namespace UE::PixelStreaming2
{
	class FEpicRtcDataTrack;

	struct FMockVideoFrameConfig
	{
		int	  Height;
		int	  Width;
		uint8 Y;
		uint8 U;
		uint8 V;
	};

	class FMockVideoSink
	{
	public:
		void									  OnFrame(const EpicRtcVideoFrame& Frame);
		bool									  HasReceivedFrame() const { return bReceivedFrame; }
		void									  ResetReceivedFrame();
		TRefCountPtr<EpicRtcVideoBufferInterface> GetReceivedBuffer() { return VideoBuffer; };

	private:
		TRefCountPtr<EpicRtcVideoBufferInterface> VideoBuffer;
		FThreadSafeBool							  bReceivedFrame = false;
	};

	class FMockPlayer
	{
	public:
		FMockPlayer();
		virtual ~FMockPlayer();

		void Connect(int StreamerPort);
		void Disconnect(const FString& Reason);
		bool Subscribe(const FString& StreamerId);

		void OnVideoTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcVideoTrackInterface* VideoTrack);
		void OnVideoTrackFrame(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcVideoFrame& Frame);
		void OnVideoTrackMuted(EpicRtcVideoTrackInterface* VideoTrack, EpicRtcBool bIsMuted);
		void OnVideoTrackRemoved(EpicRtcVideoTrackInterface* VideoTrack);
		void OnVideoTrackState(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcTrackState State);
		void OnSessionRoomsAvailableUpdate(EpicRtcStringArrayInterface* RoomsList);
		void OnSessionErrorUpdate(const EpicRtcErrorCode ErrorUpdate);
		void OnRoomStateUpdate(const EpicRtcRoomState State);
		void OnRoomJoinedUpdate(EpicRtcParticipantInterface* Participant);
		void OnRoomLeftUpdate(const EpicRtcStringView ParticipantId);
		void OnRoomErrorUpdate(const EpicRtcErrorCode Error);
		void OnSessionStateUpdate(const EpicRtcSessionState StateUpdate);
		void OnDataTrackMessage(EpicRtcDataTrackInterface*);
		void OnDataTrackRemoved(EpicRtcDataTrackInterface*);
		void OnDataTrackState(EpicRtcDataTrackInterface*, const EpicRtcTrackState);
		void OnDataTrackUpdate(EpicRtcParticipantInterface*, EpicRtcDataTrackInterface*);

		template <typename... Args>
		bool SendMessage(FString MessageType, Args... VarArgs)
		{
			if (!DataTrack)
			{
				return false;
			}

			return DataTrack->SendMessage(MessageType, VarArgs...);
		}

		bool DataChannelAvailable() { return DataTrack.IsValid(); };

		TSharedPtr<FMockVideoSink>					   GetVideoSink() { return VideoSink; };
		TSharedPtr<IPixelStreaming2DataProtocol> GetToStreamerProtocol() { return ToStreamerProtocol; }

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnMessageReceived, const TArray<uint8>&);
		FOnMessageReceived OnMessageReceived;

	private:
		TSharedPtr<FEpicRtcManager>					   EpicRtcManager;
		TSharedPtr<FMockVideoSink>					   VideoSink;
		TSharedPtr<FEpicRtcDataTrack>				   DataTrack;
		TRefCountPtr<EpicRtcPlatformInterface>		   Platform;
		TUniqueTaskPtr<FEpicRtcTickConferenceTask>	   TickConferenceTask;
		TSharedPtr<IPixelStreaming2DataProtocol> ToStreamerProtocol;

		TArray<EpicRtcVideoEncoderInitializerInterface*> EpicRtcVideoEncoderInitializers;
		TArray<EpicRtcVideoDecoderInitializerInterface*> EpicRtcVideoDecoderInitializers;

		EpicRtcSessionState SessionState = EpicRtcSessionState::Disconnected;

		FUtf8String		SubscribedStream;
		FUtf8String		PlayerName;
		static uint32_t PlayerId;
	};
} // namespace UE::PixelStreaming2

#endif // WITH_DEV_AUTOMATION_TESTS
