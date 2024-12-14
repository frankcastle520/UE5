// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "PoseSearch/MultiAnimAsset.h"
#include "PoseSearch/PoseSearchRole.h"
#include "PoseSearchInteractionAsset.generated.h"

USTRUCT(Experimental)
struct POSESEARCH_API FPoseSearchInteractionAssetItem
{
	GENERATED_BODY()

	// associated aniamtion for this FPoseSearchInteractionAssetItem
	UPROPERTY(EditAnywhere, Category = "Settings")
	TObjectPtr<UAnimationAsset> Animation;

	// associated role for this FPoseSearchInteractionAssetItem
	UPROPERTY(EditAnywhere, Category = "Settings")
	FName Role;

	// relative weight to the other FPoseSearchInteractionAssetItem::WarpingWeightRotation(s) defining which character will be rotated while warping
	// 0 - the associated character to this item will move fully to compensate the warping errors
	// > 0 && all the other FPoseSearchInteractionAssetItem::WarpingWeightTranslation as zero, and the associated character will not move
	UPROPERTY(EditAnywhere, Category = "Warping", meta = (ClampMin = "0", ClampMax = "1"))
	float WarpingWeightRotation = 0.5f;

	// relative weight to the other FPoseSearchInteractionAssetItem::WarpingWeightTranslation(s) defining which character will be translated while warping
	// 0 - the associated character to this item will move fully to compensate the warping errors
	// > 0 && all the other FPoseSearchInteractionAssetItem::WarpingWeightTranslation as zero, and the associated character will not move
	UPROPERTY(EditAnywhere, Category = "Warping", meta = (ClampMin = "0", ClampMax = "1"))
	float WarpingWeightTranslation = 0.5f;

	// offset from the origin
	UPROPERTY(EditAnywhere, Category = "Settings")
	FTransform Origin = FTransform::Identity;
};

UCLASS(Experimental, BlueprintType, Category = "Animation|Pose Search")
class POSESEARCH_API UPoseSearchInteractionAsset : public UMultiAnimAsset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FPoseSearchInteractionAssetItem> Items;

public:
#if WITH_EDITORONLY_DATA

	UPROPERTY(Transient, EditAnywhere, Category = "Debug", meta = (EditCondition=bEnableDebugWarp, EditConditionHides))
	TArray<FTransform> DebugWarpOffsets;

	// used to test warping: 0 - no warping applied, 1 - full warping/aligment applied
	// test warping actors will be offsetted by Items::DebugWarpOffset transforms from the original
	// UMultiAnimAsset::GetOrigin() definition and warped accordingly with CalculateWarpTransforms
	// following the rotation and translation weights defined in Items::WarpingWeightRotation and
	// Items::WarpingWeightTranslation as relative weights between the Items (they'll be normalized at runtime)
	UPROPERTY(Transient, EditAnywhere, Category = "Debug", meta = (ClampMin = "0", ClampMax = "1", EditCondition=bEnableDebugWarp, EditConditionHides))
	float DebugWarpAmount = 0.f;

	UPROPERTY(Transient, EditAnywhere, Category = "Debug")
	bool bEnableDebugWarp = false;
#endif // WITH_EDITORONLY_DATA

	virtual bool IsLooping() const override;
	virtual bool HasRootMotion() const override;
	virtual float GetPlayLength() const override;

#if WITH_EDITOR
	virtual int32 GetFrameAtTime(float Time) const override;
#endif // WITH_EDITOR

	virtual int32 GetNumRoles() const override { return Items.Num(); }
	virtual UE::PoseSearch::FRole GetRole(int32 RoleIndex) const override { return Items[RoleIndex].Role; }
	
	virtual UAnimationAsset* GetAnimationAsset(const FName /*UE::PoseSearch::FRole*/& Role) const override;

	virtual FTransform GetOrigin(const UE::PoseSearch::FRole& Role) const override;

#if WITH_EDITOR
	FTransform GetDebugWarpOrigin(const UE::PoseSearch::FRole& Role, bool bComposeWithDebugWarpOffset) const;
#endif // WITH_EDITOR

	virtual void CalculateWarpTransforms(float Time, const TArrayView<const FTransform> ActorRootBoneTransforms, TArrayView<FTransform> FullAlignedActorRootBoneTransforms) const override;

	FQuat FindReferenceOrientation(const TArrayView<const FTransform> Transforms, const TArrayView<int32> SortedByWarpingWeightRotationItemIndex) const;
	FVector FindReferencePosition(const TArrayView<const FTransform> Transforms, const TArrayView<float> NormalizedWarpingWeightTranslation) const;
};