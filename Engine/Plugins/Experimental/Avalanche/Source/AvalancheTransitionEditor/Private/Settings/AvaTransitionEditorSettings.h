// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"
#include "AvaTransitionEditorSettings.generated.h"

class UAvaTransitionTree;
class UAvaTransitionTreeEditorData;

UCLASS(config=EditorPerProjectUserSettings, meta=(DisplayName="Transition Logic"))
class UAvaTransitionEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAvaTransitionEditorSettings();

	UAvaTransitionTreeEditorData* LoadDefaultTemplateEditorData() const;

private:
	/** The template to use when building new transition trees */
	UPROPERTY(Config, EditAnywhere, Category="Motion Design")
	TSoftObjectPtr<UAvaTransitionTree> DefaultTemplate;
};
