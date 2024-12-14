// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcManager.h"
#include "EpicRtcWebsocketFactory.h"
#include "IWebSocket.h"
#include "Logging.h"
#include "Misc/AutomationTest.h"
#include "UtilsString.h"

#include "epic_rtc/common/common.h"
#include "epic_rtc/core/conference.h"
#include "epic_rtc/core/platform.h"

namespace UE::PixelStreaming2
{
	// A mock manager class for tests to receive callbacks from EpicRtc. Typically, the controlling class will inherit from FEpicRtcManager
	// and implement the methods itself (see streamer.cpp). However, we can't force the tests to inherit the class, so instead we have the
	// mock manager and the test bodies bind to the events they're interested in
	class FMockManager : public FEpicRtcManager
	{
	public:
		// Begin FEpicRtcAudioTrackObserver Callbacks
		void OnAudioTrackMuted(EpicRtcAudioTrackInterface* AudioTrack, EpicRtcBool bIsMuted)
		{
		}
		void OnAudioTrackFrame(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcAudioFrame& Frame)
		{
		}
		void OnAudioTrackRemoved(EpicRtcAudioTrackInterface* AudioTrack)
		{
		}
		void OnAudioTrackState(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcTrackState State)
		{
		}
		// End FEpicRtcAudioTrackObserver Callbacks

		// Begin FEpicRtcVideoTrackObserver Callbacks
		void OnVideoTrackMuted(EpicRtcVideoTrackInterface* VideoTrack, EpicRtcBool bIsMuted)
		{
		}
		void OnVideoTrackFrame(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcVideoFrame& Frame)
		{
		}
		void OnVideoTrackRemoved(EpicRtcVideoTrackInterface* VideoTrack)
		{
		}
		void OnVideoTrackState(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcTrackState State)
		{
		}
		// End FEpicRtcVideoTrackObserver Callbacks

		// Begin FEpicRtcDataTrackObserver Callbacks
		void OnDataTrackRemoved(EpicRtcDataTrackInterface* DataTrack)
		{
		}
		void OnDataTrackState(EpicRtcDataTrackInterface* DataTrack, const EpicRtcTrackState State)
		{
		}
		void OnDataTrackMessage(EpicRtcDataTrackInterface* DataTrack)
		{
		}
		// End FEpicRtcDataTrackObserver Callbacks

	public:
		TRefCountPtr<EpicRtcConferenceInterface>& GetEpicRtcConference()
		{
			return EpicRtcConference;
		}

		TRefCountPtr<EpicRtcSessionInterface>& GetEpicRtcSession()
		{
			return EpicRtcSession;
		}

		TRefCountPtr<EpicRtcRoomInterface>& GetEpicRtcRoom()
		{
			return EpicRtcRoom;
		}

		TRefCountPtr<FEpicRtcSessionObserver>& GetSessionObserver()
		{
			return SessionObserver;
		}

		TRefCountPtr<FEpicRtcRoomObserver>& GetRoomObserver()
		{
			return RoomObserver;
		}

		TRefCountPtr<FEpicRtcAudioTrackObserverFactory>& GetAudioTrackObserverFactory()
		{
			return AudioTrackObserverFactory;
		}

		TRefCountPtr<FEpicRtcVideoTrackObserverFactory>& GetVideoTrackObserverFactory()
		{
			return VideoTrackObserverFactory;
		}

		TRefCountPtr<FEpicRtcDataTrackObserverFactory>& GetDataTrackObserverFactory()
		{
			return DataTrackObserverFactory;
		}
	};

	// For faking a web socket connection
	class FMockWebSocket : public ::IWebSocket
	{
	public:
		FMockWebSocket() = default;
		virtual ~FMockWebSocket() = default;
		virtual void Connect() override
		{
			bConnected = true;
			OnConnectedEvent.Broadcast();
		}
		virtual void							Close(int32 Code = 1000, const FString& Reason = FString()) override { bConnected = false; }
		virtual bool							IsConnected() override { return bConnected; }
		virtual void							Send(const FString& Data) override { OnMessageSentEvent.Broadcast(Data); }
		virtual void							Send(const void* Data, SIZE_T Size, bool bIsBinary = false) override {}
		virtual void							SetTextMessageMemoryLimit(uint64 TextMessageMemoryLimit) override {}
		virtual FWebSocketConnectedEvent&		OnConnected() override { return OnConnectedEvent; }
		virtual FWebSocketConnectionErrorEvent& OnConnectionError() override { return OnErrorEvent; }
		virtual FWebSocketClosedEvent&			OnClosed() override { return OnClosedEvent; }
		virtual FWebSocketMessageEvent&			OnMessage() override { return OnMessageEvent; }
		virtual FWebSocketBinaryMessageEvent&	OnBinaryMessage() override { return OnBinaryMessageEvent; }
		virtual FWebSocketRawMessageEvent&		OnRawMessage() override { return OnRawMessageEvent; }
		virtual FWebSocketMessageSentEvent&		OnMessageSent() override { return OnMessageSentEvent; }

	private:
		FWebSocketConnectedEvent	   OnConnectedEvent;
		FWebSocketConnectionErrorEvent OnErrorEvent;
		FWebSocketClosedEvent		   OnClosedEvent;
		FWebSocketMessageEvent		   OnMessageEvent;
		FWebSocketBinaryMessageEvent   OnBinaryMessageEvent;
		FWebSocketRawMessageEvent	   OnRawMessageEvent;
		FWebSocketMessageSentEvent	   OnMessageSentEvent;

		bool bConnected = false;
	};

	class FMockWebSocketFactory : public EpicRtcWebsocketFactoryInterface, public TRefCountingMixin<FMockWebSocketFactory>
	{
	public:
		~FMockWebSocketFactory()
		{
			UE_LOG(LogPixelStreaming2, Log, TEXT("FMockWebSocketFactory"));
		}

		virtual EpicRtcErrorCode CreateWebsocket(EpicRtcWebsocketInterface** outWebsocket) override
		{
			if (!Websocket)
			{
				TSharedPtr<FMockWebSocket> MockWebsocketConnection = MakeShared<FMockWebSocket>();
				Websocket = MakeRefCount<FEpicRtcWebsocket>(true, MockWebsocketConnection);
			}

			Websocket->AddRef(); // increment for adding the reference to the out

			*outWebsocket = Websocket.GetReference();
			return EpicRtcErrorCode::Ok;
		}

		virtual TRefCountPtr<EpicRtcWebsocketInterface> Get(TSharedPtr<FMockWebSocket>& MockWebsocketConnection)
		{
			if (!Websocket)
			{
				MockWebsocketConnection = MakeShared<FMockWebSocket>();
				Websocket = MakeRefCount<FEpicRtcWebsocket>(true, MockWebsocketConnection);
			}
			return Websocket;
		}

		virtual TRefCountPtr<EpicRtcWebsocketInterface> Get()
		{
			TSharedPtr<FMockWebSocket> MockWebsocketConnection;
			return Get(MockWebsocketConnection);
		}

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FMockWebSocketFactory>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FMockWebSocketFactory>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FMockWebSocketFactory>::GetRefCount(); }
		// End EpicRtcRefCountInterface

	private:
		TRefCountPtr<EpicRtcWebsocketInterface> Websocket;
	};

	inline FString ToString(TArray<EpicRtcErrorCode> Errors)
	{
		FString Ret = TEXT("");
		for (size_t i = 0; i < Errors.Num(); i++)
		{
			Ret += ToString(Errors[i]);
			if (i < Errors.Num() - 1)
			{
				Ret += TEXT(", ");
			}
		}

		return Ret;
	}

	template <typename RefCountClass>
	bool ValidateRefCount(TRefCountPtr<RefCountClass>& Class, FString Name, uint32_t ExpectedCount)
	{
		if (Class.GetReference() == nullptr)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to validate %s. GetReference() = nullptr"), *Name);
			return false;
		}

		if (Class->Count() != ExpectedCount)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to validate %s. Has invalid reference count. Expected (%d), Actual (%d)"), *Name, ExpectedCount, Class->Count());
			return false;
		}

		return true;
	}

	template <typename RefCountClass>
	bool ValidateResultRefCount(TRefCountPtr<RefCountClass>& Class, FString Name, EpicRtcErrorCode Result, TArray<EpicRtcErrorCode> ExpectedResult, uint32_t ExpectedCount)
	{
		if (!ExpectedResult.Contains(Result))
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to validate %s. Unexpected result. Expected one of ([%s]), Actual (%s)"), *Name, *ToString(ExpectedResult), *ToString(Result));
			return false;
		}

		return ValidateRefCount<RefCountClass>(Class, Name, ExpectedCount);
	}

	// NOTE: Because the platform is shared between PS, EOSSDK and these tests, we can't do a != comparison because we don't know what else could have created a platform
	inline bool ValidatePlatform(TRefCountPtr<EpicRtcPlatformInterface>& Platform, EpicRtcErrorCode Result, TArray<EpicRtcErrorCode> ExpectedResult, uint8_t ExpectedCount)
	{
		// NOTE: Because platforms can return either Ok or FoundExistingPlatform (both success cases), we need to check if the result is one of them
		if (!ExpectedResult.Contains(Result))
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to validate platform. Unexpected result. Expected one of ([%s]), Actual (%s)"), *ToString(ExpectedResult), *ToString(Result));
			return false;
		}

		if (Platform.GetReference() == nullptr)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to validate platform. Platform.GetReference() = nullptr"));
			return false;
		}

		// NOTE: Because the platform is shared between PS, EOSSDK and these tests, we can't do a != comparison because we don't know what else could have created a platform
		if (Platform->Count() < ExpectedCount)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to validate platform. Platform has invalid reference count. Expected (%d), Actual (%d)"), ExpectedCount, Platform->Count());
			return false;
		}

		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FTickAndWaitOrTimeout, TSharedPtr<FMockManager>, Manager, double, TimeoutSeconds, TFunction<bool()>, CheckFunc);
	DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FDisconnectRoom, TSharedPtr<FMockManager>, Manager);
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCleanupRoom, TSharedPtr<FMockManager>, Manager, FUtf8String, RoomId);
	DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FDisconnectSession, TSharedPtr<FMockManager>, Manager);
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCleanupSession, TSharedPtr<FMockManager>, Manager, FUtf8String, SessionId);
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCleanupConference, TRefCountPtr<EpicRtcPlatformInterface>, Platform, FUtf8String, ConferenceId);
	// NOTE: This is required to be the last command for any test that uses observers. It's required to keep the manager object alive
	DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FCleanupManager, TSharedPtr<FMockManager>, Manager);
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FValidateRefCount, TRefCountPtr<EpicRtcRefCountInterface>, RefCountInterface, uint8_t, ExpectedCount);
} // namespace UE::PixelStreaming2
