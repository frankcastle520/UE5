// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcTickableTask.h"
#include "IPixelStreaming2Stats.h"
#include "PixelStreaming2PluginSettings.h"

#include "epic_rtc/core/conference.h"

namespace UE::PixelStreaming2
{
	class PIXELSTREAMING2_API FEpicRtcTickConferenceTask : public FEpicRtcTickableTask
	{
	public:
		FEpicRtcTickConferenceTask(TRefCountPtr<EpicRtcConferenceInterface> EpicRtcConference, const FString& TaskName = TEXT("EpicRtcTickConferenceTask"))
			: EpicRtcConference(EpicRtcConference)
			, TaskName(TaskName)
		{
		}

		virtual ~FEpicRtcTickConferenceTask() override
		{
			// We may get a call to destroy the task before we've had a chance to tick again.
			// So to be safe, we tick the conference a final time
			if (EpicRtcConference)
			{

				while (EpicRtcConference->NeedsTick())
				{
					EpicRtcConference->Tick();
				}
			}
		}

		// Begin FEpicRtcTickableTask
		virtual void Tick(float DeltaMs) override
		{
			if (EpicRtcConference)
			{
				MsSinceLastAudioTick += DeltaMs;
				MsSinceLastStatsTick += DeltaMs;

				// Tick conference normally. This handles things like data channel message
				IPixelStreaming2Stats::Get().GraphValue(TEXT("ConferenceTickInterval"), DeltaMs, 1, 0.f, 1.f);
				while (EpicRtcConference->NeedsTick())
				{
					EpicRtcConference->Tick();
				}

				// Tick audio (every 10 ms). This enables the pulling of audio from the ADM
				if (MsSinceLastAudioTick >= 10.f)
				{
					// Track the interval. This is helpful for seeing if we're going over the 10ms requirement
					IPixelStreaming2Stats::Get().GraphValue(TEXT("AudioTickInterval"), MsSinceLastAudioTick, 1, 0.f, 25.f, 10.f);
					EpicRtcConference->TickAudio();
					MsSinceLastAudioTick = 0.f;
				}

				// Tick stats at the configured interval
				float StatsInterval = UPixelStreaming2PluginSettings::CVarWebRTCStatsInterval.GetValueOnAnyThread() * 1000.f;
				bool  bStatsEnabled = !UPixelStreaming2PluginSettings::CVarWebRTCDisableStats.GetValueOnAnyThread();
				if (MsSinceLastStatsTick >= StatsInterval && StatsInterval > 0.f && bStatsEnabled)
				{
					IPixelStreaming2Stats::Get().GraphValue(TEXT("StatTickInterval"), MsSinceLastStatsTick, 1, 0.f, 25.f, 10.f);
					EpicRtcConference->TickStats();
					MsSinceLastStatsTick = 0.f;
				}
			}
		}

		virtual const FString& GetName() const override
		{
			return TaskName;
		}
		// End FEpicRtcTickableTask

	private:
		TRefCountPtr<EpicRtcConferenceInterface> EpicRtcConference;
		FString									 TaskName;

		float MsSinceLastAudioTick = 0.f;
		float MsSinceLastStatsTick = 0.f;
	};

} // namespace UE::PixelStreaming2