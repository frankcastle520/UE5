// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <memory>

#include "epic_rtc/common/common.h"
#include "epic_rtc/common/logging.h"
#include "epic_rtc/containers/epic_rtc_span.h"
#include "epic_rtc/containers/epic_rtc_array.h"
#include "epic_rtc/plugins/signalling/signalling_type.h"
#include "epic_rtc/core/stats.h"

#pragma pack(push, 8)

// Forward declarations
class EpicRtcWebsocketFactoryInterface;
class EpicRtcSigningPluginInterface;
class IMigrationPlugin;
class IAudioDevicePlugin
{
};

/**
 * Top-level API configuration.
 * Takes in Plugins and codec factories.
 */
struct EpicRtcConfig
{
    /**
     * User's implementation of WebSocketFactory (required).
     */
    EpicRtcWebsocketFactoryInterface* _websocketFactory;

    /**
     * Instance of the SignallingType that will be used with EMRTC instance
     */
    EpicRtcSignallingType _signallingType;

    /**
     * Instance of the SigningPlugin that will be used with EMRTC instance
     */
    EpicRtcSigningPluginInterface* _signingPlugin;

    /**
     * Instance of the MigrationPlugin that will be used with EMRTC instance
     */
    IMigrationPlugin* _migrationPlugin;
    /**
     * Instance of the AudioDevicePlugin that will be used with EMRTC instance
     */
    IAudioDevicePlugin* _audioDevicePlugin;

    /* Configuration options for audio in/out - cannot be changed later. */
    struct Audio
    {
        /* Enables EpicRtcAudioTrackInterface::OnFrame events by requesting audio from the ADM through EpicRtcConferenceInterface::Tick */
        EpicRtcBool _tickAdm = false;

        /* Audio Encoders that will be made available for streaming */
        EpicRtcAudioEncoderInitializerSpan _audioEncoderInitializers;

        /* Audio Decoders that will be made available for streaming */
        EpicRtcAudioDecoderInitializerSpan _audioDecoderInitializers;

        /* Add the codecs builtin to EpicRTC to the list of available codecs to stream with */
        EpicRtcBool _enableBuiltInAudioCodecs;

        /* The expected sample rate of audio input. Range: 8Khz - 96Khz. */
        uint32_t _recordingSampleRate = 48000;

        /* The expected number of channels for audio input. Range: 1 - 2. */
        uint8_t _recordingChannels = 2;

        /* The expected sample rate of audio output. Range: 8Khz - 96Khz. */
        uint32_t _playoutSampleRate = 48000;

        /* The expected number of channels for audio output. Range: 1 - 2. */
        uint8_t _playoutChannels = 2;

        /* The milliseconds before audio can be played on the speaker/output device. */
        uint8_t _playoutDelayMs = 0;

        /* The milliseconds before audio will go from microphone to audio ingest. */
        uint8_t _recordingDelayMs = 0;

        /* Whether to use auto gain control, if this platform supports it. */
        EpicRtcBool _autoGainControl = false;

        /* Whether to use echo cancellation, if this platform supports it. */
        EpicRtcBool _echoCancellation = false;

        /* If echo cancellation is turned on, should we run the less cpu intensive "mobile" mode. */
        EpicRtcBool _echoCancellationMobileMode = false;

        /* Whether to use noise suppression, if this platform supports it. */
        EpicRtcBool _noiseSuppression = false;

        /* Whether to enable transient suppression (a transient is a high amplitude, short-duration sound at the beginning of a waveform). */
        EpicRtcBool _transientSuppression = false;

        /* Whether to adjust audio input gain level. */
        EpicRtcBool _levelAdjustment = false;

        /* If the level adjustment is turned on, apply this scaling factor PRIOR to audio processing (e.g auto gain control). */
        float _preGainAdjustment = 1.0f;

        /* If the level adjustment is turned on, apply this scaling factor AFTER to audio processing (e.g auto gain control). */
        float _postGainAdjustment = 1.0f;

        /* Apply a high pass filter to the audio (can be useful if there low frequency hum) */
        EpicRtcBool _highPassFilter = false;
    } _audioConfig;

    static_assert(sizeof(Audio) == 80);  // Ensure Audio is expected size on all platforms

    /* Configuration options for video in/out - cannot be changed later. */
    struct Video
    {
        /**
         * Video Encoders that will be made available for streaming
         */
        EpicRtcVideoEncoderInitializerInterfaceSpan _videoEncoderInitializers;

        /**
         * Video Decoders that will be made available for streaming
         */
        EpicRtcVideoDecoderInitializerInterfaceSpan _videoDecoderInitializers;

        /**
         * Add the codecs builtin to EpicRTC to the list of available codecs to stream with
         */
        EpicRtcBool _enableBuiltInVideoCodecs;
    } _videoConfig;
    static_assert(sizeof(Video) == 40);  // Ensure Video is expected size on all platforms

    struct FieldTrials
    {
        /**
         * Set of field trials represented by single string with the format
         * `<key-1>/<value-1>/<key-2>/<value-2>/`. Note the final `/` at the end.
         * Example: "WebRTC-Foo/Enabled/WebRTC-Bar/Disabled/"
         */
        EpicRtcStringView _fieldTrials;

        /**
         * Create field trials that are backed by global variable (string).
         */
        EpicRtcBool _isGlobal = 0;

    } _fieldTrials;
    static_assert(sizeof(FieldTrials) == 24);  // Ensure FieldTrials is expected size on all platforms

    struct Logging
    {
        /**
         * Logger object which will be used to output log messages. Must be thread safe.
         * If not provided, default implementation (depends on a platform) will be used.
         */
        EpicRtcLoggerInterface* _logger = nullptr;

        /**
         * Specifies logs from what level should be printed or passed to `_logger`.
         */
        EpicRtcLogLevel _level = EpicRtcLogLevel::Info;

        /**
         * Specifies WebRtc logs from what level should be printed or passed to `_logger`.
         */
        EpicRtcLogLevel _levelWebRtc = EpicRtcLogLevel::Error;
    } _logging;

    static_assert(sizeof(Logging) == 16);  // Ensure Logging is expected size on all platforms

    /* Additional parameters */
    EpicRtcParameterPairArrayInterface* _parameters;

    struct Stats
    {
        /**
         * Callback that will be called to deliver the stats.
         */
        EpicRtcStatsCollectorCallbackInterface* _statsCollectorCallback = nullptr;

        /**
         * Callback interval, set to 0 to disable.
         */
        uint64_t _statsCollectorInterval = 0;

        /**
         * If true, EpicRtcConnectionStats will contain json string only.
         */
         EpicRtcBool _jsonFormatOnly;
    } _stats;
    static_assert(sizeof(Stats) == 24);  // Ensure Stats is expected size on all platforms
};

static_assert(sizeof(EpicRtcConfig) == 232);  // Ensure EpicRtcConfig is expected size on all platforms

#pragma pack(pop)
