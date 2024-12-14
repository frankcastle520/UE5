// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/ReplicationSystem/Prioritization/NetObjectCountLimiter.h"
#include "TestNetObjectCountLimiter.generated.h"

// Config for NetObjectCuuntLimiter in Fill mode.
UCLASS()
class UNetObjectCountLimiterFillTestConfig : public UNetObjectCountLimiterConfig
{
	GENERATED_BODY()

public:
	UNetObjectCountLimiterFillTestConfig();
};
