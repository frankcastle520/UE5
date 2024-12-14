// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

struct CAMERACALIBRATIONCORE_API FLensFileObjectVersion
{
	enum Type
	{
		BeforeCustomVersionWasAdded = 0,
		EditableFocusCurves,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
	
	const static FGuid GUID;
	
	FLensFileObjectVersion() = delete;
};