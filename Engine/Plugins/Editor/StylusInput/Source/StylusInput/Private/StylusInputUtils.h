// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <Logging/LogMacros.h>

namespace UE::StylusInput::Private
{
	DECLARE_LOG_CATEGORY_EXTERN(LogStylusInput, Log, All)

	inline void LogError(const FString& Preamble, const FString& Message)
	{
		UE_LOG(LogStylusInput, Error, TEXT("%s: %s"), *Preamble, *Message);
	}

	inline void LogWarning(const FString& Preamble, const FString& Message)
	{
		UE_LOG(LogStylusInput, Warning,  TEXT("%s: %s"), *Preamble, *Message);
	}

	inline void LogVerbose(const FString& Preamble, const FString& Message)
	{
		UE_LOG(LogStylusInput, Verbose,  TEXT("%s: %s"), *Preamble, *Message);
	}
}
