// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VVMVerseEffectSet.generated.h"

/**
 * Mirrors uLang::SEffectSet for UE serialization
 */
UENUM(Flags)
enum class EVerseEffectSet : uint8
{
	None = 0,
	Suspends = 1 << 0,
	Decides = 1 << 1,
	Diverges = 1 << 2,
	Reads = 1 << 3,
	Writes = 1 << 4,
	Allocates = 1 << 5,
	NoRollback = 1 << 6,
};
ENUM_CLASS_FLAGS(EVerseEffectSet);
