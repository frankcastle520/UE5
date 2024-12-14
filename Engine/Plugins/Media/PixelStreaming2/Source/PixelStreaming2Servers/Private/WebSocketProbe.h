// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IWebSocket.h"
#include "HAL/ThreadSafeCounter.h"
#include "WebSocketsModule.h"
#include "Templates/SharedPointer.h"
#include "Logging.h"
#include "Engine/EngineBaseTypes.h"
#include "ServerUtils.h"

namespace UE::PixelStreaming2Servers
{

	/*
	 * A utility class that tries to establish a websocket connection.
	 * Useful for testing whether servers have come online yet.
	 */
	class FWebSocketProbe
	{
	private:
		TSharedRef<IWebSocket> WebSocket;
		FThreadSafeBool		   bShouldAttemptReconnect;
		FThreadSafeBool		   bCloseRequested;

	public:
		FWebSocketProbe(FURL Url, TArray<FString> Protocols = TArray<FString>())
			: WebSocket(FWebSocketsModule::Get().CreateWebSocket(Utils::ToString(Url), Protocols))
			, bShouldAttemptReconnect(true)
			, bCloseRequested(false)
		{
			WebSocket->OnConnectionError().AddLambda([Url, &bShouldAttemptReconnect = bShouldAttemptReconnect](const FString& Error) {
				UE_LOG(LogPixelStreaming2Servers, Log, TEXT("Probing websocket %s | Msg= \"%s\" | Retrying..."), *Utils::ToString(Url), *Error);
				bShouldAttemptReconnect = true;
			});
		}

		void Close()
		{
			if (WebSocket->IsConnected() && !bCloseRequested.AtomicSet(true))
			{
				WebSocket->Close();
			}
		}

		bool IsConnected() const
		{
			return WebSocket->IsConnected();
		}

		bool Probe()
		{
			bool bIsConnected = WebSocket->IsConnected();
			bCloseRequested = false;

			if (!bIsConnected && bShouldAttemptReconnect)
			{
				WebSocket->Connect();
				bShouldAttemptReconnect = false;
			}

			return bIsConnected;
		}
	};

} // namespace UE::PixelStreaming2Servers
