// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixer.h"
#include "HAL/Platform.h"

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"

THIRD_PARTY_INCLUDES_START

#include <mmdeviceapi.h>			// IMMNotificationClient
#include <audiopolicy.h>			// IAudioSessionEvents

THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformTypes.h"

namespace Audio
{
#if PLATFORM_WINDOWS
	WINDOWSMMDEVICEENUMERATION_API const TCHAR* ToString(AudioSessionDisconnectReason InDisconnectReason);
	WINDOWSMMDEVICEENUMERATION_API const TCHAR* ToString(ERole InRole);
	WINDOWSMMDEVICEENUMERATION_API const TCHAR* ToString(EDataFlow InFlow);
	WINDOWSMMDEVICEENUMERATION_API FString ToFString(const PROPERTYKEY Key);
#endif //PLATFORM_WINDOWS

	WINDOWSMMDEVICEENUMERATION_API FString ToFString(const TArray<EAudioMixerChannel::Type>& InChannels);

	WINDOWSMMDEVICEENUMERATION_API const TCHAR* ToString(EAudioDeviceRole InRole);
	WINDOWSMMDEVICEENUMERATION_API const TCHAR* ToString(EAudioDeviceState InState);
}
 
#endif //PLATFORM_WINDOWS
