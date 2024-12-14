// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "HAL/Platform.h"
#include "UObject/ObjectMacros.h"
#include "SoftsSimulationSpace.generated.h"

UENUM()
enum struct EChaosSoftsSimulationSpace : uint8
{
	// World space
	WorldSpace = 0,

	// Component space
	ComponentSpace,

	// Top level bone for each cloth.
	ReferenceBoneSpace,
};