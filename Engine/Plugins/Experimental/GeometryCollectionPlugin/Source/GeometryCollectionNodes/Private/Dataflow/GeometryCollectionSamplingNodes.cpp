// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionSamplingNodes.h"

#include "UDynamicMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GeometryCollectionSamplingNodes)

namespace UE::Dataflow
{

	void GeometryCollectionSamplingNodes()
	{
		static const FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FUniformPointSamplingDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FNonUniformPointSamplingDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FVertexWeightedPointSamplingDataflowNode);

		// GeometryCollection|Sampling
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("GeometryCollection|Sampling", FLinearColor(.1f, 1.f, 0.6f), CDefaultNodeBodyTintColor);
	}
}

void FUniformPointSamplingDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&SamplePoints) || 
		Out->IsA(&SampleTriangleIDs) ||
		Out->IsA(&NumSamplePoints))
	{
		if (TObjectPtr<UDynamicMesh> InTargetMesh = GetValue(Context, &TargetMesh))
		{
			const UE::Geometry::FDynamicMesh3& InDynTargetMesh = InTargetMesh->GetMeshRef();

			if (InDynTargetMesh.VertexCount() > 0)
			{
				TArray<FTransform> OutSamples;
				TArray<int32> OutTriangleIDs;
				TArray<FVector> OutBarycentricCoords;

				FFractureEngineSampling::ComputeUniformPointSampling(InDynTargetMesh,
					GetValue(Context, &SamplingRadius),
					GetValue(Context, &MaxNumSamples),
					GetValue(Context, &SubSampleDensity),
					GetValue(Context, &RandomSeed),
					OutSamples, 
					OutTriangleIDs, 
					OutBarycentricCoords);

				const int32 NumSamples = OutSamples.Num();

				TArray<FVector> OutPoints;
				OutPoints.AddUninitialized(NumSamples);

				for (int32 Idx = 0; Idx < NumSamples; ++Idx)
				{
					OutPoints[Idx] = OutSamples[Idx].GetTranslation();
				}

				SetValue(Context, OutPoints, &SamplePoints);
				SetValue(Context, OutTriangleIDs, &SampleTriangleIDs);
				SetValue(Context, OutBarycentricCoords, &SampleBarycentricCoords);
				SetValue(Context, OutPoints.Num(), &NumSamplePoints);
			}
		}
	}
}

void FNonUniformPointSamplingDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&SamplePoints) ||
		Out->IsA(&SampleRadii) ||
		Out->IsA(&SampleTriangleIDs) ||
		Out->IsA(&NumSamplePoints))
	{
		if (TObjectPtr<UDynamicMesh> InTargetMesh = GetValue(Context, &TargetMesh))
		{
			const UE::Geometry::FDynamicMesh3& InDynTargetMesh = InTargetMesh->GetMeshRef();

			if (InDynTargetMesh.VertexCount() > 0)
			{
				TArray<FTransform> OutSamples;
				TArray<float> OutSampleRadii;
				TArray<int32> OutTriangleIDs;
				TArray<FVector> OutBarycentricCoords;

				FFractureEngineSampling::ComputeNonUniformPointSampling(InDynTargetMesh,
					GetValue(Context, &SamplingRadius),
					GetValue(Context, &MaxNumSamples),
					GetValue(Context, &SubSampleDensity),
					GetValue(Context, &RandomSeed),
					GetValue(Context, &MaxSamplingRadius),
					SizeDistribution,
					GetValue(Context, &SizeDistributionPower),
					OutSamples,
					OutSampleRadii,
					OutTriangleIDs, 
					OutBarycentricCoords);

				const int32 NumSamples = OutSamples.Num();

				TArray<FVector> OutPoints;
				OutPoints.AddUninitialized(NumSamples);

				for (int32 Idx = 0; Idx < NumSamples; ++Idx)
				{
					OutPoints[Idx] = OutSamples[Idx].GetTranslation();
				}

				SetValue(Context, OutPoints, &SamplePoints);
				SetValue(Context, OutSampleRadii, &SampleRadii);
				SetValue(Context, OutTriangleIDs, &SampleTriangleIDs);
				SetValue(Context, OutBarycentricCoords, &SampleBarycentricCoords);
				SetValue(Context, OutPoints.Num(), &NumSamplePoints);
			}
		}
	}
}

void FVertexWeightedPointSamplingDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&SamplePoints) ||
		Out->IsA(&SampleRadii) ||
		Out->IsA(&SampleTriangleIDs) ||
		Out->IsA(&NumSamplePoints))
	{
		if (TObjectPtr<UDynamicMesh> InTargetMesh = GetValue(Context, &TargetMesh))
		{
			const UE::Geometry::FDynamicMesh3& InDynTargetMesh = InTargetMesh->GetMeshRef();

			if (InDynTargetMesh.VertexCount() > 0)
			{
				if (IsConnected(&VertexWeights))
				{
					TArray<FTransform> OutSamples;
					TArray<float> OutSampleRadii;
					TArray<int32> OutTriangleIDs;
					TArray<FVector> OutBarycentricCoords;

					FFractureEngineSampling::ComputeVertexWeightedPointSampling(InDynTargetMesh,
						GetValue(Context, &VertexWeights),
						GetValue(Context, &SamplingRadius),
						GetValue(Context, &MaxNumSamples),
						GetValue(Context, &SubSampleDensity),
						GetValue(Context, &RandomSeed),
						GetValue(Context, &MaxSamplingRadius),
						SizeDistribution,
						GetValue(Context, &SizeDistributionPower),
						WeightMode,
						bInvertWeights,
						OutSamples,
						OutSampleRadii,
						OutTriangleIDs,
						OutBarycentricCoords);

					const int32 NumSamples = OutSamples.Num();

					TArray<FVector> OutPoints;
					OutPoints.AddUninitialized(NumSamples);

					for (int32 Idx = 0; Idx < NumSamples; ++Idx)
					{
						OutPoints[Idx] = OutSamples[Idx].GetTranslation();
					}

					SetValue(Context, OutPoints, &SamplePoints);
					SetValue(Context, OutSampleRadii, &SampleRadii);
					SetValue(Context, OutTriangleIDs, &SampleTriangleIDs);
					SetValue(Context, OutBarycentricCoords, &SampleBarycentricCoords);
					SetValue(Context, OutPoints.Num(), &NumSamplePoints);
				}
			}
		}
	}
}


