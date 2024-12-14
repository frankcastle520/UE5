// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPhysicalUnitToDefaultValueRange.h"

namespace UE::DMX
{
	const TPair<double, double>& FDMXPhysicalUnitToDefaultValueRange::GetValueRange(EDMXGDTFPhysicalUnit PhysicalUnit)
	{
		return PhysicalUnitToValueRangeMap.FindChecked(PhysicalUnit);
	}
}
