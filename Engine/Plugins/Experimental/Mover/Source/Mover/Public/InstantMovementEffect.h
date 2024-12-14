// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MoveLibrary/MovementUtilsTypes.h"
#include "InstantMovementEffect.generated.h"

class UMoverComponent;
struct FMoverTimeStep;
struct FMoverTickStartData;
struct FMoverSyncState;

struct FApplyMovementEffectParams
{
	USceneComponent* UpdatedComponent;

	UPrimitiveComponent* UpdatedPrimitive;

	const UMoverComponent* MoverComp;

	const FMoverTickStartData* StartState;

	const FMoverTimeStep* TimeStep;
};

/**
 * Instant Movement Effects are methods of affecting movement state directly on a Mover-based actor for one tick.
 * Note: This is only applied one tick and then removed
 * Common uses would be for Teleporting, Changing Movement Modes directly, one time force application, etc.
 * Multiple Instant Movement Effects can be active at the time
 */
USTRUCT(BlueprintInternalUseOnly)
struct MOVER_API FInstantMovementEffect
{
	GENERATED_USTRUCT_BODY()

	FInstantMovementEffect() { }

	virtual ~FInstantMovementEffect() { }
	
	// @return newly allocated copy of this FInstantMovementEffect. Must be overridden by child classes
	virtual FInstantMovementEffect* Clone() const;

	virtual void NetSerialize(FArchive& Ar);

	virtual UScriptStruct* GetScriptStruct() const;

	virtual FString ToSimpleString() const;

	virtual void AddReferencedObjects(class FReferenceCollector& Collector) {}

	virtual bool ApplyMovementEffect(FApplyMovementEffectParams& ApplyEffectParams, FMoverSyncState& OutputState) { return false; }
};

template<>
struct TStructOpsTypeTraits< FInstantMovementEffect > : public TStructOpsTypeTraitsBase2< FInstantMovementEffect >
{
	enum
	{
		//WithNetSerializer = true,
		WithCopy = true
	};
};
