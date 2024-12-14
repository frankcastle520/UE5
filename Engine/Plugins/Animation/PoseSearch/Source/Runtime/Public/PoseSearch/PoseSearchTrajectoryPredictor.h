// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "PoseSearchTrajectoryPredictor.generated.h"

struct FPoseSearchQueryTrajectory;


UINTERFACE(BlueprintType, Experimental)
class POSESEARCH_API UPoseSearchTrajectoryPredictorInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * PoseSearchTrajectoryPredictor: API for an object to implement to act as a predictor of the future trajectory for motion matching animation purposes
 */
class IPoseSearchTrajectoryPredictorInterface : public IInterface
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintNativeEvent)
	void Predict(FPoseSearchQueryTrajectory& InOutTrajectory, int32 NumPredictionSamples, float SecondsPerPredictionSample, int32 NumHistorySamples);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Animation|PoseSearch|Experimental")
	void GetGravity(FVector& OutGravityAccel);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Animation|PoseSearch|Experimental")
	void GetCurrentState(FVector& OutPosition, FQuat& OutFacing, FVector& OutVelocity);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Animation|PoseSearch|Experimental")
	void GetVelocity(FVector& OutVelocity);

};
