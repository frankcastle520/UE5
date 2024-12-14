// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumRange.h"

#include "LandscapeEditTypes.generated.h"

// APIs marked with LANDSCAPE_EDIT_LAYERS_BATCHED_MERGE_EXPERIMENTAL are experimental and therefore purposefully marked as UE_INTERNAL : 
//  These APIs are part of a large refactor of the landscape edit layers system (batched merge) that will eventually replace the previous global 
//  and local merge code paths but is still optional and therefore considered experimental for the time being. Since the API will likely evolve 
//  until then, use at your own risk.
#define LANDSCAPE_EDIT_LAYERS_BATCHED_MERGE_EXPERIMENTAL UE_INTERNAL

UENUM()
enum class ELandscapeToolTargetType : uint8
{
	Heightmap = 0,
	Weightmap = 1,
	Visibility = 2,
	Invalid = 3 UMETA(Hidden), // only valid for LandscapeEdMode->CurrentToolTarget.TargetType
	Count = Invalid UMETA(Hidden), // Only the elements above Invalid actually count as proper target types
};
ENUM_RANGE_BY_COUNT(ELandscapeToolTargetType, ELandscapeToolTargetType::Count);

enum class ELandscapeToolTargetTypeFlags : uint8
{
	None = 0,
	Heightmap = (1 << static_cast<uint8>(ELandscapeToolTargetType::Heightmap)),
	Weightmap = (1 << static_cast<uint8>(ELandscapeToolTargetType::Weightmap)),
	Visibility = (1 << static_cast<uint8>(ELandscapeToolTargetType::Visibility)),
	All = Heightmap | Weightmap | Visibility,
};
ENUM_CLASS_FLAGS(ELandscapeToolTargetTypeFlags);


namespace UE::Landscape
{

enum class EOutdatedDataFlags : uint8
{
	None = 0,

	// Actual flags : 
	GrassMaps = (1 << 0),
	PhysicalMaterials = (1 << 1),
	NaniteMeshes = (1 << 2),
	PackageModified = (1 << 3),

	// Not real flags, only useful to loop through the actual flags : 
	LastPlusOne, 
	Last = LastPlusOne - 1,

	// Combined flags :
	All = (GrassMaps | PhysicalMaterials | NaniteMeshes | PackageModified)
};
ENUM_CLASS_FLAGS(EOutdatedDataFlags);

inline uint32 GetOutdatedDataFlagIndex(EOutdatedDataFlags InFlag)
{
	const uint32 InFlagAsUInt32 = static_cast<uint32>(InFlag);
	check((InFlagAsUInt32 > static_cast<uint32>(EOutdatedDataFlags::None)) && (InFlagAsUInt32 < static_cast<uint32>(EOutdatedDataFlags::LastPlusOne)) && (FMath::CountBits(InFlagAsUInt32) == 1u));
	return FMath::CountTrailingZeros(InFlagAsUInt32);
}

} // namespace UE::Landscape