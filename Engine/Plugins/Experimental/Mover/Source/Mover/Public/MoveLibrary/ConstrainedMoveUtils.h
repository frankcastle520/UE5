// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/EngineBaseTypes.h"
#include "ConstrainedMoveUtils.generated.h"



/** Encapsulates info about constraining movement to a plane, such as in a side-scroller */
USTRUCT(BlueprintType)
struct MOVER_API FPlanarConstraint
{
	GENERATED_USTRUCT_BODY()

	/** If true, movement will be constrained to a plane. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PlanarMovement)
	bool bConstrainToPlane = false;

	/**
	 * The normal or axis of the plane that constrains movement, if bConstrainToPlane is enabled.
	 * If for example you wanted to constrain movement to the X-Z plane (so that Y cannot change), the normal would be set to X=0 Y=1 Z=0.
	 * It is normalized once the component is registered with the game world.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PlanarMovement, meta = (editcondition = bConstrainToPlane))
	FVector PlaneConstraintNormal = FVector::ForwardVector;

	/** The origin of the plane that constrains movement, if plane constraint is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PlanarMovement, meta = (editcondition = bConstrainToPlane))
	FVector PlaneConstraintOrigin = FVector::ZeroVector;
};



/**
 * PlanarConstraintUtils: a collection of stateless static BP-accessible functions for working with planar constraints
 */
UCLASS()
class MOVER_API UPlanarConstraintUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Sets whether or not the constraint is enabled */
	UFUNCTION(BlueprintCallable, Category = "Mover|PlanarConstraint")
	static void SetPlanarConstraintEnabled(UPARAM(ref) FPlanarConstraint& Constraint, bool bEnabled);

	/**
	 * Sets the normal of the plane that constrains movement, enforced if the plane constraint is enabled.
	 *
	 * @param PlaneNormal	The normal of the plane. If non-zero in length, it will be normalized.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mover|PlanarConstraint")
	static void SetPlanarConstraintNormal(UPARAM(ref) FPlanarConstraint& Constraint, FVector PlaneNormal);

	/** Sets the origin of the plane that constrains movement, enforced if the plane constraint is enabled. */
	UFUNCTION(BlueprintCallable, Category = "Mover|PlanarConstraint")
	static void SetPlaneConstraintOrigin(UPARAM(ref) FPlanarConstraint& Constraint, FVector PlaneOrigin);

	// APPLICATION OF CONSTRAINT

	/** Constrains a direction to be on the plane, if enabled */
	UFUNCTION(BlueprintCallable, Category = "Mover|PlanarConstraint")
	static FVector ConstrainDirectionToPlane(const FPlanarConstraint& Constraint, FVector Direction, bool bMaintainMagnitude=false);

	/** Constrains a location to be on the plane, if enabled */
	UFUNCTION(BlueprintCallable, Category = "Mover|PlanarConstraint")
	static FVector ConstrainLocationToPlane(const FPlanarConstraint& Constraint, FVector Location);

	/** Constrains a normal to be on the plane, if enabled. Result will be re-normalized. */
	UFUNCTION(BlueprintCallable, Category = "Mover|PlanarConstraint")
	static FVector ConstrainNormalToPlane(const FPlanarConstraint& Constraint, FVector Normal);

}; // UPlanarConstraintUtils