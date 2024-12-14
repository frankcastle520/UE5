// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "Dataflow/DataflowSelection.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "CompGeom/ConvexDecomposition3.h"

#include "FractureEngineConvex.h"
#include "FractureEngineUtility.h"

#include "GeometryCollectionUtilityNodes.generated.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
namespace Dataflow = UE::Dataflow;
#else
namespace UE_DEPRECATED(5.5, "Use UE::Dataflow instead.") Dataflow {}
#endif

UENUM(BlueprintType)
enum class EConvexOverlapRemovalMethodEnum : uint8
{
	Dataflow_EConvexOverlapRemovalMethod_None UMETA(DisplayName = "None"),
	Dataflow_EConvexOverlapRemovalMethod_All UMETA(DisplayName = "All"),
	Dataflow_EConvexOverlapRemovalMethod_OnlyClusters UMETA(DisplayName = "Only Clusters"),
	Dataflow_EConvexOverlapRemovalMethod_OnlyClustersVsClusters UMETA(DisplayName = "Only Clusters vs Clusters"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

USTRUCT()
struct FDataflowConvexDecompositionSettings
{
	GENERATED_USTRUCT_BODY()

public:
	// If greater than zero, the minimum geometry size (cube root of volume) to consider for convex decomposition
	UPROPERTY(EditAnywhere, Category = Filter, meta = (ClampMin = 0.0))
	float MinSizeToDecompose = 0.f;

	// If the geo volume / hull volume ratio is greater than this, do not consider convex decomposition
	UPROPERTY(EditAnywhere, Category = Filter, meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MaxGeoToHullVolumeRatioToDecompose = 1.f;

	// Stop splitting when hulls have error less than this (expressed in cm; will be cubed for volumetric error).
	// Note: ErrorTolerance must be > 0 or MaxHullsPerGeometry > 1, or decomposition will not be performed.
	UPROPERTY(EditAnywhere, Category = Decomposition, meta = (ClampMin = 0.0))
	float ErrorTolerance = 0.f;

	// If greater than zero, maximum number of convex hulls to use in each convex decomposition.
	//Note: ErrorTolerance must be > 0 or MaxHullsPerGeometry > 1, or decomposition will not be performed.
	UPROPERTY(EditAnywhere, Category = Decomposition, meta = (ClampMin = -1))
	int32 MaxHullsPerGeometry = -1;

	// Optionally specify a minimum thickness (in cm) for convex parts; parts below this thickness will always be merged away. Overrides NumOutputHulls and ErrorTolerance when needed.
	UPROPERTY(EditAnywhere, Category = Decomposition, meta = (ClampMin = 0.0))
	float MinThicknessTolerance = 0.f;

	// Control the search effort spent per convex decomposition: larger values will require more computation but may find better convex decompositions
	UPROPERTY(EditAnywhere, Category = Decomposition, meta = (ClampMin = 0))
	int32 NumAdditionalSplits = 4;
};


//~ TODO: Ideally this would be generated from the above FDataflowConvexDecompositionSettings struct
// Provide settings for running convex decomposition of geometry
USTRUCT(meta = (DataflowGeometryCollection))
struct FMakeDataflowConvexDecompositionSettingsNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeDataflowConvexDecompositionSettingsNode, "MakeConvexDecompositionSettings", "GeometryCollection|Utilities", "")

public:
	// If greater than zero, the minimum geometry size (cube root of volume) to consider for convex decomposition
	UPROPERTY(EditAnywhere, Category = Filter, meta = (ClampMin = 0.0, DataflowInput))
	float MinSizeToDecompose = 0.f;
	
	// If the geo volume / hull volume ratio is greater than this, do not consider convex decomposition
	UPROPERTY(EditAnywhere, Category = Filter, meta = (ClampMin = 0.0, ClampMax = 1.0, DataflowInput))
	float MaxGeoToHullVolumeRatioToDecompose = 1.f;

	// Stop splitting when hulls have error less than this (expressed in cm; will be cubed for volumetric error).
	// Note: ErrorTolerance must be > 0 or MaxHullsPerGeometry > 1, or decomposition will not be performed.
	UPROPERTY(EditAnywhere, Category = Decomposition, meta = (ClampMin = 0.0, DataflowInput))
	float ErrorTolerance = 0.f;

	// If greater than zero, maximum number of convex hulls to use in each convex decomposition.
	//Note: ErrorTolerance must be > 0 or MaxHullsPerGeometry > 1, or decomposition will not be performed.
	UPROPERTY(EditAnywhere, Category = Decomposition, meta = (ClampMin = -1, DataflowInput))
	int32 MaxHullsPerGeometry = -1;

	// Optionally specify a minimum thickness (in cm) for convex parts; parts below this thickness will always be merged away. Overrides NumOutputHulls and ErrorTolerance when needed.
	UPROPERTY(EditAnywhere, Category = Decomposition, meta = (ClampMin = 0.0, DataflowInput))
	float MinThicknessTolerance = 0.f;

	// Control the search effort spent per convex decomposition: larger values will require more computation but may find better convex decompositions
	UPROPERTY(EditAnywhere, Category = Decomposition, meta = (ClampMin = 0, DataflowInput))
	int32 NumAdditionalSplits = 4;

	UPROPERTY(meta = (DataflowOutput))
	FDataflowConvexDecompositionSettings DecompositionSettings;

	FMakeDataflowConvexDecompositionSettingsNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

USTRUCT(meta = (DataflowGeometryCollection))
struct FCreateLeafConvexHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCreateLeafConvexHullsDataflowNode, "CreateLeafConvexHulls", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	/** Optional transform selection to compute leaf hulls on -- if not provided, all leaf hulls will be computed. */
	UPROPERTY(meta = (DataflowInput))
	FDataflowTransformSelection OptionalSelectionFilter;

	/** How convex hulls are generated -- computed from geometry, imported from external collision shapes, or an intersection of both options. */
	UPROPERTY(EditAnywhere, Category = Options)
	EGenerateConvexMethod GenerateMethod = EGenerateConvexMethod::ExternalCollision;

	/** If GenerateMethod is Intersect, only actually intersect when the volume of the Computed Hull is less than this fraction of the volume of the External Hull(s). */
	UPROPERTY(EditAnywhere, Category = IntersectionFilters, meta = (ClampMin = 0.0, ClampMax = 1.0, EditCondition = "GenerateMethod == EGenerateConvexMethod::IntersectExternalWithComputed"))
	float IntersectIfComputedIsSmallerByFactor = 1.0f;

	/** If GenerateMethod is Intersect, only actually intersect if the volume of the External Hull(s) exceed this threshold. */
	UPROPERTY(EditAnywhere, Category = IntersectionFilters, meta = (ClampMin = 0.0, EditCondition = "GenerateMethod == EGenerateConvexMethod::IntersectExternalWithComputed"))
	float MinExternalVolumeToIntersect = 0.0f;

	/** Whether to compute the intersection before computing convex hulls. Typically should be enabled. */
	UPROPERTY(EditAnywhere, Category = Convex, meta = (EditCondition = "GenerateMethod == EGenerateConvexMethod::IntersectExternalWithComputed"))
	bool bComputeIntersectionsBeforeHull = true;

	/** Computed convex hulls are simplified to keep points spaced at least this far apart (except where needed to keep the hull from collapsing to zero volume). */
	UPROPERTY(EditAnywhere, Category = Convex, meta = (DataflowInput, ClampMin = 0.f, EditCondition = "GenerateMethod != EGenerateConvexMethod::ExternalCollision"))
	float SimplificationDistanceThreshold = 10.f;

	UPROPERTY(EditAnywhere, Category = Convex, meta = (DataflowInput))
	FDataflowConvexDecompositionSettings ConvexDecompositionSettings;

	FCreateLeafConvexHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

USTRUCT(meta = (DataflowGeometryCollection))
struct FSimplifyConvexHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FSimplifyConvexHullsDataflowNode, "SimplifyConvexHulls", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	/** Optional transform selection to compute leaf hulls on -- if not provided, all leaf hulls will be computed. */
	UPROPERTY(meta = (DataflowInput))
	FDataflowTransformSelection OptionalSelectionFilter;

	UPROPERTY(EditAnywhere, Category = "Convex")
	EConvexHullSimplifyMethod SimplifyMethod = EConvexHullSimplifyMethod::MeshQSlim;

	/** Simplified hull should preserve angles larger than this (in degrees).  Used by the AngleTolerance simplification method. */
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, ClampMin = 0.f, EditCondition = "SimplifyMethod == EConvexHullSimplifyMethod::AngleTolerance"))
	float SimplificationAngleThreshold = 10.f;

	/** Simplified hull should stay within this distance of the initial convex hull. Used by the MeshQSlim simplification method. */
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, ClampMin = 0.f, EditCondition = "SimplifyMethod == EConvexHullSimplifyMethod::MeshQSlim"))
	float SimplificationDistanceThreshold = 10.f;

	/** The minimum number of faces to use for the convex hull. For MeshQSlim simplification, this is a triangle count, which may be further reduced on conversion back to a convex hull. */
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DisplayName = "Min Target Face Count", DataflowInput, ClampMin = 4))
	int32 MinTargetTriangleCount = 12;

	/** Whether to restrict the simplified hulls to only use vertices from the original hulls. */
	UPROPERTY(EditAnywhere, Category = "Convex")
	bool bUseExistingVertices = false;

	FSimplifyConvexHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 *
 * Generates convex hull representation for the bones for simulation
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCreateNonOverlappingConvexHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCreateNonOverlappingConvexHullsDataflowNode, "CreateNonOverlappingConvexHulls", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	/** Fraction (of geometry volume) by which a cluster's convex hull volume can exceed the actual geometry volume before instead using the hulls of the children.  0 means the convex volume cannot exceed the geometry volume; 1 means the convex volume is allowed to be 100% larger (2x) the geometry volume. */
	UPROPERTY(EditAnywhere, Category = Convex, meta = (DataflowInput, DisplayName = "Allow Larger Hull Fraction", ClampMin = 0.f))
	float CanExceedFraction = .5f;

	/** Computed convex hulls are simplified to keep points spaced at least this far apart (except where needed to keep the hull from collapsing to zero volume) */
	UPROPERTY(EditAnywhere, Category = Convex, meta = (DataflowInput, ClampMin = 0.f))
	float SimplificationDistanceThreshold = 10.f;

	/** Whether and in what cases to automatically cut away overlapping parts of the convex hulls, to avoid the simulation 'popping' to fix the overlaps */
	UPROPERTY(EditAnywhere, Category = AutomaticOverlapRemoval, meta = (DisplayName = "Remove Overlaps"))
	EConvexOverlapRemovalMethodEnum OverlapRemovalMethod = EConvexOverlapRemovalMethodEnum::Dataflow_EConvexOverlapRemovalMethod_All;

	/** Overlap removal will be computed as if convex hulls were this percentage smaller (in range 0-100) */
	UPROPERTY(EditAnywhere, Category = AutomaticOverlapRemoval, meta = (DataflowInput, ClampMin = 0.f, ClampMax = 99.9f))
	float OverlapRemovalShrinkPercent = 0.f;

	/** Fraction of the convex hulls for a cluster that we can remove before using the hulls of the children */
	UPROPERTY(EditAnywhere, Category = AutomaticOverlapRemoval, meta = (DataflowInput, DisplayName = "Max Removal Fraction", ClampMin = 0.01f, ClampMax = 1.f))
	float CanRemoveFraction = 0.3f;

	FCreateNonOverlappingConvexHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

//~ Simple wrapper class to make the sphere covering data possible to pass via dataflow
// A set of spheres generated to represent empty space when creating a minimal set of convex hulls, e.g. in one of the Generate Cluster Convex Hulls nodes
USTRUCT()
struct FDataflowSphereCovering
{
	GENERATED_USTRUCT_BODY()

public:
	UE::Geometry::FSphereCovering Spheres;
};

//~ Dataflow-specific copy of the negative space sampling method enum in ConvexDecomposition3.h,
//~ so that it can be exposed as a UENUM
// Method to distribute sampling spheres
UENUM()
enum class ENegativeSpaceSampleMethodDataflowEnum : uint8
{
	// Place sample spheres in a uniform grid pattern
	Uniform,
	// Use voxel-based subtraction and offsetting methods to specifically target concavities
	VoxelSearch
};

/**
 *
 * Generates cluster convex hulls for leafs hulls
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FGenerateClusterConvexHullsFromLeafHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGenerateClusterConvexHullsFromLeafHullsDataflowNode, "GenerateClusterConvexHullsFromLeafHulls", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	// A representation of the negative space protected by the 'protect negative space' option. If negative space is not protected, this will contain zero spheres.
	UPROPERTY(meta = (DataflowOutput))
	FDataflowSphereCovering SphereCovering;

	/** Maximum number of convex to generate for a specific cluster. Will be ignored if error tolerance is used instead */
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, EditCondition = "ErrorTolerance == 0"))
	int32 ConvexCount = 2;
	
	/** 
	* Error tolerance to use to decide to merge leaf convex together. 
	* This is in centimeters and represents the side of a cube, the volume of which will be used as threshold
	* to know if the volume of the generated convex is too large compared to the sum of the volume of the leaf convex
	*/
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = "0", UIMax = "100.", Units = cm))
	double ErrorTolerance = 0.0;
	
	/** Whether to prefer available External (imported) collision shapes instead of the computed convex hulls on the Collection */
	UPROPERTY(EditAnywhere, Category = "Convex")
	bool bPreferExternalCollisionShapes = true;

	/** Method to determine which convex hull pairs can potentially be merged */
	UPROPERTY(EditAnywhere, Category = "Convex")
	EAllowConvexMergeMethod AllowMerges = EAllowConvexMergeMethod::ByProximity;

	/** Optional transform selection to compute cluster hulls on -- if not provided, all cluster hulls will be computed. */
	UPROPERTY(meta = (DataflowInput))
	FDataflowTransformSelection OptionalSelectionFilter;

	/** Whether to use a sphere cover to define negative space that should not be covered by convex hulls */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput))
	bool bProtectNegativeSpace = false;

	/** Method to use to find and sample negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (EditCondition = "bProtectNegativeSpace", EditConditionHides))
	ENegativeSpaceSampleMethodDataflowEnum SampleMethod = ENegativeSpaceSampleMethodDataflowEnum::Uniform;

	/** Whether to require that all candidate locations identified by Voxel Search are covered by negative space samples, up to the specified Min Sample Spacing. Only applies to Voxel Search. */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (EditCondition = "bProtectNegativeSpace && SampleMethod == ENegativeSpaceSampleMethodDataflowEnum::VoxelSearch", EditConditionHides))
	bool bRequireSearchSampleCoverage = false;

	/** When performing Voxel Search, only look for negative space that is connected out to the convex hull. This removes inaccessable internal negative space from consideration. Only applies to Voxel Search. */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (EditCondition = "bProtectNegativeSpace && SampleMethod == ENegativeSpaceSampleMethodDataflowEnum::VoxelSearch", EditConditionHides))
	bool bOnlyConnectedToHull = false;

	/** Approximate number of spheres to consider when covering negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = 1, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	int32 TargetNumSamples = 50;

	/** Minimum desired spacing between spheres; if > 0, will attempt not to place sphere centers closer than this */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = 0, Units = cm, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	double MinSampleSpacing = 1.0;

	/** Amount of space to leave between convex hulls and protected negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = .01, UIMin = .1, Units = cm, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	double NegativeSpaceTolerance = 2.0;

	/** Spheres smaller than this are not included in the negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = 0, Units = cm, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	double MinRadius = 10.0;

	FGenerateClusterConvexHullsFromLeafHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 * Generates cluster convex hulls for children hulls
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FGenerateClusterConvexHullsFromChildrenHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGenerateClusterConvexHullsFromChildrenHullsDataflowNode, "GenerateClusterConvexHullsFromChildrenHulls", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;
	
	// A representation of the negative space protected by the 'protect negative space' option. If negative space is not protected, this will contain zero spheres.
	UPROPERTY(meta = (DataflowOutput))
	FDataflowSphereCovering SphereCovering;

	/** Maximum number of convex to generate for a specific cluster. Will be ignored if error tolerance is used instead */
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, EditCondition = "ErrorTolerance == 0"))
	int32 ConvexCount = 2;

	/**
	* Error tolerance to use to decide to merge leaf convex together.
	* This is in centimeters and represents the side of a cube, the volume of which will be used as threshold
	* to know if the volume of the generated convex is too large compared to the sum of the volume of the leaf convex
	*/
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = "0", UIMax = "100.", Units = cm))
	double ErrorTolerance = 0.0;
	
	/** Whether to prefer available External (imported) collision shapes instead of the computed convex hulls on the Collection */
	UPROPERTY(EditAnywhere, Category = "Convex")
	bool bPreferExternalCollisionShapes = true;

	/** Optional transform selection to compute cluster hulls on -- if not provided, all cluster hulls will be computed. */
	UPROPERTY(meta = (DataflowInput))
	FDataflowTransformSelection OptionalSelectionFilter;

	/** Whether to use a sphere cover to define negative space that should not be covered by convex hulls */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput))
	bool bProtectNegativeSpace = false;

	/** Method to use to find and sample negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (EditCondition = "bProtectNegativeSpace", EditConditionHides))
	ENegativeSpaceSampleMethodDataflowEnum SampleMethod = ENegativeSpaceSampleMethodDataflowEnum::Uniform;

	/** Whether to require that all candidate locations identified by Voxel Search are covered by negative space samples, up to the specified Min Sample Spacing. Only applies to Voxel Search. */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (EditCondition = "bProtectNegativeSpace && SampleMethod == ENegativeSpaceSampleMethodDataflowEnum::VoxelSearch", EditConditionHides))
	bool bRequireSearchSampleCoverage = false;

	/** When performing Voxel Search, only look for negative space that is connected out to the convex hull. This removes inaccessable internal negative space from consideration. Only applies to Voxel Search. */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (EditCondition = "bProtectNegativeSpace && SampleMethod == ENegativeSpaceSampleMethodDataflowEnum::VoxelSearch", EditConditionHides))
	bool bOnlyConnectedToHull = false;

	/** Approximate number of spheres to consider when covering negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = 1, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	int32 TargetNumSamples = 50;

	/** Minimum desired spacing between spheres; if > 0, will attempt not to place sphere centers closer than this */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = 0, Units = cm, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	double MinSampleSpacing = 1.0;

	/** Amount of space to leave between convex hulls and protected negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = .01, UIMin = .1, Units = cm, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	double NegativeSpaceTolerance = 2.0;

	/** Spheres smaller than this are not included in the negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = 0, Units = cm, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	double MinRadius = 10.0;

	FGenerateClusterConvexHullsFromChildrenHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/** Clear convex hulls from a collection */
USTRUCT(meta = (DataflowGeometryCollection))
struct FClearConvexHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FClearConvexHullsDataflowNode, "ClearConvexHulls", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** [Optional] selection of transforms to clear convex on, if not set all the transform will be used */
	UPROPERTY(meta = (DataflowInput))
	FDataflowTransformSelection TransformSelection;

	FClearConvexHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&TransformSelection);

		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};


/** Merge convex hulls on transforms with multiple hulls */
USTRUCT(meta = (DataflowGeometryCollection))
struct FMergeConvexHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMergeConvexHullsDataflowNode, "MergeConvexHulls", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	// A representation of the negative space protected by the 'protect negative space' option. If negative space is not protected, this will contain zero spheres.
	UPROPERTY(meta = (DataflowOutput))
	FDataflowSphereCovering SphereCovering;

	/** Maximum number of convex to generate per transform. Ignored if < 0. */
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput))
	int32 MaxConvexCount = -1;

	/**
	* Error tolerance to use to decide to merge leaf convex together.
	* This is in centimeters and represents the side of a cube, the volume of which will be used as threshold
	* to know if the volume of the generated convex is too large compared to the sum of the volume of the leaf convex
	*/
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, ClampMin = "0", UIMax = "100.", Units = cm))
	double ErrorTolerance = 0.0;

	/** Optional transform selection to compute cluster hulls on -- if not provided, all cluster hulls will be computed. */
	UPROPERTY(meta = (DataflowInput))
	FDataflowTransformSelection OptionalSelectionFilter;

	/** Whether to use a sphere cover to define negative space that should not be covered by convex hulls */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput))
	bool bProtectNegativeSpace = false;

	/** Whether to compute separate negative space for each bone. Otherwise, a single negative space will be computed once and re-used for all bones. */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (EditCondition = "bProtectNegativeSpace", EditConditionHides))
	bool bComputeNegativeSpacePerBone = false;

	/** Method to use to find and sample negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (EditCondition = "bProtectNegativeSpace", EditConditionHides))
	ENegativeSpaceSampleMethodDataflowEnum SampleMethod = ENegativeSpaceSampleMethodDataflowEnum::Uniform;

	/** Whether to require that all candidate locations identified by Voxel Search are covered by negative space samples, up to the specified Min Sample Spacing. Only applies to Voxel Search. */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (EditCondition = "bProtectNegativeSpace && SampleMethod == ENegativeSpaceSampleMethodDataflowEnum::VoxelSearch", EditConditionHides))
	bool bRequireSearchSampleCoverage = false;

	/** When performing Voxel Search, only look for negative space that is connected out to the convex hull. This removes inaccessable internal negative space from consideration. Only applies to Voxel Search. */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (EditCondition = "bProtectNegativeSpace && SampleMethod == ENegativeSpaceSampleMethodDataflowEnum::VoxelSearch", EditConditionHides))
	bool bOnlyConnectedToHull = false;

	/** Approximate number of spheres to consider when covering negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = 1, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	int32 TargetNumSamples = 50;

	/** Minimum desired spacing between spheres; if > 0, will attempt not to place sphere centers closer than this */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = 0, Units = cm, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	double MinSampleSpacing = 1.0;

	/** Amount of space to leave between convex hulls and protected negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = .01, UIMin = .1, Units = cm, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	double NegativeSpaceTolerance = 2.0;

	/** Spheres smaller than this are not included in the negative space */
	UPROPERTY(EditAnywhere, Category = NegativeSpace, meta = (DataflowInput, ClampMin = 0, Units = cm, EditCondition = "bProtectNegativeSpace", EditConditionHides))
	double MinRadius = 10.0;

	FMergeConvexHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Update the Volume and Size attributes on the target Collection (and add them if they were not present)
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FUpdateVolumeAttributesDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FUpdateVolumeAttributesDataflowNode, "UpdateVolumeAttributes", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	FUpdateVolumeAttributesDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 * Get the sum of volumes of the convex hulls on the selected nodes
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FGetConvexHullVolumeDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetConvexHullVolumeDataflowNode, "GetConvexHullVolume", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput))
	FManagedArrayCollection Collection;

	/** The transforms to consider */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** Sum of convex hull volumes */
	UPROPERTY(meta = (DataflowOutput));
	float Volume = 0.f;

	/** For any cluster transform that has no convex hulls, whether to fall back to the convex hulls of the cluster's children. Otherwise, the cluster will not add to the total volume sum. */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bSumChildrenForClustersWithoutHulls = true;

	/** Whether to take the volume of the union of selected hulls, rather than the sum of each hull volume separately. This is more expensive but more accurate when hulls overlap. */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bVolumeOfUnion = false;

	FGetConvexHullVolumeDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 * 
 * Editor Fracture Mode / Utilities / TinyGeo tool
 * Merge pieces of geometry onto their neighbors -- use it to, for example, clean up too small pieces of geometry.
 * 
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FFixTinyGeoDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FFixTinyGeoDataflowNode, "FixTinyGeo", "GeometryCollection|Fracture|Utilities", "")

public:
	/** Collection to use */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** The selected pieces to use */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Whether to merge small geometry, or small clusters */
	UPROPERTY(EditAnywhere, Category = Distribution)
	EFixTinyGeoMergeType MergeType = EFixTinyGeoMergeType::MergeGeometry;

	/** Only consider bones at the current Fracture Level */
	UPROPERTY(EditAnywhere, Category = MergeSettings, meta = (EditCondition = "!bFractureLevelIsAll && MergeType == EFixTinyGeoMergeType::MergeClusters", EditConditionHides))
	bool bOnFractureLevel = true;

	/** Only auto - consider clusters for merging.Note that leaf nodes can still be consider if manually selected. */
	UPROPERTY(EditAnywhere, Category = MergeSettings, meta = (EditCondition = "!bFractureLevelIsAll && MergeType == EFixTinyGeoMergeType::MergeClusters && bOnFractureLevel", EditConditionHides))
	bool bOnlyClusters = false;

	/** Only merge to neighbors with the same parent in the hierarchy */
	UPROPERTY(EditAnywhere, Category = MergeSettings, meta = (EditCondition = "MergeType == EFixTinyGeoMergeType::MergeClusters", EditConditionHides))
	bool bOnlySameParent = true;

	/** Helper variable to let the EditConditions above check whether the Fracture Level is set to 'All' */
	UPROPERTY()
	bool bFractureLevelIsAll = false;

	UPROPERTY(EditAnywhere, Category = MergeSettings, meta = (DisplayName = "Merge To"))
	EFixTinyGeoNeighborSelectionMethod NeighborSelection = EFixTinyGeoNeighborSelectionMethod::LargestNeighbor;

	/** Only merge pieces that are connected in the proximity graph.If unchecked, connected pieces will still be favored, but if none are available the closest disconnected piece can be merged. */
	UPROPERTY(EditAnywhere, Category = MergeSettings, meta = (EditCondition = "MergeType == EFixTinyGeoMergeType::MergeClusters", EditConditionHides))
	bool bOnlyToConnected = true;

	/** Options for using the current bone selection */
	UPROPERTY(EditAnywhere, Category = MergeSettings, meta = (DisplayName = "Bone Selection"))
	EFixTinyGeoUseBoneSelection UseBoneSelection = EFixTinyGeoUseBoneSelection::NoEffect;

	UPROPERTY(EditAnywhere, Category = FilterSettings)
	EFixTinyGeoGeometrySelectionMethod SelectionMethod = EFixTinyGeoGeometrySelectionMethod::RelativeVolume;

	/** If size (cube root of volume) is less than this value, geometry should be merged into neighbors -- i.e. a value of 2 merges geometry smaller than a 2x2x2 cube */
	UPROPERTY(EditAnywhere, Category = FilterSettings, meta = (DisplayName = "MinSize", ClampMin = ".00001", UIMin = ".1", UIMax = "10", EditCondition = "SelectionMethod == EFixTinyGeoGeometrySelectionMethod::VolumeCubeRoot", EditConditionHides))
	float MinVolumeCubeRoot = 1.f;

	/** If cube root of volume relative to the overall shape's cube root of volume is less than this, the geometry should be merged into its neighbors.
	(Note: This is a bit different from the histogram viewer's "Relative Size," which instead shows values relative to the largest rigid bone.) */
	UPROPERTY(EditAnywhere, Category = FilterSettings, meta = (ClampMin = "0", UIMax = ".1", ClampMax = "1.0", EditCondition = "SelectionMethod == EFixTinyGeoGeometrySelectionMethod::RelativeVolume", EditConditionHides))
	float RelativeVolume = .01f;

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

	FFixTinyGeoDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 *
 * Editor Fracture Mode / Utilities / Normals tool
 * Recompute normals and tangents.
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FRecomputeNormalsInGeometryCollectionDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRecomputeNormalsInGeometryCollectionDataflowNode, "RecomputeNormalsInGeometryCollection", "GeometryCollection|Fracture|Utilities", "")

public:
	/** Collection to use */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** The selected pieces to use */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Whether to only recompute tangents, and leave normals as they were */
	UPROPERTY(EditAnywhere, Category = RecomputeSettings)
	bool bOnlyTangents = false;

	/** If true, update where edges are 'sharp' by comparing adjacent triangle face normals vs the Sharp Edge Angle Threshold. */
	UPROPERTY(EditAnywhere, Category = RecomputeSettings, meta = (EditCondition = "!bOnlyTangents"))
	bool bRecomputeSharpEdges = false;

	/** Threshold on angle of change in face normals across an edge, above which we create a sharp edge if bRecomputeSharpEdges is true */
	UPROPERTY(EditAnywhere, Category = RecomputeSettings, meta = (UIMin = "0.0", UIMax = "180.0", ClampMin = "0.0", ClampMax = "180.0", EditCondition = "bRecomputeSharpEdges && !bOnlyTangents"))
	float SharpEdgeAngleThreshold = 60.0f;

	/** Whether to only change internal surface normals / tangents */
	UPROPERTY(EditAnywhere, Category = RecomputeSettings, AdvancedDisplay)
	bool bOnlyInternalSurfaces = true;

	FRecomputeNormalsInGeometryCollectionDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 *
 * Editor Fracture Mode / Utilities / Resample tool
 * Resample to add collision particles in large flat regions that otherwise might have poor collision response.
 * Only useful to help improve Particle - Implicit collisions.
 * 
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FResampleGeometryCollectionDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FResampleGeometryCollectionDataflowNode, "ResampleGeometryCollection", "GeometryCollection|Fracture|Utilities", "")

public:
	/** Collection to use */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** The selected pieces to use */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

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

	FResampleGeometryCollectionDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 *
 * Editor Fracture Mode / Utilities / Validate tool
 * Ensures that geometrycollection is valid and clean.
 * 
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FValidateGeometryCollectionDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FValidateGeometryCollectionDataflowNode, "ValidateGeometryCollection", "GeometryCollection|Fracture|Utilities", "")

public:
	/** Collection to use */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Find and remove any unused geometry data */
	UPROPERTY(EditAnywhere, Category = CleanUnused)
	bool bRemoveUnreferencedGeometry = true;

	/** Whether to collapse any clusters with only a single child */
	UPROPERTY(EditAnywhere, Category = Clustering)
	bool bRemoveClustersOfOne = false;

	/** Remove dangling clusters -- Note this can invalidate caches */
	UPROPERTY(EditAnywhere, Category = Clustering)
	bool bRemoveDanglingClusters = false;

	FValidateGeometryCollectionDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

namespace UE::Dataflow
{
	void GeometryCollectionUtilityNodes();
}

