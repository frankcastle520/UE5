// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MotionWarpingAdapter.h"
#include "MoverComponent.h"
#include "MotionWarpingMoverAdapter.generated.h"

// Adapter for MoverComponent actors to participate in motion warping

UCLASS()
class MOVER_API UMotionWarpingMoverAdapter : public UMotionWarpingBaseAdapter
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

	void SetMoverComp(UMoverComponent* InMoverComp);

	virtual AActor* GetActor() const override;
	virtual USkeletalMeshComponent* GetMesh() const override;
	virtual FVector GetVisualRootLocation() const override;
	virtual FVector GetBaseVisualTranslationOffset() const override;
	virtual FQuat GetBaseVisualRotationOffset() const override;

private:
	// This is called when our Mover actor wants to warp local motion, and passes the responsibility onto the warping component
	FTransform WarpLocalRootMotionOnMoverComp(const FTransform& LocalRootMotionTransform, float DeltaSeconds, const FMotionWarpingUpdateContext* OptionalWarpingContext);

	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UMoverComponent> TargetMoverComp;
};