// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsolePhysicalUnitToUnitNameLabel.h"

namespace UE::DMX
{
	const FName& FDMXControlConsolePhysicalUnitToUnitNameLabel::GetNameLabel(EDMXGDTFPhysicalUnit PhysicalUnit)
	{
		return PhysicalUnitToUnitNameLabelMap.FindChecked(PhysicalUnit);
	}
}
