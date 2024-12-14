// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

struct FGooglePlayLeaderboardScore
{
	// Leaderboard ID as shown in GooglePlay Console
	FString GooglePlayLeaderboardId;
	// Leaderboard score
	int64 Score = 0;
};
