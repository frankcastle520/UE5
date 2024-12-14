// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcAudioSink.h"
#include "EpicRtcAudioSource.h"
#include "EpicRtcDataTrack.h"
#include "EpicRtcVideoSink.h"
#include "EpicRtcVideoSource.h"
#include "RTCStatsCollector.h"
#include "Templates/RefCounting.h"
#include "UtilsCommon.h"

#include "epic_rtc/core/audio/audio_track.h"
#include "epic_rtc/core/video/video_track.h"
#include "epic_rtc/core/data_track.h"
#include "epic_rtc/core/participant.h"

namespace UE::PixelStreaming2
{
	struct FPlayerContext
	{
		TRefCountPtr<EpicRtcParticipantInterface> ParticipantInterface;

		TSharedPtr<FEpicRtcAudioSource> AudioSource;
		TSharedPtr<FEpicRtcAudioSink>	AudioSink;

		TSharedPtr<FEpicRtcVideoSource> VideoSource;
		TSharedPtr<FEpicRtcVideoSink>	VideoSink;

		TSharedPtr<FEpicRtcDataTrack> DataTrack;

		TSharedPtr<FRTCStatsCollector> StatsCollector;
	};

} // namespace UE::PixelStreaming2