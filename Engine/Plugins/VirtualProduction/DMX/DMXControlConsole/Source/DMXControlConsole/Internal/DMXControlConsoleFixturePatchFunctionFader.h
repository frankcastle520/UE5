// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXAttribute.h"
#include "DMXControlConsoleFaderBase.h"
#include "GDTF/AttributeDefinitions/DMXGDTFPhysicalUnit.h"

#include "DMXControlConsoleFixturePatchFunctionFader.generated.h"

struct FDMXAttributeName;
struct FDMXFixtureFunction;


/** A fader matching a Fixture Patch Function in the DMX Control Console. */
UCLASS()
class DMXCONTROLCONSOLE_API UDMXControlConsoleFixturePatchFunctionFader
	: public UDMXControlConsoleFaderBase
{
	GENERATED_BODY()

public:
	/** Sets Fader's properties values using the given Fixture Function */
	void SetPropertiesFromFixtureFunction(const FDMXFixtureFunction& FixtureFunction, const int32 InUniverseID, const int32 StartingChannel);

	/** Returns the name of the attribute mapped to this Fader */
	const FDMXAttributeName& GetAttributeName() const { return Attribute; }

	/** Returns the physical unit of this Fader */
	EDMXGDTFPhysicalUnit GetPhysicalUnit() const { return PhysicalUnit; }

	/** Returns the physical value lower boundary of this Fader */
	double GetPhysicalFrom() const { return PhysicalFrom; }

	/** Returns the physical value upper boundary of this Fader */
	double GetPhysicalTo() const { return PhysicalTo; }

	/** Returns the physical value of this Fader */
	double GetPhysicalValue() const;

	// Property Name getters
	FORCEINLINE static FName GetAttributePropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFixturePatchFunctionFader, Attribute); }
	
private:
	/** The Attribute of the Function this Fader is based on */
	UPROPERTY(VisibleAnywhere, meta = (DisplayName = "Attribute Mapping", DisplayPriority = "2"), Category = "DMX Fader")
	FDMXAttributeName Attribute;

	/** The Physical Unit of the Function this Fader is based on */
	UPROPERTY()
	EDMXGDTFPhysicalUnit PhysicalUnit = EDMXGDTFPhysicalUnit::None;

	/** The lower boundary of the Physical Value range of the Function this Fader is based on */
	UPROPERTY()
	double PhysicalFrom = 0.0;

	/** The upper boundary of the Physical Value range of the Function this Fader is based on */
	UPROPERTY()
	double PhysicalTo = 1.0;
};