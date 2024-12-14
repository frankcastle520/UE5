// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "Tags/ScriptableToolGroupSet.h"
#include "Templates/SharedPointer.h"

#include "ScriptableToolsEditorModeSettings.generated.h"


UCLASS(config = Editor)
class SCRIPTABLETOOLSEDITORMODE_API UScriptableToolsModeCustomizationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// UDeveloperSettings overrides

	virtual FName GetContainerName() const { return FName("Project"); }
	virtual FName GetCategoryName() const { return FName("Plugins"); }
	virtual FName GetSectionName() const { return FName("ScriptableTools"); }

	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;

public:

	UPROPERTY(config, EditAnywhere, Category = "Scriptable Tools Mode|Tool Registration", meta = (EditCondition = "!bRegisterAllTools"))
	FScriptableToolGroupSet ToolRegistrationFilters;

	bool RegisterAllTools() const {	return ToolRegistrationFilters.GetGroups().IsEmpty(); }

};