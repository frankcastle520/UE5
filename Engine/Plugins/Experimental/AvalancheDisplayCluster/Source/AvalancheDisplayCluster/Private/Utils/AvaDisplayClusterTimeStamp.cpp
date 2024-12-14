// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaDisplayClusterTimeStamp.h"

#include "CoreGlobals.h"

FAvaDisplayClusterTimeStamp FAvaDisplayClusterTimeStamp::Now()
{
	return { FDateTime::Now(), GFrameNumber};
}
