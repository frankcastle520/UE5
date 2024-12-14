// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcWebsocket.h"

#include "epic_rtc/plugins/signalling/websocket.h"
#include "epic_rtc/plugins/signalling/websocket_factory.h"

namespace UE::PixelStreaming2
{
	class PIXELSTREAMING2_API FEpicRtcWebsocketFactory : public EpicRtcWebsocketFactoryInterface, public TRefCountingMixin<FEpicRtcWebsocketFactory>
	{
	public:
		FEpicRtcWebsocketFactory(bool bInSendKeepAlive = true)
			: bSendKeepAlive(bInSendKeepAlive)
		{
		}
		virtual ~FEpicRtcWebsocketFactory() = default;

		virtual EpicRtcErrorCode CreateWebsocket(EpicRtcWebsocketInterface** OutWebsocket) override;

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcWebsocketFactory>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcWebsocketFactory>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcWebsocketFactory>::GetRefCount(); }
		// End EpicRtcRefCountInterface

	private:
		bool bSendKeepAlive;
	};

} // namespace UE::PixelStreaming2
