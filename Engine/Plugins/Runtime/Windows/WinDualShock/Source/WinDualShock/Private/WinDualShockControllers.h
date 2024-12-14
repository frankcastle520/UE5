// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if DUALSHOCK4_SUPPORT

#include "WinDualShock.h"
#include LIBSCEPAD_PLATFORM_INCLUDE

THIRD_PARTY_INCLUDES_START
	#include <pad.h>
	#include <pad_audio.h>
THIRD_PARTY_INCLUDES_END

class FWinDualShockControllers : public FPlatformControllers
{
public:

	FWinDualShockControllers()
		: FPlatformControllers()
	{
	}

	virtual ~FWinDualShockControllers()
	{
	}

	void SetAudioGain(float InPadSpeakerGain, float InHeadphonesGain, float InMicrophoneGain, float InOutputGain)
	{
		PadSpeakerGain = InPadSpeakerGain;
		HeadphonesGain = InHeadphonesGain;
		MicrophoneGain = InMicrophoneGain;
		OutputGain = InOutputGain;
		for (int32 UserIndex = 0; UserIndex < SCE_USER_SERVICE_MAX_LOGIN_USERS; UserIndex++)
		{
			bGainChanged[UserIndex] = true;
		}
	}

	float GetOutputGain()
	{
		return OutputGain;
	}

	bool GetSupportsAudio(int32 UserIndex) const
	{
		return bSupportsAudio[UserIndex];
	}

	void RefreshControllerType(int32 UserIndex)
	{
		ControllerTypeIdentifiers[UserIndex] = GetControllerType(UserIndex);
	}

private:
	float OutputGain = 1.0f;
};

#endif // DUALSHOCK4_SUPPORT
