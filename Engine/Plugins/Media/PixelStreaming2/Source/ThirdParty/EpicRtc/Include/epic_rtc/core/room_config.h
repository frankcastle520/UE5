// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "epic_rtc/containers/epic_rtc_string_view.h"
#include "epic_rtc/core/connection_config.h"
#include "epic_rtc/core/room_observer.h"

#include "epic_rtc/core/audio/audio_track_observer.h"
#include "epic_rtc/core/data_track_observer.h"
#include "epic_rtc/core/video/video_track_observer.h"

#pragma pack(push, 8)

/**
 * Room interface configuration object.
 */
struct EpicRtcRoomConfig
{
    /**
     * Unique room id.
     */
    EpicRtcStringView _id;

    /**
     * Namespace.
     */
    EpicRtcStringView _namespace;

    /**
     * Underlying connection configuration.
     */
    EpicRtcConnectionConfig _connectionConfig;

    /**
     * Authentication ticket (token)
     */
    EpicRtcStringView _ticket;

    EpicRtcRoomObserverInterface* _observer;

    EpicRtcAudioTrackObserverFactoryInterface* _audioTrackObserverFactory;
    EpicRtcDataTrackObserverFactoryInterface* _dataTrackObserverFactory;
    EpicRtcVideoTrackObserverFactoryInterface* _videoTrackObserverFactory;
};

static_assert(sizeof(EpicRtcRoomConfig) == 16 + 16 + 64 + 16 + 8 + 8 + 8 + 8);  // Ensure EpicRtcRoomConfig is expected size on all platforms

#pragma pack(pop)
