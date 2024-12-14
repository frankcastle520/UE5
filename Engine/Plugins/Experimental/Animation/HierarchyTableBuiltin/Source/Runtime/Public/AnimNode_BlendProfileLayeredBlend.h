// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNode_BlendProfileLayeredBlend.generated.h"

class UHierarchyTable;
enum class EHierarchyTableEntryType : uint8;

USTRUCT(BlueprintInternalUseOnly)
struct HIERARCHYTABLEBUILTINRUNTIME_API FAnimNode_BlendProfileLayeredBlend : public FAnimNode_Base
{
	GENERATED_BODY()

public:
	/** The source pose */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink BasePose;

	/** The target pose */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links, meta = (BlueprintCompilerGeneratedDefaults))
	FPoseLink BlendPose;

	/**
	 * The blend mask to use to control layering of the pose, curves, and attributes
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Config)
	TObjectPtr<UHierarchyTable> BlendMask;

protected:
	// Per-bone weights for the skeleton. Serialized as these are only relative to the skeleton, but can potentially
	// be regenerated at runtime if the GUIDs dont match
	UPROPERTY()
	TArray<float> PerBoneBlendWeights;

	// transient data to handle weight and target weight
	// this array changes based on required bones
	TArray<float> DesiredBoneBlendWeights;
	TArray<float> CurrentBoneBlendWeights;

	UE::Anim::TNamedValueArray<FDefaultAllocator, UE::Anim::FCurveElement> CachedCurveMaskWeights;

	struct FNamedFloat
	{
		FName Name;
		float Value;

		FNamedFloat(const FName InName, const float InValue)
			: Name(InName)
			, Value(InValue)
		{
		}
	};
	
	TArray<FNamedFloat> CachedAttributeMaskWeights;

	// Guids for skeleton used to determine whether the PerBoneBlendWeights need rebuilding
	UPROPERTY()
	FGuid SkeletonGuid;

	// Guid for virtual bones used to determine whether the PerBoneBlendWeights need rebuilding
	UPROPERTY()
	FGuid VirtualBoneGuid;

	/** The weight of target pose */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Runtime, meta = (BlueprintCompilerGeneratedDefaults, PinShownByDefault))
	float BlendWeight;

	// Serial number of the required bones container
	uint16 RequiredBonesSerialNumber;

	/** Whether to incorporate the per-bone blend weight of the root bone when lending root motion */
	UPROPERTY(EditAnywhere, Category = Config)
	bool bBlendRootMotionBasedOnRootBone;

public:
	FAnimNode_BlendProfileLayeredBlend()
		: BlendWeight(1.0f)
		, RequiredBonesSerialNumber(0)
		, bBlendRootMotionBasedOnRootBone(true)
	{
	}

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// Invalidate the cached per-bone blend weights from the skeleton
	void InvalidatePerBoneBlendWeights() { RequiredBonesSerialNumber = 0; SkeletonGuid = FGuid(); VirtualBoneGuid = FGuid(); }

	// Invalidates the cached bone data so it is recalculated the next time this node is updated
	void InvalidateCachedBoneData() { RequiredBonesSerialNumber = 0; }

	void CreateMaskWeights(const USkeleton* Skeleton);

private:
	// Rebuild cache per bone blend weights from the skeleton
	void RebuildPerBoneBlendWeights(const USkeleton* InSkeleton);

	// Check whether per-bone blend weights are valid according to the skeleton (GUID check)
	bool ArePerBoneBlendWeightsValid(const USkeleton* InSkeleton) const;

	// Update cached data if required
	void UpdateCachedBoneData(const FBoneContainer& RequiredBones, const USkeleton* Skeleton);

	friend class UAnimGraphNode_BlendProfileLayeredBlend;

	void UpdateDesiredBoneWeight();
};