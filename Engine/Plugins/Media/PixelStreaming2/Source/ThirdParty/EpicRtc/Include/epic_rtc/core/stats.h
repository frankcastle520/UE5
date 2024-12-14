// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/containers/epic_rtc_string_view.h"
#include "epic_rtc/containers/epic_rtc_span.h"
#include "epic_rtc/core/ref_count.h"
#include "epic_rtc/common/common.h"

#pragma pack(push, 8)

struct EpicRtcInboundRtpStats
{
    uint32_t _ssrc;
    EpicRtcStringView _kind;
    EpicRtcStringView _transportId;
    EpicRtcStringView _codecId;
    uint64_t _packetsReceived;
    int64_t _packetsLost;
    double _jitter;
    EpicRtcStringView _trackIdentifier;
    EpicRtcStringView _mid;
    EpicRtcStringView _remoteId;
    uint32_t _framesDecoded;
    uint32_t _keyFramesDecoded;
    uint32_t _framesRendered;
    uint32_t _framesDropped;
    uint32_t _frameWidth;
    uint32_t _frameHeight;
    double _framesPerSecond;
    uint64_t _qpSum;
    double _totalDecodeTime;
    double _totalInterFrameDelay;
    double _totalSquaredInterFrameDelay;
    uint32_t _pauseCount;
    double _totalPausesDuration;
    uint32_t _freezeCount;
    double _totalFreezesDuration;
    double _lastPacketReceivedTimestamp;
    uint64_t _headerBytesReceived;
    uint64_t _packetsDiscarded;
    uint64_t _fecBytesReceived;
    uint64_t _fecPacketsReceived;
    uint64_t _fecPacketsDiscarded;
    uint64_t _bytesReceived;
    uint32_t _nackCount;
    uint32_t _firCount;
    uint32_t _pliCount;
    double _totalProcessingDelay;
    double _estimatedPlayoutTimestamp;
    double _jitterBufferDelay;
    double _jitterBufferTargetDelay;
    uint64_t _jitterBufferEmittedCount;
    double _jitterBufferMinimumDelay;
    uint64_t _totalSamplesReceived;
    uint64_t _concealedSamples;
    uint64_t _silentConcealedSamples;
    uint64_t _concealmentEvents;
    uint64_t _insertedSamplesForDeceleration;
    uint64_t _removedSamplesForAcceleration;
    double _audioLevel;
    double _totalAudioEnergy;
    double _totalSamplesDuration;
    uint32_t _framesReceived;
    EpicRtcStringView _decoderImplementation;
    EpicRtcStringView _playoutId;
    EpicRtcBool _powerEfficientDecoder;
    uint32_t _framesAssembledFromMultiplePackets;
    double _totalAssemblyTime;
    uint64_t _retransmittedPacketsReceived;
    uint64_t _retransmittedBytesReceived;
    uint32_t _rtxSsrc;
    uint32_t _fecSsrc;
};
static_assert(sizeof(EpicRtcInboundRtpStats) == 496);

enum class EpicRtcQualityLimitationReason : uint8_t
{
    None = 0,
    CPU,
    Bandwidth,
    Other
};

struct EpicRtcQualityLimitationDurationsStats
{
    double _none;
    double _cpu;
    double _bandwidth;
    double _other;
};
static_assert(sizeof(EpicRtcQualityLimitationDurationsStats) == 32);

struct EpicRtcOutboundRtpStats
{
    uint32_t _ssrc;
    EpicRtcStringView _kind;
    EpicRtcStringView _transportId;
    EpicRtcStringView _codecId;
    uint64_t _packetsSent;
    uint64_t _bytesSent;
    EpicRtcStringView _mid;
    EpicRtcStringView _mediaSourceId;
    EpicRtcStringView _remoteId;
    EpicRtcStringView _rid;
    uint64_t _headerBytesSent;
    uint64_t _retransmittedPacketsSent;
    uint64_t _retransmittedBytesSent;
    uint32_t _rtxSsrc;
    double _targetBitrate;
    uint64_t _totalEncodedBytesTarget;
    uint32_t _frameWidth;
    uint32_t _frameHeight;
    double _framesPerSecond;
    uint32_t _framesSent;
    uint32_t _hugeFramesSent;
    uint32_t _framesEncoded;
    uint32_t _keyFramesEncoded;
    uint64_t _qpSum;
    double _totalEncodeTime;
    double _totalPacketSendDelay;
    EpicRtcQualityLimitationReason _qualityLimitationReason;
    EpicRtcQualityLimitationDurationsStats _qualityLimitationDurations;
    uint32_t _qualityLimitationResolutionChanges;
    uint32_t _nackCount;
    uint32_t _firCount;
    uint32_t _pliCount;
    EpicRtcStringView _encoderImplementation;
    EpicRtcBool _powerEfficientEncoder;
    EpicRtcBool _active;
    EpicRtcStringView _scalabilityMode;
};
static_assert(sizeof(EpicRtcOutboundRtpStats) == 336);

struct EpicRtcRemoteInboundRtpStats
{
    uint32_t _ssrc;
    EpicRtcStringView _kind;
    EpicRtcStringView _transportId;
    EpicRtcStringView _codecId;
    uint64_t _packetsReceived;
    int64_t _packetsLost;
    double _jitter;
    EpicRtcStringView _localId;
    double _roundTripTime;
    double _totalRoundTripTime;
    double _fractionLost;
    uint64_t _roundTripTimeMeasurements;
};
static_assert(sizeof(EpicRtcRemoteInboundRtpStats) == 128);

struct EpicRtcRemoteOutboundRtpStats
{
    uint32_t _ssrc;
    EpicRtcStringView _kind;
    EpicRtcStringView _transportId;
    EpicRtcStringView _codecId;
    uint64_t _packetsSent;
    uint64_t _bytesSent;
    EpicRtcStringView _localId;
    double _remoteTimestamp;
    uint64_t _reportsSent;
    double _roundTripTime;
    double _totalRoundTripTime;
    uint64_t _roundTripTimeMeasurements;
};
static_assert(sizeof(EpicRtcRemoteOutboundRtpStats) == 128);

struct EpicRtcCodecStats
{
    uint32_t _payloadType;
    EpicRtcStringView _transportId;
    EpicRtcStringView _mimeType;
    uint32_t _clockRate;
    uint32_t _channels;
    EpicRtcStringView _sdpFmtpLine;
};
static_assert(sizeof(EpicRtcCodecStats) == 64);

struct EpicRtcAudioSourceStats
{
    EpicRtcStringView _trackIdentifier;
    double _audioLevel;
    double _totalAudioEnergy;
    double _totalSamplesDuration;
    double _echoReturnLoss;
    double _echoReturnLossEnhancement;
};
static_assert(sizeof(EpicRtcAudioSourceStats) == 56);

struct EpicRtcVideoSourceStats
{
    EpicRtcStringView _trackIdentifier;
    uint32_t _width;
    uint32_t _height;
    uint32_t _frames;
    double _framesPerSecond;
};
static_assert(sizeof(EpicRtcVideoSourceStats) == 40);

struct EpicRtcLocalTrackRtpStats
{
    EpicRtcOutboundRtpStats _local;
    EpicRtcRemoteInboundRtpStats _remote;
};
static_assert(sizeof(EpicRtcLocalTrackRtpStats) == 464);

struct EpicRtcLocalAudioTrackStats
{
    EpicRtcStringView _trackId;
    EpicRtcAudioSourceStats _source;
    EpicRtcLocalTrackRtpStats _rtp;
    EpicRtcCodecStats _codec;
    EpicRtcStringView _transportId;
};
static_assert(sizeof(EpicRtcLocalAudioTrackStats) == 616);

struct EpicRtcLocalVideoTrackStats
{
    EpicRtcStringView _trackId;
    EpicRtcVideoSourceStats _source;
    //Simulcast has rtp stats for every encoding, hence the span here
    EpicRtcLocalTrackRtpStatsSpan _rtp;
    EpicRtcCodecStats _codec;
    EpicRtcStringView _transportId;
};
static_assert(sizeof(EpicRtcLocalVideoTrackStats) == 152);

struct EpicRtcRemoteTrackRtpStats
{
    EpicRtcInboundRtpStats _local;
    EpicRtcRemoteOutboundRtpStats _remote;
};
static_assert(sizeof(EpicRtcRemoteTrackRtpStats) == 624);

struct EpicRtcRemoteTrackStats
{
    EpicRtcStringView _trackId;
    EpicRtcRemoteTrackRtpStats _rtp;
    EpicRtcCodecStats _codec;
    EpicRtcStringView _transportId;
};
static_assert(sizeof(EpicRtcRemoteTrackStats) == 720);

struct EpicRtcDataTrackStats
{
    EpicRtcStringView _id;
    EpicRtcStringView _label;
    EpicRtcStringView _protocol;
    int32_t _dataChannelIdentifier;
    EpicRtcTrackState _state;
    uint32_t _messagesSent;
    uint64_t _bytesSent;
    uint32_t _messagesReceived;
    uint64_t _bytesReceived;
};
static_assert(sizeof(EpicRtcDataTrackStats) == 88);

enum class EpicRtcIceCandidateType : uint8_t
{
    Host = 0,
    Srflx,
    Prflx,
    Relay
};

enum class EpicRtcIceServerTransportProtocol : uint8_t
{
    Udp = 0,
    Tcp,
    Tls
};

enum class EpicRtcIceTcpCandidateType : uint8_t
{
    Active = 0,
    Passive,
    So
};

struct EpicRtcIceCandidateStats
{
    EpicRtcStringView _transportId;
    EpicRtcStringView _address;
    int32_t _port;
    EpicRtcStringView _protocol;
    EpicRtcIceCandidateType _candidateType;
    int32_t _priority;
    EpicRtcStringView _url;
    EpicRtcIceServerTransportProtocol _relayProtocol;
    EpicRtcStringView _foundation;
    EpicRtcStringView _relatedAddress;
    int32_t _relatedPort;
    EpicRtcStringView _usernameFragment;
    EpicRtcIceTcpCandidateType _tcpType;
    EpicRtcBool _remote;
};
static_assert(sizeof(EpicRtcIceCandidateStats) == 152);

enum class EpicRtcIceCandidatePairState : uint8_t
{
    Frozen = 0,
    Waiting,
    InProgress,
    Failed,
    Succeeded
};

struct EpicRtcIceCandidatePairStats
{
    EpicRtcStringView _id;
    EpicRtcStringView _transportId;
    EpicRtcStringView _localCandidateId;
    EpicRtcStringView _remoteCandidateId;
    EpicRtcIceCandidatePairState _state;
    EpicRtcBool _nominated;
    uint64_t _packetsSent;
    uint64_t _packetsReceived;
    uint64_t _bytesSent;
    uint64_t _bytesReceived;
    double _lastPacketSentTimestamp;
    double _lastPacketReceivedTimestamp;
    double _totalRoundTripTime;
    double _currentRoundTripTime;
    double _availableOutgoingBitrate;
    double _availableIncomingBitrate;
    uint64_t _requestsReceived;
    uint64_t _requestsSent;
    uint64_t _responsesReceived;
    uint64_t _responsesSent;
    uint64_t _consentRequestsSent;
    uint64_t _packetsDiscardedOnSend;
    uint64_t _bytesDiscardedOnSend;
};
static_assert(sizeof(EpicRtcIceCandidatePairStats) == 208);

enum class EpicRtcIceRole : uint8_t
{
    Unknown = 0,
    Controlling,
    Controlled
};

enum class EpicRtcDtlsTransportState : uint8_t
{
    New = 0,
    Connecting,
    Connected,
    Closed,
    Failed
};

enum class EpicRtcIceTransportState : uint8_t
{
    New = 0,
    Checking,
    Connected,
    Completed,
    Disconnected,
    Failed,
    Closed
};

enum class EpicRtcDtlsRole : uint8_t
{
    Client = 0,
    Server,
    Unknown
};

struct EpicRtcTransportStats
{
    EpicRtcStringView _id;
    uint64_t _packetsSent;
    uint64_t _packetsReceived;
    uint64_t _bytesSent;
    uint64_t _bytesReceived;
    EpicRtcIceRole _iceRole;
    EpicRtcStringView _iceLocalUsernameFragment;
    EpicRtcDtlsTransportState _dtlsState;
    EpicRtcIceTransportState _iceState;
    EpicRtcStringView _selectedCandidatePairId;
    EpicRtcStringView _localCertificateId;
    EpicRtcStringView _remoteCertificateId;
    EpicRtcStringView _tlsVersion;
    EpicRtcStringView _dtlsCipher;
    EpicRtcDtlsRole _dtlsRole;
    EpicRtcStringView _srtpCipher;
    uint32_t _selectedCandidatePairChanges;
    EpicRtcIceCandidateStatsSpan _candidates;
    EpicRtcIceCandidatePairStatsSpan _candidatePairs;
};
static_assert(sizeof(EpicRtcTransportStats) == 224);

struct EpicRtcCertificateStats
{
    EpicRtcStringView _id;
    EpicRtcStringView _fingerprint;
    EpicRtcStringView _fingerprintAlgorithm;
    EpicRtcStringView _base64Certificate;
    EpicRtcStringView _issuerCertificateId;
};
static_assert(sizeof(EpicRtcCertificateStats) == 80);

struct EpicRtcConnectionStats
{
    EpicRtcStringView _connectionId;
    EpicRtcStringView _json;
    EpicRtcLocalAudioTrackStatsSpan _localAudioTracks;
    EpicRtcLocalVideoTrackStatsSpan _localVideoTracks;
    EpicRtcRemoteTrackStatsSpan _remoteAudioTracks;
    EpicRtcRemoteTrackStatsSpan _remoteVideoTracks;
    EpicRtcDataTrackStatsSpan _dataTracks;
    EpicRtcTransportStatsSpan _transports;
    EpicRtcCertificateStatsSpan _certificates;
};
static_assert(sizeof(EpicRtcConnectionStats) == 144);

struct EpicRtcRoomStats
{
    EpicRtcConnectionStatsSpan _connectionStats;
};
static_assert(sizeof(EpicRtcRoomStats) == 16);

struct EpicRtcSessionStats
{
    EpicRtcRoomStatsSpan _roomStats;
};
static_assert(sizeof(EpicRtcSessionStats) == 16);

struct EpicRtcStatsReport
{
    uint64_t _timestamp;
    EpicRtcSessionStatsSpan _sessionStats;
};
static_assert(sizeof(EpicRtcStatsReport) == 24);

class EpicRtcStatsCollectorCallbackInterface : public EpicRtcRefCountInterface
{
public:
    virtual EMRTC_API void OnStatsDelivered(const EpicRtcStatsReport& report) = 0;

    // Prevent copying
    EpicRtcStatsCollectorCallbackInterface(const EpicRtcStatsCollectorCallbackInterface&) = delete;
    EpicRtcStatsCollectorCallbackInterface& operator=(const EpicRtcStatsCollectorCallbackInterface&) = delete;
protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcStatsCollectorCallbackInterface() = default;
    virtual EMRTC_API ~EpicRtcStatsCollectorCallbackInterface() = default;
};

#pragma pack(pop)
