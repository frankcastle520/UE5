// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WaterBodyComponent.h"

struct FBuoyancyWaterSplineData
{
	FBuoyancyWaterSplineData() { }
	FBuoyancyWaterSplineData(
		const Chaos::FRigidTransform3& InTransform,
		const FInterpCurveVector& InPosition,
		const EWaterBodyType InBodyType,
		const TOptional<FInterpCurveFloat>& InWidth,
		const TOptional<FInterpCurveFloat>& InVelocity,
		const TOptional<FShallowWaterSimulationGrid>& InShallowWaterSimData
	)
		: Transform(InTransform)
		, Position(InPosition)
		, BodyType(InBodyType)
		, Width(InWidth)
		, Velocity(InVelocity)
		, ShallowWaterSimData(InShallowWaterSimData)
	{ }


	bool ShouldSampleFromShallowWaterSimulation() const
	{
		return ShallowWaterSimData.IsSet() && ShallowWaterSimData->IsValid();
	}

	// Parameters that all water bodies have
	Chaos::FRigidTransform3 Transform;
	FInterpCurveVector Position;
	EWaterBodyType BodyType;

	// Parameters that only _some_ water bodies have
	TOptional<FInterpCurveFloat> Width;
	TOptional<FInterpCurveFloat> Velocity;

	// Only some water bodies can have a baked shallow water sim representation, which
	// overrides the splines for water height/depth/velocity/normal
	TOptional<FShallowWaterSimulationGrid> ShallowWaterSimData;
};