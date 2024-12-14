// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosClothAsset/CollisionSources.h"

#include "Chaos/Levelset.h"
#include "Chaos/WeightedLatticeImplicitObject.h"
#include "ChaosCloth/ChaosClothingSimulationCollider.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "PhysicsEngine/PhysicsAsset.h"

namespace UE::Chaos::ClothAsset
{
	FCollisionSources::FCollisionSources(USkinnedMeshComponent* InOwnerComponent)
		: OwnerComponent(InOwnerComponent)
	{}

	FCollisionSources::~FCollisionSources() = default;

	void FCollisionSources::Add(USkinnedMeshComponent* SourceComponent, const UPhysicsAsset* SourcePhysicsAsset, bool bUseSphylsOnly)
	{
		using namespace UE::Chaos::ClothAsset;

		if (OwnerComponent.IsValid() && SourceComponent && SourcePhysicsAsset)
		{
			FCollisionSource* const FoundCollisionSource = CollisionSources.FindByPredicate(
				[SourceComponent, SourcePhysicsAsset](const FCollisionSource& CollisionSource)
				{
					return CollisionSource.SourceComponent == SourceComponent && CollisionSource.SourcePhysicsAsset == SourcePhysicsAsset;
				}
			);

			if (!FoundCollisionSource)
			{
				// Delegate called after the transform buffer flip, so that the cloths' transform gets updated when the component owning the cloth isn't moving, but the collision source is
				const FSimpleDelegate OnBoneTransformsFinalizedDelegate = FSimpleDelegate::CreateLambda(
					[this]()
					{
						++Version;
					});

				// Add the new collision source
				CollisionSources.Emplace(SourceComponent, SourcePhysicsAsset, OnBoneTransformsFinalizedDelegate, bUseSphylsOnly);

				// Add prerequisite so we don't end up with a frame delay
				OwnerComponent->PrimaryComponentTick.AddPrerequisite(SourceComponent, SourceComponent->PrimaryComponentTick);

				// Mark the collision sources as changed
				++Version;
			}
		}
	}

	void FCollisionSources::Remove(const USkinnedMeshComponent* SourceComponent)
	{
		using namespace UE::Chaos::ClothAsset;

		if (SourceComponent)
		{
			// Note: Stale prerequises are removed when in the QueueTickFunction once the source object has been destroyed.
			const int32 NumRemoved = CollisionSources.RemoveAll([SourceComponent](const FCollisionSource& CollisionSource)
				{
					return !CollisionSource.SourceComponent.IsValid() || CollisionSource.SourceComponent == SourceComponent;
				});
		
			// Mark the collision sources as changed
			if (NumRemoved > 0)
			{
				++Version;
			}
		}
	}

	void FCollisionSources::Remove(const USkinnedMeshComponent* SourceComponent, const UPhysicsAsset* SourcePhysicsAsset)
	{
		using namespace UE::Chaos::ClothAsset;

		if (SourceComponent)
		{
			// Note: Stale prerequises are removed when in the QueueTickFunction once the source object has been destroyed.
			const int32 NumRemoved = CollisionSources.RemoveAll([SourceComponent, SourcePhysicsAsset](const FCollisionSource& CollisionSource)
				{
					return !CollisionSource.SourceComponent.IsValid() ||
						(CollisionSource.SourceComponent == SourceComponent && CollisionSource.SourcePhysicsAsset == SourcePhysicsAsset);
				});

			// Mark the collision sources as changed
			if (NumRemoved > 0)
			{
				++Version;
			}
		}
	}

	void FCollisionSources::Reset()
	{
		CollisionSources.Reset();
		++Version;
	}

	void FCollisionSources::ExtractCollisionData(FClothCollisionData& CollisionData)
	{
		CollisionData.Reset();
		if (OwnerComponent.IsValid())
		{
			for (FCollisionSource& CollisionSource : CollisionSources)
			{
				CollisionSource.ExtractCollisionData(*OwnerComponent, CollisionData);
			}
		}
	}

	void FCollisionSourcesProxy::ExtractCollisionData()
	{
		if (Version != CollisionSources.Version)
		{
			CollisionSources.ExtractCollisionData(CollisionData);
			Version = CollisionSources.Version;
		}
	}

	FCollisionSources::FCollisionSource::FCollisionSource(
		USkinnedMeshComponent* InSourceComponent,
		const UPhysicsAsset* InSourcePhysicsAsset,
		const FSimpleDelegate& InOnBoneTransformsFinalizedDelegate,
		bool bInUseSphylsOnly)
		: SourceComponent(InSourceComponent)
		, SourcePhysicsAsset(InSourcePhysicsAsset)
		, bUseSphylsOnly(bInUseSphylsOnly)
	{
		if (InSourceComponent)
		{
			OnBoneTransformsFinalizedHandle = InSourceComponent->RegisterOnBoneTransformsFinalizedDelegate(InOnBoneTransformsFinalizedDelegate);
		}
	}

	FCollisionSources::FCollisionSource::~FCollisionSource()
	{
		if (USkinnedMeshComponent* const SourceComponentPtr = SourceComponent.Get())
		{
			check(OnBoneTransformsFinalizedHandle.IsValid());
			SourceComponentPtr->UnregisterOnBoneTransformsFinalizedDelegate(OnBoneTransformsFinalizedHandle);
		}
	}

	void FCollisionSources::FCollisionSource::ExtractCollisionData(const USkinnedMeshComponent& InOwnerComponent, FClothCollisionData& CollisionData)
	{
		if (USkinnedMeshComponent* const SourceComponentPtr = SourceComponent.Get())
		{
			const USkinnedAsset* SkinnedAsset = SourceComponentPtr->GetSkinnedAsset();

			// Extract the collision data if not already cached
			if (CachedSkinnedAsset != SkinnedAsset)
			{
				CachedSkinnedAsset = SkinnedAsset;
				CachedCollisionData.Reset();
				CachedUsedBoneIndices.Reset();

				const UPhysicsAsset* const PhysicsAsset = SourcePhysicsAsset.Get();

				if (SkinnedAsset && PhysicsAsset)
				{
					// Extract collisions
					TArray<::Chaos::FClothingSimulationCollider::FLevelSetCollisionData> LevelSetCollisions;
					TArray<::Chaos::FClothingSimulationCollider::FSkinnedLevelSetCollisionData> SkinnedLevelSetCollisions;

					constexpr bool bSkipMissingBones = true;
					::Chaos::FClothingSimulationCollider::ExtractPhysicsAssetCollision(
						PhysicsAsset,
						&SkinnedAsset->GetRefSkeleton(),
						CachedCollisionData,
						LevelSetCollisions,
						SkinnedLevelSetCollisions,
						CachedUsedBoneIndices,
						bUseSphylsOnly,
						bSkipMissingBones);
				}
			}

			// Transform and add the cached collisions
			if (CachedUsedBoneIndices.Num())
			{
				// Calculate the component to component transform
				FTransform ComponentToComponentTransform;
				if (SourceComponent != &InOwnerComponent)
				{
					FTransform DestClothComponentTransform = InOwnerComponent.GetComponentTransform();
					DestClothComponentTransform.RemoveScaling();  // The collision source doesn't need the scale of the cloth skeletal mesh applied to it (but it does need the source scale from the component transform)
					ComponentToComponentTransform = SourceComponent->GetComponentTransform() * DestClothComponentTransform.Inverse();
				}

				// Retrieve the bone transforms
				TArray<FTransform> BoneTransforms;
				BoneTransforms.Reserve(CachedUsedBoneIndices.Num());
				for (const int32 UsedBoneIndex : CachedUsedBoneIndices)
				{
					BoneTransforms.Emplace(SourceComponent->GetBoneTransform(UsedBoneIndex, ComponentToComponentTransform));
				}

				// Append the transformed collision elements
				CollisionData.AppendTransformed(CachedCollisionData, BoneTransforms);
			}
		}
	}
}  // End namespace UE::Chaos::ClothAsset
