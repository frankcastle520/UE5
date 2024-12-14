// Copyright Epic Games, Inc. All Rights Reserved.


#include "DefaultMovementSet/Modes/NavWalkingMode.h"
#include "MoverComponent.h"
#include "AI/NavigationSystemBase.h"
#include "AI/Navigation/NavigationDataInterface.h"
#include "Components/ShapeComponent.h"
#include "DefaultMovementSet/NavMoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "MoveLibrary/GroundMovementUtils.h"
#include "MoveLibrary/ModularMovement.h"
#include "MoveLibrary/MovementUtils.h"


UNavWalkingMode::UNavWalkingMode()
	: bSweepWhileNavWalking(true)
	, bProjectNavMeshWalking(false)
	, NavMeshProjectionHeightScaleUp(0.67f)
	, NavMeshProjectionHeightScaleDown(1.0f)
	, NavMeshProjectionInterval(0.1f)
	, NavMeshProjectionInterpSpeed(12.f)
	, NavMeshProjectionTimer(0)
	, NavMoverComponent(nullptr)
	, bProjectNavMeshOnBothWorldChannels(true)
{
	GameplayTags.AddTag(Mover_IsOnGround);
	GameplayTags.AddTag(Mover_IsNavWalking);
}

void UNavWalkingMode::OnGenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const
{
	const UMoverComponent* MoverComp = GetMoverComponent();
	const FCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	const FMoverDefaultSyncState* StartingSyncState = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	check(StartingSyncState);

	const float DeltaSeconds = TimeStep.StepMs * 0.001f;
	FFloorCheckResult LastFloorResult;
	FVector MovementNormal;

	UMoverBlackboard* SimBlackboard = MoverComp->GetSimBlackboard_Mutable();

	// Try to use the floor as the basis for the intended move direction (i.e. try to walk along slopes, rather than into them)
	if (SimBlackboard && SimBlackboard->TryGet(CommonBlackboard::LastFloorResult, LastFloorResult) && LastFloorResult.IsWalkableFloor())
	{
		MovementNormal = LastFloorResult.HitResult.ImpactNormal;
	}
	else
	{
		MovementNormal = MoverComp->GetUpDirection();
	}
	
	FRotator IntendedOrientation_WorldSpace;
	// If there's no intent from input to change orientation, use the current orientation
	if (!CharacterInputs || CharacterInputs->OrientationIntent.IsNearlyZero())
	{
		IntendedOrientation_WorldSpace = StartingSyncState->GetOrientation_WorldSpace();
	}
	else
	{
		IntendedOrientation_WorldSpace = CharacterInputs->GetOrientationIntentDir_WorldSpace().ToOrientationRotator();
	}
	
	FGroundMoveParams Params;

	if (CharacterInputs)
	{
		Params.MoveInputType = CharacterInputs->GetMoveInputType();
		Params.MoveInput = CharacterInputs->GetMoveInput_WorldSpace();
	}
	else
	{
		Params.MoveInputType = EMoveInputType::Invalid;
		Params.MoveInput = FVector::ZeroVector;
	}

	Params.OrientationIntent = IntendedOrientation_WorldSpace;
	Params.PriorVelocity = FVector::VectorPlaneProject(StartingSyncState->GetVelocity_WorldSpace(), MovementNormal);
	Params.PriorOrientation = StartingSyncState->GetOrientation_WorldSpace();
	Params.GroundNormal = MovementNormal;
	Params.TurningRate = CommonLegacySettings->TurningRate;
	Params.TurningBoost = CommonLegacySettings->TurningBoost;
	Params.MaxSpeed = CommonLegacySettings->MaxSpeed;
	Params.Acceleration = CommonLegacySettings->Acceleration;
	Params.Deceleration = CommonLegacySettings->Deceleration;
	Params.DeltaSeconds = DeltaSeconds;
	
	if (Params.MoveInput.SizeSquared() > 0.f && !UMovementUtils::IsExceedingMaxSpeed(Params.PriorVelocity, CommonLegacySettings->MaxSpeed))
	{
		Params.Friction = CommonLegacySettings->GroundFriction;
	}
	else
	{
		Params.Friction = CommonLegacySettings->bUseSeparateBrakingFriction ? CommonLegacySettings->BrakingFriction : CommonLegacySettings->GroundFriction;
		Params.Friction *= CommonLegacySettings->BrakingFrictionFactor;
	}

	OutProposedMove = UGroundMovementUtils::ComputeControlledGroundMove(Params);

	if (TurnGenerator)
	{
		OutProposedMove.AngularVelocity = ITurnGeneratorInterface::Execute_GetTurn(TurnGenerator, IntendedOrientation_WorldSpace, StartState, *StartingSyncState, TimeStep, OutProposedMove, SimBlackboard);
	}
}

void UNavWalkingMode::OnSimulationTick(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
	const UMoverComponent* MoverComp = Cast<UMoverComponent>(GetMoverComponent());
	ensureMsgf(MoverComp, TEXT("Nav Walking Mode couldn't find a valid MoverComponent!"));
	
	const FMoverTickStartData& StartState = Params.StartState;
	USceneComponent* UpdatedComponent = Params.MovingComps.UpdatedComponent.Get();
	UPrimitiveComponent* UpdatedPrimitive = Params.MovingComps.UpdatedPrimitive.Get();
	FProposedMove ProposedMove = Params.ProposedMove;
	FVector UpDirection = MoverComp->GetUpDirection();

	if (!UpdatedComponent || !UpdatedPrimitive)
	{
		return;
	}

	const FCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	const FMoverDefaultSyncState* StartingSyncState = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	check(StartingSyncState);

	FMoverDefaultSyncState& OutputSyncState = OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	
	const float DeltaSeconds = Params.TimeStep.StepMs * 0.001f;
	const FVector OrigMoveDelta = ProposedMove.LinearVelocity * DeltaSeconds;

	TObjectPtr<AActor> OwnerActor = UpdatedComponent->GetOwner();
	check(OwnerActor);

	FMovementRecord MoveRecord;
	MoveRecord.SetDeltaSeconds(DeltaSeconds);

	OutputSyncState.MoveDirectionIntent = (ProposedMove.bHasDirIntent ? ProposedMove.DirectionIntent : FVector::ZeroVector);

	const FRotator StartingOrient = StartingSyncState->GetOrientation_WorldSpace();
	FRotator TargetOrient = StartingOrient;

	// Apply orientation changes (if any)
	if (!UMovementUtils::IsAngularVelocityZero(ProposedMove.AngularVelocity))
	{ 
		TargetOrient += (ProposedMove.AngularVelocity * DeltaSeconds);
	}
	
	const FQuat TargetOrientQuat = TargetOrient.Quaternion();

	
	FVector StartingLocation = NavMoverComponent->GetFeetLocation();
	FVector AdjustedDest = StartingLocation + OrigMoveDelta;
	
	FFloorCheckResult CurrentFloor; 
	const bool bDeltaMoveNearlyZero = OrigMoveDelta.IsNearlyZero();
	FNavLocation DestNavLocation;

	float SimpleRadius = 0;
	float SimpleHalfHeight = 0;
	NavMoverComponent->GetSimpleCollisionCylinder(SimpleRadius, SimpleHalfHeight);
	
	bool bSameNavLocation = false;
	if (CachedNavLocation.NodeRef != INVALID_NAVNODEREF)
	{
		if (bProjectNavMeshWalking)
		{
			const float DistSq = UMovementUtils::ProjectToGravityFloor(StartingLocation - CachedNavLocation.Location, UpDirection).SizeSquared();
			const float DistDot = FMath::Abs((StartingLocation - CachedNavLocation.Location).Dot(UpDirection));
			
			const float TotalCapsuleHeight = SimpleHalfHeight * 2.0f;
			const float ProjectionScale = (StartingLocation.Dot(UpDirection) > CachedNavLocation.Location.Dot(UpDirection)) ? NavMeshProjectionHeightScaleUp : NavMeshProjectionHeightScaleDown;
			const float DistThr = TotalCapsuleHeight * FMath::Max(0.f, ProjectionScale);

			bSameNavLocation = (DistSq <= UE_KINDA_SMALL_NUMBER) && (DistDot < DistThr);
		}
		else
		{
			bSameNavLocation = CachedNavLocation.Location.Equals(StartingLocation);
		}

		if (bDeltaMoveNearlyZero && bSameNavLocation)
		{
			if (const INavigationDataInterface* NavData = GetNavData())
			{
				if (!NavData->IsNodeRefValid(CachedNavLocation.NodeRef))
				{
					CachedNavLocation.NodeRef = INVALID_NAVNODEREF;
					bSameNavLocation = false;
				}
			}
		}
	}

	if (bDeltaMoveNearlyZero && bSameNavLocation)
	{
		DestNavLocation = CachedNavLocation;
		UE_LOG(LogMover, VeryVerbose, TEXT("%s using cached navmesh location! (bProjectNavMeshWalking = %d)"), *GetNameSafe(GetMoverComponent()->GetOwner()), bProjectNavMeshWalking);
	}
	else
	{
		// Start the trace from the vertical location of the last valid trace.
		// Otherwise if we are projecting our location to the underlying geometry and it's far above or below the navmesh,
		// we'll follow that geometry's plane out of range of valid navigation.
		if (bSameNavLocation && bProjectNavMeshWalking)
		{
			UMovementUtils::SetGravityVerticalComponent(AdjustedDest, CachedNavLocation.Location.Dot(UpDirection), UpDirection);
		}

		// Find the point on the NavMesh
		const bool bHasNavigationData = FindNavFloor(AdjustedDest, DestNavLocation);
		if (!bHasNavigationData)
		{
			OutputState.MovementEndState.NextModeName = DefaultModeNames::Walking;
			OutputState.MovementEndState.RemainingMs = Params.TimeStep.StepMs;
			MoveRecord.SetDeltaSeconds((Params.TimeStep.StepMs - OutputState.MovementEndState.RemainingMs) * 0.001f);
			
			CaptureFinalState(UpdatedComponent, true, CurrentFloor, MoveRecord, OutputSyncState);
			return;
		}

		CachedNavLocation = DestNavLocation;
	}
	
	if (DestNavLocation.NodeRef != INVALID_NAVNODEREF)
	{
		FVector NewLocation = UMovementUtils::ProjectToGravityFloor(AdjustedDest, UpDirection) + UMovementUtils::GetGravityVerticalComponent(DestNavLocation.Location, UpDirection);
		if (bProjectNavMeshWalking)
		{
			const float TotalCapsuleHeight = SimpleHalfHeight * 2.0f;
			const float UpOffset = TotalCapsuleHeight * FMath::Max(0.f, NavMeshProjectionHeightScaleUp);
			const float DownOffset = TotalCapsuleHeight * FMath::Max(0.f, NavMeshProjectionHeightScaleDown);
			NewLocation = ProjectLocationFromNavMesh(DeltaSeconds, StartingLocation, NewLocation, UpOffset, DownOffset);
		}

		FVector AdjustedDelta = NewLocation - StartingLocation;
		
		if (!AdjustedDelta.IsNearlyZero())
		{
			FHitResult MoveHitResult;
			UMovementUtils::TrySafeMoveUpdatedComponent(Params.MovingComps, AdjustedDelta, TargetOrientQuat, bSweepWhileNavWalking, MoveHitResult, ETeleportType::None, MoveRecord);
		}
	}
	else
	{
		OutputState.MovementEndState.NextModeName = CommonLegacySettings->AirMovementModeName;
		OutputState.MovementEndState.RemainingMs = Params.TimeStep.StepMs;
		MoveRecord.SetDeltaSeconds((Params.TimeStep.StepMs - OutputState.MovementEndState.RemainingMs) * 0.001f);
	}

	CaptureFinalState(UpdatedComponent, true, CurrentFloor, MoveRecord, OutputSyncState);
}

bool UNavWalkingMode::FindNavFloor(const FVector& TestLocation, FNavLocation& NavFloorLocation) const
{
	const INavigationDataInterface* NavData = GetNavData();
	if (NavData == nullptr || NavMoverComponent == nullptr)
	{
		return false;
	}

	const FNavAgentProperties& AgentProps = NavMoverComponent->GetNavAgentPropertiesRef();
	const float SearchRadius = AgentProps.AgentRadius * 2.0f;
	const float SearchHeight = AgentProps.AgentHeight * AgentProps.NavWalkingSearchHeightScale;

	return NavData->ProjectPoint(TestLocation, NavFloorLocation, FVector(SearchRadius, SearchRadius, SearchHeight));
}

UObject* UNavWalkingMode::GetTurnGenerator()
{
	return TurnGenerator;
}

void UNavWalkingMode::SetTurnGeneratorClass(TSubclassOf<UObject> TurnGeneratorClass)
{
	if (TurnGeneratorClass)
	{
		TurnGenerator = NewObject<UObject>(this, TurnGeneratorClass);
	}
	else
	{
		TurnGenerator = nullptr; // Clearing the turn generator is valid - will go back to the default turn generation
	}
}

void UNavWalkingMode::SetCollisionForNavWalking(bool bEnable)
{
	if (const UMoverComponent* MoverComponent = GetMoverComponent())
	{
		if (UPrimitiveComponent* UpdatedCompAsPrimitive = Cast<UPrimitiveComponent>(MoverComponent->GetUpdatedComponent()))
		{
			if (bEnable)
			{
				// TODO NS: Eventually might want make the new collision response an option so they trigger certain things while still not colliding 
				UpdatedCompAsPrimitive->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
				UpdatedCompAsPrimitive->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
			
				if (UNavWalkingMode* NavWalkingMode = Cast<UNavWalkingMode>(MoverComponent->FindMovementMode(UNavWalkingMode::StaticClass())))
				{
					if (UMoverBlackboard* SimBlackboard = MoverComponent->GetSimBlackboard_Mutable())
					{
						SimBlackboard->Invalidate(NavWalkingBlackBoard::ProjectedNavMeshHitResult);
					}
					
					// Stagger timed updates so many different characters spawned at the same time don't update on the same frame.
					// Initially we want an immediate update though, so set time to a negative randomized range.
					NavWalkingMode->NavMeshProjectionTimer = (NavWalkingMode->NavMeshProjectionInterval > 0.f) ? FMath::FRandRange(-NavWalkingMode->NavMeshProjectionInterval, 0.f) : 0.f;
				}
			}
			else
			{
				// Grabbing the original shape settings and reverting to our old collision responses
				if (const UShapeComponent* OriginalShapeComp = UMovementUtils::GetOriginalComponentType<UShapeComponent>(MoverComponent->GetOwner()))
				{
					UpdatedCompAsPrimitive->SetCollisionResponseToChannel(ECC_WorldStatic, OriginalShapeComp->GetCollisionResponseToChannel(ECC_WorldStatic));
					UpdatedCompAsPrimitive->SetCollisionResponseToChannel(ECC_WorldDynamic, OriginalShapeComp->GetCollisionResponseToChannel(ECC_WorldDynamic));
				}
			}
		}
	}
}

void UNavWalkingMode::OnActivate()
{
	SetCollisionForNavWalking(true);
}

void UNavWalkingMode::OnDeactivate()
{
	SetCollisionForNavWalking(false);
}

const INavigationDataInterface* UNavWalkingMode::GetNavData() const
{
	const UWorld* World = GetWorld();
	
	const AActor* MoverComponentOwner;
	if (const UMoverComponent* MoverComponent = GetMoverComponent())
	{
		MoverComponentOwner = MoverComponent->GetOwner();
		if (World == nullptr || World->GetNavigationSystem() == nullptr || MoverComponentOwner == nullptr)
		{
			return nullptr;
		}
	}
	else
	{
		return nullptr;
	}
	

	const INavigationDataInterface* NavData = FNavigationSystem::GetNavDataForActor(*MoverComponentOwner);

	return NavData;
}

void UNavWalkingMode::FindBestNavMeshLocation(const FVector& TraceStart, const FVector& TraceEnd, const FVector& CurrentFeetLocation, const FVector& TargetNavLocation, FHitResult& OutHitResult) const
{
	// raycast to underlying mesh to allow us to more closely follow geometry
	// we use static objects here as a best approximation to accept only objects that
	// influence navmesh generation
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectLocation), false);

	// blocked by world static and optionally world dynamic
	FCollisionResponseParams ResponseParams(ECR_Ignore);
	ResponseParams.CollisionResponse.SetResponse(ECC_WorldStatic, ECR_Overlap);
	ResponseParams.CollisionResponse.SetResponse(ECC_WorldDynamic, bProjectNavMeshOnBothWorldChannels ? ECR_Overlap : ECR_Ignore);

	TArray<FHitResult> MultiTraceHits;
	GetWorld()->LineTraceMultiByChannel(MultiTraceHits, TraceStart, TraceEnd, ECC_WorldStatic, Params, ResponseParams);

	struct FCompareFHitResultNavMeshTrace
	{
		explicit FCompareFHitResultNavMeshTrace(const FVector& inSourceLocation) : SourceLocation(inSourceLocation)
		{
		}

		FORCEINLINE bool operator()(const FHitResult& A, const FHitResult& B) const
		{
			const float ADistSqr = (SourceLocation - A.ImpactPoint).SizeSquared();
			const float BDistSqr = (SourceLocation - B.ImpactPoint).SizeSquared();

			return (ADistSqr < BDistSqr);
		}

		const FVector& SourceLocation;
	};

	struct FRemoveNotBlockingResponseNavMeshTrace
	{
		FRemoveNotBlockingResponseNavMeshTrace(bool bInCheckOnlyWorldStatic) : bCheckOnlyWorldStatic(bInCheckOnlyWorldStatic) {}

		FORCEINLINE bool operator()(const FHitResult& TestHit) const
		{
			UPrimitiveComponent* PrimComp = TestHit.GetComponent();
			const bool bBlockOnWorldStatic = PrimComp && (PrimComp->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
			const bool bBlockOnWorldDynamic = PrimComp && (PrimComp->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);

			return !bBlockOnWorldStatic && (!bBlockOnWorldDynamic || bCheckOnlyWorldStatic);
		}

		bool bCheckOnlyWorldStatic;
	};

	MultiTraceHits.RemoveAllSwap(FRemoveNotBlockingResponseNavMeshTrace(!bProjectNavMeshOnBothWorldChannels), EAllowShrinking::No);
	if (MultiTraceHits.Num() > 0)
	{
		// Sort the hits by the closest to our origin.
		MultiTraceHits.Sort(FCompareFHitResultNavMeshTrace(TargetNavLocation));

		// Cache the closest hit and treat it as a blocking hit (we used an overlap to get all the world static hits so we could sort them ourselves)
		OutHitResult = MultiTraceHits[0];
		OutHitResult.bBlockingHit = true;
	}
}

FVector UNavWalkingMode::ProjectLocationFromNavMesh(float DeltaSeconds, const FVector& CurrentFeetLocation, const FVector& TargetNavLocation, float UpOffset, float DownOffset)
{
	FVector NewLocation = TargetNavLocation;

	const float VerticalOffset = -(DownOffset + UpOffset);
	if (VerticalOffset > -UE_SMALL_NUMBER)
	{
		return NewLocation;
	}

	const UMoverComponent* MoverComp = GetMoverComponent();
	const FVector UpDirection = MoverComp->GetUpDirection();
	
	const FVector TraceStart = TargetNavLocation + UpOffset * UpDirection;
	const FVector TraceEnd   = TargetNavLocation + DownOffset * -UpDirection;

	FHitResult CachedProjectedNavMeshHitResult;
	UMoverBlackboard* SimBlackboard = MoverComp->GetSimBlackboard_Mutable();
	SimBlackboard->TryGet(NavWalkingBlackBoard::ProjectedNavMeshHitResult, CachedProjectedNavMeshHitResult);
	
	// We can skip this trace if we are checking at the same location as the last trace (ie, we haven't moved).
	const bool bCachedLocationStillValid = (CachedProjectedNavMeshHitResult.bBlockingHit &&
											CachedProjectedNavMeshHitResult.TraceStart == TraceStart &&
											CachedProjectedNavMeshHitResult.TraceEnd == TraceEnd);

	NavMeshProjectionTimer -= DeltaSeconds;
	if (NavMeshProjectionTimer <= 0.0f)
	{
		if (!bCachedLocationStillValid)
		{
			UE_LOG(LogMover, VeryVerbose, TEXT("ProjectLocationFromNavMesh(): %s interval: %.3f [SKIP TRACE]"), *GetNameSafe(GetMoverComponent()->GetOwner()), NavMeshProjectionInterval);

			FHitResult HitResult;
			FindBestNavMeshLocation(TraceStart, TraceEnd, CurrentFeetLocation, TargetNavLocation, HitResult);

			// discard result if we were already inside something
			if (HitResult.bStartPenetrating || !HitResult.bBlockingHit)
			{
				CachedProjectedNavMeshHitResult.Reset();
				SimBlackboard->Invalidate(NavWalkingBlackBoard::ProjectedNavMeshHitResult);
			}
			else
			{
				CachedProjectedNavMeshHitResult = HitResult;
				SimBlackboard->Set(NavWalkingBlackBoard::ProjectedNavMeshHitResult, CachedProjectedNavMeshHitResult);
			}
		}
		else
		{
			UE_LOG(LogMover, VeryVerbose, TEXT("ProjectLocationFromNavMesh(): %s interval: %.3f [SKIP TRACE]"), *GetNameSafe(GetMoverComponent()->GetOwner()), NavMeshProjectionInterval);
		}

		// Wrap around to maintain same relative offset to tick time changes.
		// Prevents large framerate spikes from aligning multiple characters to the same frame (if they start staggered, they will now remain staggered).
		float ModTime = 0.f;
		if (NavMeshProjectionInterval > UE_SMALL_NUMBER)
		{
			ModTime = FMath::Fmod(-NavMeshProjectionTimer, NavMeshProjectionInterval);
		}

		NavMeshProjectionTimer = NavMeshProjectionInterval - ModTime;
	}
	
	// Project to last plane we found.
	if (CachedProjectedNavMeshHitResult.bBlockingHit)
	{
		if (bCachedLocationStillValid && FMath::IsNearlyEqual(CurrentFeetLocation.Dot(UpDirection), CachedProjectedNavMeshHitResult.ImpactPoint.Dot(UpDirection), (FVector::FReal)0.01f))
		{
			// Already at destination.
			UMovementUtils::SetGravityVerticalComponent(NewLocation, CurrentFeetLocation.Dot(UpDirection), UpDirection);
		}
		else
		{
			const FVector ProjectedPoint = FMath::LinePlaneIntersection(TraceStart, TraceEnd, CachedProjectedNavMeshHitResult.ImpactPoint, CachedProjectedNavMeshHitResult.Normal);
			FVector::FReal ProjectedVertical = ProjectedPoint.Dot(UpDirection);
			
			// Limit to not be too far above or below NavMesh location
			const FVector::FReal VertTraceStart = TraceStart.Dot(UpDirection);
			const FVector::FReal VertTraceEnd = TraceEnd.Dot(UpDirection);
			const FVector::FReal TraceMin = FMath::Min(VertTraceStart, VertTraceEnd);
			const FVector::FReal TraceMax = FMath::Max(VertTraceStart, VertTraceEnd);
			ProjectedVertical = FMath::Clamp(ProjectedVertical, TraceMin, TraceMax);

			// Interp for smoother updates (less "pop" when trace hits something new). 0 interp speed is instant.
			const FVector::FReal InterpSpeed = FMath::Max<FVector::FReal>(0.f, NavMeshProjectionInterpSpeed);
			ProjectedVertical = FMath::FInterpTo(CurrentFeetLocation.Dot(UpDirection), ProjectedVertical, (FVector::FReal)DeltaSeconds, InterpSpeed);
			ProjectedVertical = FMath::Clamp(ProjectedVertical, TraceMin, TraceMax);
			
			// Final result
			UMovementUtils::SetGravityVerticalComponent(NewLocation, ProjectedVertical, UpDirection);
		}
	}

	return NewLocation;
}

void UNavWalkingMode::OnRegistered(const FName ModeName)
{
	Super::OnRegistered(ModeName);

	UMoverComponent* MoverComponent = GetMoverComponent();
	CommonLegacySettings = MoverComponent->FindSharedSettings<UCommonLegacyMovementSettings>();
	ensureMsgf(CommonLegacySettings, TEXT("Failed to find instance of CommonLegacyMovementSettings on %s. Movement may not function properly."), *GetPathNameSafe(this));
	
	if (const AActor* MoverCompOwner = MoverComponent->GetOwner())
	{
		NavMoverComponent = MoverCompOwner->FindComponentByClass<UNavMoverComponent>();
	}
	
	if (!NavMoverComponent)
	{
		UE_LOG(LogMover, Warning, TEXT("NavWalkingMode on %s could not find a valid NavMoverComponent and will not function properly."), *GetNameSafe(GetMoverComponent()->GetOwner()));
	}
}

void UNavWalkingMode::OnUnregistered()
{
	CommonLegacySettings = nullptr;

	Super::OnUnregistered();
}

void UNavWalkingMode::CaptureFinalState(USceneComponent* UpdatedComponent, bool bDidAttemptMovement, const FFloorCheckResult& FloorResult, const FMovementRecord& Record, FMoverDefaultSyncState& OutputSyncState) const
{
	FRelativeBaseInfo PriorBaseInfo;

	UMoverBlackboard* SimBlackboard = GetMoverComponent()->GetSimBlackboard_Mutable();

	const bool bHasPriorBaseInfo = SimBlackboard->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, PriorBaseInfo);

	FRelativeBaseInfo CurrentBaseInfo = UpdateFloorAndBaseInfo(FloorResult);

	// If we're on a dynamic base and we're not trying to move, keep using the same relative actor location. This prevents slow relative 
	//  drifting that can occur from repeated floor sampling as the base moves through the world.
	if (CurrentBaseInfo.HasRelativeInfo() 
		&& bHasPriorBaseInfo && !bDidAttemptMovement 
		&& PriorBaseInfo.UsesSameBase(CurrentBaseInfo))
	{
		CurrentBaseInfo.ContactLocalPosition = PriorBaseInfo.ContactLocalPosition;
	}
	
	if (CurrentBaseInfo.HasRelativeInfo())
	{
		SimBlackboard->Set(CommonBlackboard::LastFoundDynamicMovementBase, CurrentBaseInfo);

		OutputSyncState.SetTransforms_WorldSpace( UpdatedComponent->GetComponentLocation(),
												  UpdatedComponent->GetComponentRotation(),
												  Record.GetRelevantVelocity(),
												  CurrentBaseInfo.MovementBase.Get(), CurrentBaseInfo.BoneName);
	}
	else
	{
		SimBlackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);

		OutputSyncState.SetTransforms_WorldSpace( UpdatedComponent->GetComponentLocation(),
												  UpdatedComponent->GetComponentRotation(),
												  Record.GetRelevantVelocity(),
												  nullptr);	// no movement base
	}

	UpdatedComponent->ComponentVelocity = OutputSyncState.GetVelocity_WorldSpace();
}

FRelativeBaseInfo UNavWalkingMode::UpdateFloorAndBaseInfo(const FFloorCheckResult& FloorResult) const
{
	FRelativeBaseInfo ReturnBaseInfo;

	UMoverBlackboard* SimBlackboard = GetMoverComponent()->GetSimBlackboard_Mutable();

	SimBlackboard->Set(CommonBlackboard::LastFloorResult, FloorResult);

	if (FloorResult.IsWalkableFloor() && UBasedMovementUtils::IsADynamicBase(FloorResult.HitResult.GetComponent()))
	{
		ReturnBaseInfo.SetFromFloorResult(FloorResult);
	}

	return ReturnBaseInfo;
}


