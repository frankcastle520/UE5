// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ClothCollisionData.h"
#include "Delegates/Delegate.h"

class USkinnedMeshComponent;
class USkinnedAsset;
class UPhysicsAsset;

namespace UE::Chaos::ClothAsset
{
	/**
	 * Cloth collision source container.
	 */
	class FCollisionSources final
	{
	public:
		explicit FCollisionSources(USkinnedMeshComponent* InOwnerComponent);
		~FCollisionSources();

		void Add(USkinnedMeshComponent* SourceComponent, const UPhysicsAsset* SourcePhysicsAsset, bool bUseSphylsOnly = false);

		void Remove(const USkinnedMeshComponent* SourceComponent);

		void Remove(const USkinnedMeshComponent* SourceComponent, const UPhysicsAsset* SourcePhysicsAsset);

		void Reset();

	private:
		friend class FCollisionSourcesProxy;

		struct FCollisionSource
		{
			const TWeakObjectPtr<USkinnedMeshComponent> SourceComponent;
			const TWeakObjectPtr<const UPhysicsAsset> SourcePhysicsAsset;
			TWeakObjectPtr<const USkinnedAsset> CachedSkinnedAsset;
			FClothCollisionData CachedCollisionData;
			TArray<int32> CachedUsedBoneIndices;
			FDelegateHandle OnBoneTransformsFinalizedHandle;
			bool bUseSphylsOnly = false;

			FCollisionSource(
				USkinnedMeshComponent* InSourceComponent,
				const UPhysicsAsset* InSourcePhysicsAsset,
				const FSimpleDelegate& InOnBoneTransformsFinalizedDelegate,
				bool bInUseSphylsOnly);
			~FCollisionSource();

			void ExtractCollisionData(const USkinnedMeshComponent& InOwnerComponent, FClothCollisionData& CollisionData);
		};

		void ExtractCollisionData(FClothCollisionData& CollisionData);

		const TWeakObjectPtr<USkinnedMeshComponent> OwnerComponent;
		TArray<FCollisionSource> CollisionSources;
		int32 Version = INDEX_NONE;
	};

	/**
	 * Use a proxy object to extract collision data from the collision sources.
	 * The proxy allows for a different ownership than of the CollisionSources' owning component,
	 * permitting the collision data to remains with the simulation proxy even after the simulation proxy has been replaced.
	 */
	class FCollisionSourcesProxy final
	{
	public:
		explicit FCollisionSourcesProxy(FCollisionSources& InCollisionSources) : CollisionSources(InCollisionSources) {}

		const FClothCollisionData& GetCollisionData() const { return CollisionData; }

		void ExtractCollisionData();

	protected:
		FCollisionSources& CollisionSources;
		FClothCollisionData CollisionData;
		int32 Version = INDEX_NONE;
	};
}
