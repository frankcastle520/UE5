// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ReplicationStream.h"
#include "ReplicationStreamArray.generated.h"

USTRUCT()
struct FConcertReplicationStreamArray
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FConcertReplicationStream> Streams;
};
