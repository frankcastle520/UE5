// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SimulationMaxDistanceConfigNode.h"
#include "ChaosClothAsset/ClothCollectionGroup.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ChaosClothAsset/CollectionClothSelectionFacade.h"
#include "Chaos/CollectionPropertyFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimulationMaxDistanceConfigNode)

FChaosClothAssetSimulationMaxDistanceConfigNode::FChaosClothAssetSimulationMaxDistanceConfigNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FChaosClothAssetSimulationBaseConfigNode(InParam, InGuid)
{
	KinematicVertices3D = TEXT("KinematicVertices3D");
	RegisterCollectionConnections();
	RegisterInputConnection(&MaxDistance.WeightMap)
		.SetCanHidePin(true)
		.SetPinIsHidden(true);
	RegisterInputConnection(&InKinematic, GET_MEMBER_NAME_CHECKED(FChaosClothAssetConnectableIStringValue, StringValue));
	RegisterOutputConnection(&KinematicVertices3D);
}

void FChaosClothAssetSimulationMaxDistanceConfigNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	Super::Evaluate(Context, Out);

	if (Out->IsA(&KinematicVertices3D))
	{
		SetValue(Context, KinematicVertices3D, &KinematicVertices3D);
	}
}

void FChaosClothAssetSimulationMaxDistanceConfigNode::AddProperties(FPropertyHelper& PropertyHelper) const
{
	PropertyHelper.SetPropertyWeighted(this, &MaxDistance, {}, ECollectionPropertyFlags::Intrinsic);  // Intrinsic since the deformer weights needs to be recalculated
	PropertyHelper.SetPropertyString(this, &KinematicVertices3D);
}

void FChaosClothAssetSimulationMaxDistanceConfigNode::EvaluateClothCollection(UE::Dataflow::FContext& Context, const TSharedRef<FManagedArrayCollection>& ClothCollection) const
{
	using namespace UE::Chaos::ClothAsset;
	const FName MaxDistanceString(GetValue<FString>(Context, &MaxDistance.WeightMap)); //  Override for this is already set by AddProperties
	const FName InputKinematicString(GetValue<FString>(Context, &InKinematic.StringValue));

	FCollectionClothSelectionFacade SelectionFacade(ClothCollection);
	SelectionFacade.DefineSchema();
	SelectionFacade.FindOrAddSelectionSet(FName(KinematicVertices3D), ClothCollectionGroup::SimVertices3D) =
		FClothGeometryTools::GenerateKinematicVertices3D(ClothCollection, MaxDistanceString, FVector2f(MaxDistance.Low, MaxDistance.High), InputKinematicString);

}