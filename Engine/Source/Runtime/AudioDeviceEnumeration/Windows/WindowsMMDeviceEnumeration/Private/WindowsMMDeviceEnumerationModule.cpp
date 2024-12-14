// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "HAL/Platform.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"

THIRD_PARTY_INCLUDES_START
// Linkage to define  Windows PKEY GUIDs included by Notification/DeviceInfoCache, otherwise they are unresolved extern.
#include <initguid.h>
THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformTypes.h"
#endif // PLATFORM_WINDOWS

#include "WindowsMMNotificationClient.h"
#include "WindowsMMDeviceInfoCache.h"
#include "WindowsMMDeviceEnumerationLog.h"

DEFINE_LOG_CATEGORY(LogAudioEnumeration);

namespace Audio
{
	class FWindowsMMDeviceEnumerationModule : public IModuleInterface
	{
	public:
		virtual void StartupModule() override
		{
		}

		virtual void ShutdownModule() override
		{
		}
	};
}

IMPLEMENT_MODULE(Audio::FWindowsMMDeviceEnumerationModule, WindowsMMDeviceEnumeration)
