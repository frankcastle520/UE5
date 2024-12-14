// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "LiveLinkHubTemplateTokens.generated.h"

/**
 * Predefined tokens used in template parsing. Set up as a USTRUCT so the properties and tooltips
 * can be easily iterated and displayed to the user.
 */
USTRUCT()
struct FLiveLinkHubAutomaticTokens
{
	GENERATED_BODY()

	/** Two-digit year. */
	UPROPERTY()
	FString Year2Digit = TEXT("yy");
	
	/** Four-digit year. */
	UPROPERTY()
	FString Year4Digit = TEXT("yyyy");
	
	/** Two-digit month. */
	UPROPERTY()
	FString Month = TEXT("MM");
	
	/** Two-digit day. */
	UPROPERTY()
	FString Day = TEXT("dd");
	
	/** Two-digit hour. */
	UPROPERTY()
	FString Hour = TEXT("hh");
	
	/** Two-digit minute. */
	UPROPERTY()
	FString Minute = TEXT("mm");

	/** The current session name. */
	UPROPERTY()
	FString SessionName = TEXT("sessionName");

	/** Singleton like access to this ustruct. */
	static FLiveLinkHubAutomaticTokens& GetStaticTokens()
	{
		static FLiveLinkHubAutomaticTokens AutomaticTokens;
		return AutomaticTokens;
	}
};

namespace UE::LiveLinkHub::Tokens::Private
{
	/** Return the token formatted with curly brackets. */
	inline FString CreateToken(const FString& InString)
	{
		return FString::Printf(TEXT("{%s}"), *InString);
	}
}