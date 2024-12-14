// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace UE::Insights
{

class TRACEINSIGHTSCORE_API FMiscUtils
{
public:
	/**
	 * Starts a new Unreal Insights instance.
	 * @param InCmdLine - The command line passed to the new UnrealInsights.exe process
	 * @returns true if UnrealInsights process is created successfully.
	 */
	static bool OpenUnrealInsights(const TCHAR* InCmdLine = nullptr);
};

} // namespace UE::Insights
