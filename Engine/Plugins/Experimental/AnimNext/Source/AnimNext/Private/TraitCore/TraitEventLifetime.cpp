// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraitCore/TraitEventLifetime.h"

namespace UE::AnimNext
{
	bool FTraitEventLifetime::Decrement()
	{
		if (IsInfinite())
		{
			// Event has infinite duration, we never expire
			return false;
		}
		else if (IsExpired())
		{
			// Event has already expired
			return true;
		}

		LifetimeCount--;
		return LifetimeCount == 0;
	}
}
