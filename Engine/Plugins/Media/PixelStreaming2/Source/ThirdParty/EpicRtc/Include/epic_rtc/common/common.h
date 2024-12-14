// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cstdint>

#pragma pack(push, 8)

typedef uint8_t EpicRtcBool;

/**
 * API will return an object extending this interface on every Event subscription.
 * Call the function to unsubscribe your listener from the Event.
 * @return True if listener was unsubscribed, false otherwise.
 */
class EpicRtcEventListenerHandleInterface
{
public:
    virtual ~EpicRtcEventListenerHandleInterface() = default;
    virtual EpicRtcBool Unsubscribe() = 0;
};

/**
 * Describes all possible API error codes.
 */
enum class EpicRtcErrorCode : uint32_t
{
    /**
     * Indicates a no error.
     */
    Ok = 0,

    /**
     * Indicates a general error.
     */
    GeneralError = 1,

    /**
     * Indicates that the API handle is in a wrong state for the requested operation.
     */
    BadState = 2,

    /**
     * Indicates that operation timed out.
     */
    Timeout = 3,

    /**
     * Indicates that requested feature is not supported.
     */
    Unsupported = 4,

    /**
     * Indicates that an invalid argument was passed.
     */
    InvalidArgument = 5,

    /**
     * PlatformErrors 450-499
     */

    PlatformError = 450,

    FoundExistingPlatform = 451,

    ConferenceAlreadyExists = 452,

    ConferenceDoesNotExists = 453,

    /**
     * Indicates that server is a teapot and cannot brew coffee.
     */
    ImATeapot = 418,

    /**
     * Conference errors 500-999
     */
    ConferenceError = 500,

    /**
     * Conference tried to create a Session that already exists
     */
    SessionAlreadyExists = 501,

    /*
     * Conference was asked for a session that does not exist
     */
    SessionDoesNotExist = 502,

    /**
     * Session errors 1000 - 1999
     */
    SessionError = 1000,

    /**
     * Session is unable to connect.
     */
    SessionCannotConnect = 1001,

    /**
     * Session is disconnected.
     */
    SessionDisconnected = 1002,

    /**
     * Session cannot create room.
     */
    SessionCannotCreateRoom = 1003,

    /**
     * DataTrack errors 2000-2999.
     */
    DataTrackError = 2000,

    /**
     * An operation is valid, but currently unsupported.
     */
    DataTrackUnsupportedOperation = 2001,

    /**
     * A supplied parameter is valid, but currently unsupported.
     */
    DataTrackUnsupportedParameter = 2002,

    /**
     * General error indicating that a supplied parameter is invalid.
     */
    DataTrackInvalidParameter = 2003,

    /**
     * Slightly more specific than INVALID_PARAMETER; a parameter's value was outside the allowed range.
     */
    DataTrackInvalidRange = 2004,

    /**
     * Slightly more specific than INVALID_PARAMETER; an error occurred while parsing string input.
     */
    DataTrackInvalidSyntaxError = 2005,

    /**
     * The object does not support this operation in its current state.
     */
    DataTrackInvalidState = 2006,

    /**
     * An attempt was made to modify the object in an invalid way.
     */
    DataTrackInvalidModification = 2007,

    /**
     * An error occurred within an underlying network protocol.
     */
    DataTrackNetworkError = 2008,

    /**
     * Some resource has been exhausted; file handles, hardware resources, ports, etc.
     */
    DataTrackResourceExhausted = 2009,

    /**
     * The operation failed due to an internal error.
     */
    DataTrackInternalError = 2010,

    /**
     * An error occured that has additional data.
     */
    DataTrackOperationErrorWithData = 2011,


    /**
     * unknown error occurred.
     */
    Unknown = UINT32_MAX
};

/**
 * Describes the mode the Room is running in.
 */
enum class EpicRtcRoomMode : uint8_t
{
    /**
     * The mode in which Room has only one media Connection with the MediaGateway.
     * Connection holds all the media tracks, both incoming and outgoing.
     */
    MediaServer,

    /**
     * Indicates P2P mode in which Room has a separate media Connection with each Participant and no Connection to MediaGateway.
     */
    P2P,

    /**
     * Indicates Mixed mode. The Room has a Connection to MediaGateway and to some of the Participants.
     */
    Mixed,
};

/**
 * ICE candidate policy.
 */
enum class EpicRtcIcePolicy : uint8_t
{
    /**
     * Use all candidates.
     */
    All,

    /**
     * Use relay candidates only.
     */
    Relay
};

/**
 * Describes state of the EpicRtcConnectionInterface.
 */
enum class EpicRtcConnectionState : uint8_t
{
    /**
     * Indicates newly created EpicRtcConnectionInterface.
     */
    New,
    /**
     * Indicates connection is in progress.
     */
    Pending,
    /**
     * Indicates EpicRtcConnectionInterface is connected to the remote peer.
     */
    Connected,
    /**
     * Indicates EpicRtcConnectionInterface is disconnected from the remote peer.
     */
    Disconnected,
    /**
     * Indicates EpicRtcConnectionInterface has failed and is unusable.
     */
    Failed,
};

/**
 * Describe direction of the media source.
 */
 enum class EpicRtcMediaSourceDirection : uint8_t
 {
    /**
     * Indicates that the media source will only be sending media.
     */
    SendOnly,
    /**
     * Indicates that the media source will be sending media as well as receiving it.
     */
    SendRecv,
    /**
     * Indicates that this media source will be receiving only.
     * Use this type to let the other party know that you are prepared to receive media but don't have any to send.
     */
    RecvOnly
 };

enum class EpicRtcDataSourceProtocol : uint8_t
{
    /**
     * SCTP protocol.
     */
    Sctp,

    /**
     * QUIC protocol. This has experimental support only and works in P2P mode only.
     */
    Quic,
};

/**
 * Describes EpicRtcRoomInterface state.
 */
enum class EpicRtcRoomState : uint8_t
{
    /**
     * Indicates newly created EpicRtcRoomInterface.
     */
    New,

    /**
     * Indicates join of the local participant is in progress.
     */
    Pending,

    /**
     * Indicates local participant is joined.
     */
    Joined,

    /**
     * Indicates local participant has left this EpicRtcRoomInterface. Room is not usable once in this state.
     */
    Left,

    /**
     * Indicates EpicRtcRoomInterface failed and is unusable.
     */
    Failed,

    /**
     * Indicates EpicRtcRoomInterface has terminated without a result as a response to the application exiting.
     */
    Exiting,
};

/**
 * Describes EpicRtcSessionInterface state.
 */
enum class EpicRtcSessionState : uint8_t
{
    /**
     * Indicates newly created ISession.
     */
    New,

    /**
     * Indicates connection is in progress.
     */
    Pending,

    /**
     * Indicates ISession is connected to signalling server.
     */
    Connected,

    /**
     * Indicates ISession is disconnected from the signalling server.
     */
    Disconnected,

    /**
     * Indicates ISession failed and is unusable.
     */
    Failed,

    /**
     * Indicates ISession has terminated without a result as a response to the application exiting.
     */
    Exiting,
};

/**
 * Describes state of the EpicRtcSignallingSessionInterface.
 */
enum class EpicRtcSignallingSessionState : uint8_t
{
    /**
     * Indicates newly created EpicRtcSignallingSessionInterface.
     */
    New,
    /**
     * Indicates connection is in progress.
     */
    Pending,
    /**
     * Indicates EpicRtcSignallingSessionInterface is connected to the signalling server.
     */
    Connected,
    /**
     * Indicates EpicRtcSignallingSessionInterface is disconnected from the signalling server.
     */
    Disconnected,
    /**
     * Indicates EpicRtcSignallingSessionInterface has failed and is unusable. This might be due to wrong URL or connection interruption.
     */
    Failed,
    /**
     * Indicates EpicRtcSignallingSessionInterface has terminated without a result as a response to the application exiting.
     */
    Exiting,
};

/**
 * Represents type of SDP
 */
enum class EpicRtcSdpType : uint8_t
{
    /**
     * Indicates that SDP describes an offer
     */
    Offer,

    /**
     * Indicates that SDP describes an answer
     */
    Answer,
};

/**
 * Represents track state
 */
enum class EpicRtcTrackState : uint8_t
{
    /**
     * Indicates new track
     */
    New,

    /**
     * Indicates track being in use
     */
    Active,

    /**
     * Indicates stopped track
     */
    Stopped,
};

/**
 * Represents track subscription state
 */
enum class EpicRtcTrackSubscriptionState : uint8_t
{
    /**
     * The Track is available for subscription. The media isn’t flowing, and there is no SDP m-line for the Track.
     */
    Unsubscribed,

    /**
     * The subscription is in progress.
     */
    Pending,

    /**
     * The user is subscribed to the Track, there is an SDP m-line for it, and the media is flowing.
     */
    Subscribed,
};

/**
 * Represents type of the track
 */
enum class EpicRtcTrackType : uint8_t
{
    /**
     * Indicates an audio track
     */
    Audio,

    /**
     * Indicates a video track
     */
    Video,

    /**
     * Indicates a data track
     */
    Data,
};

struct EpicRtcCommon
{
    // Use symbols of ASCII format (for example FourValueEnum('O', 'P', 'U', 'S'))
    static constexpr uint32_t FourValueEnumBigEndian(char a, char b, char c, char d)
    {
        return d | (c << 8) | (b << 16) | (a << 24);
    }
    // Use symbols of ASCII format (for example FourValueEnum('O', 'P', 'U', 'S'))
    static constexpr uint32_t FourValueEnumLittleEndian(char a, char b, char c, char d)
    {
        return a | (b << 8) | (c << 16) | (d << 24);
    }

    // Use symbols of ASCII format (for example FourValueEnum('O', 'P', 'U', 'S'))
    static constexpr uint32_t FourValueEnum(char a, char b, char c, char d)
    {
		enum HlEndianness : uint32_t {
			HlLittleEndian = 0x00000001,
			HlBigEndian = 0x01000000,
		};

		if ((1 & 0xFFFFFFFF) == HlLittleEndian)
		{
			return FourValueEnumLittleEndian(a, b, c, d);
		}

        return FourValueEnumBigEndian(a,b,c,d);
    }
};

enum class EpicRtcMediaResult : int8_t
{
    EncoderFailure = -16,
    ErrSimulcastParametersNotSupported = -15,
    FallbackSoftware = -13,
    Uninitialized = -7,
    Timeout = -6,
    ErrParameter = -4,
    Memory = -3,
    Error = -1,
    Ok = 0,
    NoOutput = 1,
    OkRequestKeyframe = 4,
    TargetBitrateOvershoot = 5,
};
#pragma pack(pop)
