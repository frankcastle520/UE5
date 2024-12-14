// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleFixturePatchFunctionFader.h"

#include "Library/DMXEntityFixtureType.h"


void UDMXControlConsoleFixturePatchFunctionFader::SetPropertiesFromFixtureFunction(const FDMXFixtureFunction& FixtureFunction, const int32 InUniverseID, const int32 StartingChannel)
{
	// Order of initialization matters
	FaderName = FixtureFunction.Attribute.Name.ToString();
	Attribute = FixtureFunction.Attribute;

	SetUniverseID(InUniverseID);

	StartingAddress = StartingChannel + (FixtureFunction.Channel - 1);
	DefaultValue = FixtureFunction.DefaultValue;
	Value = DefaultValue;
	MinValue = 0;

#if WITH_EDITOR
	PhysicalUnit = FixtureFunction.GetPhysicalUnit();
	PhysicalFrom = FixtureFunction.GetPhysicalFrom();
	PhysicalTo = FixtureFunction.GetPhysicalTo();
#endif // WITH_EDITOR

	SetDataType(FixtureFunction.DataType);

	bUseLSBMode = FixtureFunction.bUseLSBMode;
}

double UDMXControlConsoleFixturePatchFunctionFader::GetPhysicalValue() const
{
	const uint32 ValueRange = MaxValue - MinValue;
	const double NormalizedValue = static_cast<double>(Value - MinValue) / ValueRange;

	const double PhysicalValueRange = PhysicalTo - PhysicalFrom;
	const double PhysicalValue = PhysicalFrom + NormalizedValue * PhysicalValueRange;
	return PhysicalValue;
}
