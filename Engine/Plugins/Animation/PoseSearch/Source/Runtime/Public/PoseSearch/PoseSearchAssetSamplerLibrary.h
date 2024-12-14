// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimCurveTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PoseSearch/PoseSearchAssetSampler.h"
#include "PoseSearchAssetSamplerLibrary.generated.h"

USTRUCT(Experimental, BlueprintType, Category="Animation|Pose Search")
struct POSESEARCH_API FPoseSearchAssetSamplerInput
{
	GENERATED_BODY()

	// Animation to sample
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=State)
	TObjectPtr<const UAnimationAsset> Animation;

	// Sampling time for Animation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=State)
	float AnimationTime = 0.f;

	// origin used to start sampling Animation at time of zero
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=State)
	FTransform RootTransformOrigin = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=State)
	bool bMirrored = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=State)
	TObjectPtr<const UMirrorDataTable> MirrorDataTable;

	// blend parameters if Animation is a blend space
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=State)
	FVector BlendParameters = FVector::ZeroVector;
	
	// frequency of sampling while sampling the root transform of blend spaces
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=State)
	int32 RootTransformSamplingRate = UE::PoseSearch::FAnimationAssetSampler::DefaultRootTransformSamplingRate;
};

USTRUCT(Experimental, BlueprintType, Category="Animation|Pose Search")
struct POSESEARCH_API FPoseSearchAssetSamplerPose
{
	GENERATED_BODY()

	FTransform RootTransform = FTransform::Identity;
	FCompactHeapPose Pose;
	FBlendedHeapCurve Curve;
	// @todo: add Attribute(s)
	//FHeapAttributeContainer Attribute;

	FCSPose<FCompactHeapPose> ComponentSpacePose;
};

UENUM()
enum class EPoseSearchAssetSamplerSpace : uint8
{
	Local,
	Component,
	World
};

UCLASS(Experimental)
class POSESEARCH_API UPoseSearchAssetSamplerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintPure, Category = "Animation|Pose Search|Experimental", meta = (BlueprintThreadSafe))
	static FPoseSearchAssetSamplerPose SamplePose(const UAnimInstance* AnimInstance, const FPoseSearchAssetSamplerInput Input);

	UFUNCTION(BlueprintPure, Category = "Animation|Pose Search|Experimental", meta = (BlueprintThreadSafe))
	static FTransform GetTransform(UPARAM(ref) FPoseSearchAssetSamplerPose& AssetSamplerPose, int32 BoneIndex = -1, EPoseSearchAssetSamplerSpace Space = EPoseSearchAssetSamplerSpace::World);

	UFUNCTION(BlueprintPure, Category = "Animation|Pose Search|Experimental", meta = (BlueprintThreadSafe))
	static FTransform GetTransformByName(UPARAM(ref) FPoseSearchAssetSamplerPose& AssetSamplerPose, FName BoneName, EPoseSearchAssetSamplerSpace Space = EPoseSearchAssetSamplerSpace::World);

	// @todo: it'd be nice if it was threadsafe...
	UFUNCTION(BlueprintCallable, Category="Animation|Pose Search|Experimental")
	static void Draw(const UAnimInstance* AnimInstance, UPARAM(ref) FPoseSearchAssetSamplerPose& AssetSamplerPose);
};

