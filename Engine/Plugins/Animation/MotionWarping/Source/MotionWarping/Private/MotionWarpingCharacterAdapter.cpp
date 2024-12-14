// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionWarpingCharacterAdapter.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"



void UMotionWarpingCharacterAdapter::BeginDestroy()
{
	if (TargetCharacter && TargetCharacter->GetCharacterMovement())
	{
		TargetCharacter->GetCharacterMovement()->ProcessRootMotionPreConvertToWorld.Unbind();
	}

	Super::BeginDestroy();
}

void UMotionWarpingCharacterAdapter::SetCharacter(ACharacter* InCharacter)
{
	if (ensureMsgf(InCharacter && InCharacter->GetCharacterMovement(), TEXT("Invalid Character or missing CharacterMovementComponent. Motion warping will not function.")))
	{
		TargetCharacter = InCharacter;
		TargetCharacter->GetCharacterMovement()->ProcessRootMotionPreConvertToWorld.BindUObject(this, &UMotionWarpingCharacterAdapter::WarpLocalRootMotionOnCharacter);
	}
}

AActor* UMotionWarpingCharacterAdapter::GetActor() const
{ 
	return Cast<AActor>(TargetCharacter);
}

USkeletalMeshComponent* UMotionWarpingCharacterAdapter::GetMesh() const
{ 
	return TargetCharacter->GetMesh();
}

FVector UMotionWarpingCharacterAdapter::GetVisualRootLocation() const
{
	const float CapsuleHalfHeight = TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FQuat CurrentRotation = TargetCharacter->GetActorQuat();
	return  (TargetCharacter->GetActorLocation() - CurrentRotation.GetUpVector() * CapsuleHalfHeight);
}

FVector UMotionWarpingCharacterAdapter::GetBaseVisualTranslationOffset() const
{
	return TargetCharacter->GetBaseTranslationOffset();
}

FQuat UMotionWarpingCharacterAdapter::GetBaseVisualRotationOffset() const
{ 
	return TargetCharacter->GetBaseRotationOffset();
}

FTransform UMotionWarpingCharacterAdapter::WarpLocalRootMotionOnCharacter(const FTransform& LocalRootMotionTransform, UCharacterMovementComponent* TargetMoveComp, float DeltaSeconds)
{
	if (WarpLocalRootMotionDelegate.IsBound() && TargetCharacter)
	{
		FMotionWarpingUpdateContext WarpingContext;
		
		WarpingContext.DeltaSeconds = DeltaSeconds;

		// When replaying saved moves we need to look at the contributor to root motion back then.
		if (TargetCharacter->bClientUpdating)
		{
			const UCharacterMovementComponent* MoveComp = TargetCharacter->GetCharacterMovement();
			check(MoveComp);

			const FSavedMove_Character* SavedMove = MoveComp->GetCurrentReplayedSavedMove();
			check(SavedMove);

			if (SavedMove->RootMotionMontage.IsValid())
			{
				WarpingContext.Animation = SavedMove->RootMotionMontage.Get();
				WarpingContext.CurrentPosition = SavedMove->RootMotionTrackPosition;
				WarpingContext.PreviousPosition = SavedMove->RootMotionPreviousTrackPosition;
				WarpingContext.PlayRate = SavedMove->RootMotionPlayRateWithScale;
			}
		}
		else // If we are not replaying a move, just use the current root motion montage
		{
			if (const FAnimMontageInstance* RootMotionMontageInstance = TargetCharacter->GetRootMotionAnimMontageInstance())
			{
				const UAnimMontage* Montage = RootMotionMontageInstance->Montage;
				check(Montage);

				WarpingContext.Animation = Montage;
				WarpingContext.CurrentPosition = RootMotionMontageInstance->GetPosition();
				WarpingContext.PreviousPosition = RootMotionMontageInstance->GetPreviousPosition();
				WarpingContext.Weight = RootMotionMontageInstance->GetWeight();
				WarpingContext.PlayRate = RootMotionMontageInstance->Montage->RateScale * RootMotionMontageInstance->GetPlayRate();
			}
		}

		// TODO: Consider simply having a pointer to the MWComponent whereby we can call a function on it, rather than using this delegate approach
		return WarpLocalRootMotionDelegate.Execute(LocalRootMotionTransform, DeltaSeconds, &WarpingContext);
	}

	return LocalRootMotionTransform;
}