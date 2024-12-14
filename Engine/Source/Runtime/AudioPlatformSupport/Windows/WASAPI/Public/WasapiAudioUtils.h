// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

THIRD_PARTY_INCLUDES_START
#include <AudioClient.h>
THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"


namespace Audio
{
	/**
	 * FWasapiAudioUtils - WASAPI utility class
	 */
	class AUDIOPLATFORMSUPPORTWASAPI_API FWasapiAudioUtils
	{
	public:
		/** FramesToRefTime - Converts a given number of frames at the given sample rate to REFERENCE_TIME. */
		static REFERENCE_TIME FramesToRefTime(const uint32 InNumFrames, const uint32 InSampleRate);

		/** RefTimeToFrames - Converts the given REFERENCE_TIME to a number of frames at the given sample rate. */
		static uint64 RefTimeToFrames(const REFERENCE_TIME InRefTime, const uint32 InSampleRate);
	};
}
