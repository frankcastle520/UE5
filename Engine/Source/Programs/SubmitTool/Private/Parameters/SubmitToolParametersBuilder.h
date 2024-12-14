// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Misc/ConfigCacheIni.h"
#include "SubmitToolParameters.h"

class FSubmitToolParametersBuilder
{
public:
	FSubmitToolParametersBuilder(const FString& InParametersXmlFile);
	FSubmitToolParameters Build();

private:
	TArray<FConfigLayer> ConfigHierarchy;
	TArray<FString> ProjectNames;
	FConfigFile* SubmitToolConfig;
private:
	FGeneralParameters BuildGeneralParameters();
	FJiraParameters BuildJiraParameters(); 
	FTelemetryParameters GetTelemetryParameters();
	FIntegrationParameters BuildIntegrationParameters();
	TArray<FTagDefinition> BuildAvailableTags();
	TMap<FString, FString> BuildValidators();
	TMap<FString, FString> BuildPresubmitOperations();
	FCopyLogParameters BuildCopyLogParameters();
	FP4LockdownParameters BuildP4LockdownParameters();
	FOAuthTokenParams BuildOAuthParameters();
	FIncompatibleFilesParams BuildIncompatibleFilesParameters();
	FHordeParameters BuildHordeParameters();
	FAutoUpdateParameters BuildAutoUpdateParameters();

	
	FString SectionToText(const FConfigSection& InSection) const;
};
