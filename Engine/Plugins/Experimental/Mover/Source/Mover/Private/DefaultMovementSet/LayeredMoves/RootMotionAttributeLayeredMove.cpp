// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/LayeredMoves/RootMotionAttributeLayeredMove.h"
#include "MoverComponent.h"
#include "Animation/AttributesContainer.h"
#include "Animation/BuiltInAttributeTypes.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "MoveLibrary/MovementUtils.h"
#include "MoverTypes.h"
#include "MoverLog.h"


#if !UE_BUILD_SHIPPING
FAutoConsoleVariable CVarLogRootMotionAttrSteps(
	TEXT("mover.debug.LogRootMotionAttrSteps"),
	false,
	TEXT("Whether to log detailed information about root motion attribute layered moves. 0: Disable, 1: Enable"),
	ECVF_Cheat);

FAutoConsoleVariable CVarDisableRootMotionAttrContributions(
	TEXT("mover.debug.DisableRootMotionAttributes"),
	false,
	TEXT("If enabled, contributions from root motion attributes will be ignored in favor of other Mover influences"),
	ECVF_Cheat);

#endif	// !UE_BUILD_SHIPPING

FLayeredMove_RootMotionAttribute::FLayeredMove_RootMotionAttribute()
{
	DurationMs = -1.f;
	MixMode = EMoveMixMode::OverrideAll;
}

bool FLayeredMove_RootMotionAttribute::GenerateMove(const FMoverTickStartData& SimState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove)
{
#if !UE_BUILD_SHIPPING
	if (CVarDisableRootMotionAttrContributions->GetBool())
	{
		return false;	// do not contribute any movement
	}
#endif // !UE_BUILD_SHIPPING

	const float DeltaSeconds = TimeStep.StepMs / 1000.f;

	bool bDidAttrHaveRootMotion = false;
	FTransform LocalRootMotion;
	bool bHasValidWarpingContext = false;
	FMotionWarpingUpdateContext WarpingContext;

	if (!TimeStep.bIsResimulating)
	{
		// Clear resim values
		bDidAttrHaveRootMotionForResim = false;
		LocalRootMotionForResim = FTransform::Identity;
		WarpingContextForResim = FMotionWarpingUpdateContext();

		// Sample root motion from the mesh attribute
		const USkeletalMeshComponent* Mesh = Cast<const USkeletalMeshComponent>(MoverComp->GetPrimaryVisualComponent());

		if (Mesh)
		{
			// TODO: support options for different interpretations, such as velocity
			static const FName RootMotionAttributeName = "RootMotionDelta";
			static const UE::Anim::FAttributeId RootMotionAttributeId = { RootMotionAttributeName, FCompactPoseBoneIndex(0) };

			if (const FTransformAnimationAttribute* RootMotionAttribute = Mesh->GetCustomAttributes().Find<FTransformAnimationAttribute>(RootMotionAttributeId))
			{
				// NOTE this will only work for ticking modes that tick in time with the world tick, because it relies on the Mesh ticking at the same rate as the movement simulation. 
				// For fixed-tick modes, the attribute would be better as an accumulator for the movement sim to consume, along with a time accumulation attribute.
				LocalRootMotion = RootMotionAttribute->Value;
				bDidAttrHaveRootMotion = true;
			}
		}

		static const bool bShouldWarpFromMontage = true;

		if (bDidAttrHaveRootMotion && bShouldWarpFromMontage)
		{
			// Not resimulating: we are following along with any root motion montages
			const FAnimMontageInstance* RootMotionMontageInstance = Mesh && Mesh->GetAnimInstance() ? Mesh->GetAnimInstance()->GetRootMotionMontageInstance() : nullptr;

			if (RootMotionMontageInstance)
			{
				const UAnimMontage* Montage = RootMotionMontageInstance->Montage;

				WarpingContext.DeltaSeconds = DeltaSeconds;
				WarpingContext.Animation = Montage;
				WarpingContext.CurrentPosition = RootMotionMontageInstance->GetPosition();
				WarpingContext.PreviousPosition = RootMotionMontageInstance->GetPreviousPosition();
				WarpingContext.Weight = RootMotionMontageInstance->GetWeight();
				WarpingContext.PlayRate = RootMotionMontageInstance->Montage->RateScale * RootMotionMontageInstance->GetPlayRate();

				bHasValidWarpingContext = true;
			}
		}

		// Save values for resim
		{
			bDidAttrHaveRootMotionForResim = bDidAttrHaveRootMotion;
			LocalRootMotionForResim = LocalRootMotion;

			if (bHasValidWarpingContext)
			{
				WarpingContextForResim = WarpingContext;
			}
		}

	}
	else   // resimulating...
	{
		// restore the cached transform and warping parameters (if set)
		bDidAttrHaveRootMotion	= bDidAttrHaveRootMotionForResim;
		LocalRootMotion			= LocalRootMotionForResim;
		bHasValidWarpingContext = WarpingContextForResim.Animation != nullptr;
		WarpingContext			= WarpingContextForResim;
	}

	
	if (bDidAttrHaveRootMotion)
	{
		const FMoverDefaultSyncState* SyncState = SimState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
		const FCharacterDefaultInputs* InputCmd = SimState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();

		// TODO: Provide better options to control when we should skip proposed movement
		if (!bHasValidWarpingContext &&
				((InputCmd && InputCmd->bIsJumpJustPressed) || MoverComp->HasGameplayTag(Mover_IsFalling, false)))
		{
			return false;	// do not perform root motion while jumping or falling, so that we can have air control, unless we're under montage influence
		}

		// Note that we're forcing the use of the sync state's actor transform data. This is necessary when the movement simulation 
		// is running ahead of the actor's visual representation and may be rotated differently, such as in an async physics sim.
		const FTransform SimActorTransform = FTransform(SyncState->GetOrientation_WorldSpace().Quaternion(), SyncState->GetLocation_WorldSpace());
		const FTransform WorldSpaceRootMotion = MoverComp->ConvertLocalRootMotionToWorld(LocalRootMotion, DeltaSeconds, &SimActorTransform, (bHasValidWarpingContext ? &WarpingContext : nullptr));

		OutProposedMove = FProposedMove();
		OutProposedMove.MixMode = MixMode;

		// Convert the transform into linear and angular velocities
		OutProposedMove.LinearVelocity  = WorldSpaceRootMotion.GetTranslation() * (1.f / DeltaSeconds);
		OutProposedMove.AngularVelocity = WorldSpaceRootMotion.GetRotation().Rotator() * (1.f / DeltaSeconds);

#if !UE_BUILD_SHIPPING
		UE_CLOG(CVarLogRootMotionAttrSteps->GetBool(), LogMover, Log, TEXT("RootMotionAttr. SimF %i (dt %.3f) => LocalT: %s (WST: %s)  Vel: %.3f"),
			TimeStep.ServerFrame, DeltaSeconds,
			*LocalRootMotion.GetTranslation().ToString(), *WorldSpaceRootMotion.GetTranslation().ToString(), OutProposedMove.LinearVelocity.Length());
#endif // !UE_BUILD_SHIPPING

		return true;
	}

	return false;
}

FLayeredMoveBase* FLayeredMove_RootMotionAttribute::Clone() const
{
	FLayeredMove_RootMotionAttribute* CopyPtr = new FLayeredMove_RootMotionAttribute(*this);
	return CopyPtr;
}

void FLayeredMove_RootMotionAttribute::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);
}

UScriptStruct* FLayeredMove_RootMotionAttribute::GetScriptStruct() const
{
	return FLayeredMove_RootMotionAttribute::StaticStruct();
}

FString FLayeredMove_RootMotionAttribute::ToSimpleString() const
{
	return FString::Printf(TEXT("RootMotionAttribute"));
}

void FLayeredMove_RootMotionAttribute::AddReferencedObjects(class FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}
