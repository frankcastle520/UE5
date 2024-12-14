// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoneControllers/BoneControllerTypes.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_OverrideRootMotion.generated.h"

struct FAnimationInitializeContext;
struct FComponentSpacePoseContext;
struct FNodeDebugData;

USTRUCT(BlueprintInternalUseOnly, Experimental)
struct ANIMATIONWARPINGRUNTIME_API FAnimNode_OverrideRootMotion : public FAnimNode_Base
{
	GENERATED_BODY();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links, meta = (DisplayPriority = 0))
	FPoseLink Source;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Evaluation, meta = (FoldProperty, PinShownByDefault))
	float Alpha = 1.f;

	UPROPERTY(EditAnywhere, Category = Evaluation, meta = (FoldProperty, PinShownByDefault))
	FVector OverrideVelocity = FVector(0,0,0);

	//todo: rotation override support
#endif

public:
	// FAnimNode_Base interface
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	// End of FAnimNode_Base interface

	// Folded property accesors
	float GetAlpha() const;
	const FVector& GetOverrideVelocity() const;

private:

	// Internal cached anim instance proxy
	FAnimInstanceProxy* AnimInstanceProxy = nullptr;

	float DeltaTime = 0.f;
};
