// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoverPoseSearchTrajectoryPredictor.h"
 #include "PoseSearch/PoseSearchTrajectoryTypes.h"
 #include "MoverComponent.h"
 #include "MoveLibrary/MovementUtils.h"
 
void UMoverTrajectoryPredictor::Predict_Implementation(FPoseSearchQueryTrajectory& InOutTrajectory,
	int32 NumPredictionSamples, float SecondsPerPredictionSample, int NumHistorySamples)
{
	if (!MoverComponent)
	{
		UE_LOG(LogMover, Log, TEXT("Calling Predict without a Mover Component. This is invalid and the trajectory will not be modified."));
		return;
	}

	FMoverPredictTrajectoryParams PredictParams;
	PredictParams.NumPredictionSamples = NumPredictionSamples;
	PredictParams.SecondsPerSample = SecondsPerPredictionSample;
	PredictParams.bUseVisualComponentRoot = true;
	PredictParams.bDisableGravity = true;

	TArray<FTrajectorySampleInfo> MoverPredictionSamples = MoverComponent->GetPredictedTrajectory(PredictParams);

	if (InOutTrajectory.Samples.Num() < (NumHistorySamples + MoverPredictionSamples.Num()))
	{
		UE_LOG(LogMover, Warning, TEXT("InOutTrajectory Samples array does not have enough space for %i predicted samples"), MoverPredictionSamples.Num());
	}
	else
	{
		float AccumulatedSeconds = 0.0;

		for (int32 i = 0; i < NumPredictionSamples; ++i)
		{
			const int PoseSampleIdx = i + NumHistorySamples;
			const int PredictedSampleIdx = i;

			InOutTrajectory.Samples[PoseSampleIdx].Position = MoverPredictionSamples[PredictedSampleIdx].Transform.GetLocation();
			InOutTrajectory.Samples[PoseSampleIdx].Facing = MoverPredictionSamples[PredictedSampleIdx].Transform.GetRotation();
			InOutTrajectory.Samples[PoseSampleIdx].AccumulatedSeconds = AccumulatedSeconds;

			AccumulatedSeconds += SecondsPerPredictionSample;
		}
	}
}

void UMoverTrajectoryPredictor::GetGravity_Implementation(FVector& OutGravityAccel)
{
	if (!MoverComponent)
	{
		UE_LOG(LogMover, Log, TEXT("Calling GetGravity without a Mover Component. Return value will be defaulted."));
		OutGravityAccel = FVector::ZeroVector;
		return;
	}

	OutGravityAccel = MoverComponent->GetGravityAcceleration();
}


void UMoverTrajectoryPredictor::GetCurrentState_Implementation(FVector& OutPosition, FQuat& OutFacing, FVector& OutVelocity)
{
	if (!MoverComponent)
	{
		UE_LOG(LogMover, Log, TEXT("Calling GetCurrentState without a Mover Component. Return values will be defaulted."));
		OutPosition = OutVelocity = FVector::ZeroVector;
		OutFacing = FQuat::Identity;
		return;
	}

	const USceneComponent* VisualComp = MoverComponent->GetPrimaryVisualComponent();

	if (VisualComp)
	{
		OutPosition = VisualComp->GetComponentLocation();
	}
	else
	{
		OutPosition = MoverComponent->GetUpdatedComponentTransform().GetLocation();
	}

	const bool bOrientRotationToMovement = true;

	if (bOrientRotationToMovement)
	{
		if (VisualComp)
		{
			OutFacing = VisualComp->GetComponentRotation().Quaternion();
		}
		else
		{
			OutFacing = MoverComponent->GetUpdatedComponentTransform().GetRotation();
		}
	}
	else
	{
		// JAH TODO: Needs a solve
		//OutFacing = FQuat::MakeFromRotator(FRotator(0, TrajectoryDataState.DesiredControllerYawLastUpdate, 0)) * TrajectoryDataDerived.MeshCompRelativeRotation;
	}

	OutVelocity = MoverComponent->GetVelocity();
}


void UMoverTrajectoryPredictor::GetVelocity_Implementation(FVector& OutVelocity)
{
	if (!MoverComponent)
	{
		UE_LOG(LogMover, Log, TEXT("Calling GetVelocity without a Mover Component. Return value will be defaulted."));
		OutVelocity = FVector::ZeroVector;
		return;
	}

	OutVelocity = MoverComponent->GetVelocity();
}