// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SimulationVelocityScaleConfigNode.h"
#include "Chaos/CollectionPropertyFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimulationVelocityScaleConfigNode)

FChaosClothAssetSimulationVelocityScaleConfigNode::FChaosClothAssetSimulationVelocityScaleConfigNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FChaosClothAssetSimulationBaseConfigNode(InParam, InGuid)
{
	RegisterCollectionConnections();
}

void FChaosClothAssetSimulationVelocityScaleConfigNode::AddProperties(FPropertyHelper& PropertyHelper) const
{
	static const FVector3f DefaultLinearClamp = { TNumericLimits<float>::Max(), TNumericLimits<float>::Max(), TNumericLimits<float>::Max() };
	PropertyHelper.SetPropertyEnum(this, &VelocityScaleSpace);
	PropertyHelper.SetProperty(this, &LinearVelocityScale);
	PropertyHelper.SetProperty(TEXT("MaxLinearVelocity"), bEnableLinearVelocityClamping ? MaxLinearVelocity : DefaultLinearClamp);
	PropertyHelper.SetProperty(TEXT("MaxLinearAcceleration"), bEnableLinearAccelerationClamping ? MaxLinearAcceleration : DefaultLinearClamp);
	PropertyHelper.SetProperty(this, &AngularVelocityScale);
	PropertyHelper.SetProperty(TEXT("MaxAngularVelocity"), bEnableAngularVelocityClamping ? MaxAngularVelocity : TNumericLimits<float>::Max());
	PropertyHelper.SetProperty(TEXT("MaxAngularAcceleration"), bEnableAngularAccelerationClamping ? MaxAngularAcceleration : TNumericLimits<float>::Max());
	PropertyHelper.SetProperty(this, &MaxVelocityScale);
	PropertyHelper.SetProperty(this, &FictitiousAngularScale);
}
