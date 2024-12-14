// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEngineTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/CollectionClothSelectionFacade.h"
#include "ChaosClothAsset/ClothCollectionGroup.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ClothTetherData.h"

namespace UE::Chaos::ClothAsset
{

namespace Private
{
static void AppendTetherData(FCollectionClothFacade& ClothFacade, const FClothTetherData& TetherData)
{
	// Append new tethers
	TArrayView<TArray<int32>> TetherKinematicIndex = ClothFacade.GetTetherKinematicIndex();
	TArrayView<TArray<float>> TetherReferenceLength = ClothFacade.GetTetherReferenceLength();
	for (const TArray<TTuple<int32, int32, float>>& TetherBatch : TetherData.Tethers)
	{
		for (const TTuple<int32, int32, float>& Tether : TetherBatch)
		{
			// Tuple is Kinematic, Dynamic, RefLength
			const int32 DynamicIndex = Tether.Get<1>();
			TArray<int32>& KinematicIndex = TetherKinematicIndex[DynamicIndex];
			TArray<float>& ReferenceLength = TetherReferenceLength[DynamicIndex];
			check(KinematicIndex.Num() == ReferenceLength.Num());
			checkSlow(KinematicIndex.Find(Tether.Get<0>()) == INDEX_NONE);
			KinematicIndex.Add(Tether.Get<0>());
			ReferenceLength.Add(Tether.Get<2>());
		}
	}
}
}

void FClothEngineTools::GenerateTethers(const TSharedRef<FManagedArrayCollection>& ClothCollection, const FName& WeightMapName, const bool bGenerateGeodesicTethers, const FVector2f& MaxDistanceValue)
{
	FCollectionClothFacade ClothFacade(ClothCollection);
	FClothGeometryTools::DeleteTethers(ClothCollection);
	if (ClothFacade.HasWeightMap(WeightMapName))
	{
		FClothTetherData TetherData;
		TArray<uint32> SimIndices;
		SimIndices.Reserve(ClothFacade.GetNumSimFaces() * 3);
		for (const FIntVector3& Face : ClothFacade.GetSimIndices3D())
		{
			SimIndices.Add(Face[0]);
			SimIndices.Add(Face[1]);
			SimIndices.Add(Face[2]);
		}

		if (MaxDistanceValue.Equals(FVector2f(0.f, 1.f)))
		{
			TetherData.GenerateTethers(ClothFacade.GetSimPosition3D(), TConstArrayView<uint32>(SimIndices), ClothFacade.GetWeightMap(WeightMapName), bGenerateGeodesicTethers);
		}
		else
		{

			const TSet<int32> KinematicVertices = FClothGeometryTools::GenerateKinematicVertices3D(ClothCollection, WeightMapName, MaxDistanceValue, NAME_None);
			TetherData.GenerateTethers(ClothFacade.GetSimPosition3D(), TConstArrayView<uint32>(SimIndices), KinematicVertices, bGenerateGeodesicTethers);
		}

		Private::AppendTetherData(ClothFacade, TetherData);
	}
}

void FClothEngineTools::GenerateTethersFromSelectionSet(const TSharedRef<FManagedArrayCollection>& ClothCollection, const FName& FixedEndSet, const bool bGeodesicTethers)
{
	FClothGeometryTools::DeleteTethers(ClothCollection);
	FCollectionClothSelectionConstFacade SelectionFacade(ClothCollection);

	if (SelectionFacade.HasSelection(FixedEndSet) && SelectionFacade.GetSelectionGroup(FixedEndSet) == UE::Chaos::ClothAsset::ClothCollectionGroup::SimVertices3D)
	{
		FCollectionClothFacade ClothFacade(ClothCollection);
		FClothTetherData TetherData;
		TArray<uint32> SimIndices;
		SimIndices.Reserve(ClothFacade.GetNumSimFaces() * 3);
		for (const FIntVector3& Face : ClothFacade.GetSimIndices3D())
		{
			SimIndices.Add(Face[0]);
			SimIndices.Add(Face[1]);
			SimIndices.Add(Face[2]);
		}

		TetherData.GenerateTethers(ClothFacade.GetSimPosition3D(), TConstArrayView<uint32>(SimIndices), SelectionFacade.GetSelectionSet(FixedEndSet), bGeodesicTethers);
		Private::AppendTetherData(ClothFacade, TetherData);
	}
}

}  // End namespace UE::Chaos::ClothAsset
