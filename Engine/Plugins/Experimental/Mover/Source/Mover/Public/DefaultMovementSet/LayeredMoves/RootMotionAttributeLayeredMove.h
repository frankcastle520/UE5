// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LayeredMove.h"
#include "RootMotionModifier.h"
#include "RootMotionAttributeLayeredMove.generated.h"

/** 
 * Root Motion Attribute Move: handles root motion from a mesh's custom attribute. 
 * Currently only supports Independent ticking mode, and allows air control while jumping/falling.
 */
USTRUCT(BlueprintType)
struct MOVER_API FLayeredMove_RootMotionAttribute : public FLayeredMoveBase
{
	GENERATED_USTRUCT_BODY()

	FLayeredMove_RootMotionAttribute();
	virtual ~FLayeredMove_RootMotionAttribute() {}

protected:
	// These member variables are NOT replicated. They are used if we rollback and resimulate when the root motion attribute is no longer in sync.
	bool bDidAttrHaveRootMotionForResim = false;
	FTransform LocalRootMotionForResim;
	FMotionWarpingUpdateContext WarpingContextForResim;

	// Generate a movement 
	virtual bool GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove) override;

	virtual FLayeredMoveBase* Clone() const override;

	virtual void NetSerialize(FArchive& Ar) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};
