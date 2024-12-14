// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaRemoteProxyPlayableGroup.h"

bool UAvaRemoteProxyPlayableGroup::ConditionalBeginPlay(const FAvaInstancePlaySettings& InWorldPlaySettings)
{
	if (!bIsPlaying)
	{
		bIsPlaying = true;
		return true;
	}
	return false;
}

void UAvaRemoteProxyPlayableGroup::RequestEndPlayWorld(bool bInForceImmediate)
{
	bIsPlaying = false;
}