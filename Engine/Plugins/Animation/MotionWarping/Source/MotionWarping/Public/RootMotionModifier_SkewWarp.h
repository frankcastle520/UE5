// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RootMotionModifier.h"
#include "RootMotionModifier_SkewWarp.generated.h"

UCLASS(meta = (DisplayName = "Skew Warp"))
class MOTIONWARPING_API URootMotionModifier_SkewWarp : public URootMotionModifier_Warp
{
	GENERATED_BODY()

public:

	URootMotionModifier_SkewWarp(const FObjectInitializer& ObjectInitializer);

	virtual FTransform ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds) override;
	
	static FVector WarpTranslation(const FTransform& CurrentTransform, const FVector& DeltaTranslation, const FVector& TotalTranslation, const FVector& TargetLocation);

#if WITH_EDITOR	
	virtual void DrawInEditor(FPrimitiveDrawInterface* PDI, USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, const FAnimNotifyEvent& NotifyEvent) const override;
	virtual void DrawCanvasInEditor(FCanvas& Canvas, FSceneView& View, USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, const FAnimNotifyEvent& NotifyEvent) const override;
	FTransform GetDebugWarpPointTransform(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* InAnimation, const UMirrorDataTable* MirrorTable, const float NotifyEndTime) const;
#endif	

	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static URootMotionModifier_SkewWarp* AddRootMotionModifierSkewWarp(
		UPARAM(DisplayName = "Motion Warping Comp") UMotionWarpingComponent* InMotionWarpingComp,
		UPARAM(DisplayName = "Animation") const UAnimSequenceBase* InAnimation,
		UPARAM(DisplayName = "Start Time") float InStartTime,
		UPARAM(DisplayName = "End Time") float InEndTime,
		UPARAM(DisplayName = "Warp Target Name") FName InWarpTargetName,
		UPARAM(DisplayName = "Warp Point Anim Provider") EWarpPointAnimProvider InWarpPointAnimProvider,
		UPARAM(DisplayName = "Warp Point Anim Transform") FTransform InWarpPointAnimTransform,
		UPARAM(DisplayName = "Warp Point Anim Bone Name") FName InWarpPointAnimBoneName,
		UPARAM(DisplayName = "Warp Translation") bool bInWarpTranslation = true,
		UPARAM(DisplayName = "Ignore Z Axis") bool bInIgnoreZAxis = true,
		UPARAM(DisplayName = "Warp Rotation") bool bInWarpRotation = true,
		UPARAM(DisplayName = "Rotation Type") EMotionWarpRotationType InRotationType = EMotionWarpRotationType::Default,
		UPARAM(DisplayName = "Rotation Method") EMotionWarpRotationMethod InRotationMethod = EMotionWarpRotationMethod::Slerp,
		UPARAM(DisplayName = "Warp Rotation Time Multiplier") float InWarpRotationTimeMultiplier = 1.f,
		UPARAM(DisplayName = "Warp Max Rotation Rate") float InWarpMaxRotationRate = 0.f);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
