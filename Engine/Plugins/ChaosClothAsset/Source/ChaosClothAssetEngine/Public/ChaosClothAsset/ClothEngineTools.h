// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Math/Vector2D.h"

struct FManagedArrayCollection;
class FName;
class FSkeletalMeshLODModel;

namespace UE::Chaos::ClothAsset
{
	/**
	 *  Tools operating on cloth collections with Engine dependency
	 */
	struct CHAOSCLOTHASSETENGINE_API FClothEngineTools
	{
		/** Generate tether data. */
		static void GenerateTethers(const TSharedRef<FManagedArrayCollection>& ClothCollection, const FName& WeightMap, const bool bGeodesicTethers, const FVector2f& MaxDistanceValue = FVector2f(0.f, 1.f));
		static void GenerateTethersFromSelectionSet(const TSharedRef<FManagedArrayCollection>& ClothCollection, const FName& FixedEndSet, const bool bGeodesicTethers);
	};
}  // End namespace UE::Chaos::ClothAsset