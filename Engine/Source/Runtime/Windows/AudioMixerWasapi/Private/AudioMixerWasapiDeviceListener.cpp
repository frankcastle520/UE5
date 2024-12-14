// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerWasapi.h"
#include "AudioMixer.h"
#include "AudioDeviceNotificationSubsystem.h"
#include "Misc/ScopeRWLock.h"

#include <atomic>

#if PLATFORM_WINDOWS

#include "Microsoft/COMPointer.h"
#include "ScopedCom.h"					// FScopedComString

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
// Linkage for  Windows GUIDs included by Notification/DeviceInfoCache, otherwise they are extern.
#include <initguid.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

#include "WindowsMMNotificationClient.h"
#include "WindowsMMDeviceInfoCache.h"
#include "WindowsMMStringUtils.h"

namespace Audio
{	
	TSharedPtr<FWindowsMMNotificationClient> WasapiWinNotificationClient;

	void FAudioMixerWasapi::RegisterDeviceChangedListener()
	{
		if (!WasapiWinNotificationClient.IsValid())
		{
			// Shared (This is a COM object, so we don't delete it, just derecement the ref counter).
			WasapiWinNotificationClient = TSharedPtr<FWindowsMMNotificationClient>(
				new FWindowsMMNotificationClient, 
				[](FWindowsMMNotificationClient* InPtr) { InPtr->ReleaseClient(); }
			);
		}
		if (!DeviceInfoCache.IsValid())
		{
			// Setup device info cache.
			DeviceInfoCache = MakeUnique<FWindowsMMDeviceCache>();
			WasapiWinNotificationClient->RegisterDeviceChangedListener(static_cast<FWindowsMMDeviceCache*>(DeviceInfoCache.Get()));
		}

		WasapiWinNotificationClient->RegisterDeviceChangedListener(this);
	}

	void FAudioMixerWasapi::UnregisterDeviceChangedListener()
	{
		if (WasapiWinNotificationClient.IsValid())
		{
			if (DeviceInfoCache.IsValid())
			{
				// Unregister and kill cache.
				WasapiWinNotificationClient->UnRegisterDeviceDeviceChangedListener(static_cast<FWindowsMMDeviceCache*>(DeviceInfoCache.Get()));
				
				DeviceInfoCache.Reset();
			}
			
			WasapiWinNotificationClient->UnRegisterDeviceDeviceChangedListener(this);
		}
	}

	FString FAudioMixerWasapi::GetDeviceId() const
	{
		return AudioStreamInfo.DeviceInfo.DeviceId;
	}

	TComPtr<IMMDevice> FAudioMixerWasapi::GetMMDevice(const FString& InDeviceID) const
	{
		if (WasapiWinNotificationClient)
		{
			return WasapiWinNotificationClient->GetDevice(InDeviceID);
		}

		return TComPtr<IMMDevice>();
	}
}

#else 
namespace Audio
{
	void FAudioMixerWasapi::RegisterDeviceChangedListener() {}
	void FAudioMixerWasapi::UnregisterDeviceChangedListener() {}
	FString FAudioMixerWasapi::GetDeviceId() const
	{
		return AudioStreamInfo.DeviceInfo.DeviceId;
	}

	TComPtr<IMMDevice> FAudioMixerWasapi::GetMMDevice(const FString& InDeviceID) const
	{
		return TComPtr<IMMDevice>();
	}
}
#endif

