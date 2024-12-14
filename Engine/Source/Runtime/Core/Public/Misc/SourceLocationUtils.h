// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/UnrealString.h"
#include "Misc/SourceLocation.h"
#include "Misc/StringBuilder.h"

namespace UE::SourceLocation
{
	/**
	 * Returns an owning string that contains function name as well as file, line and column
	 */
	static inline FString ToFullString(const FSourceLocation& Location)
	{
#if UE_INCLUDE_SOURCE_LOCATION
		return FString(WriteToString<512>(
			Location.GetFileName(), "(",
			Location.GetLine(), ":",
			Location.GetColumn(), ") ",
			Location.GetFunctionName()).ToView());
#else
		return FString();
#endif
	}

	/**
	 * Returns an owning string that contains Source filename and line.
	 * Equvivalent to UE_SOURCE_LOCATION
	 */
	static inline FString ToFileAndLineString(const FSourceLocation& Location)
	{
#if UE_INCLUDE_SOURCE_LOCATION
		return FString(WriteToString<300>(
			Location.GetFileName(), "(", Location.GetLine(), ")").ToView());
#else
		return FString();
#endif
	}
}