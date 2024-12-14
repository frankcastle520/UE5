// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkSourceSettings.h"

#include "LiveLinkHubPlaybackSourceFactory.h"
#include "LiveLinkSourceFactory.h"

#include "LiveLinkHubPlaybackSourceSettings.generated.h"

/** Settings class for playback sources, which provides the factory class for them. */
UCLASS()
class ULiveLinkHubPlaybackSourceSettings : public ULiveLinkSourceSettings
{
public:
	GENERATED_BODY()

	ULiveLinkHubPlaybackSourceSettings()
	{
		Factory = ULiveLinkHubPlaybackSourceFactory::StaticClass();
	}
};
