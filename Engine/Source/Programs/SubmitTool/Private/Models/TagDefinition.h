// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TagDefinition.generated.h"


USTRUCT()
struct FTagValidationConfig
{
	GENERATED_BODY()

	FTagValidationConfig() = default;

	UPROPERTY()
	FString RegexValidation = FString();

	UPROPERTY()
	FString RegexErrorMessage = FString();

	UPROPERTY()
	bool bIsMandatory = false;
};

USTRUCT()
struct FTagValidationOverride
{
	GENERATED_BODY()
	
	UPROPERTY()
	FString RegexPath;

	UPROPERTY()
	FTagValidationConfig ConfigOverride;
};

USTRUCT()
struct FTagDefinition
{
	GENERATED_BODY()

	FTagDefinition() = default;

	inline const FString& GetTagId() const { return TagId; }

	UPROPERTY()
	FString TagId;

	UPROPERTY()
	FString TagLabel;

	UPROPERTY()
	FString ToolTip = FString(TEXT("There is no defined documentation for this tag."));

	UPROPERTY()
	FString DocumentationUrl;

	UPROPERTY()
	FString InputType;

	UPROPERTY()
	FString InputSubType;

	UPROPERTY()
	FString ValueDelimiter = TEXT(", ");	

	UPROPERTY()
	int32 MinValues = 0;

	UPROPERTY()
	int32 MaxValues = UINT8_MAX;

	UPROPERTY()
	FTagValidationConfig Validation = FTagValidationConfig();
	
	UPROPERTY()
	TArray<FTagValidationOverride> ValidationOverrides;

	UPROPERTY()
	int32 OrdinalOverride = 0;

	UPROPERTY()
	bool bIsDisabled = false;

	UPROPERTY()
	bool bIsUserValue = false;

	UPROPERTY()
	TArray<FString> SelectValues;

	UPROPERTY()
	TArray<FString> Filters;
};
