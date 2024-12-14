// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/CharacterMoverComponent.h"

#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"
#include "DefaultMovementSet/Modes/FallingMode.h"
#include "DefaultMovementSet/Modes/FlyingMode.h"
#include "DefaultMovementSet/Modes/WalkingMode.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "MoveLibrary/MovementUtils.h"

UCharacterMoverComponent::UCharacterMoverComponent()
	: bHandleJump(true)
	, bWantsToCrouch(false)
{
	// Default movement modes
	MovementModes.Add(DefaultModeNames::Walking, CreateDefaultSubobject<UWalkingMode>(TEXT("DefaultWalkingMode")));
	MovementModes.Add(DefaultModeNames::Falling, CreateDefaultSubobject<UFallingMode>(TEXT("DefaultFallingMode")));
	MovementModes.Add(DefaultModeNames::Flying,  CreateDefaultSubobject<UFlyingMode>(TEXT("DefaultFlyingMode")));

	StartingMovementMode = DefaultModeNames::Falling;
}

void UCharacterMoverComponent::BeginPlay()
{
	Super::BeginPlay();

	OnPreSimulationTick.AddDynamic(this, &UCharacterMoverComponent::OnMoverPreSimulationTick);
}

bool UCharacterMoverComponent::IsCrouching() const
{
	return HasGameplayTag(Mover_IsCrouching, true);
}

bool UCharacterMoverComponent::IsFlying() const
{
	return HasGameplayTag(Mover_IsFlying, true);
}

bool UCharacterMoverComponent::IsFalling() const
{
	return HasGameplayTag(Mover_IsFalling, true);
}

bool UCharacterMoverComponent::IsAirborne() const
{
	return HasGameplayTag(Mover_IsInAir, true);
}

bool UCharacterMoverComponent::IsOnGround() const
{
	return HasGameplayTag(Mover_IsOnGround, true);
}

bool UCharacterMoverComponent::IsSwimming() const
{
	return HasGameplayTag(Mover_IsSwimming, true);
}

bool UCharacterMoverComponent::IsSlopeSliding() const
{
	if (IsAirborne())
	{
		FFloorCheckResult HitResult;
		const UMoverBlackboard* MoverBlackboard = GetSimBlackboard();
		if (MoverBlackboard->TryGet(CommonBlackboard::LastFloorResult, HitResult))
		{
			return HitResult.bBlockingHit && !HitResult.bWalkableFloor;
		}
	}

	return false;
}

bool UCharacterMoverComponent::CanActorJump() const
{
	return IsOnGround();
}

bool UCharacterMoverComponent::Jump()
{
	if (const UCommonLegacyMovementSettings* CommonSettings = FindSharedSettings<UCommonLegacyMovementSettings>())
	{
		TSharedPtr<FJumpImpulseEffect> JumpMove = MakeShared<FJumpImpulseEffect>();
		JumpMove->UpwardsSpeed = CommonSettings->JumpUpwardsSpeed;
		
		QueueInstantMovementEffect(JumpMove);

		return true;
	}

	return false;
}

bool UCharacterMoverComponent::CanCrouch()
{
	return true;
}

void UCharacterMoverComponent::Crouch()
{
	if (CanCrouch())
	{
		bWantsToCrouch = true;
	}
}

void UCharacterMoverComponent::UnCrouch()
{
	bWantsToCrouch = false;
}

void UCharacterMoverComponent::OnMoverPreSimulationTick(const FMoverTimeStep& TimeStep, const FMoverInputCmdContext& InputCmd)
{
	if (bHandleJump)
	{
		const FCharacterDefaultInputs* CharacterInputs = InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
		if (CharacterInputs && CharacterInputs->bIsJumpJustPressed && CanActorJump())
		{
			Jump();
		}
	}
	
	const FStanceModifier* StanceModifier = static_cast<const FStanceModifier*>(FindMovementModifier(StanceModifierHandle));
	// This is a fail safe in case our handle was bad - try finding the modifier by type if we can
	if (!StanceModifier)
	{
		StanceModifier = FindMovementModifierByType<FStanceModifier>();
	}
	
	EStanceMode OldActiveStance = EStanceMode::Invalid;
	if (StanceModifier)
	{
		OldActiveStance = StanceModifier->ActiveStance;
	}
	
	const bool bIsCrouching = HasGameplayTag(Mover_IsCrouching, true);
	if (bIsCrouching && (!bWantsToCrouch || !CanCrouch()))
	{	
		if (StanceModifier && StanceModifier->CanExpand(this))
		{
			CancelModifierFromHandle(StanceModifier->GetHandle());
			StanceModifierHandle.Invalidate();

			StanceModifier = nullptr;
		}
	}
	else if (!bIsCrouching && bWantsToCrouch && CanCrouch())
	{
		TSharedPtr<FStanceModifier> NewStanceModifier = MakeShared<FStanceModifier>();
		StanceModifierHandle = QueueMovementModifier(NewStanceModifier);

		StanceModifier = NewStanceModifier.Get();
	}
	
	EStanceMode NewActiveStance = EStanceMode::Invalid;
	if (StanceModifier)
	{
		NewActiveStance = StanceModifier->ActiveStance;
	}

	if (OldActiveStance != NewActiveStance)
	{
		OnStanceChanged.Broadcast(OldActiveStance, NewActiveStance);
	}
}
