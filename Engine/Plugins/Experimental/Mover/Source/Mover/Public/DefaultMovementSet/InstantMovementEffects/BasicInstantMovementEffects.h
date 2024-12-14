// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "InstantMovementEffect.h"
#include "BasicInstantMovementEffects.generated.h"

/** Teleport: instantly moves an actor to a new location */
USTRUCT(BlueprintType)
struct MOVER_API FTeleportEffect : public FInstantMovementEffect
{
	GENERATED_USTRUCT_BODY()

	FTeleportEffect();
	virtual ~FTeleportEffect() {}

	// Location to teleport to, in world space
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FVector TargetLocation;

	virtual bool ApplyMovementEffect(FApplyMovementEffectParams& ApplyEffectParams, FMoverSyncState& OutputState) override;

	virtual FInstantMovementEffect* Clone() const override;

	virtual void NetSerialize(FArchive& Ar) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

/** Jump Impulse: introduces an instantaneous upwards change in velocity. This overrides the existing 'up' component of the actor's current velocity
  * Note: this only applies the impulse for one tick!
  */
USTRUCT(BlueprintType)
struct MOVER_API FJumpImpulseEffect : public FInstantMovementEffect
{
	GENERATED_USTRUCT_BODY()

	FJumpImpulseEffect();

	virtual ~FJumpImpulseEffect() {}

	// Units per second, in whatever direction the target actor considers 'up'
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover, meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm/s"))
	float UpwardsSpeed;

	virtual bool ApplyMovementEffect(FApplyMovementEffectParams& ApplyEffectParams, FMoverSyncState& OutputState) override;

	virtual FInstantMovementEffect* Clone() const override;

	virtual void NetSerialize(FArchive& Ar) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

/** Apply Velocity: provides an impulse velocity to the actor after (optionally) forcing them into a particular movement mode
  * Note: this only applies the impulse for one tick!
  */
USTRUCT(BlueprintType)
struct MOVER_API FApplyVelocityEffect : public FInstantMovementEffect
{
	GENERATED_USTRUCT_BODY()

	FApplyVelocityEffect();
	virtual ~FApplyVelocityEffect() {}

	// Velocity to apply to the actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover, meta=(ForceUnits="cm/s"))
	FVector VelocityToApply;

	// If true VelocityToApply will be added to current velocity on this actor. If false velocity will be set directly to VelocityToApply
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	bool bAdditiveVelocity;
	
	// Optional movement mode name to force the actor into before applying the impulse velocity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FName ForceMovementMode;

	virtual bool ApplyMovementEffect(FApplyMovementEffectParams& ApplyEffectParams, FMoverSyncState& OutputState) override;
	
	virtual FInstantMovementEffect* Clone() const override;

	virtual void NetSerialize(FArchive& Ar) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};
