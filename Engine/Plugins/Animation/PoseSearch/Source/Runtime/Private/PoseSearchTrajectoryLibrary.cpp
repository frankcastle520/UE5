// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "PoseSearch/PoseSearchTrajectoryPredictor.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "Kismet/KismetMathLibrary.h"

void FPoseSearchTrajectoryData::UpdateData(
	float DeltaTime,
	const FAnimInstanceProxy& AnimInstanceProxy,
	FDerived& TrajectoryDataDerived,
	FState& TrajectoryDataState) const
{
	UpdateData(DeltaTime, AnimInstanceProxy.GetAnimInstanceObject(), TrajectoryDataDerived, TrajectoryDataState);
}

void FPoseSearchTrajectoryData::UpdateData(
	float DeltaTime,
	const UObject* Context,
	FDerived& TrajectoryDataDerived,
	FState& TrajectoryDataState) const
{
	// An AnimInstance might call this during an AnimBP recompile with 0 delta time.
	if (DeltaTime <= 0.f)
	{
		return;
	}

	const ACharacter* Character = Cast<ACharacter>(Context);
	if (!Character)
	{
		const UAnimInstance* AnimInstance = Cast<UAnimInstance>(Context);
		Character = Cast<ACharacter>(AnimInstance->GetOwningActor());
		if (!Character)
		{
			return;
		}
	}

	if (const UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
	{
		TrajectoryDataDerived.MaxSpeed = FMath::Max(MoveComp->GetMaxSpeed() * MoveComp->GetAnalogInputModifier(), MoveComp->GetMinAnalogSpeed());
		TrajectoryDataDerived.BrakingDeceleration = FMath::Max(0.f, MoveComp->GetMaxBrakingDeceleration());
		TrajectoryDataDerived.BrakingSubStepTime = MoveComp->BrakingSubStepTime;
		TrajectoryDataDerived.bOrientRotationToMovement = MoveComp->bOrientRotationToMovement;

		TrajectoryDataDerived.Velocity = MoveComp->Velocity;
		TrajectoryDataDerived.Acceleration = MoveComp->GetCurrentAcceleration();
		
		TrajectoryDataDerived.bStepGroundPrediction = !MoveComp->IsFalling() && !MoveComp->IsFlying();

		if (TrajectoryDataDerived.Acceleration.IsZero())
		{
			TrajectoryDataDerived.Friction = MoveComp->bUseSeparateBrakingFriction ? MoveComp->BrakingFriction : MoveComp->GroundFriction;
			const float FrictionFactor = FMath::Max(0.f, MoveComp->BrakingFrictionFactor);
			TrajectoryDataDerived.Friction = FMath::Max(0.f, TrajectoryDataDerived.Friction * FrictionFactor);
		}
		else
		{
			TrajectoryDataDerived.Friction = MoveComp->GroundFriction;
		}
	}

	{
		const float DesiredControllerYaw = Character->GetViewRotation().Yaw;
		
		const float DesiredYawDelta = DesiredControllerYaw - TrajectoryDataState.DesiredControllerYawLastUpdate;
		TrajectoryDataState.DesiredControllerYawLastUpdate = DesiredControllerYaw;

		TrajectoryDataDerived.ControllerYawRate = FRotator::NormalizeAxis(DesiredYawDelta) * (1.f / DeltaTime);
		if (MaxControllerYawRate >= 0.f)
		{
			TrajectoryDataDerived.ControllerYawRate = FMath::Sign(TrajectoryDataDerived.ControllerYawRate) * FMath::Min(FMath::Abs(TrajectoryDataDerived.ControllerYawRate), MaxControllerYawRate);
		}
	}

	if (const USkeletalMeshComponent* MeshComp = Character->GetMesh())
	{
		TrajectoryDataDerived.Position = MeshComp->GetComponentLocation();
		TrajectoryDataDerived.MeshCompRelativeRotation = MeshComp->GetRelativeRotation().Quaternion();
		
		if (TrajectoryDataDerived.bOrientRotationToMovement)
		{
			TrajectoryDataDerived.Facing = MeshComp->GetComponentRotation().Quaternion();
		}
		else
		{
			TrajectoryDataDerived.Facing = FQuat::MakeFromRotator(FRotator(0,TrajectoryDataState.DesiredControllerYawLastUpdate,0)) * TrajectoryDataDerived.MeshCompRelativeRotation;
		}
	}
}

FVector FPoseSearchTrajectoryData::StepCharacterMovementGroundPrediction(
	float DeltaTime,
	const FVector& InVelocity,
	const FVector& InAcceleration,
	const FDerived& TrajectoryDataDerived) const
{
	FVector OutVelocity = InVelocity;

	// Braking logic is copied from UCharacterMovementComponent::ApplyVelocityBraking()
	if (InAcceleration.IsZero())
	{
		if (InVelocity.IsZero())
		{
			return FVector::ZeroVector;
		}

		const bool bZeroFriction = (TrajectoryDataDerived.Friction == 0.f);
		const bool bZeroBraking = (TrajectoryDataDerived.BrakingDeceleration == 0.f);

		if (bZeroFriction && bZeroBraking)
		{
			return InVelocity;
		}

		float RemainingTime = DeltaTime;
		const float MaxTimeStep = FMath::Clamp(TrajectoryDataDerived.BrakingSubStepTime, 1.0f / 75.0f, 1.0f / 20.0f);

		const FVector PrevLinearVelocity = OutVelocity;
		const FVector RevAccel = (bZeroBraking ? FVector::ZeroVector : (-TrajectoryDataDerived.BrakingDeceleration * OutVelocity.GetSafeNormal()));

		// Decelerate to brake to a stop
		while (RemainingTime >= UCharacterMovementComponent::MIN_TICK_TIME)
		{
			// Zero friction uses constant deceleration, so no need for iteration.
			const float dt = ((RemainingTime > MaxTimeStep && !bZeroFriction) ? FMath::Min(MaxTimeStep, RemainingTime * 0.5f) : RemainingTime);
			RemainingTime -= dt;

			// apply friction and braking
			OutVelocity = OutVelocity + ((-TrajectoryDataDerived.Friction) * OutVelocity + RevAccel) * dt;

			// Don't reverse direction
			if ((OutVelocity | PrevLinearVelocity) <= 0.f)
			{
				OutVelocity = FVector::ZeroVector;
				return OutVelocity;
			}
		}

		// Clamp to zero if nearly zero, or if below min threshold and braking
		const float VSizeSq = OutVelocity.SizeSquared();
		if (VSizeSq <= KINDA_SMALL_NUMBER || (!bZeroBraking && VSizeSq <= FMath::Square(UCharacterMovementComponent::BRAKE_TO_STOP_VELOCITY)))
		{
			OutVelocity = FVector::ZeroVector;
		}
	}
	// Acceleration logic is copied from  UCharacterMovementComponent::CalcVelocity
	else
	{
		const FVector AccelDir = InAcceleration.GetSafeNormal();
		const float VelSize = OutVelocity.Size();

		OutVelocity = OutVelocity - (OutVelocity - AccelDir * VelSize) * FMath::Min(DeltaTime * TrajectoryDataDerived.Friction, 1.f);

		OutVelocity += InAcceleration * DeltaTime;
		OutVelocity = OutVelocity.GetClampedToMaxSize(TrajectoryDataDerived.MaxSpeed);
	}

	return OutVelocity;
}

void UPoseSearchTrajectoryLibrary::InitTrajectorySamples(
	FPoseSearchQueryTrajectory& Trajectory,
	const FPoseSearchTrajectoryData& TrajectoryData,
	const FPoseSearchTrajectoryData::FDerived& TrajectoryDataDerived,
	const FPoseSearchTrajectoryData::FSampling& TrajectoryDataSampling,
	float DeltaTime)
{
	InitTrajectorySamples(
		Trajectory,
		TrajectoryData,
		TrajectoryDataDerived.Position, TrajectoryDataDerived.Facing,
		TrajectoryDataSampling, 
		DeltaTime );
}

void UPoseSearchTrajectoryLibrary::InitTrajectorySamples(
	FPoseSearchQueryTrajectory& Trajectory,
	const FPoseSearchTrajectoryData& TrajectoryData,
	FVector DefaultPosition, FQuat DefaultFacing,
	const FPoseSearchTrajectoryData::FSampling& TrajectoryDataSampling,
	float DeltaTime)
{
	const int32 NumHistorySamples = TrajectoryDataSampling.NumHistorySamples;
	const int32 NumPredictionSamples = TrajectoryDataSampling.NumPredictionSamples;

	// History + current sample + prediction
	const int32 TotalNumSamples = NumHistorySamples + 1 + NumPredictionSamples;

	if (Trajectory.Samples.Num() != TotalNumSamples)
	{
		Trajectory.Samples.SetNumUninitialized(TotalNumSamples);

		// Initialize history samples
		const float SecondsPerHistorySample = FMath::Max(TrajectoryDataSampling.SecondsPerHistorySample, 0.f);
		for (int32 i = 0; i < NumHistorySamples; ++i)
		{
			Trajectory.Samples[i].Position = DefaultPosition;
			Trajectory.Samples[i].Facing = DefaultFacing;
			Trajectory.Samples[i].AccumulatedSeconds = SecondsPerHistorySample * (i - NumHistorySamples - 1);
		}

		// Initialize current sample and prediction
		const float SecondsPerPredictionSample = FMath::Max(TrajectoryDataSampling.SecondsPerPredictionSample, 0.f);
		for (int32 i = NumHistorySamples; i < Trajectory.Samples.Num(); ++i)
		{
			Trajectory.Samples[i].Position = DefaultPosition;
			Trajectory.Samples[i].Facing = DefaultFacing;
			Trajectory.Samples[i].AccumulatedSeconds = SecondsPerPredictionSample * (i - NumHistorySamples) + DeltaTime;
		}
	}
}

void UPoseSearchTrajectoryLibrary::UpdateHistory_TransformHistory(
	FPoseSearchQueryTrajectory& Trajectory,
	const FPoseSearchTrajectoryData& TrajectoryData,
	const FPoseSearchTrajectoryData::FDerived& TrajectoryDataDerived,
	const FPoseSearchTrajectoryData::FSampling& TrajectoryDataSampling,
	float DeltaTime)
{
	UpdateHistory_TransformHistory(
		Trajectory,
		TrajectoryData,
		TrajectoryDataDerived.Position, TrajectoryDataDerived.Velocity,
		TrajectoryDataSampling,
		DeltaTime);
}

void UPoseSearchTrajectoryLibrary::UpdateHistory_TransformHistory(
	FPoseSearchQueryTrajectory& Trajectory,
	const FPoseSearchTrajectoryData& TrajectoryData,
	FVector CurrentPosition,
	FVector CurrentVelocity, 
	const FPoseSearchTrajectoryData::FSampling& TrajectoryDataSampling,
	float DeltaTime)
{
	const int32 NumHistorySamples = TrajectoryDataSampling.NumHistorySamples;
	if (NumHistorySamples > 0)
	{
		const float SecondsPerHistorySample = TrajectoryDataSampling.SecondsPerHistorySample;

		check(NumHistorySamples <= Trajectory.Samples.Num());

		// Our trajectory's "current" position assumes the we have the same delta time as the previous frame.
		// so use predicted trajectory with current time step.
		const FVector PredictedPositionAdjusted = Trajectory.GetSampleAtTime(DeltaTime).Position;

		// converting all the history samples relative to the previous character position (Trajectory.Samples[NumHistorySamples].Position)
		for (int32 Index = 0; Index < NumHistorySamples; ++Index)
		{
			Trajectory.Samples[Index].Position = PredictedPositionAdjusted - Trajectory.Samples[Index].Position;
		}

		FVector CurrentTranslation = CurrentVelocity * DeltaTime;

		// Shift history Samples when it's time to record a new one.
		if (SecondsPerHistorySample <= 0.f || FMath::Abs(Trajectory.Samples[NumHistorySamples - 1].AccumulatedSeconds) >= SecondsPerHistorySample)
		{
			for (int32 Index = 0; Index < NumHistorySamples - 1; ++Index)
			{
				Trajectory.Samples[Index].AccumulatedSeconds = Trajectory.Samples[Index + 1].AccumulatedSeconds - DeltaTime;
				Trajectory.Samples[Index].Position = Trajectory.Samples[Index + 1].Position + CurrentTranslation;
				Trajectory.Samples[Index].Facing = Trajectory.Samples[Index + 1].Facing;
			}

			Trajectory.Samples[NumHistorySamples - 1].AccumulatedSeconds = 0.f;
			Trajectory.Samples[NumHistorySamples - 1].Position = CurrentTranslation;
			Trajectory.Samples[NumHistorySamples - 1].Facing = Trajectory.Samples[NumHistorySamples].Facing;
		}
		else
		{
			for (int32 Index = 0; Index < NumHistorySamples; ++Index)
			{
				Trajectory.Samples[Index].AccumulatedSeconds -= DeltaTime;
				Trajectory.Samples[Index].Position += CurrentTranslation;
			}
		}

		// converting the history sample positions in world space by applying the current world position.
		for (int32 Index = 0; Index < NumHistorySamples; ++Index)
		{
			Trajectory.Samples[Index].Position = CurrentPosition - Trajectory.Samples[Index].Position;
		}
	}
}

FVector UPoseSearchTrajectoryLibrary::RemapVectorMagnitudeWithCurve(
	const FVector& Vector,
	bool bUseCurve,
	const FRuntimeFloatCurve& Curve)
{
	if (bUseCurve)
	{
		const float Length = Vector.Length();
		if (Length > UE_KINDA_SMALL_NUMBER)
		{
			const float RemappedLength = Curve.GetRichCurveConst()->Eval(Length);
			return Vector * (RemappedLength / Length);
		}
	}

	return Vector;
}

void UPoseSearchTrajectoryLibrary::UpdatePrediction_SimulateCharacterMovement(
	FPoseSearchQueryTrajectory& Trajectory,
	const FPoseSearchTrajectoryData& TrajectoryData,
	const FPoseSearchTrajectoryData::FDerived& TrajectoryDataDerived,
	const FPoseSearchTrajectoryData::FSampling& TrajectoryDataSampling,
	float DeltaTime)
{
	FVector CurrentPositionWS = TrajectoryDataDerived.Position;
	FVector CurrentVelocityWS = RemapVectorMagnitudeWithCurve(TrajectoryDataDerived.Velocity, TrajectoryData.bUseSpeedRemappingCurve, TrajectoryData.SpeedRemappingCurve);
	FVector CurrentAccelerationWS = RemapVectorMagnitudeWithCurve(TrajectoryDataDerived.Acceleration, TrajectoryData.bUseAccelerationRemappingCurve, TrajectoryData.AccelerationRemappingCurve);

	// Bending CurrentVelocityWS towards CurrentAccelerationWS
	if (TrajectoryData.BendVelocityTowardsAcceleration > UE_KINDA_SMALL_NUMBER && !CurrentAccelerationWS.IsNearlyZero())
	{
		const float CurrentSpeed = CurrentVelocityWS.Length();
		const FVector VelocityWSAlongAcceleration = CurrentAccelerationWS.GetUnsafeNormal() * CurrentSpeed;
		if (TrajectoryData.BendVelocityTowardsAcceleration < 1.f - UE_KINDA_SMALL_NUMBER)
		{
			CurrentVelocityWS = FMath::Lerp(CurrentVelocityWS, VelocityWSAlongAcceleration, TrajectoryData.BendVelocityTowardsAcceleration);

			const float NewLength = CurrentVelocityWS.Length();
			if (NewLength > UE_KINDA_SMALL_NUMBER)
			{
				CurrentVelocityWS *= CurrentSpeed / NewLength;
			}
			else
			{
				// @todo: consider setting the CurrentVelocityWS = VelocityWSAlongAcceleration if vel and acc are in opposite directions
			}
		}
		else
		{
			CurrentVelocityWS = VelocityWSAlongAcceleration;
		}
	}

	FQuat CurrentFacingWS = TrajectoryDataDerived.Facing;
	
	const int32 NumHistorySamples = TrajectoryDataSampling.NumHistorySamples;
	const float SecondsPerPredictionSample = TrajectoryDataSampling.SecondsPerPredictionSample;
	const FQuat ControllerRotationPerStep = FQuat::MakeFromEuler(FVector(0.f, 0.f, TrajectoryDataDerived.ControllerYawRate * SecondsPerPredictionSample));

	float AccumulatedSeconds = DeltaTime;

	const int32 LastIndex = Trajectory.Samples.Num() - 1;
	if (NumHistorySamples <= LastIndex)
	{
		for (int32 Index = NumHistorySamples; ; ++Index)
		{
			Trajectory.Samples[Index].Position = CurrentPositionWS;
			Trajectory.Samples[Index].Facing = CurrentFacingWS;
			Trajectory.Samples[Index].AccumulatedSeconds = AccumulatedSeconds;

			if (Index == LastIndex)
			{
				break;
			}

			CurrentPositionWS += CurrentVelocityWS * SecondsPerPredictionSample;
			AccumulatedSeconds += SecondsPerPredictionSample;

			if (TrajectoryDataDerived.bStepGroundPrediction)
			{
				CurrentAccelerationWS = RemapVectorMagnitudeWithCurve(ControllerRotationPerStep * CurrentAccelerationWS,
					TrajectoryData.bUseAccelerationRemappingCurve, TrajectoryData.AccelerationRemappingCurve);
				const FVector NewVelocityWS = TrajectoryData.StepCharacterMovementGroundPrediction(SecondsPerPredictionSample, CurrentVelocityWS, CurrentAccelerationWS, TrajectoryDataDerived);
				CurrentVelocityWS = RemapVectorMagnitudeWithCurve(NewVelocityWS, TrajectoryData.bUseSpeedRemappingCurve, TrajectoryData.SpeedRemappingCurve);

				// Account for the controller (e.g. the camera) rotating.
				CurrentFacingWS = ControllerRotationPerStep * CurrentFacingWS;
				if (TrajectoryDataDerived.bOrientRotationToMovement && !CurrentAccelerationWS.IsNearlyZero())
				{
					// Rotate towards acceleration.
					const FVector CurrentAccelerationCS = TrajectoryDataDerived.MeshCompRelativeRotation.RotateVector(CurrentAccelerationWS);
					CurrentFacingWS = FMath::QInterpConstantTo(CurrentFacingWS, CurrentAccelerationCS.ToOrientationQuat(), SecondsPerPredictionSample, TrajectoryData.RotateTowardsMovementSpeed);
				}
			}
		}
	}
}

void UPoseSearchTrajectoryLibrary::PoseSearchGenerateTrajectory(
	const UObject* Context, 
	UPARAM(ref)	const FPoseSearchTrajectoryData& InTrajectoryData,
	float InDeltaTime,
	UPARAM(ref) FPoseSearchQueryTrajectory& InOutTrajectory,
	UPARAM(ref) float& InOutDesiredControllerYawLastUpdate,
	FPoseSearchQueryTrajectory& OutTrajectory,
	float InHistorySamplingInterval,
	int32 InTrajectoryHistoryCount,
	float InPredictionSamplingInterval,
	int32 InTrajectoryPredictionCount)
{
	FPoseSearchTrajectoryData::FSampling TrajectoryDataSampling;
	TrajectoryDataSampling.NumHistorySamples = InTrajectoryHistoryCount;
	TrajectoryDataSampling.SecondsPerHistorySample = InHistorySamplingInterval;
	TrajectoryDataSampling.NumPredictionSamples = InTrajectoryPredictionCount;
	TrajectoryDataSampling.SecondsPerPredictionSample = InPredictionSamplingInterval;

	FPoseSearchTrajectoryData::FState TrajectoryDataState;
	TrajectoryDataState.DesiredControllerYawLastUpdate = InOutDesiredControllerYawLastUpdate;

	FPoseSearchTrajectoryData::FDerived TrajectoryDataDerived;
	InTrajectoryData.UpdateData(InDeltaTime, Context, TrajectoryDataDerived, TrajectoryDataState);
	InitTrajectorySamples(InOutTrajectory, InTrajectoryData, TrajectoryDataDerived.Position, TrajectoryDataDerived.Facing, TrajectoryDataSampling, InDeltaTime);
	UpdateHistory_TransformHistory(InOutTrajectory, InTrajectoryData, TrajectoryDataDerived.Position, TrajectoryDataDerived.Velocity, TrajectoryDataSampling, InDeltaTime);
	UpdatePrediction_SimulateCharacterMovement(InOutTrajectory, InTrajectoryData, TrajectoryDataDerived, TrajectoryDataSampling, InDeltaTime);

	InOutDesiredControllerYawLastUpdate = TrajectoryDataState.DesiredControllerYawLastUpdate;

	OutTrajectory = InOutTrajectory;
}


void UPoseSearchTrajectoryLibrary::PoseSearchGeneratePredictorTrajectory(
	UObject* InPredictor,	// must implement IPoseSearchTrajectoryPredictorInterface
	UPARAM(ref)	const FPoseSearchTrajectoryData& InTrajectoryData,
	float InDeltaTime,
	UPARAM(ref) FPoseSearchQueryTrajectory& InOutTrajectory,
	UPARAM(ref) float& InOutDesiredControllerYawLastUpdate,
	FPoseSearchQueryTrajectory& OutTrajectory,
	float InHistorySamplingInterval,
	int32 InTrajectoryHistoryCount,
	float InPredictionSamplingInterval,
	int32 InTrajectoryPredictionCount)
{
	FPoseSearchTrajectoryData::FSampling TrajectoryDataSampling;
	TrajectoryDataSampling.NumHistorySamples = InTrajectoryHistoryCount;
	TrajectoryDataSampling.SecondsPerHistorySample = InHistorySamplingInterval;
	TrajectoryDataSampling.NumPredictionSamples = InTrajectoryPredictionCount;
	TrajectoryDataSampling.SecondsPerPredictionSample = InPredictionSamplingInterval;

	// TODO: handle controller yaw
	//TrajectoryDataState.DesiredControllerYawLastUpdate = InOutDesiredControllerYawLastUpdate;

	FVector CurrentPosition = FVector::ZeroVector;
	FVector CurrentVelocity = FVector::ZeroVector;
	FQuat CurrentFacing = FQuat::Identity;

	if (InPredictor)
	{
		IPoseSearchTrajectoryPredictorInterface::Execute_GetCurrentState(InPredictor, CurrentPosition, CurrentFacing, CurrentVelocity);
	}

	InitTrajectorySamples(InOutTrajectory, InTrajectoryData, CurrentPosition, CurrentFacing, TrajectoryDataSampling, InDeltaTime);
	UpdateHistory_TransformHistory(InOutTrajectory, InTrajectoryData, CurrentPosition, CurrentVelocity, TrajectoryDataSampling, InDeltaTime);

	if (InPredictor)
	{
		IPoseSearchTrajectoryPredictorInterface::Execute_Predict(InPredictor, InOutTrajectory,
			InTrajectoryPredictionCount + 1, InPredictionSamplingInterval, InTrajectoryHistoryCount);
	}

	//InOutDesiredControllerYawLastUpdate = TrajectoryDataState.DesiredControllerYawLastUpdate;

	OutTrajectory = InOutTrajectory;
}


void UPoseSearchTrajectoryLibrary::HandleTrajectoryWorldCollisions(const UObject* WorldContextObject, const UAnimInstance* AnimInstance, UPARAM(ref) const FPoseSearchQueryTrajectory& InTrajectory, bool bApplyGravity, float FloorCollisionsOffset, FPoseSearchQueryTrajectory& OutTrajectory, FPoseSearchTrajectory_WorldCollisionResults& CollisionResult,
	ETraceTypeQuery TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, EDrawDebugTrace::Type DrawDebugType, bool bIgnoreSelf, float MaxObstacleHeight, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime)
{
	FVector StartingVelocity = FVector::ZeroVector;
	FVector GravityAccel = FVector::ZeroVector;
	if (bApplyGravity && AnimInstance)
	{
		if (const ACharacter* Character = Cast<ACharacter>(AnimInstance->GetOwningActor()))
		{
			if (const UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
			{
				GravityAccel = MoveComp->GetGravityDirection() * -MoveComp->GetGravityZ();
				StartingVelocity = Character->GetVelocity();
			}
		}
	}

	HandleTrajectoryWorldCollisionsWithGravity(WorldContextObject, InTrajectory, StartingVelocity, bApplyGravity, GravityAccel, 
		FloorCollisionsOffset, OutTrajectory, CollisionResult, TraceChannel, bTraceComplex, ActorsToIgnore, 
		DrawDebugType, bIgnoreSelf, MaxObstacleHeight, TraceColor, TraceHitColor, DrawTime);
}


void UPoseSearchTrajectoryLibrary::HandleTrajectoryWorldCollisionsWithGravity(const UObject* WorldContextObject,
	UPARAM(ref) const FPoseSearchQueryTrajectory& InTrajectory, FVector StartingVelocity, bool bApplyGravity, FVector GravityAccel, float FloorCollisionsOffset, FPoseSearchQueryTrajectory& OutTrajectory, FPoseSearchTrajectory_WorldCollisionResults& CollisionResult,
	ETraceTypeQuery TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, EDrawDebugTrace::Type DrawDebugType, bool bIgnoreSelf, float MaxObstacleHeight, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime)
{
	OutTrajectory = InTrajectory;

	TArray<FPoseSearchQueryTrajectorySample>& Samples = OutTrajectory.Samples;
	const int32 NumSamples = Samples.Num();

	FVector GravityDirection = FVector::ZeroVector;
	float GravityZ = 0.f;
	float InitialVelocityZ = StartingVelocity.Z;

	if (bApplyGravity && !GravityAccel.IsNearlyZero())
	{
		GravityAccel.ToDirectionAndLength(GravityDirection, GravityZ);
		GravityZ = -GravityZ;
		const FVector VelocityOnGravityAxis = StartingVelocity.ProjectOnTo(GravityDirection);
		
		InitialVelocityZ = VelocityOnGravityAxis.Length() * -FMath::Sign(GravityDirection.Dot(VelocityOnGravityAxis));
	}

	CollisionResult.TimeToLand = OutTrajectory.Samples.Last().AccumulatedSeconds;

	if (!FMath::IsNearlyZero(GravityZ))
	{
		FVector LastImpactPoint;
		FVector LastImpactNormal;
		bool bIsLastImpactValid = false;
		bool bIsFirstFall = true;

		const FVector Gravity = GravityDirection * -GravityZ;
		float FreeFallAccumulatedSeconds = 0.f;
		for (int32 SampleIndex = 1; SampleIndex < NumSamples; ++SampleIndex)
		{
			FPoseSearchQueryTrajectorySample& Sample = Samples[SampleIndex];
			if (Sample.AccumulatedSeconds > 0.f)
			{
				const int32 PrevSampleIndex = SampleIndex - 1;
				const FPoseSearchQueryTrajectorySample& PrevSample = Samples[PrevSampleIndex];

				FreeFallAccumulatedSeconds += Sample.AccumulatedSeconds - PrevSample.AccumulatedSeconds;

				if (bIsLastImpactValid)
				{
					const FPlane GroundPlane = FPlane(PrevSample.Position, -GravityDirection);
					Sample.Position = FPlane::PointPlaneProject(Sample.Position, GroundPlane);
				}

				// applying gravity
				const FVector FreeFallOffset =  Gravity * (0.5f * FreeFallAccumulatedSeconds * FreeFallAccumulatedSeconds);
				Sample.Position += FreeFallOffset;

				FHitResult HitResult;
				if (FloorCollisionsOffset > 0.f && UKismetSystemLibrary::LineTraceSingle(WorldContextObject, Sample.Position + (GravityDirection * -MaxObstacleHeight), Sample.Position, TraceChannel, bTraceComplex, ActorsToIgnore, DrawDebugType, HitResult, bIgnoreSelf, TraceColor, TraceHitColor, DrawTime))
				{
					// Only allow our trace to move trajectory along gravity direction.
					LastImpactPoint = UKismetMathLibrary::FindClosestPointOnLine(HitResult.ImpactPoint, Sample.Position, GravityDirection);
					LastImpactNormal = HitResult.Normal;
					bIsLastImpactValid = true;

					Sample.Position = LastImpactPoint - GravityDirection * FloorCollisionsOffset;

					if (bIsFirstFall)
					{
						const float InitialHeight = OutTrajectory.GetSampleAtTime(0.0f).Position.Z;
						const float FinalHeight = Sample.Position.Z;
						const float FallHeight = FMath::Abs(FinalHeight - InitialHeight);

						bIsFirstFall = false;
						CollisionResult.TimeToLand = (InitialVelocityZ / -GravityZ) + ((FMath::Sqrt(FMath::Square(InitialVelocityZ) + (2.f * -GravityZ * FallHeight))) / -GravityZ);
						CollisionResult.LandSpeed = InitialVelocityZ + GravityZ * CollisionResult.TimeToLand;
					}

					FreeFallAccumulatedSeconds = 0.f;
				}
			}
		}
	}
	else if (FloorCollisionsOffset > 0.f)
	{
		for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
		{
			FPoseSearchQueryTrajectorySample& Sample = OutTrajectory.Samples[SampleIndex];
			if (Sample.AccumulatedSeconds > 0.f)
			{
				FHitResult HitResult;
				if (UKismetSystemLibrary::LineTraceSingle(WorldContextObject, Sample.Position + FVector::UpVector * 3000.f, Sample.Position, TraceChannel, bTraceComplex, ActorsToIgnore, DrawDebugType, HitResult, bIgnoreSelf, TraceColor, TraceHitColor, DrawTime))
				{
					Sample.Position.Z = HitResult.ImpactPoint.Z + FloorCollisionsOffset;
				}
			}
		}
	}

	CollisionResult.LandSpeed = InitialVelocityZ + GravityZ * CollisionResult.TimeToLand;
}

void UPoseSearchTrajectoryLibrary::GetTrajectorySampleAtTime(UPARAM(ref) const FPoseSearchQueryTrajectory& InTrajectory, float Time, FPoseSearchQueryTrajectorySample& OutTrajectorySample, bool bExtrapolate)
{
	OutTrajectorySample = InTrajectory.GetSampleAtTime(Time, bExtrapolate);
}

void UPoseSearchTrajectoryLibrary::GetTrajectoryVelocity(UPARAM(ref) const FPoseSearchQueryTrajectory& InTrajectory, float Time1, float Time2, FVector& OutVelocity, bool bExtrapolate)
{
	if (FMath::IsNearlyEqual(Time1, Time2))
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("UPoseSearchTrajectoryLibrary::GetTrajectoryVelocity - Time1 is same as Time2. Invalid time horizon."));
		OutVelocity = FVector::ZeroVector;
		return;
	}

	FPoseSearchQueryTrajectorySample Sample1 = InTrajectory.GetSampleAtTime(Time1, bExtrapolate);
	FPoseSearchQueryTrajectorySample Sample2 = InTrajectory.GetSampleAtTime(Time2, bExtrapolate);

	OutVelocity = (Sample2.Position - Sample1.Position) / (Time2 - Time1);
}

void UPoseSearchTrajectoryLibrary::GetTrajectoryAngularVelocity(const FPoseSearchQueryTrajectory& InTrajectory, float Time1, float Time2, FVector& OutAngularVelocity, bool bExtrapolate /*= false*/)
{
	if (FMath::IsNearlyEqual(Time1, Time2))
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("UPoseSearchTrajectoryLibrary::GetTrajectoryAngularVelocity - Time1 is same as Time2. Invalid time horizon."));
		OutAngularVelocity = FVector::ZeroVector;
		return;
	}

	FPoseSearchQueryTrajectorySample Sample1 = InTrajectory.GetSampleAtTime(Time1, bExtrapolate);
	FPoseSearchQueryTrajectorySample Sample2 = InTrajectory.GetSampleAtTime(Time2, bExtrapolate);

	FQuat DeltaRotation = (Sample2.Facing * Sample1.Facing.Inverse());
	DeltaRotation.EnforceShortestArcWith(FQuat::Identity);

	const FVector AngularVelocityInRadians = DeltaRotation.ToRotationVector() / (Time2 - Time1);

	OutAngularVelocity = FVector(
		FMath::RadiansToDegrees(AngularVelocityInRadians.X),
		FMath::RadiansToDegrees(AngularVelocityInRadians.Y),
		FMath::RadiansToDegrees(AngularVelocityInRadians.Z));
}

void UPoseSearchTrajectoryLibrary::DrawTrajectory(const UObject* WorldContextObject, const FPoseSearchQueryTrajectory& InTrajectory, const float DebugThickness, float HeightOffset)
{
#if ENABLE_ANIM_DEBUG
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		InTrajectory.DebugDrawTrajectory(World, DebugThickness, HeightOffset);
	}
#endif // ENABLE_ANIM_DEBUG
}