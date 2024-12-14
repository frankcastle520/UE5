// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MoverComponent.h"
#include "MovementModifiers/StanceModifier.h"
#include "CharacterMoverComponent.generated.h"

/**
 * Fires when a stance is changed
 * Note: If a stance was just Activated it will fire with an invalid OldStance
 *		 If a stance was just Deactivated it will fire with an invalid NewStance
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMover_OnStanceChanged, EStanceMode, OldStance, EStanceMode, NewStance);

UCLASS(BlueprintType, meta = (BlueprintSpawnableComponent))
class MOVER_API UCharacterMoverComponent : public UMoverComponent
{
	GENERATED_BODY()
	
public:
	UCharacterMoverComponent();
	
	virtual void BeginPlay() override;
	
	/** Returns true if currently crouching */ 
	UFUNCTION(BlueprintCallable, Category = Mover)
	virtual bool IsCrouching() const;

	/** Returns true if currently flying (moving through a non-fluid volume without resting on the ground) */
	UFUNCTION(BlueprintPure, Category = Mover)
	virtual bool IsFlying() const;
	
	// Is this actor in a falling state? Note that this includes upwards motion induced by jumping.
	UFUNCTION(BlueprintPure, Category = Mover)
	virtual bool IsFalling() const;

	// Is this actor in a airborne state? (e.g. Flying, Falling)
	UFUNCTION(BlueprintPure, Category = Mover)
	virtual bool IsAirborne() const;

	// Is this actor in a grounded state? (e.g. Walking)
	UFUNCTION(BlueprintPure, Category = Mover)
	virtual bool IsOnGround() const;

	// Is this actor in a Swimming state? (e.g. Swimming)
	UFUNCTION(BlueprintPure, Category = Mover)
	virtual bool IsSwimming() const;
	
	// Is this actor sliding on an unwalkable slope
	UFUNCTION(BlueprintPure, Category = Mover)
	virtual bool IsSlopeSliding() const;

	// Can this Actor jump?
	UFUNCTION(BlueprintPure, Category = Mover)
	virtual bool CanActorJump() const;

	// Perform jump on actor
	UFUNCTION(BlueprintCallable, Category = Mover)
	virtual bool Jump();

	// Whether this component should directly handle jumping or not 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Mover)
    bool bHandleJump;
	
	// Whether this actor can currently crouch or not 
	UFUNCTION(BlueprintCallable, Category = Mover)
	virtual bool CanCrouch();
	
	// Perform crouch on actor
	UFUNCTION(BlueprintCallable, Category = Mover)
	virtual void Crouch();

	// Perform uncrouch on actor
	UFUNCTION(BlueprintCallable, Category = Mover)
	virtual void UnCrouch();

	// Broadcast when this actor changes stances.
	UPROPERTY(BlueprintAssignable, Category = Mover)
	FMover_OnStanceChanged OnStanceChanged;
	
protected:
	UFUNCTION()
	virtual void OnMoverPreSimulationTick(const FMoverTimeStep& TimeStep, const FMoverInputCmdContext& InputCmd);
	
	// ID used to keep track of the modifier responsible for crouching
	FMovementModifierHandle StanceModifierHandle;

	/** If true, try to crouch (or keep crouching) on next update. If false, try to stop crouching on next update. */
	UPROPERTY(Category = "Mover|Crouch", VisibleInstanceOnly, BlueprintReadOnly)
	bool bWantsToCrouch;
};
