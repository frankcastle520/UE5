// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "epic_rtc/common/common.h"
#include "epic_rtc/containers/epic_rtc_span.h"

#pragma pack(push, 8)

/**
 * Port allocation bit fields.
 */
enum class EpicRtcPortAllocatorOptions: uint32_t  
{
    None = 0,
    /**
     * Disable local UDP ports. This doesn't impact how we connect to relay servers.
     */
    DisableUdp = 0x01,
    DisableStun = 0x02,
    DisableRelay = 0x04,

    /**
     * Disable local TCP ports. This doesn't impact how we connect to relay servers.
     */
    DisableTcp = 0x08,
    EnableIPV6 = 0x40,
    EnableSharedSocket = 0x100,
    EnableStunRetransmitAttribute = 0x200,

    /**
     * When specified, we'll only allocate the STUN candidate for the public
     * interface as seen by regular http traffic and the HOST candidate associated
     * with the default local interface.
     */
    DisableAdapterEnumeration = 0x400,

    /**
     * When specified along with DisableAdapterEnumeration, the
     * default local candidate mentioned above will not be allocated. Only the
     * STUN candidate will be.
     */
    DisableDefaultLocalCandidate = 0x800,
    
    /**
     * Disallow use of UDP when connecting to a relay server. Since proxy servers
     * usually don't handle UDP, using UDP will leak the IP address.
     */
    DisableUdpRelay = 0x1000,

    /** 
     * When multiple networks exist, do not gather candidates on the ones with
     * high cost. So if both Wi-Fi and cellular networks exist, gather only on the
     * Wi-Fi network. If a network type is "unknown", it has a cost lower than
     * cellular but higher than Wi-Fi/Ethernet. So if an unknown network exists,
     * cellular networks will not be used to gather candidates and if a Wi-Fi
     * network is present, "unknown" networks will not be usd to gather
     * candidates. Doing so ensures that even if a cellular network type was not
     * detected initially, it would not be used if a Wi-Fi network is present.
     */
    DisableCostlyNetworks = 0x2000,

    /**
     * When specified, do not collect IPv6 ICE candidates on Wi-Fi.
     */
    EnableIPV6OnWifi = 0x4000,

    // When this flag is set, ports not bound to any specific network interface
    // will be used, in addition to normal ports bound to the enumerated
    // interfaces. Without this flag, these "any address" ports would only be
    // used when network enumeration fails or is disabled. But under certain
    // conditions, these ports may succeed where others fail, so they may allow
    // the application to work in a wider variety of environments, at the expense
    // of having to allocate additional candidates.
    EnableAnyAddressPort = 0x8000,

    /**
     * Exclude link-local network interfaces from consideration after adapter enumeration.
     */
    DisableLinkLocalNetworks = 0x10000,
};

/**
 * ICE server specific configuration
 */
struct EpicRtcIceServer
{
    /**
     * Valid formats are described in RFC7064 and RFC7065, and more may be added in the future.
     * The "host" part of the URI may contain either an IP address or a hostname.
     */
    EpicRtcStringViewSpan _urls;

    /**
     * Username for authentication
     */
    EpicRtcStringView _username;

    /**
     * Password for authentication
     */
    EpicRtcStringView _password;
};

static_assert(sizeof(EpicRtcIceServer) == 48);  // Ensure EpicRtcConnectionConfig is expected size on all platforms

/**
 * Bitrate information.
 */
struct EpicRtcBitrate
{
    /**
     * Optional minimum bitrate to use.
     */
    int32_t _minBitrateBps;

    /**
     * Minimum bitrate availability flag.
     */
    EpicRtcBool _hasMinBitrateBps;

    /**
     * Optional maximum bitrate to use.
     */
    int32_t _maxBitrateBps;

    /**
     * Maximum bitrate availability flag.
     */
    EpicRtcBool _hasMaxBitrateBps;

    /**
     * Optional initial bitrate to use. By default uses 300,000.
     */
    int32_t _startBitrateBps;

    /**
     * Start bitrate availability flag.
     */
    EpicRtcBool _hasStartBitrateBps;
};

static_assert(sizeof(EpicRtcBitrate) == 24); 

struct EpicRtcPortAllocator
{
    /**
     * Minimum port to use when allocating ports. By default 49152 is used.
     */
    int32_t _minPort;

    /**
     * Minimum port to use when allocating ports. By default 49152 is used.
     */
    EpicRtcBool _hasMinPort;

    /**
     * Maximum port to use when allocating ports. By default 65535 is used.
     */
    int32_t _maxPort;

    /**
     * Maximum port to use when allocating ports. By default 65535 is used.
     */
    EpicRtcBool _hasMaxPort;

    /**
     * Port Allocator bit fields.
     */
    EpicRtcPortAllocatorOptions _portAllocation;
};

static_assert(sizeof(EpicRtcPortAllocator) == 20);

/**
 * Connection configuration object.
 */
struct EpicRtcConnectionConfig
{
    /**
     * List of ICE servers.
     */
    EpicRtcIceServerSpan _iceServers;

    /**
     * Optional port allocation.
     */
    EpicRtcPortAllocator _portAllocator;

    /**
     * Optional bitrates.
     */
    EpicRtcBitrate _bitrate;

    /**
     * ICE policy to use.
     */
    EpicRtcIcePolicy _iceConnectionPolicy;

    /**
     * Disables tcp candidates
     */
    EpicRtcBool _disableTcpCandidates;
};

static_assert(sizeof(EpicRtcConnectionConfig) == 64);  // Ensure EpicRtcConnectionConfig is expected size on all platforms

#pragma pack(pop)
