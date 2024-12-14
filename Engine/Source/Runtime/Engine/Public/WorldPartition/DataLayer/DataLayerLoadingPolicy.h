// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WorldPartition/WorldPartition.h"
#include "DataLayerLoadingPolicy.generated.h"

class UDataLayerInstance;

UCLASS(Within = DataLayerManager)
class ENGINE_API UDataLayerLoadingPolicy : public UObject
{
	GENERATED_BODY()
#if WITH_EDITOR
public:
	virtual bool ResolveIsLoadedInEditor(TArray<const UDataLayerInstance*>& InDataLayers) const;

protected:
	EWorldPartitionDataLayersLogicOperator GetDataLayersLogicOperator() const;
#endif
};