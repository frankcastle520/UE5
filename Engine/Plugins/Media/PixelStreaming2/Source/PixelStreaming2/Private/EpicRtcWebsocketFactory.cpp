// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcWebsocketFactory.h"

namespace UE::PixelStreaming2
{

	EpicRtcErrorCode FEpicRtcWebsocketFactory::CreateWebsocket(EpicRtcWebsocketInterface** OutWebsocket)
	{
		EpicRtcWebsocketInterface* Websocket = new FEpicRtcWebsocket(bSendKeepAlive);
		// Because the ptr was created with new, we need to call AddRef ourself (ms spec compliant)
		Websocket->AddRef();

		*OutWebsocket = Websocket;
		return EpicRtcErrorCode::Ok;
	}

} // namespace UE::PixelStreaming2
