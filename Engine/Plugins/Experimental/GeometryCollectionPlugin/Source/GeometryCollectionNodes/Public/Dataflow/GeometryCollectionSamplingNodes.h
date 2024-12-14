// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowEngine.h"
#include "FractureEngineSampling.h"

#include "GeometryCollectionSamplingNodes.generated.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
namespace Dataflow = UE::Dataflow;
#else
namespace UE_DEPRECATED(5.5, "Use UE::Dataflow instead.") Dataflow {}
#endif

class FGeometryCollection;
class UDynamicMesh;

/**
 *
 * Uniform Sampling on a DynamicMesh
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FUniformPointSamplingDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FUniformPointSamplingDataflowNode, "UniformPointSampling", "PointSampling", "")

public:
	/** Mesh to sample points on */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TObjectPtr<UDynamicMesh> TargetMesh;

	/** Desired "radius" of sample points. Spacing between samples is at least 2x this value. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0.f))
	float SamplingRadius = 10.f;

	/** Maximum number of samples requested. If 0 or default value, mesh will be maximally sampled */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0))
	int32 MaxNumSamples = 0;

	/** Density of subsampling used in Poisson strategy. Larger numbers mean "more accurate" (but slower) results. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0.f))
	float SubSampleDensity = 10.f;

	/** Random Seed used to initialize sampling strategies */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput))
	int32 RandomSeed = 0;

	/** Sampled positions on the mesh */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> SamplePoints;

	/** Sampled triangleID */
	UPROPERTY(meta = (DataflowOutput))
	TArray<int32> SampleTriangleIDs;

	/** Barycentric Coordinates of each Sample Point in it's respective Triangle. */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> SampleBarycentricCoords;

	/** Number of Sampled positions on the mesh */
	UPROPERTY(meta = (DataflowOutput))
	int32 NumSamplePoints = 0;

	FUniformPointSamplingDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TargetMesh);
		RegisterInputConnection(&SamplingRadius);
		RegisterInputConnection(&MaxNumSamples);
		RegisterInputConnection(&SubSampleDensity);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&SamplePoints);
		RegisterOutputConnection(&SampleTriangleIDs);
		RegisterOutputConnection(&SampleBarycentricCoords);
		RegisterOutputConnection(&NumSamplePoints);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 *
 * NonUniform Sampling on a DynamicMesh
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FNonUniformPointSamplingDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FNonUniformPointSamplingDataflowNode, "NonUniformPointSampling", "PointSampling", "")

public:
	/** Mesh to sample points on */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TObjectPtr<UDynamicMesh> TargetMesh;

	/** Desired "radius" of sample points. Spacing between samples is at least 2x this value. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0.f))
	float SamplingRadius = 10.f;

	/** Maximum number of samples requested. If 0 or default value, mesh will be maximally sampled */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0))
	int32 MaxNumSamples = 0;

	/** Density of subsampling used in Poisson strategy. Larger numbers mean "more accurate" (but slower) results. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0.f))
	float SubSampleDensity = 10.f;

	/** Random Seed used to initialize sampling strategies */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput))
	int32 RandomSeed = 0;

	/** If MaxSampleRadius > SampleRadius, then output sample radius will be in range [SampleRadius, MaxSampleRadius] */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0.f))
	float MaxSamplingRadius = 10.f;

	/** SizeDistribution setting controls the distribution of sample radii */
	UPROPERTY(EditAnywhere, Category = Distribution)
	ENonUniformSamplingDistributionMode SizeDistribution = ENonUniformSamplingDistributionMode::ENonUniformSamplingDistributionMode_Uniform;

	/** SizeDistributionPower is used to control how extreme the Size Distribution shift is. Valid range is [1,10] */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0.f))
	float SizeDistributionPower = 2.f;

	/** Sampled positions on the mesh */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> SamplePoints;

	/** Sampled radii */
	UPROPERTY(meta = (DataflowOutput))
	TArray<float> SampleRadii;

	/** Sampled triangleID */
	UPROPERTY(meta = (DataflowOutput))
	TArray<int32> SampleTriangleIDs;

	/** Barycentric Coordinates of each Sample Point in it's respective Triangle. */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> SampleBarycentricCoords;

	/** Number of Sampled positions on the mesh */
	UPROPERTY(meta = (DataflowOutput))
	int32 NumSamplePoints = 0;

	FNonUniformPointSamplingDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TargetMesh);
		RegisterInputConnection(&SamplingRadius);
		RegisterInputConnection(&MaxNumSamples);
		RegisterInputConnection(&SubSampleDensity);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&MaxSamplingRadius);
		RegisterInputConnection(&SizeDistributionPower);
		RegisterOutputConnection(&SamplePoints);
		RegisterOutputConnection(&SampleRadii);
		RegisterOutputConnection(&SampleTriangleIDs);
		RegisterOutputConnection(&SampleBarycentricCoords);
		RegisterOutputConnection(&NumSamplePoints);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 *
 * VertexWeighted Sampling on a DynamicMesh
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FVertexWeightedPointSamplingDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FVertexWeightedPointSamplingDataflowNode, "VertexWeightedPointSampling", "PointSampling", "")

public:
	/** Mesh to sample points on */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TObjectPtr<UDynamicMesh> TargetMesh;

	/** Weight array */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<float> VertexWeights;

	/** Desired "radius" of sample points. Spacing between samples is at least 2x this value. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0.f))
	float SamplingRadius = 10.f;

	/** Maximum number of samples requested. If 0 or default value, mesh will be maximally sampled */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0))
	int32 MaxNumSamples = 0;

	/** Density of subsampling used in Poisson strategy. Larger numbers mean "more accurate" (but slower) results. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0.f))
	float SubSampleDensity = 10.f;

	/** Random Seed used to initialize sampling strategies */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput))
	int32 RandomSeed = 0;

	/** If MaxSampleRadius > SampleRadius, then output sample radius will be in range [SampleRadius, MaxSampleRadius] */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0.f))
	float MaxSamplingRadius = 10.f;

	/** SizeDistribution setting controls the distribution of sample radii */
	UPROPERTY(EditAnywhere, Category = Distribution)
	ENonUniformSamplingDistributionMode SizeDistribution = ENonUniformSamplingDistributionMode::ENonUniformSamplingDistributionMode_Uniform;

	/** SizeDistributionPower is used to control how extreme the Size Distribution shift is. Valid range is [1,10] */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, UIMin = 0.f))
	float SizeDistributionPower = 2.f;

	UPROPERTY(EditAnywhere, Category = Distribution)
	ENonUniformSamplingWeightMode WeightMode = ENonUniformSamplingWeightMode::ENonUniformSamplingWeightMode_WeightedRandom;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bInvertWeights = false;

	/** Sampled positions on the mesh */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> SamplePoints;

	/** Sampled radii */
	UPROPERTY(meta = (DataflowOutput))
	TArray<float> SampleRadii;

	/** Sampled triangleID */
	UPROPERTY(meta = (DataflowOutput))
	TArray<int32> SampleTriangleIDs;

	/** Barycentric Coordinates of each Sample Point in it's respective Triangle. */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> SampleBarycentricCoords;

	/** Number of Sampled positions on the mesh */
	UPROPERTY(meta = (DataflowOutput))
	int32 NumSamplePoints = 0;

	FVertexWeightedPointSamplingDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TargetMesh);
		RegisterInputConnection(&VertexWeights);
		RegisterInputConnection(&SamplingRadius);
		RegisterInputConnection(&MaxNumSamples);
		RegisterInputConnection(&SubSampleDensity);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&MaxSamplingRadius);
		RegisterInputConnection(&SizeDistributionPower);
		RegisterOutputConnection(&SamplePoints);
		RegisterOutputConnection(&SampleRadii);
		RegisterOutputConnection(&SampleTriangleIDs);
		RegisterOutputConnection(&SampleBarycentricCoords);
		RegisterOutputConnection(&NumSamplePoints);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};


namespace UE::Dataflow
{
	void GeometryCollectionSamplingNodes();
}

