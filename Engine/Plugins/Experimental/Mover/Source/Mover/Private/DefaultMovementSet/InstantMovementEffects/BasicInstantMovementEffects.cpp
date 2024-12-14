// Copyright Epic Games, Inc. All Rights Reserved.


#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"

#include "MoverComponent.h"
#include "MoverDataModelTypes.h"
#include "MoverSimulationTypes.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/MovementUtils.h"
#include "MoveLibrary/MoverBlackboard.h"


// -------------------------------------------------------------------
// FTeleportEffect
// -------------------------------------------------------------------

FTeleportEffect::FTeleportEffect()
	: TargetLocation(FVector::ZeroVector)
{
}

bool FTeleportEffect::ApplyMovementEffect(FApplyMovementEffectParams& ApplyEffectParams, FMoverSyncState& OutputState)
{
	if (ApplyEffectParams.UpdatedComponent->GetOwner()->TeleportTo(TargetLocation, ApplyEffectParams.UpdatedComponent->GetComponentRotation()))
	{
		FMoverDefaultSyncState& OutputSyncState = OutputState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
		
		if (const FMoverDefaultSyncState* StartingSyncState = ApplyEffectParams.StartState->SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
		{
			OutputSyncState.SetTransforms_WorldSpace( ApplyEffectParams.UpdatedComponent->GetComponentLocation(),
													  ApplyEffectParams.UpdatedComponent->GetComponentRotation(),
													  OutputSyncState.GetVelocity_WorldSpace(),
													  nullptr ); // no movement base
		
			// TODO: instead of invalidating it, consider checking for a floor. Possibly a dynamic base?
			if (UMoverBlackboard* SimBlackboard = ApplyEffectParams.MoverComp->GetSimBlackboard_Mutable())
			{
				SimBlackboard->Invalidate(CommonBlackboard::LastFloorResult);
				SimBlackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);
			}

			return true;
		}
	}

	return false;
}

FInstantMovementEffect* FTeleportEffect::Clone() const
{
	FTeleportEffect* CopyPtr = new FTeleportEffect(*this);
	return CopyPtr;
}

void FTeleportEffect::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	Ar << TargetLocation;
}

UScriptStruct* FTeleportEffect::GetScriptStruct() const
{
	return FTeleportEffect::StaticStruct();
}

FString FTeleportEffect::ToSimpleString() const
{
	return FString::Printf(TEXT("Teleport"));
}

void FTeleportEffect::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

// -------------------------------------------------------------------
// FJumpImpulseEffect
// -------------------------------------------------------------------

FJumpImpulseEffect::FJumpImpulseEffect()
	: UpwardsSpeed(0.f)
{
}

bool FJumpImpulseEffect::ApplyMovementEffect(FApplyMovementEffectParams& ApplyEffectParams, FMoverSyncState& OutputState)
{
	if (const FMoverDefaultSyncState* SyncState = ApplyEffectParams.StartState->SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
	{
		FMoverDefaultSyncState& OutputSyncState = OutputState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
		
		const FVector UpDir = ApplyEffectParams.MoverComp->GetUpDirection();
		const FVector ImpulseVelocity = UpDir * UpwardsSpeed;
	
		// Jump impulse overrides vertical velocity while maintaining the rest
		const FVector PriorVelocityWS = SyncState->GetVelocity_WorldSpace();
		const FVector StartingNonUpwardsVelocity = PriorVelocityWS - PriorVelocityWS.ProjectOnToNormal(UpDir);

		if (const UCommonLegacyMovementSettings* CommonSettings = ApplyEffectParams.MoverComp->FindSharedSettings<UCommonLegacyMovementSettings>())
		{
			OutputState.MovementMode = CommonSettings->AirMovementModeName;
		}
		
		FRelativeBaseInfo MovementBaseInfo;
		if (const UMoverBlackboard* SimBlackboard = ApplyEffectParams.MoverComp->GetSimBlackboard())
		{
			SimBlackboard->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, MovementBaseInfo);
		}

		const FVector FinalVelocity = StartingNonUpwardsVelocity + ImpulseVelocity;
		OutputSyncState.SetTransforms_WorldSpace( ApplyEffectParams.UpdatedComponent->GetComponentLocation(),
												  ApplyEffectParams.UpdatedComponent->GetComponentRotation(),
												  FinalVelocity,
												  MovementBaseInfo.MovementBase.Get(),
												  MovementBaseInfo.BoneName);
		
		ApplyEffectParams.UpdatedComponent->ComponentVelocity = FinalVelocity;
		
		return true;
	}

	return false;
}

FInstantMovementEffect* FJumpImpulseEffect::Clone() const
{
	FJumpImpulseEffect* CopyPtr = new FJumpImpulseEffect(*this);
	return CopyPtr;
}

void FJumpImpulseEffect::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	Ar << UpwardsSpeed;
}

UScriptStruct* FJumpImpulseEffect::GetScriptStruct() const
{
	return FJumpImpulseEffect::StaticStruct();
}

FString FJumpImpulseEffect::ToSimpleString() const
{
	return FString::Printf(TEXT("JumpImpulse"));
}

void FJumpImpulseEffect::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

// -------------------------------------------------------------------
// FApplyVelocityEffect
// -------------------------------------------------------------------

FApplyVelocityEffect::FApplyVelocityEffect()
	: VelocityToApply(FVector::ZeroVector)
	, bAdditiveVelocity(false)
	, ForceMovementMode(NAME_None)
{
}

bool FApplyVelocityEffect::ApplyMovementEffect(FApplyMovementEffectParams& ApplyEffectParams, FMoverSyncState& OutputState)
{
	FMoverDefaultSyncState& OutputSyncState = OutputState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	
	OutputState.MovementMode = ForceMovementMode;
	
	FRelativeBaseInfo MovementBaseInfo;
	if (const UMoverBlackboard* SimBlackboard = ApplyEffectParams.MoverComp->GetSimBlackboard())
	{
		SimBlackboard->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, MovementBaseInfo);
	}

	FVector Velocity = VelocityToApply;
	if (bAdditiveVelocity)
	{
		if (const FMoverDefaultSyncState* SyncState = ApplyEffectParams.StartState->SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
		{
			Velocity += SyncState->GetVelocity_WorldSpace();
		}
	}

	OutputSyncState.SetTransforms_WorldSpace( ApplyEffectParams.UpdatedComponent->GetComponentLocation(),
											  ApplyEffectParams.UpdatedComponent->GetComponentRotation(),
											  Velocity,
											  MovementBaseInfo.MovementBase.Get(),
											  MovementBaseInfo.BoneName);

	ApplyEffectParams.UpdatedComponent->ComponentVelocity = Velocity;
	
	return true;
}

FInstantMovementEffect* FApplyVelocityEffect::Clone() const
{
	FApplyVelocityEffect* CopyPtr = new FApplyVelocityEffect(*this);
	return CopyPtr;
}

void FApplyVelocityEffect::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	SerializePackedVector<10, 16>(VelocityToApply, Ar);

	Ar << bAdditiveVelocity;
	
	bool bUsingForcedMovementMode = !ForceMovementMode.IsNone();
	Ar.SerializeBits(&bUsingForcedMovementMode, 1);

	if (bUsingForcedMovementMode)
	{
		Ar << ForceMovementMode;
	}
}

UScriptStruct* FApplyVelocityEffect::GetScriptStruct() const
{
	return FApplyVelocityEffect::StaticStruct();
}

FString FApplyVelocityEffect::ToSimpleString() const
{
	return FString::Printf(TEXT("ApplyVelocity"));
}

void FApplyVelocityEffect::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}
