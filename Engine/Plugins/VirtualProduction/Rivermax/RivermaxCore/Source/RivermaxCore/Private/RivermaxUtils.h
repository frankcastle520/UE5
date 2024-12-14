// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RivermaxTypes.h"

namespace UE::RivermaxCore
{
	struct FRivermaxOutputStreamOptions;
}

namespace UE::RivermaxCore::Private::Utils
{	
	/** Various constants used for stream initialization */

	static constexpr uint32 FullHDHeight = 1080;
	static constexpr uint32 FullHDWidth = 1920;

	/** Maximum payload size in bytes that the plugin can send based on UDP max size and RTP header.  */
	static constexpr uint32 MaxPayloadSize = 1420;
	
	/** Smallest payload size (bytes) to use as a lower bound in search for a payload that can be equal across a line */
	static constexpr uint32 MinPayloadSize = 600;

	/** SMTPE 2110-10.The Media Clock and RTP Clock rate for streams compliant to this standard shall be 90 kHz. */
	static constexpr double MediaClockSampleRate = 90000.0;
	
	/** Common sleep time used in places where we are waiting for something to complete */
	static constexpr float SleepTimeSeconds = 50 * 1E-6;

	/** Convert a set of streaming option to its SDP description. Currently only support video type. */
	void StreamOptionsToSDPDescription(const UE::RivermaxCore::FRivermaxOutputStreamOptions& Options, float RateMultiplier, FAnsiStringBuilderBase& OutSDPDescription);

	/**
	 * Converts a timestamp in MediaClock period units to a frame number for a given frame rate
	 * 2110-20 streams uses a standard media clock rate of 90kHz
	 */
	uint32 TimestampToFrameNumber(uint32 Timestamp, const FFrameRate& FrameRate);
}
