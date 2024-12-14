// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearch/PoseSearchTrajectoryPredictor.h"
#include "MoverPoseSearchTrajectoryPredictor.generated.h"


class UMoverComponent;

/**
 * Trajectory predictor that can query from a Mover-driven actor, for use with Pose Search
 */
UCLASS(BlueprintType, EditInlineNew)
class MOVER_API UMoverTrajectoryPredictor : public UObject, public IPoseSearchTrajectoryPredictorInterface
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Animation|PoseSearch|Experimental")
	void Setup(UMoverComponent* InMoverComponent) { MoverComponent = InMoverComponent; }

	virtual void Predict_Implementation(FPoseSearchQueryTrajectory& InOutTrajectory, int32 NumPredictionSamples, float SecondsPerPredictionSample, int32 NumHistorySamples) override;

	virtual void GetGravity_Implementation(FVector& OutGravityAccel) override;

	virtual void GetCurrentState_Implementation(FVector& OutPosition, FQuat& OutFacing, FVector& OutVelocity) override;

	virtual void GetVelocity_Implementation(FVector& OutVelocity) override;

protected:
	TObjectPtr<UMoverComponent> MoverComponent;

};
