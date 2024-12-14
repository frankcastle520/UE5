// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Tickable.h"
#include "IPixelStreaming2Streamer.h"

namespace UE::PixelStreaming2
{
	/**
	 * An interval timer that triggers reconnects using `FStreamer` has it session disconnect.
	 * This is different than a websocket error reconnect because in this case we were already connected.
	 * Timer takes a `TWeakPtr<FStreamer>`, meaning the streamer object must exist for the timer to do anything.
	 * Note: this timer runs on the game thread and its reconnect interval is controlled by the CVar
	 * UE::PixelStreaming2::Settings::CVarPixelStreaming2SignalingReconnectInterval
	*/
	class FStreamerReconnectTimer : public FTickableGameObject
	{
	public:

		FStreamerReconnectTimer();
		virtual ~FStreamerReconnectTimer() = default;
		void Start(TWeakPtr<IPixelStreaming2Streamer> InWeakStreamer);
		void Stop();

		// Begin FTickableGameObject
		virtual void Tick (float DeltaTime) override;
		virtual bool IsTickableInEditor() const override { return true; }
		virtual bool IsTickableWhenPaused() const override { return true; }
		FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(PixelStreaming2StreamerReconnectTimer, STATGROUP_Tickables); }
		// End FTickableGameObject

	private:
		TWeakPtr<IPixelStreaming2Streamer> WeakStreamer;
		bool bEnabled = false;
		uint64 LastReconnectCycles = 0;

	};
} // namespace UE::PixelStreaming2