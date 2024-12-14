// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace UE::ProjectUtilities
{
	/**
	 * Allows a monolithic target to control the game directory by the presence of commandline arguments for the project.
	 */
	PROJECTS_API void ParseProjectDirFromCommandline(int32 ArgC, TCHAR* ArgV[]);
}
