// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovementModifier.h"
#include "StanceModifier.generated.h"

class UCapsuleComponent;
class UCharacterMoverComponent;

UENUM(BlueprintType)
enum class EStanceMode : uint8
{
	// Invalid default stance
	Invalid = 0,
	// Actor goes into crouch
	Crouch,
	// Actor goes into prone - not currently implemented
	Prone,
};

/**
 * Stances: Applies settings to the actor to make them go into different stances like crouch or prone(not implemented), affects actor maxacceleration and capsule height
 * Note: This modifier currently uses the CDO of the actor to reset values to "standing" values.
 *		 This modifier also assumes the actor is using a capsule as it's updated component for now
 */
USTRUCT(BlueprintType)
struct MOVER_API FStanceModifier : public FMovementModifierBase
{
	GENERATED_BODY()

public:
	FStanceModifier();
	virtual ~FStanceModifier() override {}

	EStanceMode ActiveStance;
	
	virtual bool HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const override;
	
	/** Fired when this modifier is activated. */
	virtual void OnStart(UMoverComponent* MoverComp, const FMoverTimeStep& TimeStep, const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState) override;
	
	/** Fired when this modifier is deactivated. */
	virtual void OnEnd(UMoverComponent* MoverComp, const FMoverTimeStep& TimeStep, const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState) override;
	
	/** Fired just before a Substep */
	virtual void OnPreMovement(UMoverComponent* MoverComp, const FMoverTimeStep& TimeStep) override;

	/** Fired after a Substep */
	virtual void OnPostMovement(UMoverComponent* MoverComp, const FMoverTimeStep& TimeStep, const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState) override;
	
	// @return newly allocated copy of this FMovementModifier. Must be overridden by child classes
	virtual FMovementModifierBase* Clone() const override;

	virtual void NetSerialize(FArchive& Ar) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;

	virtual bool CanExpand(const UCharacterMoverComponent* MoverComp) const;
	
	// Whether expanding should be from the base of the capsule or not
	virtual bool ShouldExpandingMaintainBase(const UCharacterMoverComponent* MoverComp) const;

protected:
	// Modifies the updated component casted to a capsule component
	void AdjustCapsule(UMoverComponent* MoverComp, float OldHalfHeight, float NewHalfHeight, float NewEyeHeight);

	// Applies any movement settings like acceleration or max speed changes
	void ApplyMovementSettings(UMoverComponent* MoverComp);
	
	// Reverts any movement settings like acceleration or max speed changes
	void RevertMovementSettings(UMoverComponent* MoverComp);
};
