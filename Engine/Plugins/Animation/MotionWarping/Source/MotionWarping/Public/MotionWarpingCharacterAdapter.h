// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MotionWarpingAdapter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingCharacterAdapter.generated.h"

// Adapter for Character / ChararacterMovementComponent actors to participate in motion warping
UCLASS()
class MOTIONWARPING_API UMotionWarpingCharacterAdapter : public UMotionWarpingBaseAdapter
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

	void SetCharacter(ACharacter* InCharacter);

	virtual AActor* GetActor() const override;
	virtual USkeletalMeshComponent* GetMesh() const override;
	virtual FVector GetVisualRootLocation() const override;
	virtual FVector GetBaseVisualTranslationOffset() const override;
	virtual FQuat GetBaseVisualRotationOffset() const override;

private:
	// Triggered when the character says it's time to pre-process local root motion. This adapter catches the request and passes along to the Warping component
	FTransform WarpLocalRootMotionOnCharacter(const FTransform& LocalRootMotionTransform, UCharacterMovementComponent* TargetMoveComp, float DeltaSeconds);

	/** The associated character */
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<ACharacter> TargetCharacter;
};