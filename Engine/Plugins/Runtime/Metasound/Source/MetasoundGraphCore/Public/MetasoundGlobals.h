// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreGlobals.h"
#include "Misc/Build.h"

namespace Metasound
{
	// Returns true if a MetaSound is expected to execute, false if not.
	METASOUNDGRAPHCORE_API bool CanEverExecuteGraph(bool bIsCooking = false);
} // namespace Metasound
