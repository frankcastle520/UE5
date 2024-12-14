// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "GDTF/AttributeDefinitions/DMXGDTFPhysicalUnit.h"

namespace UE::DMX
{
	struct FDMXControlConsolePhysicalUnitToUnitNameLabel
	{
		/** Returns the name label for the specified physical unit */
		static const FName& GetNameLabel(EDMXGDTFPhysicalUnit PhysicalUnit);

	private:
		/** A map to list all GDTF compliant Physical Units and their matching Physical Value default ranges. */
		static inline const TMap<EDMXGDTFPhysicalUnit, FName> PhysicalUnitToUnitNameLabelMap =
		{
			{ EDMXGDTFPhysicalUnit::None, "normal"},
			{ EDMXGDTFPhysicalUnit::Percent, "%" },
			{ EDMXGDTFPhysicalUnit::Length, "m"},
			{ EDMXGDTFPhysicalUnit::Mass, "kg" },
			{ EDMXGDTFPhysicalUnit::Time, "s" },
			{ EDMXGDTFPhysicalUnit::Temperature, "K" },
			{ EDMXGDTFPhysicalUnit::LuminousIntensity, "cd" },
			{ EDMXGDTFPhysicalUnit::Angle, "deg"},
			{ EDMXGDTFPhysicalUnit::Force, "N" },
			{ EDMXGDTFPhysicalUnit::Frequency, "Hz" },
			{ EDMXGDTFPhysicalUnit::Current, "A" },
			{ EDMXGDTFPhysicalUnit::Voltage, "V" },
			{ EDMXGDTFPhysicalUnit::Power, "W" },
			{ EDMXGDTFPhysicalUnit::Energy, "J" },
			{ EDMXGDTFPhysicalUnit::Area, "m2" },
			{ EDMXGDTFPhysicalUnit::Volume, "m3" },
			{ EDMXGDTFPhysicalUnit::Speed, "m/s" },
			{ EDMXGDTFPhysicalUnit::Acceleration, "m/s2" },
			{ EDMXGDTFPhysicalUnit::AngularSpeed, "deg/s" },
			{ EDMXGDTFPhysicalUnit::AngularAccc, "deg/s2" },
			{ EDMXGDTFPhysicalUnit::WaveLength, "nm" },
			{ EDMXGDTFPhysicalUnit::ColorComponent, "%" },
			{ EDMXGDTFPhysicalUnit::MaxEnumValue, "-" }
		};
	};
}
