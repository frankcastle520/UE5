// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WorldPartition/WorldPartitionRuntimeCellTransformer.h"
#include "Templates/SubclassOf.h"

#include "WorldPartitionRuntimeCellTransformerISM.generated.h"

UCLASS()
class UWorldPartitionRuntimeCellTransformerISM : public UWorldPartitionRuntimeCellTransformer
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
public:
	virtual void Transform(ULevel* InLevel) override;

private:
	bool CanAutoInstanceActor(AActor* InActor) const;
	bool CanRemoveActor(AActor* InActor) const;
#endif

#if WITH_EDITORONLY_DATA
public:
	/** Allowed classes (recursive) to convert to instances */
	UPROPERTY(EditAnywhere, Category = ISM)
	TArray<TSubclassOf<AActor>> AllowedClasses;

	/** Disallowed classes (non-recursive) to convert to instances */
	UPROPERTY(EditAnywhere, Category = ISM)
	TArray<TSubclassOf<AActor>> DisallowedClasses;

	/** Minimum number of instances required to allow converting actors to ISM */
	UPROPERTY(EditAnywhere, Category = ISM)
	uint32 MinNumInstances;
#endif
};