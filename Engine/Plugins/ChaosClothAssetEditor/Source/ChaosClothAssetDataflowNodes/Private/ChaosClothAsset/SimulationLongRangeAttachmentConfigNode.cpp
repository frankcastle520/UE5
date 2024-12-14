// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SimulationLongRangeAttachmentConfigNode.h"
#include "ChaosClothAsset/ClothEngineTools.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "Dataflow/DataflowInputOutput.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimulationLongRangeAttachmentConfigNode)

FChaosClothAssetSimulationLongRangeAttachmentConfigNode_v2::FChaosClothAssetSimulationLongRangeAttachmentConfigNode_v2(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FChaosClothAssetSimulationBaseConfigNode(InParam, InGuid)
{
	RegisterCollectionConnections();
	RegisterInputConnection(&FixedEndSet.StringValue, GET_MEMBER_NAME_CHECKED(FChaosClothAssetConnectableIStringValue, StringValue));
	RegisterInputConnection(&TetherStiffness.WeightMap)
		.SetCanHidePin(true)
		.SetPinIsHidden(true);
	RegisterInputConnection(&TetherScale.WeightMap)
		.SetCanHidePin(true)
		.SetPinIsHidden(true);
}

void FChaosClothAssetSimulationLongRangeAttachmentConfigNode_v2::AddProperties(FPropertyHelper& PropertyHelper) const
{
	PropertyHelper.SetPropertyWeighted(this, &TetherStiffness);
	PropertyHelper.SetPropertyWeighted(this, &TetherScale);
	PropertyHelper.SetPropertyBool(this, &bUseGeodesicTethers, {}, ECollectionPropertyFlags::Intrinsic);  // Intrinsic since the tethers need to be recalculated.
	PropertyHelper.SetPropertyString(this, &FixedEndSet);
}

void FChaosClothAssetSimulationLongRangeAttachmentConfigNode_v2::EvaluateClothCollection(UE::Dataflow::FContext& Context, const TSharedRef<FManagedArrayCollection>& ClothCollection) const
{
	const FString InFixedEndSetString = GetValue<FString>(Context, &FixedEndSet.StringValue);
	const FName InFixedEndSet(InFixedEndSetString);
	UE::Chaos::ClothAsset::FClothEngineTools::GenerateTethersFromSelectionSet(ClothCollection, InFixedEndSet, bUseGeodesicTethers);
}


FChaosClothAssetSimulationLongRangeAttachmentConfigNode::FChaosClothAssetSimulationLongRangeAttachmentConfigNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FChaosClothAssetSimulationBaseConfigNode(InParam, InGuid)
{
	RegisterCollectionConnections();
	RegisterInputConnection(&FixedEndWeightMap);
	RegisterInputConnection(&TetherStiffness.WeightMap)
		.SetCanHidePin(true)
		.SetPinIsHidden(true);
	RegisterInputConnection(&TetherScale.WeightMap)
		.SetCanHidePin(true)
		.SetPinIsHidden(true);
}

void FChaosClothAssetSimulationLongRangeAttachmentConfigNode::AddProperties(FPropertyHelper& PropertyHelper) const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // SetProperty functions are templated and cause deprecation warnings with the now deprecated v1
	PropertyHelper.SetPropertyWeighted(this, &TetherStiffness);
	PropertyHelper.SetPropertyWeighted(this, &TetherScale);
	PropertyHelper.SetPropertyBool(this, &bUseGeodesicTethers, {}, ECollectionPropertyFlags::Intrinsic);  // Intrinsic since the tethers need to be recalculated.
	PropertyHelper.SetPropertyString(this, &FixedEndWeightMap);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FChaosClothAssetSimulationLongRangeAttachmentConfigNode::EvaluateClothCollection(UE::Dataflow::FContext& Context, const TSharedRef<FManagedArrayCollection>& ClothCollection) const
{
	const FString InFixedEndWeightMapString = GetValue<FString>(Context, &FixedEndWeightMap);
	const FName InFixedEndWeightMap(InFixedEndWeightMapString);
	UE::Chaos::ClothAsset::FClothEngineTools::GenerateTethers(ClothCollection, InFixedEndWeightMap, bUseGeodesicTethers);
}
