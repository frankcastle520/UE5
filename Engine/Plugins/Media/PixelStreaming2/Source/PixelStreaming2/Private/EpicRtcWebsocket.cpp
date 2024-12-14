// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcWebsocket.h"

#include "IPixelStreaming2Module.h"
#include "IWebSocket.h"
#include "PixelStreaming2PluginSettings.h"
#include "Serialization/JsonSerializer.h"
#include "UtilsString.h"
#include "UtilsVideo.h"
#include "WebSocketsModule.h"

namespace UE::PixelStreaming2
{

	DECLARE_LOG_CATEGORY_EXTERN(LogEpicRtcWebsocket, Log, All);
	DEFINE_LOG_CATEGORY(LogEpicRtcWebsocket);

	FEpicRtcWebsocket::FEpicRtcWebsocket(bool bKeepAlive, TSharedPtr<IWebSocket> InWebSocket)
		: WebSocket(InWebSocket)
		, bSendKeepAlive(bKeepAlive)
	{
	}

	EpicRtcBool FEpicRtcWebsocket::Connect(EpicRtcStringView InUrl, EpicRtcWebsocketObserverInterface* InObserver)
	{
		if (WebSocket && WebSocket->IsConnected())
		{
			return false;
		}

		Observer = InObserver;
		Url = UE::PixelStreaming2::ToString(InUrl);

		if (!WebSocket)
		{
			WebSocket = FWebSocketsModule::Get().CreateWebSocket(Url, TEXT(""));
			verifyf(WebSocket, TEXT("FWebSocketsModule failed to create a valid Web Socket."));
		}

		OnConnectedHandle = WebSocket->OnConnected().AddLambda([this]() { OnConnected(); });
		OnConnectionErrorHandle = WebSocket->OnConnectionError().AddLambda([this](const FString& Error) { OnConnectionError(Error); });
		OnClosedHandle = WebSocket->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean) { OnClosed(StatusCode, Reason, bWasClean); });
		OnMessageHandle = WebSocket->OnMessage().AddLambda([this](const FString& Msg) { OnMessage(Msg); });
		OnBinaryMessageHandle = WebSocket->OnBinaryMessage().AddLambda([this](const void* Data, int32 Count, bool bIsLastFragment) { OnBinaryMessage((const uint8*)Data, Count, bIsLastFragment); });

		// Do the actual WS connection here
		WebSocket->Connect();

		return true;
	}

	void FEpicRtcWebsocket::Disconnect(const EpicRtcStringView InReason)
	{
		if (!WebSocket)
		{
			return;
		}

		WebSocket->OnConnected().Remove(OnConnectedHandle);
		WebSocket->OnConnectionError().Remove(OnConnectionErrorHandle);
		WebSocket->OnClosed().Remove(OnClosedHandle);
		WebSocket->OnMessage().Remove(OnMessageHandle);
		WebSocket->OnBinaryMessage().Remove(OnBinaryMessageHandle);

		if (WebSocket->IsConnected() && !bCloseRequested)
		{
			bCloseRequested = true;
			FString Reason;
			if (InReason._length)
			{
				Reason = ToString(InReason);
			}
			else
			{
				Reason = IsEngineExitRequested() ? TEXT("Pixel Streaming shutting down") : TEXT("Pixel Streaming closed WS under normal conditions.");
			}

			UE_LOG(LogEpicRtcWebsocket, Log, TEXT("Closing websocket to %s"), *Url);
			WebSocket->Close(1000, Reason);

			// Because we've onbound ourselves from the existing WS message, we need to manually trigger OnClosed
			OnClosed(1000, Reason, true);
		}
	}

	void FEpicRtcWebsocket::Send(EpicRtcStringView Message)
	{
		if (!WebSocket || !WebSocket->IsConnected())
		{
			return;
		}

		FString MessageString = FString{ (int32)Message._length, Message._ptr };

		// Hijacking the offer message is a bit cheeky and should be removed once RTCP-7055 is closed.
		TSharedPtr<FJsonObject>	  JsonObject = MakeShareable(new FJsonObject);
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(MessageString);
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
		{
			FString MessageType;
			JsonObject->TryGetStringField(TEXT("type"), MessageType);

			if (MessageType == TEXT("offer"))
			{
				EScalabilityMode			 ScalabilityMode = UE::PixelStreaming2::GetEnumFromCVar<EScalabilityMode>(UPixelStreaming2PluginSettings::CVarEncoderScalabilityMode);
				FString						 ScalabilityModeString = UE::PixelStreaming2::GetCVarStringFromEnum<EScalabilityMode>(ScalabilityMode);
				TSharedRef<FJsonValueString> JsonValueObject = MakeShareable(new FJsonValueString(ScalabilityModeString));

				JsonObject->SetField(TEXT("scalabilityMode"), JsonValueObject);
				TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&MessageString);
				FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);
			}
		}

		WebSocket->Send(MessageString);
	}

	void FEpicRtcWebsocket::OnConnected()
	{
		UE_LOG(LogEpicRtcWebsocket, Log, TEXT("Websocket connection made to: %s"), *Url);
		bCloseRequested = false;
		bReconnectOnError = false;
		Observer->OnOpen();
	}

	void FEpicRtcWebsocket::OnConnectionError(const FString& Error)
	{
		if (!WebSocket->IsConnected() && UPixelStreaming2PluginSettings::CVarSignalingReconnectInterval.GetValueOnAnyThread() > 0.0f)
		{
			// Reconnecting case where we had not connected yet and got an error while connecting (e.g. server not up)
			bReconnectOnError = true;

			UE_LOG(LogEpicRtcWebsocket, Log, TEXT("Failed to connect to %s. (\"%s\") - signalling server may not be up yet. Reconnecting..."), *Url, *Error);

			// To reconnect we must close the existing WS (amusingly this does no trigger WS `OnClosed`)
			WebSocket->Close();

			// Note: By NOT issuing `OnClosed` here we keep the EpicRtcSession in
			// a `pending` state while we attempt to reconnect (which seems appropriate). When a connection is made it can
			// graduate to the `connected` through `OnOpen` being fired above.
		}
		else
		{
			// In this case with we were already connected and got an error OR we have disabled reconnection
			UE_LOG(LogEpicRtcWebsocket, Log, TEXT("Failed to connect to %s - signalling server may not be up yet. Message: \"%s\""), *Url, *Error);

			// Note: Only issue `OnClosed` if we are not attempting to reconnect.
			Observer->OnClosed();
		}
	}

	void FEpicRtcWebsocket::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
	{
		bReconnectOnError = false;
		UE_LOG(LogEpicRtcWebsocket, Log, TEXT("Closed connection to %s - \n\tstatus %d\n\treason: %s\n\twas clean: %s"), *Url, StatusCode, *Reason, bWasClean ? TEXT("true") : TEXT("false"));
		Observer->OnClosed();
	}

	void FEpicRtcWebsocket::OnMessage(const FString& Msg)
	{
		// Hijacking the answer message is a bit cheeky and should be removed once RTCP-7130 is closed.
		TSharedPtr<FJsonObject>	  JsonObject = MakeShareable(new FJsonObject);
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Msg);
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
		{
			FString MessageType;
			JsonObject->TryGetStringField(TEXT("type"), MessageType);

			if (MessageType == TEXT("answer"))
			{
				FString PlayerId;
				if (JsonObject->TryGetStringField(TEXT("playerId"), PlayerId))
				{
					int		   MinBitrate;
					int		   MaxBitrate;
					const bool bGotMinBitrate = JsonObject->TryGetNumberField(TEXT("minBitrateBps"), MinBitrate);
					const bool bGotMaxBitrate = JsonObject->TryGetNumberField(TEXT("maxBitrateBps"), MaxBitrate);

					if (bGotMinBitrate && bGotMaxBitrate && MinBitrate > 0 && MaxBitrate > 0)
					{
						IPixelStreaming2Module::Get().ForEachStreamer([PlayerId, MinBitrate, MaxBitrate](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
							Streamer->PlayerRequestsBitrate(PlayerId, MinBitrate, MaxBitrate);
						});
					}
				}
			}
		}

		FUtf8String Message(Msg);
		Observer->OnMessage(UE::PixelStreaming2::ToEpicRtcStringView(Message));
	}

	void FEpicRtcWebsocket::OnBinaryMessage(const uint8* Data, int32 Length, bool bIsLastFragment)
	{
		FUtf8String Utf8String = FUtf8String::ConstructFromPtrSize(reinterpret_cast<const char*>(Data), Length);

		FString Msg = *Utf8String;
		OnMessage(Msg);
	}

	void FEpicRtcWebsocket::Tick(float DeltaTime)
	{
		if (IsEngineExitRequested())
		{
			return;
		}

		Reconnect();

		if (bSendKeepAlive)
		{
			KeepAlive();
		}
	}

	void FEpicRtcWebsocket::KeepAlive()
	{
		if (!WebSocket)
		{
			return;
		}

		if (!WebSocket->IsConnected())
		{
			return;
		}

		float KeepAliveIntervalSeconds = UPixelStreaming2PluginSettings::CVarSignalingKeepAliveInterval.GetValueOnAnyThread();

		if (KeepAliveIntervalSeconds <= 0.0f)
		{
			return;
		}

		uint64 CyclesNow = FPlatformTime::Cycles64();
		uint64 DeltaCycles = CyclesNow - LastKeepAliveCycles;
		float  DeltaSeconds = FPlatformTime::ToSeconds(DeltaCycles);

		// If enough time has elapsed, try a keepalive
		if (DeltaSeconds >= KeepAliveIntervalSeconds)
		{
			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			const double			UnixTime = FDateTime::UtcNow().ToUnixTimestamp();
			Json->SetStringField(TEXT("type"), TEXT("ping"));
			Json->SetNumberField(TEXT("time"), UnixTime);
			WebSocket->Send(UE::PixelStreaming2::ToString(Json, false));
			LastKeepAliveCycles = CyclesNow;
		}
	}

	void FEpicRtcWebsocket::Reconnect()
	{
		if (!bReconnectOnError)
		{
			return;
		}

		if (!WebSocket)
		{
			return;
		}

		if (WebSocket->IsConnected())
		{
			return;
		}

		float ReconnectIntervalSeconds = UPixelStreaming2PluginSettings::CVarSignalingReconnectInterval.GetValueOnAnyThread();

		if (ReconnectIntervalSeconds <= 0.0f)
		{
			return;
		}

		uint64 CyclesNow = FPlatformTime::Cycles64();
		uint64 DeltaCycles = CyclesNow - LastReconnectCycles;
		float  DeltaSeconds = FPlatformTime::ToSeconds(DeltaCycles);

		// If enough time has elapsed, try a reconnect
		if (DeltaSeconds >= ReconnectIntervalSeconds)
		{
			WebSocket->Connect();
			LastReconnectCycles = CyclesNow;
			// Do not try to reconnect until we hear the next error
			bReconnectOnError = false;
		}
	}

} // namespace UE::PixelStreaming2
