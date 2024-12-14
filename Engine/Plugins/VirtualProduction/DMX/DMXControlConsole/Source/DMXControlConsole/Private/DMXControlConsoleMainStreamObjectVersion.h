// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

// Custom serialization version for changes to DMX Control Console Objects in the //Main Stream
struct FDMXControlConsoleMainStreamObjectVersion
{
	enum Type
	{
		// Roughly corresponds to 5.5
		BeforeCustomVersionWasAdded = 0,

		// 5.5 Upgrade Control Console Fader Group to use a Fixture Patch Ref instead of a Soft Object Ptr
		DMXControlConsoleFaderGroupUsesFixturePatchRef,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FDMXControlConsoleMainStreamObjectVersion() {}
};
