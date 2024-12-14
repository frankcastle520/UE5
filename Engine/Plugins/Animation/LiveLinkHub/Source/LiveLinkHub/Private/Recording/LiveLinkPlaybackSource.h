// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILiveLinkSource.h"
#include "LiveLinkSourceSettings.h"

class ILiveLinkClient;

/**
 * Completely empty "source" displayed in the UI when playing back a recording.
 */
class FLiveLinkPlaybackSource : public ILiveLinkSource
{
public:
	FLiveLinkPlaybackSource() = default;

	FLiveLinkPlaybackSource(const FString& OriginalSourceName)
	{
		SourceName = *FString::Printf(TEXT("Playback (%s)"), *OriginalSourceName);
	}

	virtual ~FLiveLinkPlaybackSource() = default;

	//~ Begin ILiveLinkSource interface
	virtual bool CanBeDisplayedInUI() const override
	{
		return true;
	}
	
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override
	{
	}
	
	virtual void InitializeSettings(ULiveLinkSourceSettings* Settings) override
	{
	}
	
	virtual bool IsSourceStillValid() const override
	{
		return true; 
	}
	
	virtual bool RequestSourceShutdown() override
	{
		return true; 
	}

	virtual FText GetSourceType() const override
	{
		return FText::FromName(SourceName);
	}
	
	virtual FText GetSourceMachineName() const override
	{
		return FText();
	}
	
	virtual FText GetSourceStatus() const override
	{
		return NSLOCTEXT("LiveLinkPlaybackSource", "PlaybackSourceStatus", "Playback");
	}
	//~ End ILiveLinkSource interface

protected:
	/** Source name. */
	FName SourceName;
};
