// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Dataflow/DataflowSelection.h"
#include "FractureEngineFracturing.h"

#include "GeometryCollectionFracturingNodes.generated.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
namespace Dataflow = UE::Dataflow;
#else
namespace UE_DEPRECATED(5.5, "Use UE::Dataflow instead.") Dataflow {}
#endif

class FGeometryCollection;
class UStaticMesh;

USTRUCT(meta = (DataflowGeometryCollection, Deprecated = "5.5"))
struct FUniformScatterPointsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FUniformScatterPointsDataflowNode, "UniformScatterPoints", "Generators|Point", "")

public:
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 1));
	int32 MinNumberOfPoints = 20;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 1));
	int32 MaxNumberOfPoints = 20;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	float RandomSeed = -1.f;

	UPROPERTY(meta = (DataflowInput))
	FBox BoundingBox = FBox(ForceInit);

	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Points;

	FUniformScatterPointsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&MinNumberOfPoints);
		RegisterInputConnection(&MaxNumberOfPoints);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&Points);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT(meta = (DataflowGeometryCollection))
struct FUniformScatterPointsDataflowNode_v2 : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FUniformScatterPointsDataflowNode_v2, "UniformScatterPoints", "Generators|Point", "")

public:
	/** Minimum for the random range */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 1));
	int32 MinNumberOfPoints = 20;

	/** Maximum for the random range */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 1));
	int32 MaxNumberOfPoints = 20;

	/** Seed for random */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = "0"));
	int32 RandomSeed = 0;

	/** BoundingBox to generate points inside of */
	UPROPERTY(meta = (DataflowInput))
	FBox BoundingBox = FBox(ForceInit);

	/** Generated points */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Points;

	FUniformScatterPointsDataflowNode_v2(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&MinNumberOfPoints);
		RegisterInputConnection(&MaxNumberOfPoints);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&Points);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT(meta = (DataflowGeometryCollection, Deprecated = "5.5"))
struct FRadialScatterPointsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRadialScatterPointsDataflowNode, "RadialScatterPoints", "Generators|Point", "")

public:
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	FVector Center = FVector(0.0);

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	FVector Normal = FVector(0.0, 0.0, 1.0);

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 0.01f));
	float Radius = 50.f;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 1, UIMax = 50));
	int32 AngularSteps = 5;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 1, UIMax = 50));
	int32 RadialSteps = 5;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	float AngleOffset = 0.f;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 0.f));
	float Variability = 0.f;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	float RandomSeed = -1.f;

	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Points;

	FRadialScatterPointsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Center);
		RegisterInputConnection(&Normal);
		RegisterInputConnection(&Radius);
		RegisterInputConnection(&AngularSteps);
		RegisterInputConnection(&RadialSteps);
		RegisterInputConnection(&AngleOffset);
		RegisterInputConnection(&Variability);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&Points);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT(meta = (DataflowGeometryCollection))
struct FRadialScatterPointsDataflowNode_v2 : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRadialScatterPointsDataflowNode_v2, "RadialScatterPoints", "Generators|Point", "")

public:
	/** BoundingBox to generate points inside of */
	UPROPERTY(meta = (DataflowInput))
	FBox BoundingBox = FBox(ForceInit);

	/** Center of generated pattern */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	FVector Center = FVector(0.0);

	/** Normal to plane in which sites are generated */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	FVector Normal = FVector(0.0, 0.0, 1.0);

	/** Seed for random */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = "0"));
	int32 RandomSeed = 0;

	/** Number of angular steps */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 1, UIMax = 50, ClampMin = "1"));
	int32 AngularSteps = 5;

	/** Angle offset at each radial step (in degrees) */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	float AngleOffset = 0.f;

	/** Amount of global variation to apply to each angular step (in degrees) */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	float AngularNoise = 0.f;

	/** Pattern radius (in cm) */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = "0.0", ClampMin = "0.0"));
	float Radius = 50.f;

	/** Number of radial steps */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = "1", UIMax = "50", ClampMin = "1"));
	int32 RadialSteps = 5;

	/** Radial steps will follow a distribution based on this exponent, i.e., Pow(distance from center, RadialStepExponent) */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = ".01", UIMax = "10", ClampMin = ".01", ClampMax = "20"));
	float RadialStepExponent = 1.f;

	/** Minimum radial separation between any two voronoi points (in cm) */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = ".25", ClampMin = ".01"))
	float RadialMinStep = 1.f;

	/** Amount of global variation to apply to each radial step (in cm) */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, ClampMin = "0"))
	float RadialNoise = 0.f;

	/** Amount to randomly displace each Voronoi site radially (in cm) */
	UPROPERTY(EditAnywhere, Category = "Per Point Variability", meta = (DataflowInput, ClampMin = "0.0"))
	float RadialVariability = 0.f;

	/** Amount to randomly displace each Voronoi site in angle (in degrees) */
	UPROPERTY(EditAnywhere, Category = "Per Point Variability", meta = (DataflowInput, ClampMin = "0.0"))
	float AngularVariability = 0.f;

	/** Amount to randomly displace each Voronoi site in the direction of the rotation axis (in cm) */
	UPROPERTY(EditAnywhere, Category = "Per Point Variability", meta = (DataflowInput, ClampMin = "0.0"))
	float AxialVariability = 0.f;

	/** Generated points */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Points;

	FRadialScatterPointsDataflowNode_v2(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&Center);
		RegisterInputConnection(&Normal);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&AngularSteps);
		RegisterInputConnection(&AngleOffset);
		RegisterInputConnection(&AngularNoise);
		RegisterInputConnection(&Radius);
		RegisterInputConnection(&RadialSteps);
		RegisterInputConnection(&RadialStepExponent);
		RegisterInputConnection(&RadialMinStep);
		RegisterInputConnection(&RadialNoise);
		RegisterInputConnection(&RadialVariability);
		RegisterInputConnection(&AngularVariability);
		RegisterInputConnection(&AxialVariability);
		RegisterOutputConnection(&Points);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

USTRUCT(meta = (DataflowGeometryCollection))
struct FGridScatterPointsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGridScatterPointsDataflowNode, "GridScatterPoints", "Generators|Point", "")

public:
	/** Number of points in X direction */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = "0"));
	int32 NumberOfPointsInX = 5;

	/** Number of points in Y direction */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = "0"));
	int32 NumberOfPointsInY = 5;

	/** Number of points in Z direction */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = "0"));
	int32 NumberOfPointsInZ = 5;

	/** Seed for random */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = "0"));
	int32 RandomSeed = 0;

	/** Random displacement in X direction will be in the range (-MaxRandomDisplacementX, MaxRandomDisplacementX) */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = "0.0"));
	float MaxRandomDisplacementX = 0.f;

	/** Random displacement in Y direction will be in the range (-MaxRandomDisplacementY, MaxRandomDisplacementY) */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = "0.0"));
	float MaxRandomDisplacementY = 0.f;

	/** Random displacement in Z direction will be in the range (-MaxRandomDisplacementZ, MaxRandomDisplacementZ) */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = "0.0"));
	float MaxRandomDisplacementZ = 0.f;

	/** BoundingBox to generate points inside of */
	UPROPERTY(meta = (DataflowInput))
	FBox BoundingBox = FBox(ForceInit);

	/** Generated points */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Points;

	FGridScatterPointsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&NumberOfPointsInX);
		RegisterInputConnection(&NumberOfPointsInY);
		RegisterInputConnection(&NumberOfPointsInZ);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&MaxRandomDisplacementX);
		RegisterInputConnection(&MaxRandomDisplacementY);
		RegisterInputConnection(&MaxRandomDisplacementZ);
		RegisterOutputConnection(&Points);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 *
 * Editor Fracture Mode / Fracture / Uniform tool
 * Fracture using a Voronoi diagram with a uniform random pattern, creating fracture pieces of similar volume across the shape.
 *
 */
USTRUCT(meta = (DataflowGeometryCollection, Deprecated = "5.5"))
struct FVoronoiFractureDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FVoronoiFractureDataflowNode, "VoronoiFracture", "GeometryCollection|Fracture", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector> Points;

	/**   */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput));
	float RandomSeed = -1.f;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 0.f, UIMax = 1.f));
	float ChanceToFracture = 1.f;

	UPROPERTY(EditAnywhere, Category = "Fracture");
	bool GroupFracture = true;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 0.f));
	float Grout = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Amplitude = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.00001f));
	float Frequency = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Persistence = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Lacunarity = 2.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	int32 OctaveNumber = 4;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float PointSpacing = 10.f;

	UPROPERTY(EditAnywhere, Category = "Collision");
	bool AddSamplesForCollision = false;

	UPROPERTY(EditAnywhere, Category = "Collision", meta = (DataflowInput, UIMin = 0.f));
	float CollisionSampleSpacing = 50.f;

	FVoronoiFractureDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&Points);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&ChanceToFracture);
		RegisterInputConnection(&Grout);
		RegisterInputConnection(&Amplitude);
		RegisterInputConnection(&Frequency);
		RegisterInputConnection(&Persistence);
		RegisterInputConnection(&Lacunarity);
		RegisterInputConnection(&OctaveNumber);
		RegisterInputConnection(&PointSpacing);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 * Editor Fracture Mode / Fracture / Uniform tool
 * Fracture using a Voronoi diagram with a uniform random pattern, creating fracture pieces of similar volume across the shape.
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FVoronoiFractureDataflowNode_v2 : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FVoronoiFractureDataflowNode_v2, "VoronoiFracture", "GeometryCollection|Fracture", "")

public:
	/** Collection to fracture */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Voronoi source points */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector> Points;

	/** Pieces to fracture */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "TransformSelection", DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Transform to apply to cut planes */
	UPROPERTY(meta = (DataflowInput, DisplayName = "Transform"))
	FTransform Transform = FTransform::Identity;

	// Made it hidden to hide this parameter, the random seed for Voronoi fracturing is on the point generation nodes
	/** Seed for random */
	UPROPERTY();
	int32 RandomSeed = 0;

	/** Chance to fracture each selected bone. If 0, no bones will fracture; if 1, all bones will fracture. */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"));
	float ChanceToFracture = 1.f;

	/** Whether to split the fractured mesh pieces based on geometric connectivity after fracturing */
	UPROPERTY(EditAnywhere, Category = "Common Fracture");
	bool SplitIslands = true;

	/** Amount of space to leave between cut pieces */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = 0.f));
	float Grout = 0.f;

	/** Size of the Perlin noise displacement (in cm). If 0, no noise will be applied */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Amplitude = 0.f;

	/** Period of the Perlin noise.  Smaller values will create a smoother noise pattern */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.00001f));
	float Frequency = 0.1f;

	/** Persistence of the layers of Perlin noise. At each layer (octave) after the first, the amplitude of the Perlin noise is scaled by this factor */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Persistence = 0.5f;

	/** Lacunarity of the layers of Perlin noise. At each layer (octave) after the first, the frequency of the Perlin noise is scaled by this factor */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Lacunarity = 2.f;

	/**
	 * Number of fractal layers of Perlin noise to apply. Each layer is additive, with Amplitude and Frequency parameters scaled by Persistence and Lacunarity
	 * Smaller values (1 or 2) will create noise that looks like gentle rolling hills, while larger values (> 4) will tend to look more like craggy mountains
	 */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	int32 OctaveNumber = 4;

	/** Distance (in cm) between vertices on cut surfaces where noise is added.  Larger spacing between vertices will create more efficient meshes with fewer triangles, but less resolution to see the shape of the added noise  */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float PointSpacing = 10.f;

	/**
	 * If enabled, add extra vertices (without triangles) to the geometry in regions where vertices are spaced too far apart (e.g. across large triangles)
	 * These extra vertices will be used as collision samples in particle-implicit collisions, and can help the physics system detect collisions more accurately
	 *
	 * Note this is *only* useful for simulations that use particle-implicit collisions
	 */
	UPROPERTY(EditAnywhere, Category = "Collision");
	bool AddSamplesForCollision = false;

	/**
	 * The number of centimeters to allow between vertices on the mesh surface: If there are gaps larger than this, add additional vertices (without triangles) to help support particle-implicit collisions
	 * Only used if Add Samples For Collision is enabled
	 */
	UPROPERTY(EditAnywhere, Category = "Collision", meta = (DataflowInput, UIMin = 0.f, EditCondition = "AddSamplesForCollision"));
	float CollisionSampleSpacing = 50.f;

	/** Fractured Pieces */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "FracturedTransformSelection "))
	FDataflowTransformSelection NewGeometryTransformSelection;

	FVoronoiFractureDataflowNode_v2(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&Points);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Transform);
		RegisterInputConnection(&ChanceToFracture);
		RegisterInputConnection(&Grout);
		RegisterInputConnection(&Amplitude);
		RegisterInputConnection(&Frequency);
		RegisterInputConnection(&Persistence);
		RegisterInputConnection(&Lacunarity);
		RegisterInputConnection(&OctaveNumber);
		RegisterInputConnection(&PointSpacing);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&NewGeometryTransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 *
 * Editor Fracture Mode / Fracture / Planar tool
 * Fracture using a set of noised up planes.
 *
 */
USTRUCT(meta = (DataflowGeometryCollection, Deprecated = "5.5"))
struct FPlaneCutterDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FPlaneCutterDataflowNode, "PlaneCutter", "GeometryCollection|Fracture", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FBox BoundingBox = FBox(ForceInit);

	/**   */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 1))
	int32 NumPlanes = 1;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput))
	float RandomSeed = -1.f;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 0.f))
	float Grout = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Amplitude = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.00001f));
	float Frequency = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Persistence = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Lacunarity = 2.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	int32 OctaveNumber = 4;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float PointSpacing = 10.f;

	UPROPERTY(EditAnywhere, Category = "Collision");
	bool AddSamplesForCollision = false;

	UPROPERTY(EditAnywhere, Category = "Collision", meta = (DataflowInput, UIMin = 0.f));
	float CollisionSampleSpacing = 50.f;

	FPlaneCutterDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&NumPlanes);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&Grout);
		RegisterInputConnection(&Amplitude);
		RegisterInputConnection(&Frequency);
		RegisterInputConnection(&Persistence);
		RegisterInputConnection(&Lacunarity);
		RegisterInputConnection(&OctaveNumber);
		RegisterInputConnection(&PointSpacing);
		RegisterInputConnection(&AddSamplesForCollision);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 *
 * Editor Fracture Mode / Fracture / Planar tool
 * Fracture using a set of noised up planes.
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FPlaneCutterDataflowNode_v2 : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FPlaneCutterDataflowNode_v2, "PlaneCutter", "GeometryCollection|Fracture", "")

public:
	/** Collection to fracture */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Bound for plane centers */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FBox BoundingBox = FBox(ForceInit);

	/** Pieces to cut */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "TransformSelection", DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Transform to apply to cut planes */
	UPROPERTY(meta = (DataflowInput, DisplayName = "Transform"))
	FTransform Transform = FTransform::Identity;

	/** Number of cutting planes */
	UPROPERTY(EditAnywhere, Category = "Plane Cut", meta = (DataflowInput, DisplayName = "Number of Cuts", UIMin = 1))
	int32 NumPlanes = 1;

	/** Seed for random */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = "0"))
	int32 RandomSeed = 0;

	/** Chance to fracture each selected bone. If 0, no bones will fracture; if 1, all bones will fracture. */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = 0.f, DisplayName = "Chance To Fracture Per Bone", UIMax = 1.f));
	float ChanceToFracture = 1.f;

	/** Whether to split the fractured mesh pieces based on geometric connectivity after fracturing */
	UPROPERTY(EditAnywhere, Category = "Common Fracture");
	bool SplitIslands = true;

	/** Amount of space to leave between cut pieces */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = 0.f));
	float Grout = 0.f;

	/** Size of the Perlin noise displacement (in cm). If 0, no noise will be applied */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Amplitude = 0.f;

	/** Period of the Perlin noise.  Smaller values will create a smoother noise pattern */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.00001f));
	float Frequency = 0.1f;

	/** Persistence of the layers of Perlin noise. At each layer (octave) after the first, the amplitude of the Perlin noise is scaled by this factor */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Persistence = 0.5f;

	/** Lacunarity of the layers of Perlin noise. At each layer (octave) after the first, the frequency of the Perlin noise is scaled by this factor */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Lacunarity = 2.f;

	/**
	 * Number of fractal layers of Perlin noise to apply. Each layer is additive, with Amplitude and Frequency parameters scaled by Persistence and Lacunarity
	 * Smaller values (1 or 2) will create noise that looks like gentle rolling hills, while larger values (> 4) will tend to look more like craggy mountains
	 */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	int32 OctaveNumber = 4;

	/** Distance (in cm) between vertices on cut surfaces where noise is added.  Larger spacing between vertices will create more efficient meshes with fewer triangles, but less resolution to see the shape of the added noise  */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float PointSpacing = 10.f;

	/**
	 * If enabled, add extra vertices (without triangles) to the geometry in regions where vertices are spaced too far apart (e.g. across large triangles)
	 * These extra vertices will be used as collision samples in particle-implicit collisions, and can help the physics system detect collisions more accurately
	 *
	 * Note this is *only* useful for simulations that use particle-implicit collisions
	 */
	UPROPERTY(EditAnywhere, Category = "Collision");
	bool AddSamplesForCollision = false;

	/**
	 * The number of centimeters to allow between vertices on the mesh surface: If there are gaps larger than this, add additional vertices (without triangles) to help support particle-implicit collisions
	 * Only used if Add Samples For Collision is enabled
	 */
	UPROPERTY(EditAnywhere, Category = "Collision", meta = (DataflowInput, UIMin = 0.f, EditCondition = "AddSamplesForCollision"));
	float CollisionSampleSpacing = 50.f;

	/** Fractured Pieces */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "FracturedTransformSelection "))
	FDataflowTransformSelection NewGeometryTransformSelection;

	FPlaneCutterDataflowNode_v2(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Transform);
		RegisterInputConnection(&NumPlanes);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&ChanceToFracture);
		RegisterInputConnection(&Grout);
		RegisterInputConnection(&Amplitude);
		RegisterInputConnection(&Frequency);
		RegisterInputConnection(&Persistence);
		RegisterInputConnection(&Lacunarity);
		RegisterInputConnection(&OctaveNumber);
		RegisterInputConnection(&PointSpacing);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&NewGeometryTransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 *
 * "Explodes" the pieces from the Collection for better visualization
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FExplodedViewDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FExplodedViewDataflowNode, "ExplodedView", "GeometryCollection|Fracture|Utilities", "")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender",FGeometryCollection::StaticType(),  "Collection")

public:
	/** Collection to explode */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Scale amount to expand the pieces uniformly in all directions */
	UPROPERTY(EditAnywhere, Category = "Scale", meta = (DataflowInput))
	float UniformScale = 1.f;

	/** Scale amounts to expand the pieces in all 3 directions */
	UPROPERTY(EditAnywhere, Category = "Scale", meta = (DataflowInput))
	FVector Scale = FVector(1.0);

	FExplodedViewDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&UniformScale);
		RegisterInputConnection(&Scale);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

private:
	// todo(chaos) this is a copy of a function in FractureEditorModeToolkit, we should move this to a common place  
	static bool GetValidGeoCenter(FGeometryCollection* Collection, const TManagedArray<int32>& TransformToGeometryIndex, const TArray<FTransform>& Transforms, const TManagedArray<TSet<int32>>& Children, const TManagedArray<FBox>& BoundingBox, int32 TransformIndex, FVector& OutGeoCenter);
};

/**
 *
 * Editor Fracture Mode / Fracture / Slice tool
 * Fracture with a grid of X, Y, and Z slices, with optional random variation in angle and offset.
 * 
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FSliceCutterDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FSliceCutterDataflowNode, "SliceCutter", "GeometryCollection|Fracture", "")

public:
	/** Collection to fracture */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FBox BoundingBox = FBox(ForceInit);

	/** The selected pieces to cut */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "TransformSelection", DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Number of slices along the X axis */
	UPROPERTY(EditAnywhere, Category = Slicing, meta = (DataflowInput, UIMin = "0"))
	int32 SlicesX = 3;

	/** Number of slices along the Y axis */
	UPROPERTY(EditAnywhere, Category = Slicing, meta = (DataflowInput, UIMin = "0"))
	int32 SlicesY = 3;

	/** Number of slices along the Z axis */
	UPROPERTY(EditAnywhere, Category = Slicing, meta = (DataflowInput, UIMin = "0"))
	int32 SlicesZ = 1;

	/** Maximum angle (in degrees) to randomly rotate each slicing plane */
	UPROPERTY(EditAnywhere, Category = Slicing, meta = (DataflowInput, DisplayName = "Random Angle Variation", UIMin = "0.0", UIMax = "90.0"))
	float SliceAngleVariation = 0.f;

	/** Maximum distance (in cm) to randomly shift each slicing plane */
	UPROPERTY(EditAnywhere, Category = Slicing, meta = (DataflowInput, DisplayName = "Random Offset Variation", UIMin = "0.0"))
	float SliceOffsetVariation = 0.f;

	/** Seed for random */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = "0"))
	int32 RandomSeed = 0;

	/** Chance to fracture each selected bone. If 0, no bones will fracture; if 1, all bones will fracture. */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = 0.f, UIMax = 1.f));
	float ChanceToFracture = 1.f;

	/** Whether to split the fractured mesh pieces based on geometric connectivity after fracturing */
	UPROPERTY(EditAnywhere, Category = "Common Fracture");
	bool SplitIslands = true;

	/** Amount of space to leave between cut pieces */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = 0.f));
	float Grout = 0.f;

	/** Size of the Perlin noise displacement (in cm). If 0, no noise will be applied */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Amplitude = 0.f;

	/** Period of the Perlin noise.  Smaller values will create a smoother noise pattern */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.00001f));
	float Frequency = 0.1f;

	/** Persistence of the layers of Perlin noise. At each layer (octave) after the first, the amplitude of the Perlin noise is scaled by this factor */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Persistence = 0.5f;

	/** Lacunarity of the layers of Perlin noise. At each layer (octave) after the first, the frequency of the Perlin noise is scaled by this factor */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Lacunarity = 2.f;

	/**
	 * Number of fractal layers of Perlin noise to apply. Each layer is additive, with Amplitude and Frequency parameters scaled by Persistence and Lacunarity
	 * Smaller values (1 or 2) will create noise that looks like gentle rolling hills, while larger values (> 4) will tend to look more like craggy mountains
	 */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	int32 OctaveNumber = 4;

	/** Distance (in cm) between vertices on cut surfaces where noise is added.  Larger spacing between vertices will create more efficient meshes with fewer triangles, but less resolution to see the shape of the added noise  */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float PointSpacing = 10.f;

	/**
	 * If enabled, add extra vertices (without triangles) to the geometry in regions where vertices are spaced too far apart (e.g. across large triangles)
	 * These extra vertices will be used as collision samples in particle-implicit collisions, and can help the physics system detect collisions more accurately
	 *
	 * Note this is *only* useful for simulations that use particle-implicit collisions
	 */
	UPROPERTY(EditAnywhere, Category = "Collision");
	bool AddSamplesForCollision = false;

	/**
	 * The number of centimeters to allow between vertices on the mesh surface: If there are gaps larger than this, add additional vertices (without triangles) to help support particle-implicit collisions
	 * Only used if Add Samples For Collision is enabled
	 */
	UPROPERTY(EditAnywhere, Category = "Collision", meta = (DataflowInput, UIMin = 0.f, EditCondition = "AddSamplesForCollision"));
	float CollisionSampleSpacing = 50.f;

	/** Fractured Pieces */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "FracturedTransformSelection "))
	FDataflowTransformSelection NewGeometryTransformSelection;

	FSliceCutterDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&SlicesX);
		RegisterInputConnection(&SlicesY);
		RegisterInputConnection(&SlicesZ);
		RegisterInputConnection(&SliceAngleVariation);
		RegisterInputConnection(&SliceOffsetVariation);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&ChanceToFracture);
		RegisterInputConnection(&Grout);
		RegisterInputConnection(&Amplitude);
		RegisterInputConnection(&Frequency);
		RegisterInputConnection(&Persistence);
		RegisterInputConnection(&Lacunarity);
		RegisterInputConnection(&OctaveNumber);
		RegisterInputConnection(&PointSpacing);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&NewGeometryTransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 * 
 * Editor Fracture Mode / Fracture / Brick tool
 * Fracture with a customizable brick pattern. 
 * Note: Currently only supports fracturing with at least some (non-zero) Grout.
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FBrickCutterDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FBrickCutterDataflowNode, "BrickCutter", "GeometryCollection|Fracture", "")

public:
	/** Collection to cut */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Boundingbox to create the cutting planes in */
	UPROPERTY(meta = (DataflowInput))
	FBox BoundingBox = FBox(ForceInit);

	/** The selected pieces to cut */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "TransformSelection", DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Transform to apply to cut planes */
	UPROPERTY(meta = (DataflowInput, DisplayName = "Transform"))
	FTransform Transform = FTransform::Identity;

	/** The brick bond pattern defines how the bricks are arranged */
	UPROPERTY(EditAnywhere, Category = Brick)
	EFractureBrickBondEnum Bond = EFractureBrickBondEnum::Dataflow_FractureBrickBond_Stretcher;

	/** Brick length (in cm) */
	UPROPERTY(EditAnywhere, Category = Brick, meta = (DataflowInput, UIMin = "0.1", UIMax = "500.0", ClampMin = "0.001"))
	float BrickLength = 194.f;

	/** Brick height (in cm) */
	UPROPERTY(EditAnywhere, Category = Brick, meta = (DataflowInput, UIMin = "0.1", UIMax = "500.0", ClampMin = "0.001"))
	float BrickHeight = 57.f;

	/** Brick depth (in cm) */
	UPROPERTY(EditAnywhere, Category = Brick, meta = (DataflowInput, UIMin = "0.1", UIMax = "500.0", ClampMin = "0.001"))
	float BrickDepth = 92.f;

	/** Seed for random */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = "0"))
	int32 RandomSeed = 0;

	/** Chance to fracture each selected bone. If 0, no bones will fracture; if 1, all bones will fracture. */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = 0.f, UIMax = 1.f));
	float ChanceToFracture = 1.f;

	/** Whether to split the fractured mesh pieces based on geometric connectivity after fracturing */
	UPROPERTY(EditAnywhere, Category = "Common Fracture");
	bool SplitIslands = true;

	/** Amount of space to leave between cut pieces */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = 0.f));
	float Grout = 0.f;

	/** Size of the Perlin noise displacement (in cm). If 0, no noise will be applied */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Amplitude = 0.f;

	/** Period of the Perlin noise.  Smaller values will create a smoother noise pattern */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.00001f));
	float Frequency = 0.1f;

	/** Persistence of the layers of Perlin noise. At each layer (octave) after the first, the amplitude of the Perlin noise is scaled by this factor */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Persistence = 0.5f;

	/** Lacunarity of the layers of Perlin noise. At each layer (octave) after the first, the frequency of the Perlin noise is scaled by this factor */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Lacunarity = 2.f;

	/**
	 * Number of fractal layers of Perlin noise to apply. Each layer is additive, with Amplitude and Frequency parameters scaled by Persistence and Lacunarity
	 * Smaller values (1 or 2) will create noise that looks like gentle rolling hills, while larger values (> 4) will tend to look more like craggy mountains
	 */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	int32 OctaveNumber = 4;

	/** Distance (in cm) between vertices on cut surfaces where noise is added.  Larger spacing between vertices will create more efficient meshes with fewer triangles, but less resolution to see the shape of the added noise  */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float PointSpacing = 10.f;

	/**
	 * If enabled, add extra vertices (without triangles) to the geometry in regions where vertices are spaced too far apart (e.g. across large triangles)
	 * These extra vertices will be used as collision samples in particle-implicit collisions, and can help the physics system detect collisions more accurately
	 *
	 * Note this is *only* useful for simulations that use particle-implicit collisions
	 */
	UPROPERTY(EditAnywhere, Category = "Collision");
	bool AddSamplesForCollision = false;

	/**
	 * The number of centimeters to allow between vertices on the mesh surface: If there are gaps larger than this, add additional vertices (without triangles) to help support particle-implicit collisions
	 * Only used if Add Samples For Collision is enabled
	 */
	UPROPERTY(EditAnywhere, Category = "Collision", meta = (DataflowInput, UIMin = 0.f, EditCondition = "AddSamplesForCollision"));
	float CollisionSampleSpacing = 50.f;

	/** Fractured Pieces */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "FracturedTransformSelection "))
	FDataflowTransformSelection NewGeometryTransformSelection;

	FBrickCutterDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Transform);
		RegisterInputConnection(&BrickLength);
		RegisterInputConnection(&BrickHeight);
		RegisterInputConnection(&BrickDepth);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&ChanceToFracture);
		RegisterInputConnection(&Grout);
		RegisterInputConnection(&Amplitude);
		RegisterInputConnection(&Frequency);
		RegisterInputConnection(&Persistence);
		RegisterInputConnection(&Lacunarity);
		RegisterInputConnection(&OctaveNumber);
		RegisterInputConnection(&PointSpacing);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&NewGeometryTransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 * 
 * Editor Fracture Mode / Fracture / Mesh tool
 * Fracture using the shape of a chosen static mesh
 * 
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FMeshCutterDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMeshCutterDataflowNode, "MeshCutter", "GeometryCollection|Fracture", "")

public:
	/** Collection to cut */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Boundingbox to create the cutting planes in */
	UPROPERTY(meta = (DataflowInput))
	FBox BoundingBox = FBox(ForceInit);

	/** The selected pieces to cut */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "TransformSelection", DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Transform to apply to cut planes */
	UPROPERTY(meta = (DataflowInput, DisplayName = "Transform"))
	FTransform Transform = FTransform::Identity;

	/** Mesh to cut with */
	UPROPERTY(EditAnywhere, Category = "StaticMesh", meta = (DataflowInput, DataflowIntrinsic))
	TObjectPtr<UStaticMesh> CuttingStaticMesh;

	/** Output the HiRes representation, if set to true and HiRes doesn't exist it will output empty mesh */
	UPROPERTY(EditAnywhere, Category = "StaticMesh", meta = (DisplayName = "Use HiRes"));
	bool bUseHiRes = false;

	/** Specifies the LOD level to use */
	UPROPERTY(EditAnywhere, Category = "StaticMesh", meta = (DisplayName = "LOD Level"));
	int32 LODLevel = 0;

	/** How to arrange the mesh cuts in space */
	UPROPERTY(EditAnywhere, Category = Distribution)
	EMeshCutterCutDistribution CutDistribution = EMeshCutterCutDistribution::SingleCut;

	/** Number of meshes to random scatter */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DataflowInput, ClampMin = "1", UIMax = "5000", EditCondition = "CutDistribution == EMeshCutterCutDistribution::UniformRandom", EditConditionHides))
	int32 NumberToScatter = 10;

	/** Number of meshes to add to grid in X */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DataflowInput, DisplayName = "Grid Width", ClampMin = "1", UIMax = "100", ClampMax = "5000", EditCondition = "CutDistribution == EMeshCutterCutDistribution::Grid", EditConditionHides))
	int32 GridX = 2;

	/** Number of meshes to add to grid in Y */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DataflowInput, DisplayName = "Grid Depth", ClampMin = "1", UIMax = "100", ClampMax = "5000", EditCondition = "CutDistribution == EMeshCutterCutDistribution::Grid", EditConditionHides))
	int32 GridY = 2;

	/** Number of meshes to add to grid in Z */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DataflowInput, DisplayName = "Grid Height", ClampMin = "1", UIMax = "100", ClampMax = "5000", EditCondition = "CutDistribution == EMeshCutterCutDistribution::Grid", EditConditionHides))
	int32 GridZ = 2;

	/** Magnitude of random displacement to cutting meshes */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DataflowInput, DisplayName = "Variability", EditCondition = "CutDistribution == EMeshCutterCutDistribution::Grid", EditConditionHides, UIMin = "0.0", ClampMin = "0.0"))
	float Variability = 0.f;

	/** Minimum scale factor to apply to cutting meshes. A random scale will be chosen between Min and Max */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DataflowInput, ClampMin = "0.001", EditCondition = "CutDistribution != EMeshCutterCutDistribution::SingleCut", EditConditionHides))
	float MinScaleFactor = .5;

	/** Maximum scale factor to apply to cutting meshes. A random scale will be chosen between Min and Max */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DataflowInput, ClampMin = "0.001", EditCondition = "CutDistribution != EMeshCutterCutDistribution::SingleCut", EditConditionHides))
	float MaxScaleFactor = 1.5;

	/** Whether to randomly vary the orientation of the cutting meshes */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (EditCondition = "CutDistribution != EMeshCutterCutDistribution::SingleCut", EditConditionHides))
	bool bRandomOrientation = true;

	/** Roll will be chosen between -Range and +Range */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DataflowInput, DisplayName = "+/- Roll Range", EditCondition = "CutDistribution != EMeshCutterCutDistribution::SingleCut && bRandomOrientation", EditConditionHides, ClampMin = "0", ClampMax = "180"))
	float RollRange = 180;

	/** Pitch will be chosen between -Range and +Range */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DataflowInput, DisplayName = "+/- Pitch Range", EditCondition = "CutDistribution != EMeshCutterCutDistribution::SingleCut && bRandomOrientation", EditConditionHides, ClampMin = "0", ClampMax = "180"))
	float PitchRange = 180;

	/** Yaw will be chosen between -Range and +Range */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DataflowInput, DisplayName = "+/- Yaw Range", EditCondition = "CutDistribution != EMeshCutterCutDistribution::SingleCut && bRandomOrientation", EditConditionHides, ClampMin = "0", ClampMax = "180"))
	float YawRange = 180;

	/** Seed for random */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = "0"))
	int32 RandomSeed = 0;

	/** Chance to fracture each selected bone. If 0, no bones will fracture; if 1, all bones will fracture. */
	UPROPERTY(EditAnywhere, Category = "Common Fracture", meta = (DataflowInput, UIMin = 0.f, UIMax = 1.f));
	float ChanceToFracture = 1.f;

	/** Whether to split the fractured mesh pieces based on geometric connectivity after fracturing */
	UPROPERTY(EditAnywhere, Category = "Common Fracture");
	bool SplitIslands = true;
	
	/**
	 * If enabled, add extra vertices (without triangles) to the geometry in regions where vertices are spaced too far apart (e.g. across large triangles)
	 * These extra vertices will be used as collision samples in particle-implicit collisions, and can help the physics system detect collisions more accurately
	 *
	 * Note this is *only* useful for simulations that use particle-implicit collisions
	 */
	UPROPERTY(EditAnywhere, Category = "Collision");
	bool AddSamplesForCollision = false;

	/**
	 * The number of centimeters to allow between vertices on the mesh surface: If there are gaps larger than this, add additional vertices (without triangles) to help support particle-implicit collisions
	 * Only used if Add Samples For Collision is enabled
	 */
	UPROPERTY(EditAnywhere, Category = "Collision", meta = (DataflowInput, DisplayName = "Point Spacing", UIMin = 0.f, EditCondition = "AddSamplesForCollision"));
	float CollisionSampleSpacing = 50.f;

	/** Fractured Pieces */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "FracturedTransformSelection "))
	FDataflowTransformSelection NewGeometryTransformSelection;

	FMeshCutterDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Transform);
		RegisterInputConnection(&CuttingStaticMesh);
		RegisterInputConnection(&NumberToScatter);
		RegisterInputConnection(&GridX);
		RegisterInputConnection(&GridY);
		RegisterInputConnection(&GridZ);
		RegisterInputConnection(&Variability);
		RegisterInputConnection(&MinScaleFactor);
		RegisterInputConnection(&MaxScaleFactor);
		RegisterInputConnection(&RollRange);
		RegisterInputConnection(&PitchRange);
		RegisterInputConnection(&YawRange);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&ChanceToFracture);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&NewGeometryTransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

namespace UE::Dataflow
{
	void GeometryCollectionFracturingNodes();
}

