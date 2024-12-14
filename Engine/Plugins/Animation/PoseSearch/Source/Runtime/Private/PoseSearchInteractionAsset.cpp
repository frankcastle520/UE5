// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchInteractionAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "PoseSearch/PoseSearchAssetSampler.h"

bool UPoseSearchInteractionAsset::IsLooping() const
{
	float CommonPlayLength = -1.f;
	for (const FPoseSearchInteractionAssetItem& Item : Items)
	{
		if (const UAnimationAsset* AnimationAsset = Item.Animation.Get())
		{
			if (const UAnimSequenceBase* SequenceBase = Cast<UAnimSequenceBase>(AnimationAsset))
			{
				if (!SequenceBase->bLoop)
				{
					return false;
				}
			}
			else if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
			{
				if (!BlendSpace->bLoop)
				{
					return false;
				}
			}
			else
			{
				unimplemented();
			}

			if (CommonPlayLength < 0.f)
			{
				CommonPlayLength = AnimationAsset->GetPlayLength();
			}
			else if (!FMath::IsNearlyEqual(CommonPlayLength, AnimationAsset->GetPlayLength()))
			{
				return false;
			}
		}
	}
	return true;
}

bool UPoseSearchInteractionAsset::HasRootMotion() const
{
	bool bHasAtLeastOneValidItem = false;
	bool bHasRootMotion = true;

	for (const FPoseSearchInteractionAssetItem& Item : Items)
	{
		if (const UAnimationAsset* AnimationAsset = Item.Animation.Get())
		{
			if (const UAnimSequenceBase* SequenceBase = Cast<UAnimSequenceBase>(AnimationAsset))
			{
				bHasRootMotion &= SequenceBase->HasRootMotion();
			}
			else if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
			{
				BlendSpace->ForEachImmutableSample([&bHasRootMotion](const FBlendSample& Sample)
				{
					if (const UAnimSequence* Sequence = Sample.Animation.Get())
					{
						bHasRootMotion &= Sequence->HasRootMotion();
					}
				});
			}
			else
			{
				unimplemented();
			}
			bHasAtLeastOneValidItem = true;
		}
	}

	return bHasAtLeastOneValidItem && bHasRootMotion;
}

float UPoseSearchInteractionAsset::GetPlayLength() const
{
	float MaxPlayLength = 0.f;
	for (const FPoseSearchInteractionAssetItem& Item : Items)
	{
		if (const UAnimationAsset* AnimationAsset = Item.Animation.Get())
		{
			MaxPlayLength = FMath::Max(MaxPlayLength, AnimationAsset->GetPlayLength());
		}
	}
	return MaxPlayLength;
}

#if WITH_EDITOR
int32 UPoseSearchInteractionAsset::GetFrameAtTime(float Time) const
{
	const UAnimationAsset* MaxPlayLengthAnim = nullptr;
	float MaxPlayLength = -1.f;
	for (const FPoseSearchInteractionAssetItem& Item : Items)
	{
		if (const UAnimationAsset* AnimationAsset = Item.Animation.Get())
		{
			const float PlayLength = AnimationAsset->GetPlayLength();
			if (PlayLength > MaxPlayLength)
			{
				MaxPlayLength = PlayLength;
				MaxPlayLengthAnim = AnimationAsset;
			}
		}
	}
	
	if (MaxPlayLengthAnim)
	{
		if (const UAnimSequenceBase* SequenceBase = Cast<UAnimSequenceBase>(MaxPlayLengthAnim))
		{
			return SequenceBase->GetFrameAtTime(Time);
		}

		if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(MaxPlayLengthAnim))
		{
			// returning the percentage of time as value to diplay in the pose search debugger (NoTe: BlendSpace->GetPlayLength() is one)
			return FMath::RoundToInt(Time * 100.f);
		}

		unimplemented();
	}

	return 0;
}
#endif // WITH_EDITOR

FQuat UPoseSearchInteractionAsset::FindReferenceOrientation(const TArrayView<const FTransform> Transforms, const TArrayView<int32> SortedByWarpingWeightRotationItemIndex) const
{
	const int32 ItemsNum = Items.Num();

	check(ItemsNum > 0);
	check(Transforms.Num() == ItemsNum);
	check(Transforms.Num() == SortedByWarpingWeightRotationItemIndex.Num());

	const int32 LastItemIndex = ItemsNum - 1;
	if (ItemsNum > 1)
	{
		FVector OtherItemsPositionsSum = Transforms[SortedByWarpingWeightRotationItemIndex[0]].GetTranslation();
		for (int32 ItemIndex = 1; ItemIndex < LastItemIndex; ++ItemIndex)
		{
			OtherItemsPositionsSum += Transforms[SortedByWarpingWeightRotationItemIndex[ItemIndex]].GetTranslation();
		}
		
		const FVector OtherItemsPositionAverage = OtherItemsPositionsSum / LastItemIndex;
		const FVector DeltaPosition = OtherItemsPositionAverage - Transforms[SortedByWarpingWeightRotationItemIndex[LastItemIndex]].GetTranslation();

		if (!DeltaPosition.IsNearlyZero())
		{
			return DeltaPosition.ToOrientationQuat();
		}
	}

	return Transforms[SortedByWarpingWeightRotationItemIndex[LastItemIndex]].GetRotation();
}

FVector UPoseSearchInteractionAsset::FindReferencePosition(const TArrayView<const FTransform> Transforms, const TArrayView<float> NormalizedWarpingWeightTranslation) const
{
	const int32 ItemsNum = Items.Num();
	
	check(ItemsNum > 0);
	check(Transforms.Num() == ItemsNum);
	check(Transforms.Num() == NormalizedWarpingWeightTranslation.Num());

	FVector PositionsSum = FVector::ZeroVector;
	for (int32 ItemIndex = 0; ItemIndex < ItemsNum; ++ItemIndex)
	{
		PositionsSum += Transforms[ItemIndex].GetTranslation() * NormalizedWarpingWeightTranslation[ItemIndex];
	}

	return PositionsSum;
}

UAnimationAsset* UPoseSearchInteractionAsset::GetAnimationAsset(const UE::PoseSearch::FRole& Role) const
{
	for (const FPoseSearchInteractionAssetItem& Item : Items)
	{
		if (Item.Role == Role)
		{
			return Item.Animation;
		}
	}
	return nullptr;
}

FTransform UPoseSearchInteractionAsset::GetOrigin(const UE::PoseSearch::FRole& Role) const
{
	for (const FPoseSearchInteractionAssetItem& Item : Items)
	{
		if (Item.Role == Role)
		{
			return Item.Origin;
		}
	}
	return FTransform::Identity;
}

#if WITH_EDITOR
FTransform UPoseSearchInteractionAsset::GetDebugWarpOrigin(const UE::PoseSearch::FRole& Role, bool bComposeWithDebugWarpOffset) const
{
	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		const FPoseSearchInteractionAssetItem& Item = Items[ItemIndex];
		if (Item.Role == Role)
		{
#if WITH_EDITORONLY_DATA
			if (bComposeWithDebugWarpOffset && bEnableDebugWarp && DebugWarpOffsets.IsValidIndex(ItemIndex))
			{
				return DebugWarpOffsets[ItemIndex] * Item.Origin;
			}
#endif // WITH_EDITORONLY_DATA

			return Item.Origin;
		}
	}
	return FTransform::Identity;
}
#endif // WITH_EDITOR

void UPoseSearchInteractionAsset::CalculateWarpTransforms(float Time, const TArrayView<const FTransform> ActorRootBoneTransforms, TArrayView<FTransform> FullAlignedActorRootBoneTransforms) const
{
	check(ActorRootBoneTransforms.Num() == GetNumRoles());
	check(FullAlignedActorRootBoneTransforms.Num() == GetNumRoles());

	const int32 ItemsNum = Items.Num();
	if (ItemsNum == 0)
	{
		return;
	}

	TArray<FTransform, TInlineAllocator<UE::PoseSearch::PreallocatedRolesNum>> AssetRootBoneTransforms;
	AssetRootBoneTransforms.SetNum(ItemsNum);

	// ItemIndex is the RoleIndex and Role = Item.Role
	for (int32 ItemIndex = 0; ItemIndex < ItemsNum; ++ItemIndex)
	{
		const FPoseSearchInteractionAssetItem& Item = Items[ItemIndex];

		// sampling the AnimationAsset to extract the current time transform and the initial (time of 0) transform
		const UE::PoseSearch::FAnimationAssetSampler Sampler(Item.Animation, Item.Origin);
		AssetRootBoneTransforms[ItemIndex] = Sampler.ExtractRootTransform(Time);

#if DO_CHECK
		// array containing the bone index of the root bone (0)
		TArray<uint16, TInlineAllocator<1>> BoneIndices;
		BoneIndices.SetNumZeroed(1);

		// extracting the pose, containing only the root bone from the Sampler 
		FMemMark Mark(FMemStack::Get());
		FCompactPose Pose;
		FBoneContainer BoneContainer;
		BoneContainer.InitializeTo(BoneIndices, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll), *Items[ItemIndex].Animation->GetSkeleton());
		Pose.SetBoneContainer(&BoneContainer);
		Sampler.ExtractPose(Time, Pose);

		// making sure the animation root bone transform is Identity, so we can confuse the root with the root BONE transform and preserve performances!
		const FTransform& RootBoneTransform = Pose.GetBones()[0];
		check(RootBoneTransform.Equals(FTransform::Identity));
#endif // DO_CHECK
	}

	TArray<int32, TInlineAllocator<UE::PoseSearch::PreallocatedRolesNum>> SortedByWarpingWeightRotationItemIndex;
	TArray<float, TInlineAllocator<UE::PoseSearch::PreallocatedRolesNum>> NormalizedWarpingWeightTranslation;
	SortedByWarpingWeightRotationItemIndex.SetNum(ItemsNum);
	NormalizedWarpingWeightTranslation.SetNum(ItemsNum);

	float WarpingWeightTranslationSum = 0.f;
	float WarpingWeightRotationSum = 0.f;
	for (int32 ItemIndex = 0; ItemIndex < ItemsNum; ++ItemIndex)
	{
		SortedByWarpingWeightRotationItemIndex[ItemIndex] = ItemIndex;
		WarpingWeightTranslationSum += Items[ItemIndex].WarpingWeightTranslation;
		WarpingWeightRotationSum += Items[ItemIndex].WarpingWeightRotation;
	}

	const float NormalizedHomogeneousWeight = 1.f / ItemsNum;
	if (WarpingWeightTranslationSum > UE_KINDA_SMALL_NUMBER)
	{
		for (int32 ItemIndex = 0; ItemIndex < ItemsNum; ++ItemIndex)
		{
			NormalizedWarpingWeightTranslation[ItemIndex] = Items[ItemIndex].WarpingWeightTranslation / WarpingWeightTranslationSum;
		}
	}
	else
	{
		for (int32 ItemIndex = 0; ItemIndex < ItemsNum; ++ItemIndex)
		{
			NormalizedWarpingWeightTranslation[ItemIndex] = NormalizedHomogeneousWeight;
		}
	}

	if (WarpingWeightRotationSum > UE_KINDA_SMALL_NUMBER)
	{
		SortedByWarpingWeightRotationItemIndex.Sort([this](const int32 A, const int32 B)
			{
				return Items[A].WarpingWeightRotation < Items[B].WarpingWeightRotation;
			});
	}

	const FQuat AssetReferenceOrientation = FindReferenceOrientation(AssetRootBoneTransforms, SortedByWarpingWeightRotationItemIndex);
	const FQuat ActorsReferenceOrientation = FindReferenceOrientation(ActorRootBoneTransforms, SortedByWarpingWeightRotationItemIndex);
	
	FQuat WeightedActorsReferenceOrientation = ActorsReferenceOrientation;
	if (WarpingWeightRotationSum > UE_KINDA_SMALL_NUMBER)
	{
		// ItemIndex are in order of WarpingWeightRotation. the last one is the one with the highest most WarpingWeightRotation, the most "important"
		for (int32 ItemIndex : SortedByWarpingWeightRotationItemIndex)
		{
			const FPoseSearchInteractionAssetItem& Item = Items[ItemIndex];
			const float NormalizedWarpingWeightRotation = Item.WarpingWeightRotation / WarpingWeightRotationSum;
			if (NormalizedWarpingWeightRotation > NormalizedHomogeneousWeight)
			{
				// NormalizedHomogeneousWeight is one only if ItemsNum is one, 
				// BUT NormalizedWarpingWeightRotation > NormalizedHomogeneousWeight should always be false
				check(!FMath::IsNearlyEqual(NormalizedHomogeneousWeight, 1.f));

				// how much this item wants to reorient the ReferenceOrientation from the homogeneous "fair" value
				const float SlerpParam = (NormalizedWarpingWeightRotation - NormalizedHomogeneousWeight) / (1.f - NormalizedHomogeneousWeight);

				// calculating the reference orientation relative to the character
				// AssetReferenceOrientation in actor world orientation
				const FQuat ActorAssetReferenceOrientation = ActorRootBoneTransforms[ItemIndex].GetRotation() * (AssetRootBoneTransforms[ItemIndex].GetRotation().Inverse() * AssetReferenceOrientation);

				WeightedActorsReferenceOrientation = FQuat::Slerp(WeightedActorsReferenceOrientation, ActorAssetReferenceOrientation, SlerpParam);
			}
		}
	}

	const FVector AssetReferencePosition = FindReferencePosition(AssetRootBoneTransforms, NormalizedWarpingWeightTranslation);
	const FVector ActorsReferencePosition = FindReferencePosition(ActorRootBoneTransforms, NormalizedWarpingWeightTranslation);

	// aligning all the actors to ActorsReferencePosition, WeightedActorsReferenceOrientation
	const FTransform AssetReferenceTransform(AssetReferenceOrientation, AssetReferencePosition);
	const FTransform ActorsReferenceTransform(WeightedActorsReferenceOrientation, ActorsReferencePosition);
	const FTransform AssetReferenceInverseTransform = AssetReferenceTransform.Inverse();
	
	for (int32 ItemIndex = 0; ItemIndex < ItemsNum; ++ItemIndex)
	{
		FullAlignedActorRootBoneTransforms[ItemIndex] = (AssetRootBoneTransforms[ItemIndex] * AssetReferenceInverseTransform) * ActorsReferenceTransform;
	}
}