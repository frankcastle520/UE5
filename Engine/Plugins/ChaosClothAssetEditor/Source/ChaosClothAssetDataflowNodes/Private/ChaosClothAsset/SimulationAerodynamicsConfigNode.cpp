// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SimulationAerodynamicsConfigNode.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "ChaosClothAsset/SimulationBaseConfigNode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimulationAerodynamicsConfigNode)

FChaosClothAssetSimulationAerodynamicsConfigNode::FChaosClothAssetSimulationAerodynamicsConfigNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FChaosClothAssetSimulationBaseConfigNode(InParam, InGuid)
{
	RegisterCollectionConnections();
	RegisterInputConnection(&Drag.WeightMap)
		.SetCanHidePin(true)
		.SetPinIsHidden(true);
	RegisterInputConnection(&OuterDrag.WeightMap)
		.SetCanHidePin(true)
		.SetPinIsHidden(true);
	RegisterInputConnection(&Lift.WeightMap)
		.SetCanHidePin(true)
		.SetPinIsHidden(true);
	RegisterInputConnection(&OuterLift.WeightMap)
		.SetCanHidePin(true)
		.SetPinIsHidden(true);
}

void FChaosClothAssetSimulationAerodynamicsConfigNode::AddProperties(FPropertyHelper& PropertyHelper) const
{
	PropertyHelper.SetProperty(this, &FluidDensity);
	PropertyHelper.SetPropertyEnum(this, &WindVelocitySpace);
	PropertyHelper.SetProperty(this, &WindVelocity);
	
	PropertyHelper.SetSolverPropertyWeighted(FName(TEXT("Drag")), Drag, [](
				const UE::Chaos::ClothAsset::FCollectionClothFacade& ClothFacade)-> float
	{
		return ClothFacade.GetSolverAirDamping();
	},{});

	if (bEnableOuterDrag)
	{
		PropertyHelper.SetSolverPropertyWeighted(FName(TEXT("OuterDrag")), OuterDrag, [](
			const UE::Chaos::ClothAsset::FCollectionClothFacade& ClothFacade)-> float
			{
				return ClothFacade.GetSolverAirDamping();
			}, {});
	}

	PropertyHelper.SetSolverPropertyWeighted(FName(TEXT("Lift")), Lift, [](
				const UE::Chaos::ClothAsset::FCollectionClothFacade& ClothFacade)-> float
	{
		return ClothFacade.GetSolverAirDamping();
	},{});

	if (bEnableOuterLift)
	{
		PropertyHelper.SetSolverPropertyWeighted(FName(TEXT("OuterLift")), OuterLift, [](
			const UE::Chaos::ClothAsset::FCollectionClothFacade& ClothFacade)-> float
			{
				return ClothFacade.GetSolverAirDamping();
			}, {});
	}
}
