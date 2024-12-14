// Copyright Epic Games, Inc. All Rights Reserved.

#include "Insights/ViewModels/BaseTimingTrack.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "BaseTimingTrack"

INSIGHTS_IMPLEMENT_RTTI(FBaseTimingTrack)

// start auto generated ids from a big number (MSB set to 1) to avoid collisions with ids for GPU/CPU tracks based on 32bit timeline index
uint64 FBaseTimingTrack::IdGenerator = (1ULL << 63);

void FBaseTimingTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
}

#undef LOCTEXT_NAMESPACE