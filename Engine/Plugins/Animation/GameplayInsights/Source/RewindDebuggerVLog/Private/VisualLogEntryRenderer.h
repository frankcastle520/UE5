// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class FVisualLogEntryRenderer
{
public:
	static void RenderLogEntry(class UWorld* World, const struct FVisualLogEntry& Entry, TFunctionRef<bool(const FName&, ELogVerbosity::Type)> MatchCategoryFilters,
		class UCanvas* Canvas, class UFont* Font, class UFont* MonospaceFont, int32& ScreenTextY);
};
