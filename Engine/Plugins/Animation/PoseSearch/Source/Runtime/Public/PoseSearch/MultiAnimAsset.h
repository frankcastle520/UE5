// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// @todo: move UMultiAnimAsset as well as IMultiAnimAssetEditor to Engine or a base plugin for multi character animation assets

#include "MultiAnimAsset.generated.h"

class UAnimationAsset;

// UObject defining tuples of UAnimationAsset(s) with associated Role(s) and relative transforms from a shared reference system via GetOrigin
UCLASS(Abstract, Experimental, BlueprintType, Category = "Animation")
class POSESEARCH_API UMultiAnimAsset : public UObject
{
	GENERATED_BODY()
public:

	[[nodiscard]] virtual bool IsLooping() const PURE_VIRTUAL(UMultiAnimAsset::IsLooping, return false;);
	[[nodiscard]] virtual bool HasRootMotion() const PURE_VIRTUAL(UMultiAnimAsset::HasRootMotion, return false;);
	[[nodiscard]] virtual float GetPlayLength() const PURE_VIRTUAL(UMultiAnimAsset::GetPlayLength, return 0.f;);

#if WITH_EDITOR
	[[nodiscard]] virtual int32 GetFrameAtTime(float Time) const PURE_VIRTUAL(UMultiAnimAsset::GetFrameAtTime, return 0;);
#endif // WITH_EDITOR

	[[nodiscard]] virtual int32 GetNumRoles() const PURE_VIRTUAL(UMultiAnimAsset::GetNumRoles, return 0;);
	[[nodiscard]] virtual FName GetRole(int32 RoleIndex) const PURE_VIRTUAL(UMultiAnimAsset::GetRole, return FName(););
	[[nodiscard]] virtual UAnimationAsset* GetAnimationAsset(const FName& Role) const PURE_VIRTUAL(UMultiAnimAsset::GetAnimationAsset, return nullptr;);
	[[nodiscard]] virtual FTransform GetOrigin(const FName& Role) const PURE_VIRTUAL(UMultiAnimAsset::GetOrigin, return FTransform::Identity;);

	virtual void CalculateWarpTransforms(float Time, const TArrayView<const FTransform> ActorRootBoneTransforms, TArrayView<FTransform> FullAlignedActorRootBoneTransforms) const PURE_VIRTUAL(UMultiAnimAsset::CalculateWarpTransforms, );

	UFUNCTION(BlueprintPure, Category = "Animation", meta=(BlueprintThreadSafe, DisplayName = "Get Animation Asset"))
	UAnimationAsset* BP_GetAnimationAsset(const FName& Role) const { return GetAnimationAsset(Role); }

	UFUNCTION(BlueprintPure, Category = "Animation", meta=(BlueprintThreadSafe, DisplayName = "Get Origin"))
	FTransform BP_GetOrigin(const FName& Role) const { return GetOrigin(Role); }
};