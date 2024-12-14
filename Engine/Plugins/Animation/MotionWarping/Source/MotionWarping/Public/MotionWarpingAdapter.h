// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RootMotionModifier.h"
#include "MotionWarpingAdapter.generated.h"


DECLARE_DELEGATE_RetVal_ThreeParams(FTransform, FOnWarpLocalspaceRootMotionWithContext, const FTransform&, float, const FMotionWarpingUpdateContext*)
DECLARE_DELEGATE_RetVal_ThreeParams(FTransform, FOnWarpWorldspaceRootMotionWithContext, const FTransform&, float, const FMotionWarpingUpdateContext*)

/**
 * MotionWarpingBaseAdapter: base class to adapt/apply motion warping to a target. Concrete subclasses should override
 */
UCLASS(Abstract)
class MOTIONWARPING_API UMotionWarpingBaseAdapter : public UObject
{
	GENERATED_BODY()

public:
	virtual ~UMotionWarpingBaseAdapter() {}
	virtual AActor* GetActor() const { return nullptr; }
	virtual USkeletalMeshComponent* GetMesh() const { return nullptr; }
	virtual FVector GetVisualRootLocation() const { return FVector::ZeroVector; }
	virtual FVector GetBaseVisualTranslationOffset() const { return FVector::ZeroVector; }
	virtual FQuat GetBaseVisualRotationOffset() const { return FQuat::Identity; }

	// A MotionWarpingComponent will bind to this delegate to perform warping when it is triggered
	FOnWarpLocalspaceRootMotionWithContext WarpLocalRootMotionDelegate;
};

