// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "StructUtils/InstancedStruct.h"
#include "PoseSearch/PoseSearchAssetSampler.h"
#include "PoseSearch/PoseSearchCost.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchResult.h"
#include "PoseSearch/PoseSearchRole.h"
#include "PoseSearchDatabase.generated.h"

class UAnimationAsset;
class UAnimComposite;
class UAnimMontage;
class UBlendSpace;
class UMultiAnimAsset;

#if WITH_EDITORONLY_DATA
class UPoseSearchNormalizationSet;
#endif // WITH_EDITORONLY_DATA

namespace UE::PoseSearch
{
	struct FSearchContext;
} // namespace UE::PoseSearch

UENUM()
enum class EPoseSearchMode : int32
{
	// Database searches will be evaluated extensively. the system will evaluate all the indexed poses to search for the best one.
	BruteForce,

	// Optimized search mode: the database projects the poses into a PCA space using only the most significant "NumberOfPrincipalComponents" dimensions, and construct a kdtree to facilitate the search.
	PCAKDTree,

	// Optimized search mode using a vantage point tree
	// Experimental, this feature might be removed without warning, not for production use
	VPTree UMETA(DisplayName = "VPTree (Experimental)")
};

UENUM()
enum class EPoseSearchMirrorOption : int32
{
	UnmirroredOnly UMETA(DisplayName = "Original Only"),
	MirroredOnly UMETA(DisplayName = "Mirrored Only"),
	UnmirroredAndMirrored UMETA(DisplayName = "Original and Mirrored")
};

USTRUCT()
struct POSESEARCH_API FPoseSearchDatabaseAnimationAssetBase
{
	GENERATED_BODY()
	virtual ~FPoseSearchDatabaseAnimationAssetBase() = default;
	virtual UObject* GetAnimationAsset() const { return nullptr; }
	virtual float GetPlayLength() const;
	virtual int32 GetNumRoles() const { return 1; }
	virtual UE::PoseSearch::FRole GetRole(int32 RoleIndex) const { return UE::PoseSearch::DefaultRole; }
	virtual UAnimationAsset* GetAnimationAssetForRole(const UE::PoseSearch::FRole& Role) const;
	virtual FTransform GetRootTransformOriginForRole(const UE::PoseSearch::FRole& Role) const;

#if WITH_EDITOR
	virtual int32 GetFrameAtTime(float Time) const;
	virtual bool IsSkeletonCompatible(TObjectPtr<const UPoseSearchSchema> InSchema) const;
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	virtual bool IsDisableReselection() const { return bDisableReselection; }
	virtual void SetDisableReselection(bool bValue) { bDisableReselection = bValue; }
	virtual UClass* GetAnimationAssetStaticClass() const { return nullptr; }
	virtual bool IsLooping() const { return false; }
	virtual const FString GetName() const;
	virtual bool IsEnabled() const { return bEnabled; }
	virtual void SetIsEnabled(bool bValue) { bEnabled = bValue; }
	virtual bool IsRootMotionEnabled() const { return false; }
	virtual EPoseSearchMirrorOption GetMirrorOption() const { return MirrorOption; }

	// [0, 0] represents the entire frame range of the original animation.
	virtual FFloatInterval GetSamplingRange() const { return FFloatInterval(0.f, 0.f); }
	FFloatInterval GetEffectiveSamplingRange() const;
	static FFloatInterval GetEffectiveSamplingRange(float PlayLength, const FFloatInterval& SamplingRange);

	virtual int64 GetEditorMemSize() const;
	virtual int64 GetApproxCookedSize() const { return GetEditorMemSize(); }

	bool IsSynchronizedWithExternalDependency() const { return bSynchronizeWithExternalDependency_DEPRECATED || BranchInId != 0; }

	// This allows users to enable or exclude animations from this database. Useful for debugging.
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (DisplayPriority = 1))
	bool bEnabled = true;

	// if bDisableReselection is true, poses from the same asset cannot be reselected. Useful to avoid jumping on frames on the same looping animations
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ExcludeFromHash, DisplayPriority = 2))
	bool bDisableReselection = false;

	// This allows users to set if this animation is original only (no mirrored data), original and mirrored, or only the mirrored version of this animation.
	// It requires the mirror table to be set up in the database Schema.
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (DisplayPriority = 3))
	EPoseSearchMirrorOption MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;

	// SynchronizeWithExternalDependency is true when this asset has been added via SynchronizeWithExternalDependencies.
	// To delete it, remove the PoseSearchBranchIn notify state
	UPROPERTY()
	bool bSynchronizeWithExternalDependency_DEPRECATED = false;

	// SynchronizeWithExternalDependency is true when this asset has been added via SynchronizeWithExternalDependencies.
	// To delete it, remove the PoseSearchBranchIn notify state
	UPROPERTY(VisibleAnywhere, Category = "Settings", meta = (DisplayPriority = 20))
	uint32 BranchInId = 0;
#endif // WITH_EDITORONLY_DATA

	friend bool operator==(const FPoseSearchDatabaseAnimationAssetBase& A, const FPoseSearchDatabaseAnimationAssetBase& B)
	{
#if WITH_EDITORONLY_DATA
		return A.bEnabled == B.bEnabled &&
			A.bDisableReselection == B.bDisableReselection &&
			A.MirrorOption == B.MirrorOption &&
			A.bSynchronizeWithExternalDependency_DEPRECATED == B.bSynchronizeWithExternalDependency_DEPRECATED &&
			A.BranchInId == B.BranchInId;
#else // WITH_EDITORONLY_DATA
		return true;
#endif // WITH_EDITORONLY_DATA
	}
};

template<> struct TStructOpsTypeTraits<FPoseSearchDatabaseAnimationAssetBase> : public TStructOpsTypeTraitsBase2<FPoseSearchDatabaseAnimationAssetBase>
{
	enum { WithIdenticalViaEquality = true };
};

/** A sequence entry in a UPoseSearchDatabase. */
USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseSequence : public FPoseSearchDatabaseAnimationAssetBase
{
	GENERATED_BODY()
	virtual ~FPoseSearchDatabaseSequence() = default;

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (DisplayPriority = 0))
	TObjectPtr<UAnimSequence> Sequence;

#if WITH_EDITORONLY_DATA
	// It allows users to set a time range to an individual animation sequence in the database. 
	// This is effectively trimming the beginning and end of the animation in the database (not in the original sequence).
	// If set to [0, 0] it will be the entire frame range of the original sequence.
	// Set to readonly if this asset is synchronized via PoseSearchBranchIn notify state.
	// To edit its value update the associated PoseSearchBranchIn in Sequence
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (EditCondition="BranchInId == 0", ClampToMinMaxLimits, DisplayPriority = 2))
	FFloatInterval SamplingRange = FFloatInterval(0.f, 0.f);

	virtual UClass* GetAnimationAssetStaticClass() const override;
	virtual bool IsLooping() const override;
	virtual bool IsRootMotionEnabled() const override;
	virtual FFloatInterval GetSamplingRange() const override { return SamplingRange; }
#endif // WITH_EDITORONLY_DATA
	
	virtual UObject* GetAnimationAsset() const override;

	friend bool operator==(const FPoseSearchDatabaseSequence& A, const FPoseSearchDatabaseSequence& B)
	{
		return static_cast<const FPoseSearchDatabaseAnimationAssetBase&>(A) == static_cast<const FPoseSearchDatabaseAnimationAssetBase&>(B)
			&& A.Sequence == B.Sequence
#if WITH_EDITORONLY_DATA
			&& A.SamplingRange == B.SamplingRange
#endif // WITH_EDITORONLY_DATA
			;
	}
};

template<> struct TStructOpsTypeTraits<FPoseSearchDatabaseSequence> : public TStructOpsTypeTraitsBase2<FPoseSearchDatabaseSequence>
{
	enum { WithIdenticalViaEquality = true };
};

/** An blend space entry in a UPoseSearchDatabase. */
USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseBlendSpace : public FPoseSearchDatabaseAnimationAssetBase
{
	GENERATED_BODY()
	virtual ~FPoseSearchDatabaseBlendSpace() = default;

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (DisplayPriority = 0))
	TObjectPtr<UBlendSpace> BlendSpace;

#if WITH_EDITORONLY_DATA

	// If true this BlendSpace will output a single segment in the database.
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (DisplayPriority = 4))
	bool bUseSingleSample = false;

	// When turned on, this will use the set grid samples of the blend space asset for sampling. This will override the Number of Horizontal/Vertical Samples.
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (EditCondition = "!bUseSingleSample", EditConditionHides, DisplayPriority = 5))
	bool bUseGridForSampling = false;

	// Sets the number of horizontal samples in the blend space to pull the animation data coverage from. The larger the samples the more the data, but also the more memory and performance it takes.
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (EditCondition = "!bUseSingleSample && !bUseGridForSampling", EditConditionHides, ClampMin = "1", UIMin = "1", UIMax = "25", DisplayPriority = 6))
	int32 NumberOfHorizontalSamples = 9;
	
	// Sets the number of vertical samples in the blend space to pull the animation data coverage from.The larger the samples the more the data, but also the more memory and performance it takes.
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (EditCondition = "!bUseSingleSample && !bUseGridForSampling", EditConditionHides, ClampMin = "1", UIMin = "1", UIMax = "25", DisplayPriority = 7))
	int32 NumberOfVerticalSamples = 2;

	// BlendParams used to sample this BlendSpace
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (EditCondition = "bUseSingleSample", EditConditionHides, DisplayPriority = 8))
	float BlendParamX = 0.f;

	// BlendParams used to sample this BlendSpace
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (EditCondition = "bUseSingleSample", EditConditionHides, DisplayPriority = 9))
	float BlendParamY = 0.f;

	// It allows users to set a time range to an individual blend space in the database.
	// This is effectively trimming the beginning and end of the animation in the database (not in the original blend space).
	// If set to [0, 0] it will be the entire frame range of the original blend space.
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampToMinMaxLimits, DisplayPriority = 2))
	FFloatInterval SamplingRange = FFloatInterval(0.f, 0.f);

	virtual UClass* GetAnimationAssetStaticClass() const override;
	virtual bool IsLooping() const override;
	virtual bool IsRootMotionEnabled() const override;
	virtual FFloatInterval GetSamplingRange() const override { return SamplingRange; }

	void GetBlendSpaceParameterSampleRanges(int32& HorizontalBlendNum, int32& VerticalBlendNum) const;
	FVector BlendParameterForSampleRanges(int32 HorizontalBlendIndex, int32 VerticalBlendIndex) const;
#endif // WITH_EDITORONLY_DATA

	virtual UObject* GetAnimationAsset() const override;

#if WITH_EDITOR
	virtual int32 GetFrameAtTime(float Time) const override;
#endif // WITH_EDITOR

	friend bool operator==(const FPoseSearchDatabaseBlendSpace& A, const FPoseSearchDatabaseBlendSpace& B)
	{
		return static_cast<const FPoseSearchDatabaseAnimationAssetBase&>(A) == static_cast<const FPoseSearchDatabaseAnimationAssetBase&>(B)
			&& A.BlendSpace == B.BlendSpace
#if WITH_EDITORONLY_DATA
			&& A.bUseSingleSample == B.bUseSingleSample
			&& A.bUseGridForSampling == B.bUseGridForSampling
			&& A.NumberOfHorizontalSamples == B.NumberOfHorizontalSamples
			&& A.NumberOfVerticalSamples == B.NumberOfVerticalSamples
			&& A.BlendParamX == B.BlendParamX
			&& A.BlendParamY == B.BlendParamY
			&& A.SamplingRange == B.SamplingRange
#endif // WITH_EDITORONLY_DATA
			;
	}
};

template<> struct TStructOpsTypeTraits<FPoseSearchDatabaseBlendSpace> : public TStructOpsTypeTraitsBase2<FPoseSearchDatabaseBlendSpace>
{
	enum { WithIdenticalViaEquality = true };
};

/** An entry in a UPoseSearchDatabase. */
USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseAnimComposite : public FPoseSearchDatabaseAnimationAssetBase
{
	GENERATED_BODY()
	virtual ~FPoseSearchDatabaseAnimComposite() = default;

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (DisplayPriority = 0))
	TObjectPtr<UAnimComposite> AnimComposite;

#if WITH_EDITORONLY_DATA
	// It allows users to set a time range to an individual animation composite in the database. 
	// This is effectively trimming the beginning and end of the animation in the database (not in the original composite).
	// If set to [0, 0] it will be the entire frame range of the original composite.
	// Set to readonly if this asset is synchronized via PoseSearchBranchIn notify state.
	// To edit its value update the associated PoseSearchBranchIn in AnimComposite
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (EditCondition="BranchInId == 0", ClampToMinMaxLimits, DisplayPriority = 3))
	FFloatInterval SamplingRange = FFloatInterval(0.f, 0.f);

	virtual UClass* GetAnimationAssetStaticClass() const override;
	virtual bool IsLooping() const override;
	virtual bool IsRootMotionEnabled() const override;
	virtual FFloatInterval GetSamplingRange() const override { return SamplingRange; }
#endif // WITH_EDITORONLY_DATA

	virtual UObject* GetAnimationAsset() const override;

	friend bool operator==(const FPoseSearchDatabaseAnimComposite& A, const FPoseSearchDatabaseAnimComposite& B)
	{
		return static_cast<const FPoseSearchDatabaseAnimationAssetBase&>(A) == static_cast<const FPoseSearchDatabaseAnimationAssetBase&>(B)
			&& A.AnimComposite == B.AnimComposite
#if WITH_EDITORONLY_DATA
			&& A.SamplingRange == B.SamplingRange
#endif // WITH_EDITORONLY_DATA
			;
	}
};

template<> struct TStructOpsTypeTraits<FPoseSearchDatabaseAnimComposite> : public TStructOpsTypeTraitsBase2<FPoseSearchDatabaseAnimComposite>
{
	enum { WithIdenticalViaEquality = true };
};

/** An anim montage entry in a UPoseSearchDatabase. */
USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseAnimMontage : public FPoseSearchDatabaseAnimationAssetBase
{
	GENERATED_BODY()
	virtual ~FPoseSearchDatabaseAnimMontage() = default;

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (DisplayPriority = 0))
	TObjectPtr<UAnimMontage> AnimMontage;

#if WITH_EDITORONLY_DATA
	// It allows users to set a time range to an individual animation montage in the database. 
	// This is effectively trimming the beginning and end of the animation in the database (not in the original montage).
	// If set to [0, 0] it will be the entire frame range of the original montage.
	// Set to readonly if this asset is synchronized via PoseSearchBranchIn notify state.
	// To edit its value update the associated PoseSearchBranchIn in AnimMontage
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (EditCondition="BranchInId == 0", ClampToMinMaxLimits, DisplayPriority = 2))
	FFloatInterval SamplingRange = FFloatInterval(0.f, 0.f);

	virtual UClass* GetAnimationAssetStaticClass() const override;
	virtual bool IsLooping() const override;
	virtual bool IsRootMotionEnabled() const override;
	virtual FFloatInterval GetSamplingRange() const override { return SamplingRange; }
#endif // WITH_EDITORONLY_DATA

	virtual UObject* GetAnimationAsset() const override;

	friend bool operator==(const FPoseSearchDatabaseAnimMontage& A, const FPoseSearchDatabaseAnimMontage& B)
	{
		return static_cast<const FPoseSearchDatabaseAnimationAssetBase&>(A) == static_cast<const FPoseSearchDatabaseAnimationAssetBase&>(B)
			&& A.AnimMontage == B.AnimMontage
#if WITH_EDITORONLY_DATA
			&& A.SamplingRange == B.SamplingRange
#endif // WITH_EDITORONLY_DATA
			;
	}
};

template<> struct TStructOpsTypeTraits<FPoseSearchDatabaseAnimMontage> : public TStructOpsTypeTraitsBase2<FPoseSearchDatabaseAnimMontage>
{
	enum { WithIdenticalViaEquality = true };
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseMultiAnimAsset : public FPoseSearchDatabaseAnimationAssetBase
{
	GENERATED_BODY()
	virtual ~FPoseSearchDatabaseMultiAnimAsset() = default;

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (DisplayPriority = 0))
	TObjectPtr<UMultiAnimAsset> MultiAnimAsset;

#if WITH_EDITORONLY_DATA
	// It allows users to set a time range to an individual UMultiAnimAsset in the database. 
	// This is effectively trimming the beginning and end of the animation in the database (not in the original UMultiAnimAsset).
	// If set to [0, 0] it will be the entire frame range of the original UMultiAnimAsset.
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampToMinMaxLimits, DisplayPriority = 2))
	FFloatInterval SamplingRange = FFloatInterval(0.f, 0.f);

	virtual UClass* GetAnimationAssetStaticClass() const override;
	virtual bool IsLooping() const override;
	virtual bool IsRootMotionEnabled() const override;
	virtual FFloatInterval GetSamplingRange() const override { return SamplingRange; }
#endif // WITH_EDITORONLY_DATA

	virtual UObject* GetAnimationAsset() const override;
	virtual float GetPlayLength() const override;

	virtual int32 GetNumRoles() const override;
	virtual UE::PoseSearch::FRole GetRole(int32 RoleIndex) const override;
	virtual UAnimationAsset* GetAnimationAssetForRole(const UE::PoseSearch::FRole& Role) const override;
	virtual FTransform GetRootTransformOriginForRole(const UE::PoseSearch::FRole& Role) const override;

#if WITH_EDITOR
	virtual int32 GetFrameAtTime(float Time) const override;
#endif // WITH_EDITOR

	friend bool operator==(const FPoseSearchDatabaseMultiAnimAsset& A, const FPoseSearchDatabaseMultiAnimAsset& B)
	{
		return static_cast<const FPoseSearchDatabaseAnimationAssetBase&>(A) == static_cast<const FPoseSearchDatabaseAnimationAssetBase&>(B)
			&& A.MultiAnimAsset == B.MultiAnimAsset
#if WITH_EDITORONLY_DATA
			&& A.SamplingRange == B.SamplingRange
#endif // WITH_EDITORONLY_DATA
			;
	}
};

template<> struct TStructOpsTypeTraits<FPoseSearchDatabaseMultiAnimAsset> : public TStructOpsTypeTraitsBase2<FPoseSearchDatabaseMultiAnimAsset>
{
	enum { WithIdenticalViaEquality = true };
};

/** A data asset for indexing a collection of animation sequences. */
UCLASS(BlueprintType, Category = "Animation|Pose Search", meta = (DisplayName = "Pose Search Database"))
class POSESEARCH_API UPoseSearchDatabase : public UDataAsset
{
	GENERATED_BODY()
public:

	// The Schema sets what channels this database will use to match against (bones, trajectory and what properties of those you’re interested in, such as position and velocity).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Database")
	TObjectPtr<const UPoseSearchSchema> Schema;

	// Cost added to the continuing pose from this database. This allows users to apply a cost bias (positive or negative) to the continuing pose.
	// This is useful to help the system stay in one animation segment longer, or shorter depending on how you set this bias.
	// Negative values make it more likely to be picked, or stayed in, positive values make it less likely to be picked or stay in.
	// Note: excluded from DDC hash, since used only at runtime in SearchContinuingPose
	UPROPERTY(EditAnywhere, Category = "Database", meta = (ExcludeFromHash)) 
	float ContinuingPoseCostBias = -0.01f;

	// Base Cost added or removed to all poses from this database. It can be overridden by Anim Notify: Pose Search Modify Cost at the frame level of animation data.
	// Negative values make it more likely to be picked, or stayed in, Positive values make it less likely to be picked or stay in.
	UPROPERTY(EditAnywhere, Category = "Database")
	float BaseCostBias = 0.f;

	// Cost added to all looping animation assets in this database. This allows users to make it more or less likely to pick the looping animation segments.
	// Negative values make it more likely to be picked, or stayed in, Positive values make it less likely to be picked or stay in.
	UPROPERTY(EditAnywhere, Category = "Database")
	float LoopingCostBias = -0.005f;

#if WITH_EDITORONLY_DATA
	// These settings allow users to trim the start and end of animations in the database to preserve start/end frames for blending, and prevent the system from selecting the very last frames before it blends out.
	// valid animation frames will be AnimationAssetTimeStart + ExcludeFromDatabaseParameters.Min, AnimationAssetTimeEnd + ExcludeFromDatabaseParameters.Max
	UPROPERTY(EditAnywhere, Category = "Database", meta = (AllowInvertedInterval))
	FFloatInterval ExcludeFromDatabaseParameters = FFloatInterval(0.f, -0.3f);

	// extrapolation of animation assets will be clamped by AnimationAssetTimeStart + AdditionalExtrapolationTime.Min, AnimationAssetTimeEnd + AdditionalExtrapolationTime.Max
	UPROPERTY(EditAnywhere, Category = "Database", meta = (AllowInvertedInterval))
	FFloatInterval AdditionalExtrapolationTime = FFloatInterval(-100.f, 100.f);
#endif // WITH_EDITORONLY_DATA

	// use GetNumAnimationAssets() and GetDatabaseAnimationAsset(...) APIs to query for AnimationAssets
	UE_DEPRECATED(5.4, "This property will be made private")
	UPROPERTY()
	TArray<FInstancedStruct> AnimationAssets;

	/** Array of tags that can be used as metadata. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Database")
	TArray<FName> Tags;

#if WITH_EDITORONLY_DATA
	// This optional asset defines a list of databases you want to normalize together. Without it, it would be difficult to compare costs from separately normalized databases containing different types of animation,
	// like only idles versus only runs animations, given that the range of movement would be dramatically different.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Database")
	TObjectPtr<const UPoseSearchNormalizationSet> NormalizationSet;

	// If null, the default preview mesh for the skeleton will be used. Otherwise, this will be used in preview scenes.
	// @todo: Move this to be a setting in the Pose Search Database editor. 
	UPROPERTY(EditAnywhere, Category = "Preview")
	TObjectPtr<USkeletalMesh> PreviewMesh = nullptr;
#endif // WITH_EDITORONLY_DATA

	// This dictates how the database will perform the search.
	UPROPERTY(EditAnywhere, Category = "Performance")
	EPoseSearchMode PoseSearchMode = EPoseSearchMode::PCAKDTree;

#if WITH_EDITORONLY_DATA
	// Number of dimensions used to create the kdtree. More dimensions allows a better explanation of the variance of the dataset that usually translates in better search results, but will imply more memory usage and worse performances.
	UPROPERTY(EditAnywhere, Category = "Performance", meta = (EditCondition = "PoseSearchMode == EPoseSearchMode::PCAKDTree", EditConditionHides, ClampMin = "1", ClampMax = "64", UIMin = "1", UIMax = "64"))
	int32 NumberOfPrincipalComponents = 4;

	UPROPERTY(EditAnywhere, Category = "Performance", meta = (EditCondition = "PoseSearchMode == EPoseSearchMode::PCAKDTree", EditConditionHides, ClampMin = "1", ClampMax = "256", UIMin = "1", UIMax = "256"))
	int32 KDTreeMaxLeafSize = 16;
#endif // WITH_EDITORONLY_DATA
	
	// @todo: rename to KNNQueryNumNeighbors to be usable with the VPTree as well
	// Out of a kdtree search, results will have only an approximate cost, so the database search will select the best “KDTree Query Num Neighbors” poses to perform the full cost analysis, and be able to elect the best pose.
	// Memory & Performance Optimization! If KDTreeQueryNumNeighbors is 1 all the SearchIndexPrivate::Values will be stripped away, and the search will exclusively rely on the KDTree query result from the PCA space encoded values (SearchIndexPrivate::PCAValues).
	UPROPERTY(EditAnywhere, Category = "Performance", meta = (DisplayName = "KNNQueryNumNeighbors", EditCondition = "PoseSearchMode == EPoseSearchMode::PCAKDTree || PoseSearchMode == EPoseSearchMode::VPTree", EditConditionHides, ClampMin = "1", ClampMax = "600", UIMin = "1"))
	int32 KDTreeQueryNumNeighbors = 200;

#if WITH_EDITORONLY_DATA
	// if two poses values (multi dimensional point with the schema cardinality) are closer than PosePruningSimilarityThreshold,
	// only one will be saved into the database FSearchIndexBase (to save memory) and accessed by the two different pose indexes
	UPROPERTY(EditAnywhere, Category = "Performance")
	float PosePruningSimilarityThreshold = 0.f;

	// if two PCA values (multi dimensional point with the GetNumberOfPrincipalComponents cardinality) are closer than PCAValuesPruningSimilarityThreshold,
	// only one will be saved into the database FSearchIndex (to save memory).
	UPROPERTY(EditAnywhere, Category = "Performance", meta = (EditCondition = "PoseSearchMode == EPoseSearchMode::PCAKDTree", EditConditionHides))
	float PCAValuesPruningSimilarityThreshold = 0.f;
#endif // WITH_EDITORONLY_DATA

	// @todo: rename to KNNQueryNumNeighborsWithDuplicates to be usable with the VPTree as well
	// if PCAValuesPruningSimilarityThreshold > 0 the kdtree will remove duplicates, every result out of the KDTreeQueryNumNeighbors could potentially references multiple poses.
	// KDTreeQueryNumNeighborsWithDuplicates is the upper bound number of poses the system will perform the full cost evaluation. if KDTreeQueryNumNeighborsWithDuplicates is zero then there's no upper bound
	UPROPERTY(EditAnywhere, Category = "Performance", meta = (DisplayName = "KNNQueryNumNeighborsWithDuplicates", EditCondition = "PoseSearchMode == EPoseSearchMode::PCAKDTree && PCAValuesPruningSimilarityThreshold > 0", EditConditionHides, ClampMin = "0", ClampMax = "600", UIMin = "1"))
	int32 KDTreeQueryNumNeighborsWithDuplicates = 0;
	
private:
	// Do not use it directly. Use GetSearchIndex / SetSearchIndex interact with it and validate that is ok to do so.
	UE::PoseSearch::FSearchIndex SearchIndexPrivate;
	
	// CachedAssetMap is NOT serialized in operator<< but recalculated by UpdateCachedProperties every time SearchIndexPrivate changes
	TMap<TWeakObjectPtr<UObject>, TArray<int32>> CachedAssetMap;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnDerivedDataRebuildMulticaster);
	FOnDerivedDataRebuildMulticaster OnDerivedDataRebuild;

	DECLARE_MULTICAST_DELEGATE(FOnSynchronizeWithExternalDependenciesMulticaster);
	FOnSynchronizeWithExternalDependenciesMulticaster OnSynchronizeWithExternalDependencies;
#endif // WITH_EDITOR

public:
	virtual ~UPoseSearchDatabase();

	void SetSearchIndex(const UE::PoseSearch::FSearchIndex& SearchIndex);
	const UE::PoseSearch::FSearchIndex& GetSearchIndex() const;
	
	bool GetSkipSearchIfPossible() const;

	int32 GetPoseIndexFromTime(float AssetTime, const UE::PoseSearch::FSearchIndexAsset& SearchIndexAsset) const;

	void AddAnimationAsset(FInstancedStruct AnimationAsset);
	void RemoveAnimationAssetAt(int32 AnimationAssetIndex);

	template<typename TDatabaseAnimationAsset> const TDatabaseAnimationAsset* GetDatabaseAnimationAsset(int32 AnimationAssetIndex) const;
	template<typename TDatabaseAnimationAsset> const TDatabaseAnimationAsset* GetDatabaseAnimationAsset(const UE::PoseSearch::FSearchIndexAsset& SearchIndexAsset) const;
	template<typename TDatabaseAnimationAsset> TDatabaseAnimationAsset* GetMutableDatabaseAnimationAsset(int32 AnimationAssetIndex);
	template<typename TDatabaseAnimationAsset> TDatabaseAnimationAsset* GetMutableDatabaseAnimationAsset(const UE::PoseSearch::FSearchIndexAsset& SearchIndexAsset);

	UE_DEPRECATED(5.4, "Use GetNumAnimationAssets to iterate over GetDatabaseAnimationAsset instead")
	const TArray<FInstancedStruct>& GetAnimationAssets() const;

	UE_DEPRECATED(5.4, "Use GetDatabaseAnimationAsset instead")
	const FInstancedStruct& GetAnimationAssetStruct(int32 AnimationAssetIndex) const;
	UE_DEPRECATED(5.4, "Use GetDatabaseAnimationAsset instead")
	const FInstancedStruct& GetAnimationAssetStruct(const UE::PoseSearch::FSearchIndexAsset& SearchIndexAsset) const;
	UE_DEPRECATED(5.4, "Use GetMutableDatabaseAnimationAsset instead")
	FInstancedStruct& GetMutableAnimationAssetStruct(int32 AnimationAssetIndex);
	UE_DEPRECATED(5.4, "Use GetMutableDatabaseAnimationAsset instead")
	FInstancedStruct& GetMutableAnimationAssetStruct(const UE::PoseSearch::FSearchIndexAsset& SearchIndexAsset);
	
	UE_DEPRECATED(5.4, "Use GetDatabaseAnimationAsset instead")
	const FPoseSearchDatabaseAnimationAssetBase* GetAnimationAssetBase(int32 AnimationAssetIndex) const { return GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(AnimationAssetIndex); }
	UE_DEPRECATED(5.4, "Use GetDatabaseAnimationAsset instead")
	const FPoseSearchDatabaseAnimationAssetBase* GetAnimationAssetBase(const UE::PoseSearch::FSearchIndexAsset& SearchIndexAsset) const { return GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(SearchIndexAsset); }
	UE_DEPRECATED(5.4, "Use GetMutableDatabaseAnimationAsset instead")
	FPoseSearchDatabaseAnimationAssetBase* GetMutableAnimationAssetBase(int32 AnimationAssetIndex) { return GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(AnimationAssetIndex); }
	UE_DEPRECATED(5.4, "Use GetMutableDatabaseAnimationAsset instead")
	FPoseSearchDatabaseAnimationAssetBase* GetMutableAnimationAssetBase(const UE::PoseSearch::FSearchIndexAsset& SearchIndexAsset) { return GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(SearchIndexAsset); }

	float GetRealAssetTime(int32 PoseIdx) const;
	float GetNormalizedAssetTime(int32 PoseIdx) const;

	// Begin UObject
	virtual void PostLoad() override;
	virtual void PreSaveRoot(FObjectPreSaveRootContext ObjectSaveContext) override;
	virtual void PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext) override;
	virtual void Serialize(FArchive& Ar) override;
	// End UObject
	
	UE::PoseSearch::FSearchResult Search(UE::PoseSearch::FSearchContext& SearchContext) const;
	UE::PoseSearch::FSearchResult SearchContinuingPose(UE::PoseSearch::FSearchContext& SearchContext) const;

	bool Contains(const UObject* Object) const;

	UFUNCTION(BlueprintPure, Category = "Animation|Pose Search|Experimental", meta = (BlueprintThreadSafe))
	int32 GetNumAnimationAssets() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Pose Search|Experimental", meta = (BlueprintThreadSafe))
	UObject* GetAnimationAsset(int32 Index) const;

#if WITH_EDITOR
	int32 GetNumberOfPrincipalComponents() const;

	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
	virtual bool IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform) override;

	typedef FOnDerivedDataRebuildMulticaster::FDelegate FOnDerivedDataRebuild;
	void RegisterOnDerivedDataRebuild(const FOnDerivedDataRebuild& Delegate) { OnDerivedDataRebuild.Add(Delegate); }
	void UnregisterOnDerivedDataRebuild(void* Unregister) { OnDerivedDataRebuild.RemoveAll(Unregister); }
	void NotifyDerivedDataRebuild() const { OnDerivedDataRebuild.Broadcast(); }

	typedef FOnSynchronizeWithExternalDependenciesMulticaster::FDelegate FOnSynchronizeWithExternalDependencies;
	void RegisterOnSynchronizeWithExternalDependencies(const FOnSynchronizeWithExternalDependencies& Delegate) { OnSynchronizeWithExternalDependencies.Add(Delegate); }
	void UnregisterOnSynchronizeWithExternalDependencies(void* Unregister) { OnSynchronizeWithExternalDependencies.RemoveAll(Unregister); }
	void NotifySynchronizeWithExternalDependencies() const { OnSynchronizeWithExternalDependencies.Broadcast(); }

	void SynchronizeWithExternalDependencies();
	void SynchronizeWithExternalDependencies(TConstArrayView<UAnimSequenceBase*> SequencesBase);
#endif // WITH_EDITOR

#if WITH_EDITOR && ENABLE_ANIM_DEBUG
	void TestSynchronizeWithExternalDependencies();
#endif // WITH_EDITOR && ENABLE_ANIM_DEBUG

	TConstArrayView<int32> GetAssetIndexesForSourceAsset(const UObject* SourceAsset) const;

private:
	UE::PoseSearch::FSearchResult SearchPCAKDTree(UE::PoseSearch::FSearchContext& SearchContext) const;
	UE::PoseSearch::FSearchResult SearchVPTree(UE::PoseSearch::FSearchContext& SearchContext) const;
	UE::PoseSearch::FSearchResult SearchBruteForce(UE::PoseSearch::FSearchContext& SearchContext) const;

	typedef TArray<int32, TInlineAllocator<256, TMemStackAllocator<>>> FSelectableAssetIdx;
	void PopulateSelectableAssetIdx(FSelectableAssetIdx& SelectableAssetIdx, TConstArrayView<const UObject*> AssetsToConsider) const;

	typedef TArray<int32, TInlineAllocator<256, TMemStackAllocator<>>> FNonSelectableIdx;
	void PopulateNonSelectableIdx(FNonSelectableIdx& NonSelectableIdx, UE::PoseSearch::FSearchContext& SearchContext
#if UE_POSE_SEARCH_TRACE_ENABLED
		, TConstArrayView<float> QueryValues
#endif //UE_POSE_SEARCH_TRACE_ENABLED
	) const;

	void UpdateCachedProperties();
};

template<typename TDatabaseAnimationAsset>
inline const TDatabaseAnimationAsset* UPoseSearchDatabase::GetDatabaseAnimationAsset(int32 AnimationAssetIndex) const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (AnimationAssets.IsValidIndex(AnimationAssetIndex))
	{
		return AnimationAssets[AnimationAssetIndex].GetPtr<TDatabaseAnimationAsset>();
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	return nullptr;
}

template<typename TDatabaseAnimationAsset>
inline const TDatabaseAnimationAsset* UPoseSearchDatabase::GetDatabaseAnimationAsset(const UE::PoseSearch::FSearchIndexAsset& SearchIndexAsset) const
{
	return GetDatabaseAnimationAsset<TDatabaseAnimationAsset>(SearchIndexAsset.GetSourceAssetIdx());
}

template<typename TDatabaseAnimationAsset>
inline TDatabaseAnimationAsset* UPoseSearchDatabase::GetMutableDatabaseAnimationAsset(int32 AnimationAssetIndex)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (AnimationAssets.IsValidIndex(AnimationAssetIndex))
	{
		return AnimationAssets[AnimationAssetIndex].GetMutablePtr<TDatabaseAnimationAsset>();
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	return nullptr;
}

template<typename TDatabaseAnimationAsset>
inline TDatabaseAnimationAsset* UPoseSearchDatabase::GetMutableDatabaseAnimationAsset(const UE::PoseSearch::FSearchIndexAsset& SearchIndexAsset)
{
	return GetMutableDatabaseAnimationAsset<TDatabaseAnimationAsset>(SearchIndexAsset.GetSourceAssetIdx());
}