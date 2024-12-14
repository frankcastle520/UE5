// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "AudioInsightsEditorSettings.generated.h"

UCLASS(config = EditorPerProjectUserSettings)
class AUDIOINSIGHTSEDITOR_API UAudioInsightsEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:
	virtual FName GetCategoryName() const override;
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;

	/** Whether to automatically set the first PIE client in Audio Insights World Filter. */
	UPROPERTY(Config, EditAnywhere, Category="World Filter")
	bool bWorldFilterDefaultsToFirstClient = false;
};
