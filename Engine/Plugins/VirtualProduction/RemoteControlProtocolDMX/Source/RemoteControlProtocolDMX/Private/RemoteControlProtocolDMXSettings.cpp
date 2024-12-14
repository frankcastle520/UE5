// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolDMXSettings.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FSimpleMulticastDelegate URemoteControlProtocolDMXSettings::OnRemoteControlProtocolDMXSettingsChangedDelegate_DEPRECATED;

FGuid URemoteControlProtocolDMXSettings::GetOrCreateDefaultInputPortId()
{
	// DEPRECATED 5.5

	return FGuid();
}

FSimpleMulticastDelegate& URemoteControlProtocolDMXSettings::GetOnRemoteControlProtocolDMXSettingsChanged()
{
	return OnRemoteControlProtocolDMXSettingsChangedDelegate_DEPRECATED;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
