// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrimitiveSceneInfoData.h"
#include "GameFramework/Actor.h"

/** Next id to be used by a component. */
// 0 is reserved to mean invalid
FThreadSafeCounter FPrimitiveSceneInfoData::NextPrimitiveId;

void FPrimitiveSceneInfoData::SetLastRenderTime(float InLastRenderTime, bool bUpdateLastRenderTimeOnScreen) const
{
	LastRenderTime = InLastRenderTime;

	if (bUpdateLastRenderTimeOnScreen)
	{
		LastRenderTimeOnScreen = InLastRenderTime;
	}

	if (OwnerLastRenderTimePtr)
	{
		OwnerLastRenderTimePtr->LastRenderTime = InLastRenderTime;
	}
}