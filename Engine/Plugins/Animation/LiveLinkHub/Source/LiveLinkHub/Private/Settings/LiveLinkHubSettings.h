// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "Misc/FrameRate.h"

#include "LiveLinkHubSettings.generated.h"

/**
 * Settings for LiveLinkHub.
 */
UCLASS(config=Engine, defaultconfig)
class LIVELINKHUB_API ULiveLinkHubSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Parse templates and set example output fields. */
	void CalculateExampleOutput();
	
protected:
	//~ Begin UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface
	
public:
	/** If enabled, discovered clients will be automatically added to the current session. */
	UPROPERTY(config, EditAnywhere, Category="LiveLinkHub", DisplayName = "Automatically add discovered clients")
	bool bAutoAddDiscoveredClients = true;

	/** The size in megabytes to buffer when streaming a recording. */
	UPROPERTY(config, EditAnywhere, Category="LiveLinkHub", meta = (ClampMin = "1", UIMin = "1"))
	int32 FrameBufferSizeMB = 100;

	/** Number of frames to buffer at once. */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="LiveLinkHub", meta = (ClampMin = "2", UIMin = "2"))
	int32 BufferBatchSize = 5;

	/** Maximum number of frame ranges to store in history while scrubbing. Increasing can make scrubbing faster but temporarily use more memory. */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="LiveLinkHub")
	int32 MaxBufferRangeHistory = 25;
	
	/** Which project settings sections to display when opening the settings viewer. */
	UPROPERTY(config)
	TArray<FName> ProjectSettingsToDisplay;

	/**
	 * - Experimental - If this is disabled, LiveLinkHub's LiveLink Client will tick outside of the game thread.
	 * This allows processing LiveLink frame snapshots without the risk of being blocked by the game / ui thread.
	 * Note that this should only be relevant for virtual subjects since data is already forwarded to UE outside of the game thread.
	 */
	UPROPERTY(config, EditAnywhere, Category = "LiveLinkHub", meta = (ConfigRestartRequired = true))
	bool bTickOnGameThread = false;

	/** Target framerate for ticking LiveLinkHub. */
	UPROPERTY(config, EditAnywhere, Category="LiveLinkHub", meta = (ConfigRestartRequired = true, ClampMin="15.0"))
	float TargetFrameRate = 60.0f;

	/** Maximum time in seconds to wait for sources to clean up. */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="LiveLinkHub", meta = (ClampMin="0.0"))
	float SourceMaxCleanupTime = 0.25f;
	
	/** The filename template to use when creating recordings. */
	UPROPERTY(config, EditAnywhere, Category="Templates")
	FString FilenameTemplate = TEXT("NewLiveLinkRecording");

	/** Example parsed output of the template. */
	UPROPERTY(VisibleAnywhere, Category="Templates", DisplayName="Output")
	FString FilenameOutput;

	/** Placeholder for a list of the automatic tokens, set from the customization. */
	UPROPERTY(VisibleAnywhere, Category="Templates")
	FText AutomaticTokens;
};
