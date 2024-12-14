// Copyright Epic Games, Inc. All Rights Reserved.

#include "StreamerReconnectTimer.h"

#include "Logging.h"
#include "PixelStreaming2PluginSettings.h"

namespace UE::PixelStreaming2
{

	FStreamerReconnectTimer::FStreamerReconnectTimer()
	{
	}

	void FStreamerReconnectTimer::Start(TWeakPtr<IPixelStreaming2Streamer> InWeakStreamer)
	{
		WeakStreamer = InWeakStreamer;
		bEnabled = true;
	}

	void FStreamerReconnectTimer::Stop()
	{
		bEnabled = false;
	}

	void FStreamerReconnectTimer::Tick(float DeltaTime)
	{
		if (IsEngineExitRequested())
		{
			return;
		}

		if (!bEnabled)
		{
			return;
		}

		TSharedPtr<IPixelStreaming2Streamer> Streamer = WeakStreamer.Pin();

		if (!Streamer)
		{
			return;
		}

		// Do not attempt a reconnect is we are already connected/streaming
		if (Streamer->IsStreaming())
		{
			return;
		}

		float ReconnectInterval = UPixelStreaming2PluginSettings::CVarSignalingReconnectInterval.GetValueOnAnyThread();

		if (ReconnectInterval <= 0.0f)
		{
			return;
		}

		uint64 CyclesNow = FPlatformTime::Cycles64();
		uint64 DeltaCycles = CyclesNow - LastReconnectCycles;
		float DeltaSeconds = FPlatformTime::ToSeconds(DeltaCycles);

		// If enough time has elapsed, try a reconnect
		if (DeltaSeconds >= ReconnectInterval)
		{
			UE_LOG(LogPixelStreaming2, Log, TEXT("Streamer reconnecting..."))
				Streamer->StartStreaming();
			LastReconnectCycles = CyclesNow;
		}
	}

} // namespace UE::PixelStreaming2