// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "EpicRtcAudioTrackObserverFactory.h"
#include "EpicRtcDataTrackObserverFactory.h"
#include "EpicRtcRoomObserver.h"
#include "EpicRtcSessionObserver.h"
#include "EpicRtcVideoTrackObserverFactory.h"

#include "epic_rtc/core/conference.h"
#include "epic_rtc/core/room.h"
#include "epic_rtc/core/session.h"
#include "epic_rtc/plugins/signalling/websocket.h"

namespace UE::PixelStreaming2
{
	/**
	 * A base class for managing epic rtc sessions, rooms and participants
	 *
	 * Extending this class allows observers to callback to the manager
	 */
	class PIXELSTREAMING2_API FEpicRtcManager
	{
	public:
		virtual ~FEpicRtcManager() = default;

	public:
		// Begin FEpicRtcSessionObserver Callbacks
		DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnSessionStateUpdate, const EpicRtcSessionState);
		FOnSessionStateUpdate OnSessionStateUpdate;

		DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnSessionErrorUpdate, const EpicRtcErrorCode);
		FOnSessionErrorUpdate OnSessionErrorUpdate;

		DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnSessionRoomsAvailableUpdate, EpicRtcStringArrayInterface*);
		FOnSessionRoomsAvailableUpdate OnSessionRoomsAvailableUpdate;
		// End FEpicRtcSessionObserver Callbacks

		// Begin FEpicRtcRoomObserver Callbacks
		DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnRoomStateUpdate, const EpicRtcRoomState);
		FOnRoomStateUpdate OnRoomStateUpdate;

		DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnRoomJoinedUpdate, EpicRtcParticipantInterface*);
		FOnRoomJoinedUpdate OnRoomJoinedUpdate;

		DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnRoomLeftUpdate, const EpicRtcStringView);
		FOnRoomLeftUpdate OnRoomLeftUpdate;

		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnAudioTrackUpdate, EpicRtcParticipantInterface*, EpicRtcAudioTrackInterface*);
		FOnAudioTrackUpdate OnAudioTrackUpdate;

		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnVideoTrackUpdate, EpicRtcParticipantInterface*, EpicRtcVideoTrackInterface*);
		FOnVideoTrackUpdate OnVideoTrackUpdate;

		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnDataTrackUpdate, EpicRtcParticipantInterface*, EpicRtcDataTrackInterface*);
		FOnDataTrackUpdate OnDataTrackUpdate;

		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnLocalSdpUpdate, EpicRtcParticipantInterface*, EpicRtcSdpInterface*);
		FOnLocalSdpUpdate OnLocalSdpUpdate;

		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnRemoteSdpUpdate, EpicRtcParticipantInterface*, EpicRtcSdpInterface*);
		FOnRemoteSdpUpdate OnRemoteSdpUpdate;

		DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnRoomErrorUpdate, const EpicRtcErrorCode);
		FOnRoomErrorUpdate OnRoomErrorUpdate;
		// End FEpicRtcRoomObserver Callbacks

		// Begin FEpicRtcAudioTrackObserver Callbacks
		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnAudioTrackMuted, EpicRtcAudioTrackInterface*, EpicRtcBool);
		FOnAudioTrackMuted OnAudioTrackMuted;

		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnAudioTrackFrame, EpicRtcAudioTrackInterface*, const EpicRtcAudioFrame&);
		FOnAudioTrackFrame OnAudioTrackFrame;

		DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnAudioTrackRemoved, EpicRtcAudioTrackInterface*);
		FOnAudioTrackRemoved OnAudioTrackRemoved;

		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnAudioTrackState, EpicRtcAudioTrackInterface*, const EpicRtcTrackState);
		FOnAudioTrackState OnAudioTrackState;
		// End FEpicRtcAudioTrackObserver Callbacks

		// Begin FEpicRtcVideoTrackObserver Callbacks
		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnVideoTrackMuted, EpicRtcVideoTrackInterface*, EpicRtcBool);
		FOnVideoTrackMuted OnVideoTrackMuted;

		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnVideoTrackFrame, EpicRtcVideoTrackInterface*, const EpicRtcVideoFrame&);
		FOnVideoTrackFrame OnVideoTrackFrame;

		DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnVideoTrackRemoved, EpicRtcVideoTrackInterface*);
		FOnVideoTrackRemoved OnVideoTrackRemoved;

		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnVideoTrackState, EpicRtcVideoTrackInterface*, const EpicRtcTrackState);
		FOnVideoTrackState OnVideoTrackState;
		// End FEpicRtcVideoTrackObserver Callbacks

		// Begin FEpicRtcDataTrackObserver Callbacks
		DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnDataTrackRemoved, EpicRtcDataTrackInterface*);
		FOnDataTrackRemoved OnDataTrackRemoved;

		DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnDataTrackState, EpicRtcDataTrackInterface*, const EpicRtcTrackState);
		FOnDataTrackState OnDataTrackState;

		DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnDataTrackMessage, EpicRtcDataTrackInterface*);
		FOnDataTrackMessage OnDataTrackMessage;
		// End FEpicRtcDataTrackObserver Callbacks

	public:
		// Begin EpicRtc Classes
		TRefCountPtr<EpicRtcConferenceInterface> EpicRtcConference;
		TRefCountPtr<EpicRtcSessionInterface>	 EpicRtcSession;
		TRefCountPtr<EpicRtcRoomInterface>		 EpicRtcRoom;
		// End EpicRtc Classes

		// Begin EpicRtc Observers
		TRefCountPtr<FEpicRtcSessionObserver>			SessionObserver;
		TRefCountPtr<FEpicRtcRoomObserver>				RoomObserver;
		TRefCountPtr<FEpicRtcAudioTrackObserverFactory> AudioTrackObserverFactory;
		TRefCountPtr<FEpicRtcVideoTrackObserverFactory> VideoTrackObserverFactory;
		TRefCountPtr<FEpicRtcDataTrackObserverFactory>	DataTrackObserverFactory;
		// End EpicRtc Observers
	};
} // namespace UE::PixelStreaming2