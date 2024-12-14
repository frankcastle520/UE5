// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartitionRuntimeCellTransformer.h"
#include "WorldPartitionRuntimeCellTransformerLog.generated.h"

UCLASS()
class UWorldPartitionRuntimeCellTransformerLog : public UWorldPartitionRuntimeCellTransformer
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
public:
	virtual void PreTransform(ULevel* InLevel) override;
	virtual void PostTransform(ULevel* InLevel) override;

private:
	void GatherLevelContentStats(ULevel* InLevel, TMap<UClass*, int32>& OutClassNumInstances);
	void DumpLevelContentStats(ULevel* InLevel, const TMap<UClass*, int32>& InClassNumInstances);
#endif

#if WITH_EDITORONLY_DATA
public:
	/** Only log when the level was actually transformed */
	UPROPERTY(EditAnywhere, Category = ISM)
	bool bOnlyLogDifferences;

	TMap<UClass*, int32> ClassNumInstancesBefore;
#endif
};