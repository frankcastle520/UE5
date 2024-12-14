// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace UE::Insights
{

/**
 * Utility class used by profiler managers to limit how often they check for availability conditions.
 */
class TRACEINSIGHTSCORE_API FAvailabilityCheck
{
public:
	/** Returns true if managers are allowed to do (slow) availability check during this tick. */
	bool Tick();

	/** Disables the "availability check" (i.e. Tick() calls will return false when disabled). */
	void Disable();

	/** Enables the "availability check" with a specified initial delay. */
	void Enable(double InWaitTime);

private:
	double WaitTime = 0.0;
	uint64 NextTimestamp = (uint64)-1;
};

} // namespace UE::Insights
