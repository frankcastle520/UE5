// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovementUtilsTypes.generated.h"

class USceneComponent;
class UPrimitiveComponent;
class UMoverComponent;


UENUM()
enum class EMoveMixMode : uint8
{
	/** Velocity (linear and angular) is intended to be added with other sources */
	AdditiveVelocity = 0,
	/** Velocity (linear and angular) should override others */
	OverrideVelocity = 1,
	/** All move parameters should override others */
	OverrideAll      = 2,
};


/** Encapsulates info about an intended move that hasn't happened yet */
USTRUCT(BlueprintType)
struct MOVER_API FProposedMove
{
	GENERATED_USTRUCT_BODY()

	FProposedMove() : 
		bHasDirIntent(false)
	{}

	// Determines how this move should resolve with other moves
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	EMoveMixMode MixMode = EMoveMixMode::AdditiveVelocity;

	/**
	 * Indicates that we should switch to a particular movement mode before the next simulation step is performed.
	 * Note: If this is set from a layered move the preferred mode will only be set at the beginning of the layered move, not continuously.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FName	PreferredMode = NAME_None;

	// Signals whether there was any directional intent specified
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	uint8	 bHasDirIntent : 1;

	// Directional, per-axis magnitude [-1, 1] in world space (length of 1 indicates max speed intent). Only valid if bHasDirIntent is set.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FVector  DirectionIntent = FVector::ZeroVector;

	// Units per second, world space, possibly mapped onto walking surface
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FVector  LinearVelocity = FVector::ZeroVector;
	
	// Degrees per second, local space
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FRotator AngularVelocity = FRotator::ZeroRotator;
};


/** 
 * Encapsulates components involved in movement. Used by many library functions. 
 * Only a scene component is required for movement, but this is typically a primitive
 * component so we provide a pre-cast ptr for convenience.
 */
USTRUCT(BlueprintType)
struct MOVER_API FMovingComponentSet
{
	GENERATED_USTRUCT_BODY()

	FMovingComponentSet() {}
	FMovingComponentSet(USceneComponent* InUpdatedComponent);
	FMovingComponentSet(UMoverComponent* InMoverComponent);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	TWeakObjectPtr<USceneComponent> UpdatedComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	TWeakObjectPtr<UPrimitiveComponent> UpdatedPrimitive = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	TWeakObjectPtr<UMoverComponent> MoverComponent = nullptr;
};