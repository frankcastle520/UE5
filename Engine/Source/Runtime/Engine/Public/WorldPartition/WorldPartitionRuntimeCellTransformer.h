// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartitionRuntimeCellTransformer.generated.h"

UCLASS(Abstract)
class UWorldPartitionRuntimeCellTransformer : public UObject
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
public:
	virtual void PreTransform(ULevel* InLevel) {}
	virtual void Transform(ULevel* InLevel) {}
	virtual void PostTransform(ULevel* InLevel) {}
#endif
};