// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPlacementModeModule.h"

struct FBspBuilderType;

class FBspDragHandler : public FPlaceableItem::FDragHandler
{
public:

	FBspDragHandler();
	void Initialize( TSharedRef<FBspBuilderType> InBspBuilder );
};
