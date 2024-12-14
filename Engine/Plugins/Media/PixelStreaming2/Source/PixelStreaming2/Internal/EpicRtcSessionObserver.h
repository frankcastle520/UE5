// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"

#include "epic_rtc/core/session_observer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcManager;

	class PIXELSTREAMING2_API FEpicRtcSessionObserver : public EpicRtcSessionObserverInterface, public TRefCountingMixin<FEpicRtcSessionObserver>
	{
	public:
		FEpicRtcSessionObserver(TWeakPtr<FEpicRtcManager> Manager);
		virtual ~FEpicRtcSessionObserver() = default;

	private:
		// Begin EpicRtcSessionObserver
		virtual void OnSessionStateUpdate(const EpicRtcSessionState State) override;
		virtual void OnSessionErrorUpdate(const EpicRtcErrorCode Error) override;
		virtual void OnSessionRoomsAvailableUpdate(EpicRtcStringArrayInterface* RoomsList) override;
		// End EpicRtcSessionObserver
	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcSessionObserver>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcSessionObserver>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcSessionObserver>::GetRefCount(); }
		// End EpicRtcRefCountInterface
	private:
		TWeakPtr<FEpicRtcManager> Manager;
	};

} // namespace UE::PixelStreaming2
