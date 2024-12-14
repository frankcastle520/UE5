// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "UObject/Object.h"

#include "VCamStringPrompt.generated.h"

/** Result of a request for string input from a VCam streaming client */
UENUM(BlueprintType)
enum class EVCamStringPromptResult : uint8
{
	/** The user submitted a string, which is contained in the response's Input field */
	Submitted,

	/** The user cancelled the string prompt */
	Cancelled,

	/** The user disconnected before responding to the string prompt */
	Disconnected,

	/** String prompts are not available in the current VCam configuration */
	Unavailable
};

/** Response to a request for string input from a VCam streaming client */
USTRUCT(BlueprintType)
struct VCAMCORE_API FVCamStringPromptResponse
{
	GENERATED_BODY()

	FVCamStringPromptResponse() {}

	FVCamStringPromptResponse(EVCamStringPromptResult Result, FString Entry = "")
		: Result(Result), Entry(Entry)
	{}

	/** The result of the string prompt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = VCam)
	EVCamStringPromptResult Result = EVCamStringPromptResult::Unavailable;

	/** The string that the user provided. Empty if the result is not Submitted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = VCam)
	FString Entry = "";
};

/** Request for string input which will be sent to a VCam streaming client */
USTRUCT(BlueprintType)
struct VCAMCORE_API FVCamStringPromptRequest
{
	GENERATED_BODY()

	/** The default value to show in the client's text input form */
	FString DefaultValue;

	/** The title to show in the client's text input form */
	FString PromptTitle;
};