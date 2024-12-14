// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryProcessing/CombineMeshInstancesImpl.h"

#include "Async/ParallelFor.h"
#include "Tasks/Task.h"

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "DynamicMeshEditor.h"
#include "Parameterization/DynamicMeshUVEditor.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshTangents.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"

#include "ShapeApproximation/ShapeDetection3.h"
#include "ShapeApproximation/MeshSimpleShapeApproximation.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "Generators/MinimalBoxMeshGenerator.h"

#include "Polygroups/PolygroupsGenerator.h"
#include "GroupTopology.h"
#include "Operations/PolygroupRemesh.h"

#include "MeshSimplification.h"
#include "DynamicMesh/ColliderMesh.h"
#include "MeshConstraintsUtil.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"
#include "Operations/RemoveOccludedTriangles.h"
#include "Operations/MeshResolveTJunctions.h"
#include "Operations/MeshBoolean.h"

#include "MeshBoundaryLoops.h"
#include "Curve/PlanarComplex.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Curve/PolygonOffsetUtils.h"
#include "ConstrainedDelaunay2.h"
#include "Generators/FlatTriangulationMeshGenerator.h"
#include "Operations/ExtrudeMesh.h"

#include "XAtlasWrapper.h"

#include "Physics/CollisionGeometryConversion.h"
#include "Physics/PhysicsDataCollection.h"

#include "TransformSequence.h"
#include "Sampling/SphericalFibonacci.h"
#include "Util/IteratorUtil.h"

#include "Implicit/Morphology.h"
#include "ProjectionTargets.h"


using namespace UE::Geometry;



static TAutoConsoleVariable<int> CVarGeometryCombineMeshInstancesRemoveHidden(
	TEXT("geometry.CombineInstances.DebugRemoveHiddenStrategy"),
	1,
	TEXT("Configure hidden-removal strategy via (temporary debug)"));


static TAutoConsoleVariable<bool> CVarGeometryCombineMeshInstancesVerbose(
	TEXT("geometry.CombineInstances.Verbose"),
	false,
	TEXT("Enable Verbose logging in Combine Mesh Instances, also disables parallel LOD processing"));


enum class EMeshDetailLevel
{
	Base = 0,
	Standard = 1,
	Small = 2,
	Decorative = 3
};


enum class ECombinedLODType
{
	Copied = 0,
	Simplified = 1,
	Approximated = 2,
	VoxWrapped = 3
};



// FMeshPartInstance represents a single instance of a FMeshPart
struct FMeshPartInstance
{
	UE::Geometry::FTransformSequence3d WorldTransform;
	TArray<UMaterialInterface*> Materials;

	UPrimitiveComponent* SourceComponent;
	int32 SourceInstanceIndex;

	EMeshDetailLevel DetailLevel = EMeshDetailLevel::Standard;

	int32 FilterLODLevel = -1;

	bool bAllowApproximation = true;

	// allow FMeshPartInstance to maintain link to external representation of instance
	FIndex3i ExternalInstanceIndex = FIndex3i::Invalid();

	int32 SubsetID = 0;
};


// MeshPart represents a set of FMeshPartInstances of a particular FSourceGeometry
struct FMeshPart
{
	// only one of the values below can be non-null
	UStaticMesh* SourceAsset = nullptr;
	const IGeometryProcessing_CombineMeshInstances::FMeshLODSet* SourceMeshLODSet = nullptr;

	// Precomputed part meshes
	TSharedPtr<IGeometryProcessing_CombineMeshInstances::FSinglePartMeshSet> PrecomputedMeshes = nullptr;

	TArray<FMeshPartInstance> Instances;

	bool bPreserveUVs = false;
	bool bAllowMerging = true;
	bool bAllowApproximation = true;

	IGeometryProcessing_CombineMeshInstances::EApproximationType ApproxFilter = IGeometryProcessing_CombineMeshInstances::EApproximationType::NoConstraint;

	int32 GetNumTriangles() const 
	{
		if (SourceAsset != nullptr)
		{
			return SourceAsset->GetNumTriangles(0);
		}
		else if ( SourceMeshLODSet != nullptr )
		{
			return SourceMeshLODSet->ReferencedMeshLODs[0]->Triangles().Num();
		}
		else
		{
			ensure(false);
			return 0;
		}
	}
};


struct FSourceGeometry
{
	// Note: These arrays cannot be resized
	TArray<UE::Geometry::FDynamicMesh3> SourceMeshLODs;
	UE::Geometry::FSimpleShapeSet3d CollisionShapes;
};

struct FOptimizedGeometry
{
	// Note: These arrays cannot be resized
	TArray<UE::Geometry::FDynamicMesh3> SimplifiedMeshLODs;

	TArray<UE::Geometry::FDynamicMesh3> ApproximateMeshLODs;

	//UE::Geometry::FSimpleShapeSet3d CollisionShapes;
};


class FMeshPartsAssembly
{
public:
	// this is necessary due to TArray<TUniquePtr> below
	FMeshPartsAssembly() = default;
	FMeshPartsAssembly(FMeshPartsAssembly&) = delete;
	FMeshPartsAssembly& operator=(const FMeshPartsAssembly&) = delete;

	// Array of parts, sorted in descending order of triangle count. Note each part can have multiple instances
	TArray<TUniquePtr<FMeshPart>> Parts;

	// All materials used by parts in this assembly
	TArray<UMaterialInterface*> UniqueMaterials;
	TMap<UMaterialInterface*, int32> MaterialMap;


	// For each part, and array of source LODs
	TArray<FSourceGeometry> SourceMeshGeometry;
	// For each part, arrays of simplified and approximated versions of the part
	TArray<FOptimizedGeometry> OptimizedMeshGeometry;
	// Array of aabb tree per SourceMeshGeometry; note these are always wrt LOD 0 of the corresponding source geometry
	TArray<FDynamicMeshAABBTree3> SourceMeshSpatials;

	// allow external code to preprocess dynamic mesh for a specific instance
	TFunction<void(FDynamicMesh3&, const FMeshPartInstance&)> PreProcessInstanceMeshFunc;
};






void InitializeMeshPartAssembly(
	const IGeometryProcessing_CombineMeshInstances::FSourceInstanceList& SourceInstanceList,
	FMeshPartsAssembly& AssemblyOut)
{
	TMap<UStaticMesh*, FMeshPart*> StaticMeshToPartMap;

	int32 NumStaticMeshInstances = SourceInstanceList.StaticMeshInstances.Num();
	for ( int32 Index = 0; Index < NumStaticMeshInstances; ++Index)
	{
		const IGeometryProcessing_CombineMeshInstances::FStaticMeshInstance& SourceMeshInstance = SourceInstanceList.StaticMeshInstances[Index];

		UStaticMesh* StaticMesh = SourceMeshInstance.SourceMesh;
		FMeshPart** FoundPart = StaticMeshToPartMap.Find(StaticMesh);
		FMeshPart* LocalPtr = nullptr;
		if (FoundPart == nullptr)
		{
			TUniquePtr<FMeshPart> NewPart = MakeUnique<FMeshPart>();
			NewPart->SourceAsset = StaticMesh;
			NewPart->PrecomputedMeshes = SourceMeshInstance.PrecomputedMeshes;
			LocalPtr = NewPart.Get();
					
			AssemblyOut.Parts.Add(MoveTemp(NewPart));
			// store source model?

			StaticMeshToPartMap.Add(StaticMesh, LocalPtr);
			FoundPart = &LocalPtr;
		}

		FMeshPartInstance NewInstance;
		NewInstance.ExternalInstanceIndex = FIndex3i(Index, 0, -1);
		NewInstance.SubsetID = SourceMeshInstance.InstanceSubsetID;

		if ( SourceMeshInstance.GroupDataIndex >= 0 && SourceMeshInstance.GroupDataIndex < SourceInstanceList.InstanceGroupDatas.Num() )
		{
			const IGeometryProcessing_CombineMeshInstances::FMeshInstanceGroupData& GroupData = 
				SourceInstanceList.InstanceGroupDatas[SourceMeshInstance.GroupDataIndex];
			NewInstance.Materials = GroupData.MaterialSet;

			(*FoundPart)->bPreserveUVs = GroupData.bPreserveUVs;
			(*FoundPart)->bAllowMerging = GroupData.bAllowMerging;
			(*FoundPart)->bAllowApproximation = GroupData.bAllowApproximation;
			(*FoundPart)->ApproxFilter = GroupData.ApproximationConstraint;
		}

		NewInstance.SourceComponent = SourceMeshInstance.SourceComponent;
		NewInstance.SourceInstanceIndex = SourceMeshInstance.SourceInstanceIndex;
		NewInstance.DetailLevel = static_cast<EMeshDetailLevel>( static_cast<int32>(SourceMeshInstance.DetailLevel) );
		NewInstance.FilterLODLevel = SourceMeshInstance.FilterLODLevel;
		NewInstance.bAllowApproximation = SourceMeshInstance.bAllowApproximation;
		for ( FTransform3d Transform : SourceMeshInstance.TransformSequence )
		{
			NewInstance.WorldTransform.Append( Transform );
		}
		(*FoundPart)->Instances.Add(NewInstance);
	}


	// todo: possibly should not assume that MeshLODSets contains unique sets, ie should find uniques and remap them?

	TMap<const IGeometryProcessing_CombineMeshInstances::FMeshLODSet*, FMeshPart*> MeshLODSetToPartMap;

	int32 NumMeshLODSetInstances = SourceInstanceList.MeshLODSetInstances.Num();
	for (int32 Index = 0; Index < NumMeshLODSetInstances; ++Index)
	{
		const IGeometryProcessing_CombineMeshInstances::FMeshLODSetInstance& SourceMeshInstance = SourceInstanceList.MeshLODSetInstances[Index];
		int32 MeshSetIndex = SourceMeshInstance.MeshLODSetIndex;
		if (MeshSetIndex < 0 || MeshSetIndex > SourceInstanceList.MeshLODSets.Num())
		{
			ensure(false);
			continue;
		}
		const IGeometryProcessing_CombineMeshInstances::FMeshLODSet* MeshLODSet = &SourceInstanceList.MeshLODSets[MeshSetIndex];

		FMeshPart** FoundPart = MeshLODSetToPartMap.Find(MeshLODSet);
		FMeshPart* LocalPtr = nullptr;
		if (FoundPart == nullptr)
		{
			TUniquePtr<FMeshPart> NewPart = MakeUnique<FMeshPart>();
			NewPart->SourceMeshLODSet = MeshLODSet;
			NewPart->PrecomputedMeshes = SourceMeshInstance.PrecomputedMeshes;
			LocalPtr = NewPart.Get();

			AssemblyOut.Parts.Add(MoveTemp(NewPart));
			// store source model?

			MeshLODSetToPartMap.Add(MeshLODSet, LocalPtr);
			FoundPart = &LocalPtr;
		}

		FMeshPartInstance NewInstance;
		NewInstance.ExternalInstanceIndex = FIndex3i(Index, 1, -1);
		NewInstance.SubsetID = SourceMeshInstance.InstanceSubsetID;

		if (SourceMeshInstance.GroupDataIndex >= 0 && SourceMeshInstance.GroupDataIndex < SourceInstanceList.InstanceGroupDatas.Num())
		{
			const IGeometryProcessing_CombineMeshInstances::FMeshInstanceGroupData& GroupData =
				SourceInstanceList.InstanceGroupDatas[SourceMeshInstance.GroupDataIndex];
			NewInstance.Materials = GroupData.MaterialSet;

			(*FoundPart)->bPreserveUVs = GroupData.bPreserveUVs;
			(*FoundPart)->bAllowMerging = GroupData.bAllowMerging;
			(*FoundPart)->bAllowApproximation = GroupData.bAllowApproximation;
			(*FoundPart)->ApproxFilter = GroupData.ApproximationConstraint;
		}

		NewInstance.SourceComponent = nullptr;
		NewInstance.SourceInstanceIndex = 0;
		NewInstance.DetailLevel = static_cast<EMeshDetailLevel>(static_cast<int32>(SourceMeshInstance.DetailLevel));
		NewInstance.FilterLODLevel = SourceMeshInstance.FilterLODLevel;
		NewInstance.bAllowApproximation = SourceMeshInstance.bAllowApproximation;
		for (FTransform3d Transform : SourceMeshInstance.TransformSequence)
		{
			NewInstance.WorldTransform.Append(Transform);
		}
		(*FoundPart)->Instances.Add(NewInstance);
	}




	// sort parts by largest triangle count first
	AssemblyOut.Parts.Sort([&](const TUniquePtr<FMeshPart>& A, const TUniquePtr<FMeshPart>& B)
	{
		return A->GetNumTriangles() > B->GetNumTriangles();
	});


	auto CollectUniqueMaterials = [&AssemblyOut](const FMeshPart& Part)
	{
		for (const FMeshPartInstance& Instance : Part.Instances)
		{
			for (UMaterialInterface* Material : Instance.Materials)
			{
				if (AssemblyOut.MaterialMap.Contains(Material) == false)
				{
					int32 NewIndex = AssemblyOut.UniqueMaterials.Num();
					AssemblyOut.UniqueMaterials.Add(Material);
					AssemblyOut.MaterialMap.Add(Material, NewIndex);
				}
			}
		}
	};


	// todo: why are these map iterations?? can't we just iterate over AssemblyOut.Parts?
	// collect unique materials
	for (TPair<UStaticMesh*, FMeshPart*>& Pair : StaticMeshToPartMap)
	{
		FMeshPart& Part = *(Pair.Value);
		CollectUniqueMaterials(Part);
	}
	for (TPair<const IGeometryProcessing_CombineMeshInstances::FMeshLODSet*, FMeshPart*>& Pair : MeshLODSetToPartMap)
	{
		FMeshPart& Part = *(Pair.Value);
		CollectUniqueMaterials(Part);
	}
}


// Fetch a given LOD index of the Part and return in OutputLODMesh. Return false if LOD mesh is not available.
bool ExtractSourceMeshLOD(
	FMeshPart& Part,
	int32 LODIndex,
	FDynamicMesh3& OutputLODMesh)
{
	const FMeshDescription* UseMeshDescription = nullptr;
	if (Part.SourceAsset != nullptr)
	{
		UStaticMesh* StaticMesh = Part.SourceAsset;
		if (LODIndex < StaticMesh->GetNumSourceModels())
		{
			UseMeshDescription = StaticMesh->GetMeshDescription(LODIndex);
		}
	}
	else if (Part.SourceMeshLODSet != nullptr)
	{
		if (LODIndex < Part.SourceMeshLODSet->ReferencedMeshLODs.Num())
		{
			UseMeshDescription = Part.SourceMeshLODSet->ReferencedMeshLODs[LODIndex];
		}
	}
	else
	{
		ensure(false);
	}

	if (UseMeshDescription != nullptr)
	{
		FMeshDescriptionToDynamicMesh Converter;
		Converter.bEnableOutputGroups = true;
		Converter.bTransformVertexColorsLinearToSRGB = true;		// possibly this should be false...
		Converter.Convert(UseMeshDescription, OutputLODMesh);
		return true;
	}
	return false;
}


void InitializeAssemblySourceMeshesFromLOD(
	FMeshPartsAssembly& Assembly,
	int32 SourceAssetBaseLOD,
	int32 NumSourceLODs)
{
	using namespace UE::Geometry;

	check(NumSourceLODs > 0);

	int32 NumParts = Assembly.Parts.Num();
	Assembly.SourceMeshGeometry.SetNum(NumParts);

	// collect mesh for each assembly item
	ParallelFor(NumParts, [&](int32 Index)
	{
		TUniquePtr<FMeshPart>& Part = Assembly.Parts[Index];
		FSourceGeometry& Target = Assembly.SourceMeshGeometry[Index];
		Target.SourceMeshLODs.SetNum(NumSourceLODs);

		for (int32 k = 0; k < NumSourceLODs; ++k)
		{
			int32 LODIndex = SourceAssetBaseLOD + k;
			ExtractSourceMeshLOD(*Part, LODIndex, Target.SourceMeshLODs[k]);
		}

		// if first LOD is missing try getting LOD0 again  (why?? Only doing for static mesh asset path because it was done before...)
		if (Target.SourceMeshLODs[0].TriangleCount() == 0 && Part->SourceAsset != nullptr)
		{
			ExtractSourceMeshLOD(*Part, 0, Target.SourceMeshLODs[0]);
		}

		// now make sure every one of our Source LODs has a mesh by copying from N-1
		for (int32 k = 1; k < NumSourceLODs; ++k)
		{
			if (Target.SourceMeshLODs[k].TriangleCount() == 0)
			{
				Target.SourceMeshLODs[k] = Target.SourceMeshLODs[k-1];
			}
		}

	});


	// not clear that it is safe to do this in parallel...
	for (int32 Index = 0; Index < NumParts; ++Index)
	{
		TUniquePtr<FMeshPart>& Part = Assembly.Parts[Index];
		FSourceGeometry& Target = Assembly.SourceMeshGeometry[Index];

		if ( UStaticMesh* StaticMesh = Part->SourceAsset )
		{
			if ( UBodySetup* BodySetup = StaticMesh->GetBodySetup() )
			{
				UE::Geometry::GetShapeSet(BodySetup->AggGeom, Target.CollisionShapes);
			}
		}
		else if (Part->SourceMeshLODSet != nullptr)
		{
			UE::Geometry::GetShapeSet(Part->SourceMeshLODSet->SimpleCollisionShapes, Target.CollisionShapes);
		}

		// sometimes simple collision is a convex when it's actually a box - could try to detect here?

	}

}




/**
 * @return ( Sqrt(Sum-of-squared-distances) , Max(distance)  )
 * 
 */
static FVector2d ComputeGeometricDeviation(
	const FDynamicMesh3& MeasureMesh, 
	const FDynamicMeshAABBTree3& SourceBVH)
{
	int PointCount = 0;
	double SumDistanceSqr = 0;
	double MaxDistanceSqr = 0;
	auto TestPointFunc = [&SumDistanceSqr, &MaxDistanceSqr, &PointCount, &SourceBVH](FVector3d Point)
	{
		double NearDistSqr;
		SourceBVH.FindNearestTriangle(Point, NearDistSqr);
		if (NearDistSqr > MaxDistanceSqr)
		{
			MaxDistanceSqr = NearDistSqr;
		}
		SumDistanceSqr += NearDistSqr;
		PointCount++;
	};

	for (int32 vid : MeasureMesh.VertexIndicesItr())
	{
		TestPointFunc(MeasureMesh.GetVertex(vid));
	}

	for (int32 tid : MeasureMesh.TriangleIndicesItr())
	{
		TestPointFunc(MeasureMesh.GetTriCentroid(tid));
	}

	for (int32 eid : MeasureMesh.EdgeIndicesItr())
	{
		TestPointFunc(MeasureMesh.GetEdgePoint(eid, 0.5));
	}

	return FVector2d(
		FMathd::Sqrt(SumDistanceSqr),
		FMathd::Sqrt(MaxDistanceSqr) );
}





/**
 * @return ( Sqrt(Sum-of-squared-distances) / NumPoints , Max(distance)  )
 * 
 */
static FVector2d DeviationMetric(
	const FDynamicMesh3& MeasureMesh, 
	const FDynamicMeshAABBTree3& SourceBVH)
{
	// todo: could consider normal deviation?
	int PointCount = 0;
	double SumDistanceSqr = 0;
	double MaxDistanceSqr = 0;
	auto TestPointFunc = [&SumDistanceSqr, &MaxDistanceSqr, &PointCount, &SourceBVH](FVector3d Point)
	{
		double NearDistSqr;
		SourceBVH.FindNearestTriangle(Point, NearDistSqr);
		if (NearDistSqr > MaxDistanceSqr)
		{
			MaxDistanceSqr = NearDistSqr;
		}
		SumDistanceSqr += NearDistSqr;
		PointCount++;
	};

	for (int32 vid : MeasureMesh.VertexIndicesItr())
	{
		TestPointFunc(MeasureMesh.GetVertex(vid));
	}

	for (int32 tid : MeasureMesh.TriangleIndicesItr())
	{
		TestPointFunc(MeasureMesh.GetTriCentroid(tid));
	}

	for (int32 eid : MeasureMesh.EdgeIndicesItr())
	{
		TestPointFunc(MeasureMesh.GetEdgePoint(eid, 0.5));
	}

	return FVector2d(
		FMathd::Sqrt(SumDistanceSqr) / (double)PointCount,
		FMathd::Sqrt(MaxDistanceSqr) );
}



class FPartApproxSelector
{
public:
	double TriangleCost = 0.7;
	double MaxAllowableDeviation = 0;		// 0 = disabled

	struct FResultOption
	{
		FVector2d DeviationMetric;
		double CostMetric;
		TSharedPtr<FDynamicMesh3> Mesh;
		int32 MethodID;
	};
	TArray<FResultOption> Options;

	const FDynamicMesh3* SourceMesh;
	const FDynamicMeshAABBTree3* Spatial;

	void Initialize(const FDynamicMesh3* SourceMeshIn, const FDynamicMeshAABBTree3* SpatialIn)
	{
		SourceMesh = SourceMeshIn;
		Spatial = SpatialIn;
	}

	void AddGeneratedMesh(
		const FDynamicMesh3& ExternalMesh,
		int32 MethodID )
	{
		FResultOption Option;
		Option.MethodID = MethodID;
		Option.Mesh = MakeShared<FDynamicMesh3>(ExternalMesh);
		ComputeMetric(Option);
		Options.Add(Option);
	}

	void AddGeneratedMesh(
		TFunctionRef<void(FDynamicMesh3&)> GeneratorFunc,
		int32 MethodID )
	{
		FResultOption Option;
		Option.MethodID = MethodID;
		Option.Mesh = MakeShared<FDynamicMesh3>(*SourceMesh);
		GeneratorFunc(*Option.Mesh);
		ComputeMetric(Option);
		Options.Add(Option);
	}

	static double ComputeMetricFromDeviation(FVector2d DeviationMetric, int32 MethodID, int32 TriCount, double MaxAllowableDeviation, double TriangleCost = .7)
	{
		int32 BaseTriCount = 12;		// 2 tris for each face of box
		if (MaxAllowableDeviation > 0 && DeviationMetric[1] > MaxAllowableDeviation)
		{
			return TNumericLimits<float>::Max() + (double)MethodID;
		}
		else
		{
			return DeviationMetric[0] * FMathd::Pow((double)TriCount / (double)BaseTriCount, TriangleCost);
		}
	}

	// Compute an error metric to decide which approximation of the input shape to favor
	// Note this is a point-sampled (not area weighted) average squared error scaled by (TriCount / 12)^.7, or a huge number (but ordered by generation method) if the max error is too high
	// So the metric favors e.g. using fewer triangles in higher error regions (so there are less samples there) and a higher error is 'ok' if it comes with an associated lower tri count.
	// Note this metric is only 'for' comparing meshes that represent the same shape, it's not intended to compare quality of two different parts meshes
	// (e.g. do not use it to decide which part to 'promote' to a worse LOD to hit a tri budget)
	void ComputeMetric(FResultOption& Option)
	{
		Option.DeviationMetric = DeviationMetric(*Option.Mesh, *Spatial);
		Option.CostMetric = ComputeMetricFromDeviation(Option.DeviationMetric, Option.MethodID, Option.Mesh->TriangleCount(), MaxAllowableDeviation, TriangleCost);
	}

	void SelectBestOption(
		FDynamicMesh3& ResultMesh, 
		int32& MethodID)
	{
		Options.StableSort( [&](const FResultOption& A, const FResultOption& B) { return A.CostMetric < B.CostMetric; } );
		MethodID = Options[0].MethodID;
		ResultMesh = MoveTemp(*Options[0].Mesh);
	}
};










void InitializePartAssemblySpatials(FMeshPartsAssembly& Assembly)
{
	int32 NumParts = Assembly.Parts.Num();
	Assembly.SourceMeshSpatials.SetNum(NumParts);
	
	ParallelFor(NumParts, [&](int32 Index)
	{
		TUniquePtr<FMeshPart>& Part = Assembly.Parts[Index];
		FSourceGeometry& Target = Assembly.SourceMeshGeometry[Index];
		FDynamicMeshAABBTree3& Spatial = Assembly.SourceMeshSpatials[Index];
		Spatial.SetMesh(&Target.SourceMeshLODs[0], true);
	});
}



/**
 * Simplification can make a mess on low-poly shapes and sometimes just using a simple
 * approximation would be better, use our metric to make this decision.
 * (todo: this could maybe be folded into simplified-mesh computations...)
 */
void ReplaceBadSimplifiedLODs(FMeshPartsAssembly& Assembly, const IGeometryProcessing_CombineMeshInstances::FOptions& CombineOptions)
{
	int32 NumParts = Assembly.Parts.Num();
	
	ParallelFor(NumParts, [&](int32 Index)
	{
		TUniquePtr<FMeshPart>& Part = Assembly.Parts[Index];
		FSourceGeometry& Target = Assembly.SourceMeshGeometry[Index];
		FDynamicMeshAABBTree3& Spatial = Assembly.SourceMeshSpatials[Index];
		FOptimizedGeometry& OptimizedTargets = Assembly.OptimizedMeshGeometry[Index];

		for ( int32 k = OptimizedTargets.SimplifiedMeshLODs.Num()-1; k >= 0; --k )
		{
			FPartApproxSelector Selector;
			Selector.MaxAllowableDeviation = CombineOptions.MaxAllowableApproximationDeviation;
			Selector.Initialize(Spatial.GetMesh(), &Spatial);
			if ( k == OptimizedTargets.SimplifiedMeshLODs.Num()-1 )
			{
				if (OptimizedTargets.ApproximateMeshLODs.Num() > 0)
				{
					Selector.AddGeneratedMesh(OptimizedTargets.ApproximateMeshLODs[0], 2);
				}
			}
			else
			{
				Selector.AddGeneratedMesh(OptimizedTargets.SimplifiedMeshLODs[k+1], 1);
			}
			Selector.AddGeneratedMesh(OptimizedTargets.SimplifiedMeshLODs[k], 0);

			// either keep current mesh or replace w/ simplified version
			int32 SelectedMethodID = -1;
			Selector.SelectBestOption(OptimizedTargets.SimplifiedMeshLODs[k], SelectedMethodID);
		}
	});
}



// This function tries to find "corners" of the mesh that should be exactly preserved,
// which can help to maintain important shape features (but this is a very rough heuristic)
static void SetupSimplifyConstraints(
	FDynamicMesh3& Mesh, 
	FMeshConstraints& Constraints,
	double HardEdgeAngleThresholdDeg,
	double LargeAreaThreshold)
{
	// save polygroups if they exist
	TArray<int32> ExistingGroups;
	if (Mesh.HasTriangleGroups())
	{
		ExistingGroups.SetNum(Mesh.MaxTriangleID());
		for (int32 tid : Mesh.TriangleIndicesItr())
		{
			ExistingGroups[tid] = Mesh.GetTriangleGroup(tid);
		}
	}

	// generate polygroups for planar areas of the mesh
	FPolygroupsGenerator Generator(&Mesh);
	const bool bUVSeams = false, bNormalSeams = false;
	double DotTolerance = 1.0 - FMathd::Cos(HardEdgeAngleThresholdDeg * FMathd::DegToRad);
	Generator.FindPolygroupsFromFaceNormals(DotTolerance, bUVSeams, bNormalSeams);
	Generator.CopyPolygroupsToMesh();

	FGroupTopology GroupTopology(&Mesh, true);

	// find "large" areas, where large is basically defined as larger than a square area.
	// This is not a good heuristic...
	TSet<int32> LargeGroups;
	for (const FGroupTopology::FGroup& Group : GroupTopology.Groups)
	{
		double Area = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh, Group.Triangles).Y;
		if (Area > LargeAreaThreshold)
		{
			LargeGroups.Add(Group.GroupID);
		}
	}


	// iterate over corners, ie junctions between 3 groups. Pin corner if at least
	// two adjacent groups are "large"
	int32 NumCorners = 0;
	for (const FGroupTopology::FCorner& Corner : GroupTopology.Corners)
	{
		int32 NumLargeGroups = 0;
		for (int32 GroupID : Corner.NeighbourGroupIDs)
		{
			if (LargeGroups.Contains(GroupID))
			{
				NumLargeGroups++;
			}
		}
		if (NumLargeGroups >= 2)
		{
			FVertexConstraint Constraint = Constraints.GetVertexConstraint(Corner.VertexID);
			Constraint.bCanMove = false;
			Constraint.bCannotDelete = true;
			Constraints.SetOrUpdateVertexConstraint(
				Corner.VertexID, Constraint);
			NumCorners++;
		}
	}

	// restore groups
	if (ExistingGroups.Num() > 0)
	{
		for (int32 tid : Mesh.TriangleIndicesItr())
		{
			Mesh.SetTriangleGroup(tid, ExistingGroups[tid]);
		}
	}
	else
	{
		Mesh.DiscardTriangleGroups();
	}
}


static void SimplifyPartMesh(
	FDynamicMesh3& EditMesh, 
	double Tolerance, 
	double RecomputeNormalsAngleThreshold,
	bool bTryToPreserveSalientCorners = false,
	bool bPreserveUVs = false,
	bool bPreserveVertexColors = false,
	double PreserveCornersAngleThreshold = 44,
	double MinSalientPartDimension = 1.0)
{
	// currently bowties need to be split for Welder
	FDynamicMeshEditor MeshEditor(&EditMesh);
	FDynamicMeshEditResult EditResult;
	MeshEditor.SplitBowties(EditResult);

	// weld edges in case input was unwelded...
	FMergeCoincidentMeshEdges Welder(&EditMesh);
	Welder.MergeVertexTolerance = Tolerance * 0.001;
	Welder.OnlyUniquePairs = false;
	Welder.Apply();

	// Skip out for very low-poly parts, they are unlikely to simplify very nicely.
	if (EditMesh.VertexCount() < 16)
	{
		return;
	}

	// clear out attributes so it doesn't affect simplification
	bool bAllAttributesCleared = (!bPreserveUVs) && (!bPreserveVertexColors);
	if (bPreserveUVs == false)
	{
		EditMesh.Attributes()->SetNumUVLayers(0);
	}
	if (bPreserveVertexColors == false)
	{
		EditMesh.Attributes()->DisablePrimaryColors();
	}
	EditMesh.Attributes()->DisableTangents();
	FMeshNormals::InitializeOverlayToPerVertexNormals(EditMesh.Attributes()->PrimaryNormals(), false);


	// todo: if preserving UVs or Vertex Colors, might prefer the Attribute simplifier here? 
	// Unclear how to do that conditionally as it's a template, though...
	using SimplifierType = FVolPresMeshSimplification;
	//using SimplifierType = FQEMSimplification;
	SimplifierType Simplifier(&EditMesh);

	Simplifier.ProjectionMode = SimplifierType::ETargetProjectionMode::NoProjection;

	FColliderMesh ColliderMesh;
	ColliderMesh.Initialize(EditMesh);
	FColliderMeshProjectionTarget ProjectionTarget(&ColliderMesh);
	Simplifier.SetProjectionTarget(&ProjectionTarget);

	Simplifier.DEBUG_CHECK_LEVEL = 0;

	// Memory seems to work better on low-poly parts...
	// This should perhaps be based on some heuristics about 'part type'
	Simplifier.bRetainQuadricMemory = true;
												
	// if preserving any attributes, have to clean up seams
	if (bAllAttributesCleared == false )
	{
		Simplifier.bAllowSeamCollapse = true;
		Simplifier.SetEdgeFlipTolerance(1.e-5);
		if (EditMesh.HasAttributes())
		{
			EditMesh.Attributes()->SplitAllBowties();	// eliminate any bowties that might have formed on attribute seams.
		}
	}

	// do these flags matter here since we are not flipping??
	EEdgeRefineFlags MeshBoundaryConstraints = EEdgeRefineFlags::NoFlip;
	EEdgeRefineFlags GroupBorderConstraints = EEdgeRefineFlags::NoConstraint;
	EEdgeRefineFlags MaterialBorderConstraints = EEdgeRefineFlags::NoConstraint;

	FMeshConstraints Constraints;
	FMeshConstraintsUtil::ConstrainAllBoundariesAndSeams(Constraints, EditMesh,
		MeshBoundaryConstraints, GroupBorderConstraints, MaterialBorderConstraints, true, false, true);

	// add optional constraints to try to preserve area
	if (bTryToPreserveSalientCorners)
	{
		SetupSimplifyConstraints(EditMesh, Constraints, PreserveCornersAngleThreshold, MinSalientPartDimension * MinSalientPartDimension);
	}


	Simplifier.SetExternalConstraints(MoveTemp(Constraints));

	Simplifier.GeometricErrorConstraint = SimplifierType::EGeometricErrorCriteria::PredictedPointToProjectionTarget;
	Simplifier.GeometricErrorTolerance = Tolerance;

	Simplifier.SimplifyToTriangleCount( 1 );

	// compact result
	EditMesh.CompactInPlace();

	// recompute normals
	FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&EditMesh, EditMesh.Attributes()->PrimaryNormals(), RecomputeNormalsAngleThreshold);
	FMeshNormals::QuickRecomputeOverlayNormals(EditMesh);
}







/**
 * This function uses FPolygroupRemesh to try to completely retriangulate planar faces
 */
static void PlanarRetriangulatePartMesh(
	FDynamicMesh3& EditMesh,
	double Tolerance,
	double RecomputeNormalsAngleThreshold)
{
	// currently bowties need to be split for Welder
	FDynamicMeshEditor MeshEditor(&EditMesh);
	FDynamicMeshEditResult EditResult;
	MeshEditor.SplitBowties(EditResult);

	// weld edges in case input was unwelded...
	FMergeCoincidentMeshEdges Welder(&EditMesh);
	Welder.MergeVertexTolerance = Tolerance * 0.001;
	Welder.OnlyUniquePairs = false;
	Welder.Apply();

	// Skip out for very low-poly parts, they are unlikely to simplify very nicely.
	if (EditMesh.VertexCount() < 16)
	{
		return;
	}

	double AngleToleranceDeg = 2.0;

	// generate polygroups for planar areas of the mesh
	FPolygroupsGenerator Generator(&EditMesh);
	const bool bUVSeams = false, bNormalSeams = false;
	double DotTolerance = 1.0 - FMathd::Cos(AngleToleranceDeg * FMathd::DegToRad);
	Generator.FindPolygroupsFromFaceNormals(DotTolerance, bUVSeams, bNormalSeams);
	Generator.CopyPolygroupsToMesh();

	FGroupTopology UseTopology(&EditMesh, true);

	FPolygroupRemesh Simplifier(&EditMesh, &UseTopology, ConstrainedDelaunayTriangulate<double>);
	Simplifier.SimplificationAngleTolerance = AngleToleranceDeg;
	Simplifier.Compute();

	// compact result
	EditMesh.CompactInPlace();

	// recompute normals
	FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&EditMesh, EditMesh.Attributes()->PrimaryNormals(), RecomputeNormalsAngleThreshold);
	FMeshNormals::QuickRecomputeOverlayNormals(EditMesh);
}




static void ComputeBoxApproximation(
	const FDynamicMesh3& SourceMesh,
	FDynamicMesh3& OutputMesh,
	bool bForceAxisAligned)
{
	UE::Geometry::FOrientedBox3d OrientedBox;
	if (bForceAxisAligned)
	{
		OrientedBox = UE::Geometry::FOrientedBox3d(SourceMesh.GetBounds(false));
	}
	else
	{
		FMeshSimpleShapeApproximation ShapeApprox;
		ShapeApprox.InitializeSourceMeshes({ &SourceMesh });
		ShapeApprox.bDetectBoxes = ShapeApprox.bDetectCapsules = ShapeApprox.bDetectConvexes = ShapeApprox.bDetectSpheres = false;

		FSimpleShapeSet3d ResultBoxes;
		ShapeApprox.Generate_OrientedBoxes(ResultBoxes);
		OrientedBox = ResultBoxes.Boxes[0].Box;

		// oriented box fitting is under-determined, in cases where the AABB and the OBB have the nearly the
		// same volume, generally we prefer an AABB
		// (note: this rarely works due to tessellation of (eg) circles/spheres, and should be replaced w/ a better heuristic)
		FAxisAlignedBox3d AlignedBox = SourceMesh.GetBounds(false);
		if (AlignedBox.Volume() < 1.2 * OrientedBox.Volume())
		{
			OrientedBox = UE::Geometry::FOrientedBox3d(AlignedBox);
		}
	}

	FGridBoxMeshGenerator BoxGen;
	BoxGen.Box = OrientedBox;
	BoxGen.EdgeVertices = {0,0,0};
	OutputMesh.Copy(&BoxGen.Generate());
}



enum class EApproximatePartMethod : uint8
{
	AxisAlignedBox = 0,
	OrientedBox = 1,
	MinVolumeSweptHull = 2,
	ConvexHull = 3,
	MinTriCountHull = 4,
	FlattendExtrusion = 5,

	AutoBestFit = 10,

	SimplifiedMesh = 55,
	OverrideAxisBox = 77,

	Original = 100

};



static void ComputeSimplePartApproximation(
	const FDynamicMesh3& SourcePartMesh, 
	FDynamicMesh3& DestMesh,
	EApproximatePartMethod ApproxMethod)
{

	if (SourcePartMesh.TriangleCount() == 0)
	{
		// Nothing to approximate.
		return;
	}

	if (ApproxMethod == EApproximatePartMethod::AxisAlignedBox)
	{
		ComputeBoxApproximation(SourcePartMesh, DestMesh, true);
		return;
	}

	if (ApproxMethod == EApproximatePartMethod::OrientedBox)
	{
		ComputeBoxApproximation(SourcePartMesh, DestMesh, false);
		return;
	}

	FMeshSimpleShapeApproximation ShapeApprox;
	ShapeApprox.InitializeSourceMeshes( {&SourcePartMesh} );
	ShapeApprox.bDetectBoxes = ShapeApprox.bDetectCapsules = ShapeApprox.bDetectConvexes = ShapeApprox.bDetectSpheres = false;

	FDynamicMesh3 ResultMesh;

	FDynamicMesh3 ConvexMesh;
	if ( ApproxMethod == EApproximatePartMethod::ConvexHull || ApproxMethod == EApproximatePartMethod::MinTriCountHull )
	{
		FSimpleShapeSet3d ResultConvex;
		ShapeApprox.Generate_ConvexHulls(ResultConvex);
		ConvexMesh = (ResultConvex.Convexes.Num() > 0) ? MoveTemp(ResultConvex.Convexes[0].Mesh) : FDynamicMesh3();
	}

	FDynamicMesh3 MinVolumeHull;
	if ( ApproxMethod != EApproximatePartMethod::ConvexHull )
	{
		FSimpleShapeSet3d ResultX, ResultY, ResultZ;
		ShapeApprox.Generate_ProjectedHulls(ResultX, FMeshSimpleShapeApproximation::EProjectedHullAxisMode::X);
		ShapeApprox.Generate_ProjectedHulls(ResultY, FMeshSimpleShapeApproximation::EProjectedHullAxisMode::Y);
		ShapeApprox.Generate_ProjectedHulls(ResultZ, FMeshSimpleShapeApproximation::EProjectedHullAxisMode::Z);
		FDynamicMesh3 SweptHullX = (ResultX.Convexes.Num() > 0) ? MoveTemp(ResultX.Convexes[0].Mesh) : FDynamicMesh3();
		double VolumeX = (SweptHullX.TriangleCount() > 0) ? TMeshQueries<FDynamicMesh3>::GetVolumeArea(SweptHullX)[0] : TNumericLimits<double>::Max();
		FDynamicMesh3 SweptHullY = (ResultY.Convexes.Num() > 0) ? MoveTemp(ResultY.Convexes[0].Mesh) : FDynamicMesh3();
		double VolumeY = (SweptHullY.TriangleCount() > 0) ? TMeshQueries<FDynamicMesh3>::GetVolumeArea(SweptHullY)[0] : TNumericLimits<double>::Max();
		FDynamicMesh3 SweptHullZ = (ResultZ.Convexes.Num() > 0) ? MoveTemp(ResultZ.Convexes[0].Mesh) : FDynamicMesh3();
		double VolumeZ = (SweptHullZ.TriangleCount() > 0) ? TMeshQueries<FDynamicMesh3>::GetVolumeArea(SweptHullZ)[0] : TNumericLimits<double>::Max();

		int Idx = MinElementIndex(FVector(VolumeX, VolumeY, VolumeZ));
		MinVolumeHull = (Idx == 0) ? SweptHullX : (Idx == 1) ? SweptHullY : SweptHullZ;
	}

	if (ApproxMethod == EApproximatePartMethod::ConvexHull)
	{
		ResultMesh = (ConvexMesh.TriangleCount() > 0) ?  MoveTemp(ConvexMesh) : SourcePartMesh;
	}
	else if (ApproxMethod == EApproximatePartMethod::MinVolumeSweptHull)
	{
		ResultMesh = (MinVolumeHull.TriangleCount() > 0) ?  MoveTemp(MinVolumeHull) : SourcePartMesh;
	}
	else if (ApproxMethod == EApproximatePartMethod::MinTriCountHull)
	{
		ResultMesh = (MinVolumeHull.TriangleCount() < ConvexMesh.TriangleCount()) ? 
			MoveTemp(MinVolumeHull) : MoveTemp(ConvexMesh);
	}

	DestMesh = (ResultMesh.TriangleCount() > 0) ? MoveTemp(ResultMesh) : SourcePartMesh;
}







static void ComputeSweptSolidApproximation(
	const FDynamicMesh3& SourcePartMesh,
	FDynamicMesh3& DestMesh,
	FVector3d Direction,
	double MergeOffset = 0.1,
	double SimplifyTolerance = 1.0,
	double MinHoleArea = 1.0)
{
	FFrame3d ProjectFrame(FVector3d::Zero(), Direction);
	FVector3d XAxis(ProjectFrame.GetAxis(0));
	FVector3d YAxis(ProjectFrame.GetAxis(1));

	FDynamicMesh3 FilteredMesh(SourcePartMesh);
	FInterval1d AxisRange = FInterval1d::Empty();
	for (FVector3d Position : FilteredMesh.VerticesItr())
	{
		AxisRange.Contain(Position.Dot(Direction));
	}

	TArray<int32> DeleteTris;
	for (int32 tid : FilteredMesh.TriangleIndicesItr())
	{
		if (FilteredMesh.GetTriNormal(tid).Dot(Direction) < 0.1)
		{
			DeleteTris.Add(tid);
		}
	}
	for (int32 tid : DeleteTris)
	{
		FilteredMesh.RemoveTriangle(tid);
	}

	FMeshBoundaryLoops Loops(&FilteredMesh);
	FPlanarComplexd PlanarComplex;
	for (FEdgeLoop& Loop : Loops.Loops)
	{
		TArray<FVector3d> Vertices;
		Loop.GetVertices<FVector3d>(Vertices);
		FPolygon2d Polygon;
		for (FVector3d V : Vertices)
		{
			Polygon.AppendVertex(FVector2d(V.Dot(XAxis), V.Dot(YAxis)));
		}
		Polygon.Reverse();		// mesh orientation comes out backwards...
		PlanarComplex.Polygons.Add(MoveTemp(Polygon));
	}
	PlanarComplex.bTrustOrientations = true;		// have to do this or overlapping projections will create holes
	PlanarComplex.FindSolidRegions();
	TArray<FGeneralPolygon2d> Polygons = PlanarComplex.ConvertOutputToGeneralPolygons();

	if (Polygons.Num() == 0)
	{
		// failed to find anything??
		ComputeSimplePartApproximation(SourcePartMesh, DestMesh, EApproximatePartMethod::OrientedBox);
		return;
	}

	double UnionMergeOffset = 0.1;
	if (Polygons.Num() > 1)
	{
		// nudge all polygons outwards to ensure that when we boolean union exactly-coincident polygons
		// they intersect a bit, otherwise we may end up with zero-area cracks/holes
		if (UnionMergeOffset > 0)
		{
			for (FGeneralPolygon2d& Polygon : Polygons)
			{
				Polygon.VtxNormalOffset(UnionMergeOffset);
			}
		}

		TArray<FGeneralPolygon2d> ResultPolygons;
		PolygonsUnion(Polygons, ResultPolygons, true);
		Polygons = MoveTemp(ResultPolygons);

		if (UnionMergeOffset > 0)
		{
			for (FGeneralPolygon2d& Polygon : Polygons)
			{
				Polygon.VtxNormalOffset(-UnionMergeOffset);	// undo offset
			}
		}
	}

	// above result is likely to be extremely noisy, so we want to clean it up a bit, particularly
	// if we are going to do an offset/inset closure...
	double CleanupTol = FMath::Max(SimplifyTolerance * 0.25, 0.1);
	for (FGeneralPolygon2d& Polygon : Polygons)
	{
		Polygon.Simplify(CleanupTol, CleanupTol);
	}

	// can optionally try to reduce polygon complexity by topological closure (dilate/erode)
	if (MergeOffset > 0)
	{
		TArray<FGeneralPolygon2d> TmpPolygons;
		PolygonsOffsets(MergeOffset, -MergeOffset,
			Polygons, TmpPolygons, true, MergeOffset * FMathd::Sqrt2,
			EPolygonOffsetJoinType::Miter,
			EPolygonOffsetEndType::Polygon);

		Polygons = MoveTemp(TmpPolygons);
	}

	// clean up polygons, remove small holes, and pass to triangulator
	FConstrainedDelaunay2d Triangulator;
	for (FGeneralPolygon2d& Polygon : Polygons)
	{
		if (SimplifyTolerance > 0)
		{
			Polygon.Simplify(SimplifyTolerance, SimplifyTolerance * 0.25);		// 0.25 is kind of arbitrary here...
		}
		if (MinHoleArea > 0)
		{
			Polygon.FilterHoles([&](const FPolygon2d& HolePoly) { return HolePoly.Area() < MinHoleArea; });
		}
		Triangulator.Add(Polygon);
	}

	Triangulator.Triangulate([&Polygons](const TArray<FVector2d>& Vertices, FIndex3i Tri)
	{
		FVector2d Point = (Vertices[Tri.A] + Vertices[Tri.B] + Vertices[Tri.C]) / 3.0;
		for (const FGeneralPolygon2d& Polygon : Polygons)
		{
			if (Polygon.Contains(Point))
			{
				return true;
			}
		}
		return false;
	});

	if (Triangulator.Triangles.Num() == 0)
	{
		ComputeSimplePartApproximation(SourcePartMesh, DestMesh, EApproximatePartMethod::OrientedBox);
		return;
	}

	FFlatTriangulationMeshGenerator TriangulationMeshGen;
	TriangulationMeshGen.Vertices2D = Triangulator.Vertices;
	TriangulationMeshGen.Triangles2D = Triangulator.Triangles;
	FDynamicMesh3 ResultMesh(&TriangulationMeshGen.Generate());

	if (ResultMesh.TriangleCount() < 3)
	{
		// failed to find anything??
		ComputeSimplePartApproximation(SourcePartMesh, DestMesh, EApproximatePartMethod::OrientedBox);
		return;
	}

	ProjectFrame.Origin = FVector3d::Zero() + AxisRange.Min * Direction;
	MeshTransforms::FrameCoordsToWorld(ResultMesh, ProjectFrame);

	FExtrudeMesh Extruder(&ResultMesh);
	Extruder.DefaultExtrudeDistance = AxisRange.Length();
	Extruder.UVScaleFactor = 1.0;
	FVector3d ExtrudeNormal = Direction;
	Extruder.Apply();

	DestMesh = MoveTemp(ResultMesh);
}




static void SelectBestFittingMeshApproximation(
	const FDynamicMesh3& OriginalMesh, 
	const FDynamicMeshAABBTree3& OriginalMeshSpatial,
	IGeometryProcessing_CombineMeshInstances::EApproximationType ApproxTypes,
	FDynamicMesh3& ResultMesh,
	EApproximatePartMethod& BestMethodIDOut,
	double AcceptableDeviationTol,
	double TriangleCost,
	double MaxDeviation,
	int32 UseExtrudeAxis = -1  // axis index, or -1 means auto / try all 3
	)
{
	FPartApproxSelector ApproxSelector;
	ApproxSelector.Initialize(&OriginalMesh, &OriginalMeshSpatial);
	ApproxSelector.TriangleCost = TriangleCost;
	ApproxSelector.MaxAllowableDeviation = MaxDeviation;
	bool bNoApproxFilter = (ApproxTypes == IGeometryProcessing_CombineMeshInstances::EApproximationType::NoConstraint);

	if ( bNoApproxFilter || ((int)ApproxTypes & (int)IGeometryProcessing_CombineMeshInstances::EApproximationType::AxisAlignedBox) > 0 )
	{
		ApproxSelector.AddGeneratedMesh( [&](FDynamicMesh3& PartMeshInOut) {
			ComputeSimplePartApproximation(PartMeshInOut, PartMeshInOut, EApproximatePartMethod::AxisAlignedBox);
		}, (int32)EApproximatePartMethod::AxisAlignedBox);
	}

	if (bNoApproxFilter || ((int)ApproxTypes & (int)IGeometryProcessing_CombineMeshInstances::EApproximationType::OrientedBox) > 0)
	{
		ApproxSelector.AddGeneratedMesh( [&](FDynamicMesh3& PartMeshInOut) {
			ComputeSimplePartApproximation(PartMeshInOut, PartMeshInOut, EApproximatePartMethod::OrientedBox);
		}, (int32)EApproximatePartMethod::OrientedBox );
	}

	if (bNoApproxFilter || ((int)ApproxTypes & (int)IGeometryProcessing_CombineMeshInstances::EApproximationType::SweptHull) > 0)
	{
		ApproxSelector.AddGeneratedMesh( [&](FDynamicMesh3& PartMeshInOut) {
			ComputeSimplePartApproximation(PartMeshInOut, PartMeshInOut, EApproximatePartMethod::MinVolumeSweptHull);
		}, (int32)EApproximatePartMethod::MinVolumeSweptHull );
	}

	if (bNoApproxFilter || ((int)ApproxTypes & (int)IGeometryProcessing_CombineMeshInstances::EApproximationType::ConvexHull) > 0)
	{
		ApproxSelector.AddGeneratedMesh( [&](FDynamicMesh3& PartMeshInOut) {
			ComputeSimplePartApproximation(PartMeshInOut, PartMeshInOut, EApproximatePartMethod::ConvexHull);
		}, (int32)EApproximatePartMethod::ConvexHull );
	}

	// Add swept-solid approximations
	// Currently this is a bit hardcoded and some of these numbers should be exposed as parameters
	if (bNoApproxFilter || ((int)ApproxTypes & (int)IGeometryProcessing_CombineMeshInstances::EApproximationType::SweptProjection) > 0)
	{
		const double MinHoleSize = 10.0;		// very aggressive, should be exposed as a parameter
		const double MinHoleArea = MinHoleSize * MinHoleSize;
		const double PolyMergeTol = 0.1;
		const double PolySimplifyTol = AcceptableDeviationTol;

		if (UseExtrudeAxis == 0 || UseExtrudeAxis == -1)
		{
			ApproxSelector.AddGeneratedMesh([&](FDynamicMesh3& PartMeshInOut) {
				ComputeSweptSolidApproximation(PartMeshInOut, PartMeshInOut,
					FVector3d::UnitX(), PolyMergeTol, PolySimplifyTol, MinHoleArea);
			}, (int32)EApproximatePartMethod::FlattendExtrusion);
		}

		if (UseExtrudeAxis == 1 || UseExtrudeAxis == -1)
		{
			ApproxSelector.AddGeneratedMesh([&](FDynamicMesh3& PartMeshInOut) {
				ComputeSweptSolidApproximation(PartMeshInOut, PartMeshInOut,
					FVector3d::UnitY(), PolyMergeTol, PolySimplifyTol, MinHoleArea);
			}, (int32)EApproximatePartMethod::FlattendExtrusion);
		}

		if (UseExtrudeAxis == 2 || UseExtrudeAxis == -1)
		{
			ApproxSelector.AddGeneratedMesh([&](FDynamicMesh3& PartMeshInOut) {
				ComputeSweptSolidApproximation(PartMeshInOut, PartMeshInOut,
					FVector3d::UnitZ(), PolyMergeTol, PolySimplifyTol, MinHoleArea);
			}, (int32)EApproximatePartMethod::FlattendExtrusion);
		}
	}

	int32 SelectedMethodID;
	ApproxSelector.SelectBestOption(ResultMesh, SelectedMethodID);
	BestMethodIDOut = static_cast<EApproximatePartMethod>(SelectedMethodID);

	// If Axis-Aligned box volume is less than (100+k%) larger than best option, just use that instead.
	// Default is 10%, but if approximation is likely to also be a box, double it.
	// (todo should be configurable)
	if (bNoApproxFilter || ((int)ApproxTypes & (int)IGeometryProcessing_CombineMeshInstances::EApproximationType::AxisAlignedBox) > 0)
	{
		FVector2d ApproxMeshVolArea = TMeshQueries<FDynamicMesh3>::GetVolumeArea(ResultMesh);
		FAxisAlignedBox3d AlignedBox = OriginalMesh.GetBounds(false);
		double BoxVolume = AlignedBox.Volume();
		double VolRatio = AlignedBox.Volume() / ApproxMeshVolArea.X;
		double BoxPreferenceVolumeRatioPercent = (ResultMesh.TriangleCount() <= 12) ? 20.0 : 10.0;
		if (VolRatio < (1.0 + BoxPreferenceVolumeRatioPercent/100.0) )
		{
			ComputeSimplePartApproximation(OriginalMesh, ResultMesh, EApproximatePartMethod::AxisAlignedBox);
			BestMethodIDOut = EApproximatePartMethod::OverrideAxisBox;
		}
	}

}








void ComputeMeshApproximations(
	IGeometryProcessing_CombineMeshInstances::FOptions CombineOptions,
	FMeshPartsAssembly& Assembly)
{
	using namespace UE::Geometry;
	const double AngleThresholdDeg = CombineOptions.HardNormalAngleDeg;

	const int32 NumParts = Assembly.Parts.Num();
	Assembly.OptimizedMeshGeometry.SetNum(NumParts);

	const int32 NumSimplifiedLODs = CombineOptions.NumSimplifiedLODs;
	const int32 NumApproxLODs = FMath::Max(0, 
		CombineOptions.NumLODs - CombineOptions.NumCopiedLODs - CombineOptions.NumSimplifiedLODs);
	const bool bNeedsApproximateDecorativePartLODs = CombineOptions.NumLODs >= CombineOptions.FilterDecorativePartsLODLevel - CombineOptions.ApproximateDecorativePartLODs;

	bool bVerbose = CVarGeometryCombineMeshInstancesVerbose.GetValueOnAnyThread();
	ParallelFor(NumParts, [&](int32 Index)
	{
		TUniquePtr<FMeshPart>& Part = Assembly.Parts[Index];
		FSourceGeometry& SourceGeo = Assembly.SourceMeshGeometry[Index];
		int32 NumSourceLODs = SourceGeo.SourceMeshLODs.Num();
		const FDynamicMesh3* OptimizationSourceMesh = &SourceGeo.SourceMeshLODs.Last();
		if (CombineOptions.ApproximationSourceLOD < NumSourceLODs)
		{
			OptimizationSourceMesh = &SourceGeo.SourceMeshLODs[CombineOptions.ApproximationSourceLOD];
		}
		FOptimizedGeometry& ApproxGeo = Assembly.OptimizedMeshGeometry[Index];

		FDynamicMeshAABBTree3 OptimizationSourceMeshSpatial(OptimizationSourceMesh, true);

		// compute simplified part LODs
		ApproxGeo.SimplifiedMeshLODs.SetNum(NumSimplifiedLODs);
		double InitialTolerance = CombineOptions.SimplifyBaseTolerance;
		for (int32 k = 0; k < NumSimplifiedLODs; ++k)
		{
			ApproxGeo.SimplifiedMeshLODs[k] = *OptimizationSourceMesh;
			int32 SimplifiedLODIndex = NumSourceLODs+k;
			SimplifyPartMesh(ApproxGeo.SimplifiedMeshLODs[k], 
				InitialTolerance, 
				AngleThresholdDeg,
				CombineOptions.bSimplifyPreserveCorners,
				Part->bPreserveUVs || CombineOptions.bSimplifyPreserveUVs || (SimplifiedLODIndex <= CombineOptions.PreserveUVLODLevel),
				CombineOptions.bSimplifyPreserveVertexColors,
				CombineOptions.SimplifySharpEdgeAngleDeg, 
				CombineOptions.SimplifyMinSalientDimension);
			InitialTolerance *= CombineOptions.SimplifyLODLevelToleranceScale;
		}

		// Approximation Source LOD may be set to a simplified LOD
		if (CombineOptions.ApproximationSourceLOD >= NumSourceLODs && CombineOptions.ApproximationSourceLOD < (NumSourceLODs+NumSimplifiedLODs))
		{
			OptimizationSourceMesh = &ApproxGeo.SimplifiedMeshLODs[CombineOptions.ApproximationSourceLOD - NumSourceLODs];
		}

		// Compute shape approximation LODs. 
		// Note that ExtraLODs is a hack here - we are computing more than necessary
		// so that the cost approximation strategy below has additional simplified approximations available. 
		// This could be smarter, but this dumb method works OK for now...
		const int32 ExtraLODs = 10;
		const int32 UseNumApproxLODs = NumApproxLODs > 0 || bNeedsApproximateDecorativePartLODs ? NumApproxLODs + ExtraLODs : 0;
		ApproxGeo.ApproximateMeshLODs.SetNum(UseNumApproxLODs);
		double InitialTriCost = CombineOptions.OptimizeBaseTriCost;
		TArray<EApproximatePartMethod> SelectedMethodID; SelectedMethodID.SetNum(UseNumApproxLODs);		// useful for debugging
		for (int32 k = 0; k < UseNumApproxLODs; ++k)
		{
			SelectBestFittingMeshApproximation(*OptimizationSourceMesh, OptimizationSourceMeshSpatial, 
				Part->ApproxFilter,
				ApproxGeo.ApproximateMeshLODs[k], SelectedMethodID[k], 
				CombineOptions.SimplifyBaseTolerance, InitialTriCost, CombineOptions.MaxAllowableApproximationDeviation);
			if (k < NumApproxLODs)
			{
				InitialTriCost *= CombineOptions.OptimizeLODLevelTriCostScale;
			}
			else
			{
				InitialTriCost += 0.25;		// TriCost is used as a power so if it gets too big things go badly...
			}

			// update enabled attribs (is this good?)
			ApproxGeo.ApproximateMeshLODs[k].EnableMatchingAttributes(*OptimizationSourceMesh);

			// recompute normals
			FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&ApproxGeo.ApproximateMeshLODs[k], ApproxGeo.ApproximateMeshLODs[k].Attributes()->PrimaryNormals(), AngleThresholdDeg);
			FMeshNormals::QuickRecomputeOverlayNormals(ApproxGeo.ApproximateMeshLODs[k]);
		}

		// try planar simplification for lower Source LODs, reducing triangle count in flat regions
		if (CombineOptions.bRetriangulateSourceLODs)
		{
			for (int32 SourceLODIndex = CombineOptions.StartRetriangulateSourceLOD; SourceLODIndex < NumSourceLODs; ++SourceLODIndex)
			{
				if (!(Part->bPreserveUVs || CombineOptions.bSimplifyPreserveUVs || (SourceLODIndex <= CombineOptions.PreserveUVLODLevel)))
				{
					PlanarRetriangulatePartMesh(
						SourceGeo.SourceMeshLODs[SourceLODIndex],
						CombineOptions.SimplifyBaseTolerance,
						AngleThresholdDeg);
				}
			}
		}

	}, (bVerbose) ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None );


	// try to filter out simplifications that did bad things
	ReplaceBadSimplifiedLODs(Assembly, CombineOptions);


	// Now that we have our per-part LOD stacks, we can estimate total triangle count that will be
	// used by the combined mesh. This will not be accurate due to hidden removal and face merging.
	// But we can, given a target triangle budget, try to "promote" simpler part approximations up
	// their individual LOD chains in an attempt to achieve a triangle budget. Generally the external
	// code will want to provide larger triangle counts, eg 40-50% larger, than the desired final
	// triangle count, to account for hidden removal and face merging. 

	int32 TotalNumLODs = CombineOptions.NumCopiedLODs + NumSimplifiedLODs + NumApproxLODs;
	int32 LODsToProcess = FMath::Min(CombineOptions.HardLODBudgets.Num(), TotalNumLODs);
	if (CombineOptions.bEnableBudgetStrategy_PartLODPromotion && LODsToProcess > 0)
	{
		struct FPartCostInfo
		{
			// these fields are precomputed
			int32 PartIndex = 0;			// index into Assembly.Parts
			int32 NumInstances = 0;			// number of instances of this part in the final mesh, currently *excluding* decorative parts

			TArray<FDynamicMesh3*> LODChainMeshes;	// flattened list of mesh pointers for [Source LODs][Simplified LODs][Approximate LODs]
			TArray<ECombinedLODType> LODChainMeshTypes;
			TArray<bool> LODHasUVs;

			// these fields are temporary storage updated during the algorilthm below
			int32 PartTriCount = 0;			// triangle count of the part for the active LOD
			int32 TotalTriCount = 0;		// total estimated triangle count for this part in the combined mesh 

			double ReplacedWeight = 1.0;	// reduce this as we shift LODs up, to try to let other comparable parts take the hit...

			double PartCostWeight() const 
			{
				if (NumInstances == 0 || PartTriCount <= 12) return 0.0;		// assume a box is min-cost and cannot be improved
				return (double)TotalTriCount * ReplacedWeight;
			}
		};
		TArray<FPartCostInfo> CostInfo;
		CostInfo.SetNum(NumParts);

		// initialize precomputed parts of the CostInfo array that we will incrementally update
		for (int32 SetIndex = 0; SetIndex < NumParts; ++SetIndex)
		{
			const TUniquePtr<FMeshPart>& Part = Assembly.Parts[SetIndex];
			CostInfo[SetIndex].PartIndex = SetIndex;

			CostInfo[SetIndex].NumInstances = 0;
			for (const FMeshPartInstance& Instance : Part->Instances)
			{
				bool bSkipInstance = (Instance.DetailLevel == EMeshDetailLevel::Decorative);
				if (bSkipInstance == false)
				{
					CostInfo[SetIndex].NumInstances++;
				}
			}

			int32 LODIndex = 0;
			for (FDynamicMesh3& SourceLODMesh : Assembly.SourceMeshGeometry[SetIndex].SourceMeshLODs)
			{
				CostInfo[SetIndex].LODChainMeshes.Add(&SourceLODMesh);
				CostInfo[SetIndex].LODChainMeshTypes.Add(ECombinedLODType::Copied);
				CostInfo[SetIndex].LODHasUVs.Add(true);
				LODIndex++;
			}
			for (FDynamicMesh3& SimplifiedLODMesh : Assembly.OptimizedMeshGeometry[SetIndex].SimplifiedMeshLODs)
			{
				CostInfo[SetIndex].LODChainMeshes.Add(&SimplifiedLODMesh);
				CostInfo[SetIndex].LODChainMeshTypes.Add(ECombinedLODType::Simplified);
				CostInfo[SetIndex].LODHasUVs.Add(
					Part->bPreserveUVs || CombineOptions.bSimplifyPreserveUVs || LODIndex <= CombineOptions.PreserveUVLODLevel);
				LODIndex++;
			}
			for (FDynamicMesh3& ApproximateLODMesh : Assembly.OptimizedMeshGeometry[SetIndex].ApproximateMeshLODs)
			{
				CostInfo[SetIndex].LODChainMeshes.Add(&ApproximateLODMesh);
				CostInfo[SetIndex].LODChainMeshTypes.Add(ECombinedLODType::Approximated);
				CostInfo[SetIndex].LODHasUVs.Add(false);
				LODIndex++;
			}
		}

		// for each LOD where we have a budget, while we are overbudget, do iterations of selecting
		// a "most expensive" part and promoting LODN+1 up to LODN for that part. Repeat until
		// budget is reached, or Max Iterations
		for (int32 LODIndex = 0; LODIndex < LODsToProcess; ++LODIndex)
		{
			int32 LODTriangleBudget = CombineOptions.HardLODBudgets[LODIndex] * CombineOptions.PartLODPromotionBudgetMultiplier;
			if (LODTriangleBudget <= 0) continue;

			// reset replaced weights that are incrementally updated below
			for (FPartCostInfo& PartCostInfo : CostInfo)
			{
				PartCostInfo.ReplacedWeight = 1.0;
			}

			int32 LastTotalCurLODTriCount = TMathUtil<int32>::SafeLargeValue;
			int32 MaxIters = 1000;
			int32 NoProgressIters = 0;
			for ( int32 NumIter = 0; NumIter < MaxIters; ++NumIter)
			{
				// compute current estimate of per-part and total tri counts for this LOD
				int32 TotalCurLODTriCount = 0;
				for (int32 SetIndex = 0; SetIndex < NumParts; ++SetIndex)
				{
					const FDynamicMesh3* CurSourcePartMeshLOD = CostInfo[SetIndex].LODChainMeshes[LODIndex];
					CostInfo[SetIndex].PartTriCount = CurSourcePartMeshLOD->TriangleCount();
					CostInfo[SetIndex].TotalTriCount = CostInfo[SetIndex].NumInstances * CostInfo[SetIndex].PartTriCount;
					TotalCurLODTriCount += CostInfo[SetIndex].TotalTriCount;
				}
				if (TotalCurLODTriCount == LastTotalCurLODTriCount)
				{
					NoProgressIters++;
				}
				else
				{
					NoProgressIters = 0;
				}
				if (bVerbose && (NumIter % 25 == 0))
				{
					UE_LOG(LogGeometry, Log, TEXT("    PartPromotion LOD %1d: Iter %4d  CurTris %6d LastTris %6d Budget %6d  NoProgress %d"), LODIndex, NumIter, TotalCurLODTriCount, LastTotalCurLODTriCount, LODTriangleBudget, NoProgressIters);
				}
				if (TotalCurLODTriCount < LODTriangleBudget || NoProgressIters > 25)
				{
					break;		// either we are within budget, or we made no progress for too long
				}
				LastTotalCurLODTriCount = TotalCurLODTriCount;

				// "No Progress" has to be allowed to occur for more than one step because it is often the case that the search gets 
				// "stuck" for a while, promoting a LODN+1 to LODN that have the same triangle count. This is quite common eg whenever
				// a simple box is reached in the LOD chain, as the entire LOD chain from that point will have the same triangle count. 
				// It also occurs because of the effect of the per-part ReplacedWeight, which makes an expensive part "cheaper" 
				// immediately after it is replaced. 
				// This could be improved by making the search below smarter, ie if bumping the max-cost part doesn't help, try
				// a different one. *However* note that because of how ReplacedWeight incrementally grows for each part over time,
				// it is generally the case that even if there are a few no-progress iterations, eventually the ReplacedWeight on
				// still-more-expensive parts will grow larger and progress is made again

				// find part with largest current cost
				int32 MaxSetIndex = 0;
				for (int32 k = 1; k < NumParts; ++k)
				{
					double MaxCost = CostInfo[MaxSetIndex].PartCostWeight();
					double CurCost = CostInfo[k].PartCostWeight();
					if (CurCost > MaxCost)
					{
						MaxSetIndex = k;
					}
				}
				int32 SetIndex = MaxSetIndex;

				// if our worst part is a box (CostWeight == 0), there is no point in replacing it
				if ( (CostInfo[SetIndex].PartCostWeight() > 0) 
					&& (LODIndex < CostInfo[SetIndex].LODChainMeshes.Num()-2) )	
				{
					FPartCostInfo& ReplaceInfo = CostInfo[SetIndex];

					ECombinedLODType PartCurLODType = ReplaceInfo.LODChainMeshTypes[LODIndex];
					ECombinedLODType PartNextLODType = ReplaceInfo.LODChainMeshTypes[LODIndex+1];

					// if we want to preserve UVs for a part, or for a LOD level, we cannot allow
					// a part with no UVs to be swapped in for a part that does have UVs. 
					bool bReplacementIsAllowed = true;
					bool bPartPreserveUVs = Assembly.Parts[ReplaceInfo.PartIndex]->bPreserveUVs;
					if (bPartPreserveUVs || LODIndex <= CombineOptions.PreserveUVLODLevel)
					{
						bool bCurHasUVs = ReplaceInfo.LODHasUVs[LODIndex];
						bool bNextHasUVs = ReplaceInfo.LODHasUVs[LODIndex+1];
						if (bCurHasUVs && bNextHasUVs == false)
						{
							bReplacementIsAllowed = false;
						}
					}

					if (bReplacementIsAllowed == false)
					{
						if (bVerbose)
						{
							UE_LOG(LogGeometry, Log, TEXT("    PartPromotion LOD %1d: Iter %4d  Disallowed Promoting Part %4d"),
								LODIndex, NumIter, SetIndex);
						}

						// if replacement is not allowed at this LOD, set a large negative weight so that we do not
						// consider this part again
						ReplaceInfo.ReplacedWeight = -9999.0;
					}
					else
					{
						if (bVerbose)
						{
							UE_LOG(LogGeometry, Log, TEXT("    PartPromotion LOD %1d: Iter %4d  Promoting Part %4d, from %5d tris to %5d tris (replacing type %d with type %d)"), 
								LODIndex, NumIter, SetIndex, ReplaceInfo.LODChainMeshes[LODIndex]->TriangleCount(), ReplaceInfo.LODChainMeshes[LODIndex+1]->TriangleCount(), 
								(int32)PartCurLODType, (int32)PartNextLODType);
						}

						// shift all meshes in the LOD chain down one slot
						int32 NumAllLODs = ReplaceInfo.LODChainMeshes.Num();
						for (int32 k = LODIndex; k < NumAllLODs - 1; ++k)
						{
							// if we want to preserve UVs for this part, or up to some LOD level, and the
							// next LOD has no UVs, we have to stop shifting
							if ((bPartPreserveUVs || k == CombineOptions.PreserveUVLODLevel)
								&& ReplaceInfo.LODHasUVs[k + 1] == false)
							{
								break;
							}

							*ReplaceInfo.LODChainMeshes[k] = *ReplaceInfo.LODChainMeshes[k + 1];
							ReplaceInfo.LODChainMeshTypes[k] = ReplaceInfo.LODChainMeshTypes[k + 1];
							ReplaceInfo.LODHasUVs[k] = ReplaceInfo.LODHasUVs[k + 1];
						}

						ReplaceInfo.ReplacedWeight *= 0.5;
					}
				}

				// slowly increase weights of parts   (should this be modulated by tri count?)
				for (int32 k = 0; k < NumParts; ++k)
				{
					CostInfo[k].ReplacedWeight += 0.1;
				}
			}
		}

	}

}


// Remove hidden faces by (approximately) computing Ambient Occlusion, fully occluded faces are hidden
static void RemoveHiddenFaces_Occlusion(FDynamicMesh3& EditMesh, double MaxDistance = 200)
{
	TRemoveOccludedTriangles<FDynamicMesh3> Jacket(&EditMesh);

	Jacket.InsideMode = UE::Geometry::EOcclusionCalculationMode::SimpleOcclusionTest;
	Jacket.TriangleSamplingMethod = UE::Geometry::EOcclusionTriangleSampling::Centroids;
	Jacket.WindingIsoValue = 0.5;
	Jacket.NormalOffset = FMathd::ZeroTolerance;
	Jacket.AddRandomRays = 25;
	Jacket.AddTriangleSamples = 100;
	//if (MaxDistance > 0)
	//{
	//	Jacket.MaxDistance = MaxDistance;
	//}

	TArray<FTransformSRT3d> NoTransforms;
	NoTransforms.Add(FTransformSRT3d::Identity());

	//  set up AABBTree and FWNTree lists
	FDynamicMeshAABBTree3 Spatial(&EditMesh);
	TArray<FDynamicMeshAABBTree3*> OccluderTrees; 
	OccluderTrees.Add(&Spatial);
		
	TFastWindingTree<FDynamicMesh3> FastWinding(&Spatial, false);
	TArray<TFastWindingTree<FDynamicMesh3>*> OccluderWindings; 
	OccluderWindings.Add(&FastWinding);

	Jacket.Select(NoTransforms, OccluderTrees, OccluderWindings, NoTransforms);
	
	if (Jacket.RemovedT.Num() > 0)
	{
		Jacket.RemoveSelected();
	}

	EditMesh.CompactInPlace();
}




// Remove hidden faces by casting rays from exterior at sample points on triangles
// (This method works quite well and should eventually be extracted out to a general algorithm...)
static void RemoveHiddenFaces_ExteriorVisibility(
	FDynamicMesh3& TargetMesh, 
	double SampleRadius, 
	bool bDoubleSided,
	int32 LODIndex)
{
	FDynamicMeshAABBTree3 Spatial(&TargetMesh, true);
	FAxisAlignedBox3d Bounds = Spatial.GetBoundingBox();
	double Radius = Bounds.DiagonalLength();

	// geometric magic numbers used below that have been slightly tuned...
	double GlancingAngleDotTolerance = FMathd::Cos(85.0 * FMathd::DegToRad);
	const double TriScalingAlpha = 0.999;
	const double BaryCoordsThreshold = 0.001;

	auto FindHitTriangleTest = [&](FVector3d TargetPosition, FVector3d TargetNormal, FVector3d FarPosition) -> int
	{
		FVector3d RayDir(TargetPosition - FarPosition);
		double Distance = Normalize(RayDir);
		if (bDoubleSided == false && RayDir.Dot(TargetNormal) > -0.001)
		{
			return IndexConstants::InvalidID;
		}
		FRay3d Ray(FarPosition, RayDir, true);
		return Spatial.FindNearestHitTriangle(Ray, IMeshSpatial::FQueryOptions(Distance + 1.0));	// 1.0 is random fudge factor here...
	};

	// final triangle visibility, atomics can be updated on any thread
	TArray<std::atomic<bool>> ThreadSafeTriVisible;
	ThreadSafeTriVisible.SetNum(TargetMesh.MaxTriangleID());
	for (int32 tid : TargetMesh.TriangleIndicesItr())
	{
		ThreadSafeTriVisible[tid] = false;
	}

	// array of (+/-)X/Y/Z directions
	TArray<FVector3d> CardinalDirections;
	for (int32 k = 0; k < 3; ++k)
	{
		FVector3d Direction(0,0,0);
		Direction[k] = 1.0;
		CardinalDirections.Add(Direction);
		CardinalDirections.Add(-Direction);
	}

	// TODO: it seems like a quite common failure case is triangles that are somewhat deeply
	// nested inside cavities/etc. A possible way to help here would be to essentially raytrace
	// orthographic images from top/bottom/etc. This could be done async and combined w/ the
	// visibilty determination below...


	// First pass. For each triangle, cast a ray at it's centroid from 
	// outside the model, along the X/Y/Z directions and tri normal.
	// If tri is hit we mark it as having 'known' status, allowing it
	// to be skipped in the more expensive pass below
	//
	TArray<bool> TriStatusKnown;
	TriStatusKnown.Init(false, TargetMesh.MaxTriangleID());
	ParallelFor(TargetMesh.MaxTriangleID(), [&](int32 tid)
	{
		FVector3d Normal, Centroid; double Area;
		TargetMesh.GetTriInfo(tid, Normal, Area, Centroid);
		if (Normal.SquaredLength() < 0.1 || Area <= FMathd::ZeroTolerance)
		{
			TriStatusKnown[tid] = true;
			return;
		}	

		for (FVector3d Direction : CardinalDirections)
		{
			// if direction is orthogonal to the triangle, hit-test is unstable, but even
			// worse, on rectilinear shapes (eg imagine some stacked cubes or adjacent parts)
			// the ray can get "through" the cracks between adjacent connected triangles
			// and manage to hit the search triangle
			if ( FMathd::Abs(Direction.Dot(Normal)) > GlancingAngleDotTolerance )
			{
				if (FindHitTriangleTest(Centroid, Normal, Centroid + Radius*Direction) == tid)
				{
					ThreadSafeTriVisible[tid] = true;
					TriStatusKnown[tid] = true;
					return;
				}
			}
		}
		if (FindHitTriangleTest(Centroid, Normal, Centroid + Radius*Normal) == tid)
		{
			ThreadSafeTriVisible[tid] = true;
			TriStatusKnown[tid] = true;
			return;
		}

		// triangle is not definitely visible or hidden
	});

	//
	// Construct set of exterior visibility test directions, below we will check if sample 
	// points on the mesh triangles are visible from the exterior along these directions.
	// Order is modulo-shuffled in hopes that for visible tris we don't waste a bunch
	// of time on the 'far' side
	int32 NumVisibilityTestDirections = 128;
	TSphericalFibonacci<double> SphereSampler(NumVisibilityTestDirections);
	TArray<FVector3d> VisibilityDirections;
	FModuloIteration ModuloIter(NumVisibilityTestDirections);
	uint32 DirectionIndex = 0;
	while (ModuloIter.GetNextIndex(DirectionIndex))
	{
		VisibilityDirections.Add( Normalized(SphereSampler[DirectionIndex]) );
	}
	// Fibonacci set generally does not include the cardinal directions, but they are highly useful to check
	VisibilityDirections.Append(CardinalDirections);
	NumVisibilityTestDirections = VisibilityDirections.Num();

	// For each triangle we will generate a set of sample points on the triangle surface,
	// and then check if that point is visible along any of the sample directions.
	// The number of sample points allocated to a triangle is based on it's area and the SampleRadius.
	// However for small triangles this may be < 1, so we will clamp to at least this many samples.
	// (this value should perhaps be relative to the mesh density, or exposed as a parameter...)
	const int32 MinTriSamplesPerSamplePoint = 8;

	// For each triangle, generate a set of sample points on the triangle surface,
	
	// This is the expensive part!
	ParallelFor(TargetMesh.MaxTriangleID(), [&](int32 tid)
	{
		// if we already found out this triangle is visible or hidden, we can skip it
		if ( TriStatusKnown[tid] || ThreadSafeTriVisible[tid] ) return;

		FVector3d A,B,C;
		TargetMesh.GetTriVertices(tid, A,B,C);
		FVector3d Centroid = (A + B + C) / 3.0;
		double TriArea;
		FVector3d TriNormal = VectorUtil::NormalArea(A, B, C, TriArea);		// TriStatusKnown should skip degen tris, do not need to check here

		FFrame3d TriFrame(Centroid, TriNormal);
		FTriangle2d UVTriangle(TriFrame.ToPlaneUV(A), TriFrame.ToPlaneUV(B), TriFrame.ToPlaneUV(C));

		// Slightly shrink the triangle, this helps to avoid spurious hits
		// TODO obviously should scale by an actual dimension and not just a relative %...
		FVector2d Center = (UVTriangle.V[0] + UVTriangle.V[1] + UVTriangle.V[2]) / 3.0;
		for (int32 k = 0; k < 3; ++k)
		{
			UVTriangle.V[k] = (1- TriScalingAlpha)*Center + (TriScalingAlpha)*UVTriangle.V[k];
		}

		double DiscArea = (FMathd::Pi * SampleRadius * SampleRadius);
		int NumSamples = FMath::Max( (int)(TriArea / DiscArea), MinTriSamplesPerSamplePoint);
		FVector2d V1 = UVTriangle.V[1] - UVTriangle.V[0];
		FVector2d V2 = UVTriangle.V[2] - UVTriangle.V[0];

		TArray<int32> HitTris;		// re-use this array in inner loop to avoid hitting atomics so often

		int NumTested = 0; int Iterations = 0;
		FRandomStream RandomStream(tid);
		while (NumTested < NumSamples && Iterations++ < 10000)
		{
			double a1 = RandomStream.GetFraction();
			double a2 = RandomStream.GetFraction();
			FVector2d PointUV = UVTriangle.V[0] + a1 * V1 + a2 * V2;
			if (UVTriangle.IsInside(PointUV))
			{
				NumTested++;
				FVector3d Position = TriFrame.FromPlaneUV(PointUV, 2);

				// cast ray from all exterior sample locations for this triangle sample point
				HitTris.Reset();
				for (int32 k = 0; k < NumVisibilityTestDirections; ++k)
				{
					FVector3d Direction = VisibilityDirections[k];
					if (FMathd::Abs(Direction.Dot(TriNormal)) < GlancingAngleDotTolerance)
					{
						continue;
					}

					FVector3d RayFrom = Position + 2.0 * Radius * VisibilityDirections[k];
					int32 HitTriID = FindHitTriangleTest(Position, TriNormal, RayFrom);
					if ( HitTriID != IndexConstants::InvalidID && TriStatusKnown[HitTriID] == false )
					{
						// Want to filter out on-edge triangle hits, as they are generally spurious and
						// will result in interior triangles remaining visible
						FRay3d Ray(RayFrom, Normalized(Position-RayFrom), true);
						FIntrRay3Triangle3d RayHit = TMeshQueries<FDynamicMesh3>::RayTriangleIntersection(TargetMesh, HitTriID, Ray);
						if (RayHit.IntersectionType == EIntersectionType::Point &&
							RayHit.TriangleBaryCoords.GetMin() > BaryCoordsThreshold &&
							RayHit.TriangleBaryCoords.GetMax() < (1.0-BaryCoordsThreshold) )
						{

							HitTris.AddUnique(HitTriID);		// we hit some triangle, whether or not it is the one we are testing...
							if (HitTriID == tid)
							{
								break;
							}
						}
					}
				}

				// mark any hit tris
				for ( int32 HitTriID : HitTris )
				{
					ThreadSafeTriVisible[HitTriID] = true;
				}

				// if our triangle has become visible (in this thread or another) we can terminate now
				if (ThreadSafeTriVisible[tid])
				{
					return;
				}
			}
		}

		// should we at any point lock and update TriStatusKnown?
	});

	// delete hidden tris
	TArray<int32> TrisToDelete;
	for (int32 tid : TargetMesh.TriangleIndicesItr())
	{
		if (ThreadSafeTriVisible[tid] == false)
		{
			TrisToDelete.Add(tid);
		}
	}
	FDynamicMeshEditor Editor(&TargetMesh);
	Editor.RemoveTriangles(TrisToDelete, true);

	TargetMesh.CompactInPlace();
}


// internal struct used in PostProcessHiddenFaceRemovedMesh
struct FMergeTriInfo
{
	int32 MaterialID = 0;
	FIndex3i ExternalGroupingID = FIndex3i::Zero();

	bool operator==(const FMergeTriInfo& Other) const { return MaterialID == Other.MaterialID && ExternalGroupingID == Other.ExternalGroupingID; }
	uint32 GetTypeHash() const
	{
		return HashCombineFast( ::GetTypeHash(MaterialID), FCrc::MemCrc_DEPRECATED(&ExternalGroupingID, sizeof(ExternalGroupingID)) );
	}
	friend uint32 GetTypeHash(const FMergeTriInfo& TriInfo)
	{
		return TriInfo.GetTypeHash();
	}
};


// Assuming SourcePartMesh is epsilon-planar, it's border polygons can be projected to a plane and
// remeshed using 2D triangulation to get the minimal triangle count. And once it is polygons,
// they can be boolean-unioned, topologically-closed, small holes can be removed, etc
// TODO: this code is very similar to ComputeSweptSolidApproximation and it would be nice if they could be combined
static void ComputePlanarPolygonApproximation(
	const FDynamicMesh3& SourcePartMesh,
	FDynamicMesh3& NewPlanarMesh,
	FVector3d Direction,
	double MergeOffset = 0.1,
	double SimplifyTolerance = 1.0,
	double MinHoleArea = 1.0)
{
	check(SourcePartMesh.IsCompactT());
	FFrame3d ProjectFrame(SourcePartMesh.GetTriCentroid(0), Direction);

	double MaxDistanceZ = 0.0;		// maximum distance between vertices and the projection plane

	FMeshBoundaryLoops Loops(&SourcePartMesh);
	FPlanarComplexd PlanarComplex;
	for (FEdgeLoop& Loop : Loops.Loops)
	{
		TArray<FVector3d> Vertices;
		Loop.GetVertices<FVector3d>(Vertices);
		FPolygon2d Polygon;
		for (FVector3d V : Vertices)
		{
			FVector LocalV = ProjectFrame.ToFramePoint(V);
			MaxDistanceZ = FMathd::Max(MaxDistanceZ, FMathd::Abs(LocalV.Z));
			Polygon.AppendVertex( FVector2d(LocalV.X, LocalV.Y) );
		}
		Polygon.Reverse();		// mesh orientation comes out backwards...
		PlanarComplex.Polygons.Add(MoveTemp(Polygon));
	}
	PlanarComplex.bTrustOrientations = true;		// have to do this or overlapping projections will create holes
	PlanarComplex.FindSolidRegions();
	TArray<FGeneralPolygon2d> Polygons = PlanarComplex.ConvertOutputToGeneralPolygons();

	if (Polygons.Num() == 0)
	{
		NewPlanarMesh = SourcePartMesh;
		return;
	}

	double UnionMergeOffset = 0.1;
	if (Polygons.Num() > 1)
	{
		// nudge all polygons outwards to ensure that when we boolean union exactly-coincident polygons
		// they intersect a bit, otherwise we may end up with zero-area cracks/holes
		if (UnionMergeOffset > 0)
		{
			for (FGeneralPolygon2d& Polygon : Polygons)
			{
				Polygon.VtxNormalOffset(UnionMergeOffset);
			}
		}

		TArray<FGeneralPolygon2d> ResultPolygons;
		PolygonsUnion(Polygons, ResultPolygons, true);
		Polygons = MoveTemp(ResultPolygons);

		if (UnionMergeOffset > 0)
		{
			for (FGeneralPolygon2d& Polygon : Polygons)
			{
				Polygon.VtxNormalOffset(-UnionMergeOffset);	// undo offset
			}
		}
	}

	// can optionally try to reduce polygon complexity by topological closure (dilate/erode)
	if (MergeOffset > 0)
	{
		TArray<FGeneralPolygon2d> TmpPolygons;
		PolygonsOffsets(MergeOffset, -MergeOffset,
			Polygons, TmpPolygons, true, 1.0,
			EPolygonOffsetJoinType::Square,
			EPolygonOffsetEndType::Polygon);

		Polygons = MoveTemp(TmpPolygons);
	}

	FConstrainedDelaunay2d Triangulator;
	for (FGeneralPolygon2d& Polygon : Polygons)
	{
		if (SimplifyTolerance > 0)
		{
			Polygon.Simplify(SimplifyTolerance, SimplifyTolerance * 0.25);		// 0.25 is kind of arbitrary here...
		}
		if (MinHoleArea > 0)
		{
			Polygon.FilterHoles([&](const FPolygon2d& HolePoly) { return HolePoly.Area() < MinHoleArea; });
		}
		Triangulator.Add(Polygon);
	}

	Triangulator.Triangulate([&Polygons](const TArray<FVector2d>& Vertices, FIndex3i Tri)
	{
		FVector2d Point = (Vertices[Tri.A] + Vertices[Tri.B] + Vertices[Tri.C]) / 3.0;
		for (const FGeneralPolygon2d& Polygon : Polygons)
		{
			if (Polygon.Contains(Point))
			{
				return true;
			}
		}
		return false;
	});

	FFlatTriangulationMeshGenerator TriangulationMeshGen;
	TriangulationMeshGen.Vertices2D = Triangulator.Vertices;
	TriangulationMeshGen.Triangles2D = Triangulator.Triangles;
	FDynamicMesh3 PolygonsMesh(&TriangulationMeshGen.Generate());

	if (PolygonsMesh.TriangleCount() < 2)
	{
		NewPlanarMesh = SourcePartMesh;
		return;
	}

	// map back to 3D plane
	MeshTransforms::FrameCoordsToWorld(PolygonsMesh, ProjectFrame);

	// todo trivially parallelizable...
	// since we projected the mesh to plane, we may have introduced some cracks. Unfortunately since
	// we did topological operations we cannot guarantee the verts are in the exact same place anymore, or
	// that we have the same vertices at all. So instead we will try to find a vertex inside an epsilon-ball,
	// and if found, we will snap to that location
	double SnapTolerance = 2.0 * MaxDistanceZ;		// maybe should be based on edge length instead?
	if (MaxDistanceZ > FMathd::ZeroTolerance)
	{
		for (int32 vid : PolygonsMesh.VertexIndicesItr())
		{
			FVector3d Pos = PolygonsMesh.GetVertex(vid);

			FVector3d NearestOriginalPos = Pos;
			double NearestDistSqr = 2.0 * SnapTolerance;
			for (FVector3d OrigPos : SourcePartMesh.VerticesItr())
			{
				double DistSqr = DistanceSquared(Pos, OrigPos);
				if (DistSqr < NearestDistSqr)
				{
					NearestDistSqr = DistSqr;
					NearestOriginalPos = OrigPos;
				}
			}
			if (NearestDistSqr < SnapTolerance * SnapTolerance)
			{
				PolygonsMesh.SetVertex(vid, NearestOriginalPos);
			}
		}
	}


	NewPlanarMesh = MoveTemp(PolygonsMesh);
}



// Find sets of triangles that lie in the same 3D plane on TargetMesh,
// and then extract out those areas, pull out the boundary polygons,
// union them together, and do a 2D polygon-with-holes triangulation.
static void RetriangulatePlanarFacePolygons(FDynamicMesh3& TargetMesh, double BaseGeometricTolerance)
{
	TArray<FFrame3d> PlaneSet;

	// plane detection tolerances
	const double AngleDotTol = 0.99;
	const double DistanceTol = BaseGeometricTolerance * 0.05;

	if (TargetMesh.IsCompactT() == false)
	{
		TargetMesh.CompactInPlace();
	}

	double InitialArea = TMeshQueries<FDynamicMesh3>::GetVolumeArea(TargetMesh).Y;

	TArray<int32> TriPlaneID;
	TriPlaneID.Init(-1, TargetMesh.MaxTriangleID());

	auto SamePlaneCheck = [&](FFrame3d& Frame, FVector3d TriNormal, FVector3d TriCentroid)
	{
		FVector3d PlaneNormal = Frame.Z();
		if (PlaneNormal.Dot(TriNormal) < AngleDotTol) return false;
		FVector3d LocalVec = TriCentroid - Frame.Origin;
		double SignedDist = LocalVec.Dot(PlaneNormal);
		if (FMathd::Abs(SignedDist) > DistanceTol) return false;
		return true;
	};

	// accumulate set of unique planes. Would be nice if plane search could be
	// done more efficiently but there is not an obvious spatial data structure...
	for (int32 tid : TargetMesh.TriangleIndicesItr())
	{
		FVector3d TriNormal, Centroid; double Area;
		TargetMesh.GetTriInfo(tid, TriNormal, Area, Centroid);
		FFrame3d TriPlane(Centroid, TriNormal);

		bool bFound = false;
		for (int32 k = 0; k < PlaneSet.Num() && bFound == false; ++k)
		{
			if (SamePlaneCheck(PlaneSet[k], TriNormal, Centroid))
			{
				TriPlaneID[tid] = k;
				bFound = true;
			}
		}

		if (bFound == false)
		{
			TriPlaneID[tid] = PlaneSet.Num();
			PlaneSet.Add(TriPlane);
		}
	}
	if (PlaneSet.Num() < 2)
	{
		return;
	}

	// if we have vertex colors, transfer them to new meshes by finding a value at nearest vertex
	bool bTargetHasAttributes = TargetMesh.HasAttributes();
	FDynamicMeshColorOverlay* SourceColors = bTargetHasAttributes ? TargetMesh.Attributes()->PrimaryColors() : nullptr;
	FDynamicMeshAABBTree3 TargetMeshSpatial(&TargetMesh, (SourceColors != nullptr));

	// TODO: we do not actually have to split here, we can just send the triangle ROI to ComputePlanarPolygonApproximation and
	// it can use RegionBoundaryLoops. Then can delete and append new mesh if it is better (although messier to parallelize)
	TArray<FDynamicMesh3> SplitMeshes;
	FDynamicMeshEditor::SplitMesh(&TargetMesh, SplitMeshes, [&](int32 tid) { return TriPlaneID[tid]; });

	for (FDynamicMesh3& Mesh : SplitMeshes)		// ought to be trivially parallelizable...
	{
		if (Mesh.TriangleCount() <= 2) continue;		// technically even 2 tris might form a single triangle...

		const double MinHoleArea = 10.0;

		// planar areas can be very small, if we use full tolerance then they may end up being
		// partially collapsed by topological operations, and creating cracks/holes. Deriving from
		// area of submesh helps to prevent this, but results in too large of values for large areas,
		// so clamp to geometric tolerance.
		FVector2d VolumeArea = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh);
		double EstSquareEdgeLen = FMathd::Sqrt(VolumeArea.Y);
		double MergeOffset = FMath::Min(EstSquareEdgeLen * 0.02, BaseGeometricTolerance);
		double SimplifyTolerance = FMath::Min(MergeOffset, BaseGeometricTolerance);

		// ideally this is better than just using one arbitrary triangle normal
		FVector3d AverageNormal = FVector3d::Zero();
		for (int32 tid : Mesh.TriangleIndicesItr())
		{
			AverageNormal += Mesh.GetTriNormal(tid);
		}
		AverageNormal.Normalize();

		// subset of triangles can easily have bowties, and since we are going to be using boundary loops
		// this can lead to messy situations, so just split any bowties
		FDynamicMeshEditor BowtieSplitter(&Mesh);
		FDynamicMeshEditResult TmpEditResult;
		BowtieSplitter.SplitBowties(TmpEditResult);

		FDynamicMesh3 NewPlanarMesh;
		ComputePlanarPolygonApproximation(Mesh, NewPlanarMesh,
			AverageNormal, MergeOffset, SimplifyTolerance, MinHoleArea);

		// only take this new mesh if we actually improved the situation
		if (NewPlanarMesh.TriangleCount() < Mesh.TriangleCount())
		{
			Mesh = MoveTemp(NewPlanarMesh);
			if (!bTargetHasAttributes) continue;

			Mesh.EnableAttributes();
			Mesh.Attributes()->SetNumUVLayers(0);
			FMeshNormals::InitializeOverlayToPerVertexNormals(Mesh.Attributes()->PrimaryNormals(), false);

			// project source colors to new mesh vertices
			if (SourceColors)
			{
				Mesh.Attributes()->EnablePrimaryColors();
				FDynamicMeshColorOverlay* SetColors = Mesh.Attributes()->PrimaryColors();
				TArray<int32> VertexToElementMap; 
				VertexToElementMap.SetNum(Mesh.MaxVertexID());
				for (int32 vid : Mesh.VertexIndicesItr())
				{
					double NearestDistSqr = TNumericLimits<double>::Max();
					int32 TargetVID = TargetMeshSpatial.FindNearestVertex(Mesh.GetVertex(vid), NearestDistSqr);
					FVector4f UseColor = FVector4f::Zero();
					SourceColors->EnumerateVertexElements(TargetVID, [&](int TriID, int ElemID, const FVector4f& ElemColor) { UseColor = ElemColor; return false; }, false);
					VertexToElementMap[vid] = SetColors->AppendElement(UseColor);
				}
				for (int32 tid : Mesh.TriangleIndicesItr())
				{
					FIndex3i Triangle = Mesh.GetTriangle(tid);
					SetColors->SetTriangle(tid, FIndex3i(VertexToElementMap[Triangle.A], VertexToElementMap[Triangle.B], VertexToElementMap[Triangle.C]));
				}
			}
		}
	}

	FDynamicMesh3 NewMesh;
	if (bTargetHasAttributes)
	{
		NewMesh.EnableMatchingAttributes(TargetMesh);
	}

	FDynamicMeshEditor Editor(&NewMesh);
	for (FDynamicMesh3& Mesh : SplitMeshes)
	{
		FMeshIndexMappings Mappings;
		Editor.AppendMesh(&Mesh, Mappings);
	}

	if (NewMesh.TriangleCount() == 0)
	{
		return;
	}

	// Sanity check that we have not dramatically changed the mesh area. Some
	// area change is expected because of (eg) filling holes, merging, etc,
	// so the tolerance here is quite large and mainly intended to catch catastrophic failures
	double FinalArea = TMeshQueries<FDynamicMesh3>::GetVolumeArea(NewMesh).Y;
	if (FinalArea < 0.5 * InitialArea)
	{
		return;
	}

	// currently assuming input mesh has been split by MaterialID, so no projection here,
	// just use any MaterialID
	if (bTargetHasAttributes)
	{
		if (const FDynamicMeshMaterialAttribute* SourceMaterialIDs = TargetMesh.Attributes()->GetMaterialID())
		{
			if (FDynamicMeshMaterialAttribute* TargetMaterialIDs = NewMesh.Attributes()->GetMaterialID())
			{
				int32 ConstantMaterialID = SourceMaterialIDs->GetValue(0);
				for (int32 tid : NewMesh.TriangleIndicesItr())
				{
					TargetMaterialIDs->SetValue(tid, ConstantMaterialID);
				}
			}
		}
	}

	TargetMesh = MoveTemp(NewMesh);
}


//
// After hidden face removal, a mesh can often be optimized to at least save some vertices (by welding open borders),
// and then in some cases, now-connected triangle areas can be retriangulated to require fewer triangles.
// The latter is really only possible if UVs/Normal seams are not involved, and generally such merging
// of areas needs to be prevented between different Material regions.
// To support Materials that define different material regions internally (eg indexed colors encoded in vertex colors,
// custom primitive data, etc) a function is provided to allow external code to provide 3 "unique triangle group" integers.
// All integers must match for a triangle regions to be merged for retriangulation.
//
static void PostProcessHiddenFaceRemovedMesh(
	FDynamicMesh3& TargetMesh, 
	double BaseGeometricTolerance,
	bool bTryToMergeFaces,
	bool bApplyPlanarRetriangulation,
	TFunctionRef<FIndex3i(const FDynamicMesh3& Mesh, int32 TriangleID)> GetTriangleGroupingIDFunc,
	TSet<int32>* SkipMaterialIDs = nullptr)
{
	bool bVerbose = CVarGeometryCombineMeshInstancesVerbose.GetValueOnAnyThread();

	// weld edges in case input was unwelded...
	{
		// currently FMergeCoincidentMeshEdges can break the mesh if it has bowties, remove
		// them to work around the issue
		FDynamicMeshEditor MeshEditor(&TargetMesh);
		FDynamicMeshEditResult EditResult;
		MeshEditor.SplitBowties(EditResult);

		FMergeCoincidentMeshEdges Welder(&TargetMesh);
		Welder.MergeVertexTolerance = BaseGeometricTolerance * 0.01;
		Welder.OnlyUniquePairs = false;
		Welder.bWeldAttrsOnMergedEdges = true;
		Welder.Apply();
	}

	if (!bTryToMergeFaces)
	{
		TargetMesh.CompactInPlace();
		return;
	}

	bool bTargetHasAttributes = TargetMesh.HasAttributes();
	const FDynamicMeshMaterialAttribute* MaterialIDs =
		(bTargetHasAttributes && TargetMesh.Attributes()->HasMaterialID()) ? TargetMesh.Attributes()->GetMaterialID() : nullptr;
	
	TMap<FMergeTriInfo, int> UniqueMatIndices;
	TArray<int32> TriSortIndex;
	TriSortIndex.SetNum(TargetMesh.MaxTriangleID());
	for (int32 tid : TargetMesh.TriangleIndicesItr())
	{
		FMergeTriInfo TriInfo;
		TriInfo.MaterialID = (MaterialIDs) ? MaterialIDs->GetValue(tid) : -1;
		TriInfo.ExternalGroupingID = GetTriangleGroupingIDFunc(TargetMesh, tid);

		int32* Found = UniqueMatIndices.Find(TriInfo);
		if (Found == nullptr)
		{
			int32 NewIndex = UniqueMatIndices.Num();
			UniqueMatIndices.Add(TriInfo, NewIndex);
			TriSortIndex[tid] = NewIndex;
		}
		else
		{
			TriSortIndex[tid] = *Found;
		}
	}


	TArray<FDynamicMesh3> SplitMeshes;
	if ( UniqueMatIndices.Num() == 1 )
	{
		SplitMeshes.Add(MoveTemp(TargetMesh));
	}
	else
	{
		FDynamicMeshEditor::SplitMesh(&TargetMesh, SplitMeshes, [&](int32 tid) { return TriSortIndex[tid]; });
	}

	for (FDynamicMesh3& SubRegionMesh : SplitMeshes)
	{
		// we split by MaterialID, so we can check any triangle against the SkipMaterialID list
		if (SkipMaterialIDs != nullptr && SubRegionMesh.HasAttributes() && SubRegionMesh.Attributes()->HasMaterialID())
		{
			int32 MaterialID = SubRegionMesh.Attributes()->GetMaterialID()->GetValue(0);
			if (SkipMaterialIDs->Contains(MaterialID))
			{
				continue;
			}
		}

		// resolving T-junctions tends to make things worse...
		//FMeshResolveTJunctions Resolver(&SubRegionMesh);
		//Resolver.DistanceTolerance = 0.01;
		//Resolver.Apply();

		// try weld again just in case
		FMergeCoincidentMeshEdges Welder(&SubRegionMesh);
		Welder.MergeVertexTolerance = BaseGeometricTolerance * 0.01;
		Welder.OnlyUniquePairs = false;
		Welder.bWeldAttrsOnMergedEdges = true;
		Welder.Apply();

		// although we split bowties above, we have now pulled out submeshes which may have created more bowties
		FDynamicMeshEditor BowtieSplitter(&SubRegionMesh);
		FDynamicMeshEditResult TmpEditResult;
		BowtieSplitter.SplitBowties(TmpEditResult);
		if (SubRegionMesh.HasAttributes())
		{
			SubRegionMesh.Attributes()->SplitAllBowties();
		}
		SubRegionMesh.CompactInPlace();

		// simplify to planar
		FQEMSimplification Simplifier(&SubRegionMesh);
		Simplifier.bAllowSeamCollapse = true;
		Simplifier.bRetainQuadricMemory = false;

		EEdgeRefineFlags BoundaryConstraint = EEdgeRefineFlags::NoFlip;

		// set up constraints, necessary to avoid crashing in presence of attributes
		FMeshConstraints Constraints;
		FMeshConstraintsUtil::ConstrainAllBoundariesAndSeams(Constraints, SubRegionMesh,
			BoundaryConstraint, EEdgeRefineFlags::NoConstraint, EEdgeRefineFlags::NoConstraint, true, false, Simplifier.bAllowSeamCollapse);
		Simplifier.SetExternalConstraints(Constraints);
		// need to transfer constraint setting to the simplifier, these are used to update the constraints as edges collapse.
		Simplifier.MeshBoundaryConstraint = BoundaryConstraint;
		Simplifier.GroupBoundaryConstraint = EEdgeRefineFlags::NoConstraint;
		Simplifier.MaterialBoundaryConstraint = EEdgeRefineFlags::NoConstraint;

		Simplifier.CollapseMode = FQEMSimplification::ESimplificationCollapseModes::AverageVertexPosition;
		Simplifier.SimplifyToMinimalPlanar( 0.01 );

		// minimal-planar simplification is good, but for planar areas, we can go further and extract 2D
		// polygons that can be delaunay-triangulated. This is currently somewhat expensive...
		if (bApplyPlanarRetriangulation)
		{
			RetriangulatePlanarFacePolygons(SubRegionMesh, BaseGeometricTolerance);
		}
	}

	TargetMesh.Clear();
	if (bTargetHasAttributes)
	{
		TargetMesh.EnableMatchingAttributes(SplitMeshes[0], true, true);
	}
	FDynamicMeshEditor Editor(&TargetMesh);
	for (FDynamicMesh3& SubRegionMesh : SplitMeshes)
	{
		FMeshIndexMappings Mappings;
		Editor.AppendMesh(&SubRegionMesh, Mappings);
	}

	// weld edges back together again
	{
		// currently FMergeCoincidentMeshEdges can break the mesh if it has bowties, remove
		// them to work around the issue
		FDynamicMeshEditor MeshEditor(&TargetMesh);
		FDynamicMeshEditResult EditResult;
		MeshEditor.SplitBowties(EditResult);

		FMergeCoincidentMeshEdges Welder(&TargetMesh);
		Welder.MergeVertexTolerance = BaseGeometricTolerance * 0.01;
		Welder.OnlyUniquePairs = false;
		Welder.bWeldAttrsOnMergedEdges = true;
		Welder.Apply();
	}


	// make sure we have necessary attribute sets
	if (bTargetHasAttributes && TargetMesh.Attributes()->NumUVLayers() == 0)
	{
		TargetMesh.Attributes()->SetNumUVLayers(1);
	}

	TargetMesh.CompactInPlace();

	if ( bVerbose )
	{
		UE_LOG(LogGeometry, Log, TEXT("    Merge Faces           [Tris %6d Verts %6d]"), TargetMesh.TriangleCount(), TargetMesh.VertexCount());
	}
}



namespace UE::Geometry
{




static void ComputeVoxWrapMesh(
	const FDynamicMesh3& CombinedMesh, 
	FDynamicMeshAABBTree3& CombinedMeshSpatial,
	FDynamicMesh3& ResultMesh,
	double ClosureDistance,
	double& TargetCellSizeInOut)
{
	TImplicitMorphology<FDynamicMesh3> Morphology;
	Morphology.Source = &CombinedMesh;
	Morphology.SourceSpatial = &CombinedMeshSpatial;
	Morphology.MorphologyOp = TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Close;
	Morphology.Distance = FMath::Max(ClosureDistance, 0.001);

	FAxisAlignedBox3d Bounds = CombinedMeshSpatial.GetBoundingBox();
	double UseCellSize = FMath::Max(0.001, TargetCellSizeInOut);
	int MaxGridDimEstimate = (int32)(Bounds.MaxDim() / UseCellSize);
	if (MaxGridDimEstimate > 256)
	{
		UseCellSize = (float)Bounds.MaxDim() / 256;
	}
	Morphology.GridCellSize = UseCellSize;
	Morphology.MeshCellSize = UseCellSize;
	TargetCellSizeInOut = UseCellSize;

	ResultMesh.Copy(&Morphology.Generate());
	ResultMesh.DiscardAttributes();
}

static void ComputeSimplifiedVoxWrapMesh(
	FDynamicMesh3& VoxWrapMesh,
	const FDynamicMesh3* CombinedMesh, 
	FDynamicMeshAABBTree3* CombinedMeshSpatial,
	double SimplifyTolerance,
	double MaxTriCount)
{
	FVolPresMeshSimplification Simplifier(&VoxWrapMesh);

	Simplifier.ProjectionMode = FVolPresMeshSimplification::ETargetProjectionMode::NoProjection;

	//FMeshProjectionTarget ProjectionTarget(&CombinedMesh, &CombinedMeshSpatial);
	//Simplifier.SetProjectionTarget(&ProjectionTarget);

	Simplifier.DEBUG_CHECK_LEVEL = 0;
	Simplifier.bRetainQuadricMemory = false; 

	//Simplifier.GeometricErrorConstraint = FVolPresMeshSimplification::EGeometricErrorCriteria::PredictedPointToProjectionTarget;
	//Simplifier.GeometricErrorTolerance = SimplifyTolerance;

	//Simplifier.SimplifyToTriangleCount( 1 );

	if (VoxWrapMesh.TriangleCount() > MaxTriCount)
	{
		//Simplifier.SetProjectionTarget(nullptr);
		//Simplifier.GeometricErrorConstraint = FVolPresMeshSimplification::EGeometricErrorCriteria::None;
		Simplifier.SimplifyToTriangleCount( MaxTriCount );
	}

	VoxWrapMesh.CompactInPlace();
}


/**
 * Computes best cardinal-axis swept-solid approximation to CombinedMesh and returns in ResultMesh.
 * The swept-solid approximation is found by flattening ResultMesh along that axis, and then doing
 * polygon booleans and topological closure. The various simplification parameters are derived from
 * the ClosureDistance parameter.
 */
static void ComputeBestFullProjectionMesh(
	const FDynamicMesh3& CombinedMesh,
	FDynamicMeshAABBTree3& CombinedMeshSpatial,
	FDynamicMesh3& ResultMesh,
	double ClosureDistance)
{
	TArray<FVector3d, TInlineAllocator<3>> Directions;
	Directions.Add(FVector3d::UnitZ());
	Directions.Add(FVector3d::UnitX());
	Directions.Add(FVector3d::UnitY());
	int32 N = Directions.Num();

	TArray<FDynamicMesh3, TInlineAllocator<3>> DirectionMeshes;
	DirectionMeshes.SetNum(N);

	TArray<FVector2d> DeviationMeasures;
	DeviationMeasures.SetNum(N);

	ParallelFor(Directions.Num(), [&](int32 k)
	{
		FDynamicMesh3& UseMesh = DirectionMeshes[k];
		FVector3d UseDirection = Directions[k];
		ComputeSweptSolidApproximation(CombinedMesh, UseMesh, UseDirection,
			ClosureDistance, ClosureDistance / 4, 4.0f * ClosureDistance * ClosureDistance);
		UseMesh.DiscardAttributes();

		// simplify to planar
		FQEMSimplification Simplifier(&UseMesh);

		Simplifier.CollapseMode = FQEMSimplification::ESimplificationCollapseModes::MinimalExistingVertexError;
		// no constraints as we discarded attributes
		Simplifier.SimplifyToMinimalPlanar(ClosureDistance/2);

		DeviationMeasures[k] = ComputeGeometricDeviation(UseMesh, CombinedMeshSpatial);
	});

	// select option w/ smallest max geometric deviation
	double MinMaxDistance = TNumericLimits<double>::Max();
	int32 UseIndex = 0;
	for (int32 k = 0; k < N; ++k)
	{
		if (DeviationMeasures[k].Y < MinMaxDistance)
		{
			MinMaxDistance = DeviationMeasures[k].Y;
			UseIndex = k;
		}
	}

	ResultMesh = MoveTemp(DirectionMeshes[UseIndex]);
	ResultMesh.CompactInPlace();
}




/**
 * Computes best cardinal-axis swept-solid approximation to CombinedMesh and returns in ResultMesh.
 * The swept-solid approximation is found by flattening ResultMesh along that axis, and then doing
 * polygon booleans and topological closure. The various simplification parameters are derived from
 * the ClosureDistance parameter.
 */
static void ComputeProjectionMeshIntersection(
	const FDynamicMesh3& CombinedMesh,
	FDynamicMeshAABBTree3& CombinedMeshSpatial,
	FDynamicMesh3& ResultMesh,
	double ClosureDistance)
{
	TArray<FVector3d, TInlineAllocator<3>> Directions;
	Directions.Add(FVector3d::UnitZ());
	Directions.Add(FVector3d::UnitX());
	Directions.Add(FVector3d::UnitY());
	int32 N = Directions.Num();

	TArray<FDynamicMesh3, TInlineAllocator<3>> DirectionMeshes;
	DirectionMeshes.SetNum(N);

	TArray<FVector2d> DeviationMeasures;
	DeviationMeasures.SetNum(N);

	ParallelFor(Directions.Num(), [&](int32 k)
	{
		FDynamicMesh3& UseMesh = DirectionMeshes[k];
		FVector3d UseDirection = Directions[k];
		ComputeSweptSolidApproximation(CombinedMesh, UseMesh, UseDirection,
			ClosureDistance, ClosureDistance / 4, 4.0f * ClosureDistance * ClosureDistance);
		UseMesh.DiscardAttributes();

		// simplify to planar
		FQEMSimplification Simplifier(&UseMesh);

		Simplifier.CollapseMode = FQEMSimplification::ESimplificationCollapseModes::MinimalExistingVertexError;
		// no constraints as we discarded attributes
		Simplifier.SimplifyToMinimalPlanar(ClosureDistance/2);

		DeviationMeasures[k] = ComputeGeometricDeviation(UseMesh, CombinedMeshSpatial);
	});

	// intersect
	FMeshBoolean MeshBoolean1(
		&DirectionMeshes[0], FTransformSRT3d::Identity(),
		&DirectionMeshes[1], FTransformSRT3d::Identity(),
		&ResultMesh, FMeshBoolean::EBooleanOp::Intersect);
	MeshBoolean1.bPutResultInInputSpace = true;
	MeshBoolean1.bSimplifyAlongNewEdges = true;
	MeshBoolean1.Compute();
	
	FDynamicMesh3 TmpMesh;
	FMeshBoolean MeshBoolean2(
		&ResultMesh, FTransformSRT3d::Identity(),
		&DirectionMeshes[2], FTransformSRT3d::Identity(),
		&TmpMesh, FMeshBoolean::EBooleanOp::Intersect);
	MeshBoolean2.bPutResultInInputSpace = true;
	MeshBoolean2.bSimplifyAlongNewEdges = true;
	MeshBoolean2.Compute();

	ResultMesh = MoveTemp(TmpMesh);

	//RetriangulatePlanarFacePolygons(ResultMesh);

	//FMergeCoincidentMeshEdges Welder(&ResultMesh);
	//Welder.MergeVertexTolerance = ClosureDistance/5;
	//Welder.OnlyUniquePairs = false;
	//Welder.bWeldAttrsOnMergedEdges = true;
	//Welder.Apply();

	//FQEMSimplification Simplifier2(&ResultMesh);
	//Simplifier2.CollapseMode = FQEMSimplification::ESimplificationCollapseModes::MinimalExistingVertexError;
	//Simplifier2.SimplifyToEdgeLength(ClosureDistance);

	PostProcessHiddenFaceRemovedMesh(ResultMesh, 1.0, true, true,
		[](const FDynamicMesh3& Mesh, int32 TriangleID) { return FIndex3i::Zero(); });

	ResultMesh.CompactInPlace();
}



template<typename SimplificationType>
void DoSimplifyMesh(
	FDynamicMesh3& EditMesh,
	int32 TargetTriCount,
	FMeshProjectionTarget* ProjectionTarget = nullptr,
	double GeometricTolerance = 0)
{
	SimplificationType Simplifier(&EditMesh);

	Simplifier.ProjectionMode = SimplificationType::ETargetProjectionMode::NoProjection;
	if (ProjectionTarget != nullptr)
	{
		Simplifier.SetProjectionTarget(ProjectionTarget);
	}

	Simplifier.DEBUG_CHECK_LEVEL = 0;
	Simplifier.bRetainQuadricMemory = true;
	Simplifier.bAllowSeamCollapse = true;
	//if (Options.bAllowSeamCollapse)		// always true
	{
		Simplifier.SetEdgeFlipTolerance(1.e-5);
		if (EditMesh.HasAttributes())
		{
			EditMesh.Attributes()->SplitAllBowties();	// eliminate any bowties that might have formed on attribute seams.
		}
	}

	// If we allow boundary collapse, this can introduce visible holes in the simplified result
	constexpr bool bAllowBoundaryCollapse = false;
	EEdgeRefineFlags MeshBoundaryConstraints = bAllowBoundaryCollapse ? EEdgeRefineFlags::NoFlip : EEdgeRefineFlags::SplitsOnly;
	EEdgeRefineFlags GroupBorderConstraints = EEdgeRefineFlags::NoConstraint;
	EEdgeRefineFlags MaterialBorderConstraints = EEdgeRefineFlags::NoConstraint;

	FMeshConstraints Constraints;
	FMeshConstraintsUtil::ConstrainAllBoundariesAndSeams(Constraints, EditMesh,
		MeshBoundaryConstraints, GroupBorderConstraints, MaterialBorderConstraints,
		true, false, true);
	Simplifier.SetExternalConstraints(MoveTemp(Constraints));

	if (ProjectionTarget != nullptr && GeometricTolerance > 0)
	{
		Simplifier.GeometricErrorConstraint = SimplificationType::EGeometricErrorCriteria::PredictedPointToProjectionTarget;
		Simplifier.GeometricErrorTolerance = GeometricTolerance;
	}

	Simplifier.SimplifyToTriangleCount(FMath::Max(1, TargetTriCount));

	EditMesh.CompactInPlace();
}




static void ComputeVoxWrapMeshAutoUV(FDynamicMesh3& EditMesh)
{
	check(EditMesh.IsCompact());
	check(EditMesh.HasAttributes());

	FDynamicMeshUVEditor UVEditor(&EditMesh, 0, true);
	FDynamicMeshUVOverlay* UVOverlay = UVEditor.GetOverlay();

	const bool bFixOrientation = false;
	//const bool bFixOrientation = true;
	//FDynamicMesh3 FlippedMesh(EMeshComponents::FaceGroups);
	//FlippedMesh.Copy(Mesh, false, false, false, false);
	//if (bFixOrientation)
	//{
	//	FlippedMesh.ReverseOrientation(false);
	//}

	int32 NumVertices = EditMesh.VertexCount();
	TArray<FVector3f> VertexBuffer;
	VertexBuffer.SetNum(NumVertices);
	for (int32 k = 0; k < NumVertices; ++k)
	{
		VertexBuffer[k] = (FVector3f)EditMesh.GetVertex(k);
	}

	TArray<int32> IndexBuffer;
	IndexBuffer.Reserve(EditMesh.TriangleCount() * 3);
	for (FIndex3i Triangle : EditMesh.TrianglesItr())
	{
		IndexBuffer.Add(Triangle.A);
		IndexBuffer.Add(Triangle.B);
		IndexBuffer.Add(Triangle.C);
	}

	TArray<FVector2D> UVVertexBuffer;
	TArray<int32>     UVIndexBuffer;
	TArray<int32>     VertexRemapArray; // This maps the UV vertices to the original position vertices.  Note multiple UV vertices might share the same positional vertex (due to UV boundaries)
	XAtlasWrapper::XAtlasChartOptions ChartOptions;
	ChartOptions.MaxIterations = 1;
	XAtlasWrapper::XAtlasPackOptions PackOptions;
	bool bSuccess = XAtlasWrapper::ComputeUVs(IndexBuffer, VertexBuffer, ChartOptions, PackOptions,
		UVVertexBuffer, UVIndexBuffer, VertexRemapArray);
	if (bSuccess == false)
	{
		return;
	}

	UVOverlay->ClearElements();

	int32 NumUVs = UVVertexBuffer.Num();
	TArray<int32> UVOffsetToElID;  UVOffsetToElID.Reserve(NumUVs);
	for (int32 i = 0; i < NumUVs; ++i)
	{
		FVector2D UV = UVVertexBuffer[i];
		const int32 VertOffset = VertexRemapArray[i];		// The associated VertID in the dynamic mesh
		const int32 NewID = UVOverlay->AppendElement(FVector2f(UV));		// add the UV to the mesh overlay
		UVOffsetToElID.Add(NewID);
	}

	int32 NumUVTris = UVIndexBuffer.Num() / 3;
	for (int32 i = 0; i < NumUVTris; ++i)
	{
		int32 t = i * 3;
		FIndex3i UVTri(UVIndexBuffer[t], UVIndexBuffer[t + 1], UVIndexBuffer[t + 2]);	// The triangle in UV space
		FIndex3i TriVertIDs;				// the triangle in terms of the VertIDs in the DynamicMesh
		for (int c = 0; c < 3; ++c)
		{
			int32 Offset = VertexRemapArray[UVTri[c]];		// the offset for this vertex in the LinearMesh
			TriVertIDs[c] = Offset;
		}

		// NB: this could be slow.. 
		int32 TriID = EditMesh.FindTriangle(TriVertIDs[0], TriVertIDs[1], TriVertIDs[2]);
		if (TriID != IndexConstants::InvalidID)
		{
			FIndex3i ElTri = (bFixOrientation) ?
				FIndex3i(UVOffsetToElID[UVTri[1]], UVOffsetToElID[UVTri[0]], UVOffsetToElID[UVTri[2]])
				: FIndex3i(UVOffsetToElID[UVTri[0]], UVOffsetToElID[UVTri[1]], UVOffsetToElID[UVTri[2]]);
			UVOverlay->SetTriangle(TriID, ElTri);
		}
	}
}








static void ComputeMissingUVs(FDynamicMesh3& EditMesh)
{
	check(EditMesh.IsCompact());
	check(EditMesh.HasAttributes());

	FDynamicMeshUVEditor UVEditor(&EditMesh, 0, true);
	FDynamicMeshUVOverlay* UVOverlay = UVEditor.GetOverlay();

	TArray<int32> UnsetTriangles;
	for (int32 tid : EditMesh.TriangleIndicesItr())
	{
		if (UVOverlay->IsSetTriangle(tid) == false)
		{
			UnsetTriangles.Add(tid);
		}
	}

	FAxisAlignedBox3d Bounds = EditMesh.GetBounds();
	UVEditor.SetTriangleUVsFromBoxProjection(UnsetTriangles,
		[](const FVector3d& V) { return V; },
		FFrame3d(Bounds.Center()), Bounds.Diagonal(), 1);
	UVEditor.ScaleUVAreaToBoundingBox(UnsetTriangles,
		FAxisAlignedBox2f(FVector2f::Zero(), FVector2f::One()), true, true);
}




static void InitializeNormalsFromAngleThreshold(
	FDynamicMesh3& TargetMesh,
	double NormalAngleThreshDeg)
{
	if (TargetMesh.HasAttributes() == false)
	{
		TargetMesh.EnableAttributes();
	}

	// recompute normals
	FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&TargetMesh, TargetMesh.Attributes()->PrimaryNormals(), NormalAngleThreshDeg);
	FMeshNormals::QuickRecomputeOverlayNormals(TargetMesh);
}



static void ProjectAttributes(
	FDynamicMesh3& TargetMesh,
	const FDynamicMesh3* SourceMesh,
	FDynamicMeshAABBTree3* SourceMeshSpatial)
{
	if (SourceMesh == nullptr || SourceMeshSpatial == nullptr)
	{
		return;
	}

	TargetMesh.EnableTriangleGroups();
	if (TargetMesh.HasAttributes() == false)
	{
		TargetMesh.EnableAttributes();
	}

	const FDynamicMeshColorOverlay* SourceColors = nullptr;
	FDynamicMeshColorOverlay* TargetColors = nullptr;
	if (SourceMesh->HasAttributes() && SourceMesh->Attributes()->HasPrimaryColors())
	{
		SourceColors = SourceMesh->Attributes()->PrimaryColors();
		TargetMesh.Attributes()->EnablePrimaryColors();
		TargetColors = TargetMesh.Attributes()->PrimaryColors();
	}

	const FDynamicMeshMaterialAttribute* SourceMaterialID = nullptr;
	FDynamicMeshMaterialAttribute* TargetMaterialID = nullptr;
	if (SourceMesh->HasAttributes() && SourceMesh->Attributes()->HasMaterialID())
	{
		SourceMaterialID = SourceMesh->Attributes()->GetMaterialID();
		TargetMesh.Attributes()->EnableMaterialID();
		TargetMaterialID = TargetMesh.Attributes()->GetMaterialID();
	}

	// compute projected group and MaterialID and vertex colors
	for (int32 tid : TargetMesh.TriangleIndicesItr())
	{
		FVector3d Centroid = TargetMesh.GetTriCentroid(tid);

		double NearDistSqr = 0;
		int32 NearestTID = SourceMeshSpatial->FindNearestTriangle(Centroid, NearDistSqr);

		if (SourceMaterialID != nullptr)
		{
			int32 MaterialID = SourceMaterialID->GetValue(NearestTID);
			TargetMaterialID->SetValue(tid, MaterialID);
		}

		if (SourceColors != nullptr)
		{
			if (SourceColors->IsSetTriangle(NearestTID))
			{
				FIndex3i SourceTriElems = SourceColors->GetTriangle(NearestTID);
				// TODO be smarter here...
				FVector4f Color = SourceColors->GetElement(SourceTriElems.A);
				int A = TargetColors->AppendElement(Color);
				int B = TargetColors->AppendElement(Color);
				int C = TargetColors->AppendElement(Color);
				TargetColors->SetTriangle(tid, FIndex3i(A, B, C));
			}
		}
	}
}





struct FCombinedMeshLOD
{
	FDynamicMesh3 Mesh;
	FDynamicMeshEditor Editor;
	FDynamicMeshMaterialAttribute* MaterialIDs = nullptr;

	FDynamicMeshPolygroupAttribute* SubsetIDs = nullptr;

	FCombinedMeshLOD()
		: Editor(&Mesh)
	{
		Mesh.EnableAttributes();
		Mesh.Attributes()->EnableMaterialID();
		
		// should we do this? maybe should be done via enable-matching?
		Mesh.Attributes()->EnablePrimaryColors();

		MaterialIDs = Mesh.Attributes()->GetMaterialID();
	}


	void SetMesh(FDynamicMesh3&& ExternalMesh)
	{
		Mesh = MoveTemp(ExternalMesh);
		Editor = FDynamicMeshEditor(&Mesh);
		check(Mesh.HasAttributes() && Mesh.Attributes()->HasPrimaryColors() && Mesh.Attributes()->HasMaterialID())
		MaterialIDs = Mesh.Attributes()->GetMaterialID();
	}
};

}



static void SortMesh(FDynamicMesh3& Mesh)
{
	if ( ! ensure(Mesh.HasAttributes() == false) ) return;

	TRACE_CPUPROFILER_EVENT_SCOPE(SortMesh);

	struct FVert
	{
		FVector3d Position;
		int32 VertexID;
		bool operator<(const FVert& V2) const
		{
			if ( Position.X != V2.Position.X ) { return Position.X < V2.Position.X; }
			if ( Position.Y != V2.Position.Y ) { return Position.Y < V2.Position.Y; }
			if ( Position.Z != V2.Position.Z ) { return Position.Z < V2.Position.Z; }
			return VertexID < V2.VertexID;
		}
	};
	struct FTri
	{
		FIndex3i Triangle;
		bool operator<(const FTri& Tri2) const
		{
			if ( Triangle.A != Tri2.Triangle.A ) { return Triangle.A < Tri2.Triangle.A; }
			if ( Triangle.B != Tri2.Triangle.B ) { return Triangle.B < Tri2.Triangle.B; }
			return Triangle.C < Tri2.Triangle.C; 
		}
	};

	TArray<FVert> Vertices;
	for ( int32 vid : Mesh.VertexIndicesItr() )
	{
		Vertices.Add( FVert{Mesh.GetVertex(vid), vid} );
	}
	Vertices.Sort();

	TArray<int32> VertMap;
	VertMap.SetNum(Mesh.MaxVertexID());
	for (int32 k = 0; k < Vertices.Num(); ++k)
	{
		const FVert& Vert = Vertices[k];
		VertMap[Vert.VertexID] = k;
	}

	TArray<FTri> Triangles;
	for ( int32 tid : Mesh.TriangleIndicesItr() )
	{
		FIndex3i Tri = Mesh.GetTriangle(tid);
		Tri.A = VertMap[Tri.A];
		Tri.B = VertMap[Tri.B];
		Tri.C = VertMap[Tri.C];
		Triangles.Add( FTri{Tri} );
	}
	Triangles.Sort();

	FDynamicMesh3 SortedMesh;
	for (const FVert& Vert : Vertices)
	{
		SortedMesh.AppendVertex(Mesh, Vert.VertexID);
	}
	for (const FTri& Tri : Triangles)
	{
		SortedMesh.AppendTriangle(Tri.Triangle.A, Tri.Triangle.B, Tri.Triangle.C);
	}

	Mesh = MoveTemp(SortedMesh);
}



bool ComputeHiddenRemovalForLOD(
	FDynamicMesh3& MeshLOD,
	int32 LODIndex,
	IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode RemoveHiddenFacesMethod,
	double RemoveHiddenSamplingDensity,
	bool bDoubleSidedHiddenRemoval)
{
	bool bVerbose = CVarGeometryCombineMeshInstancesVerbose.GetValueOnAnyThread();

	TRACE_CPUPROFILER_EVENT_SCOPE(RemoveHidden_LOD);
	bool bModified = false;
	switch (RemoveHiddenFacesMethod)
	{
		case IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode::OcclusionBased:
			RemoveHiddenFaces_Occlusion(MeshLOD, 200);		// 200 is arbitrary here! should improve once max-distance is actually available (currently ignored)
			bModified = true;
			break;
		case IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode::ExteriorVisibility:
		case IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode::Fastest:
			RemoveHiddenFaces_ExteriorVisibility(MeshLOD, RemoveHiddenSamplingDensity, bDoubleSidedHiddenRemoval, LODIndex);
			bModified = true;
			break;
	}

	if ( bVerbose )
	{
		UE_LOG(LogGeometry, Log, TEXT("    Remove Hidden Faces - [Tris %6d Verts %6d]"), MeshLOD.TriangleCount(), MeshLOD.VertexCount());
	}

	return bModified;
}

bool ComputeHiddenRemovalForLOD(
	FDynamicMesh3& MeshLOD,
	int32 LODIndex,
	IGeometryProcessing_CombineMeshInstances::FOptions CombineOptions)
{
	return ComputeHiddenRemovalForLOD(MeshLOD, LODIndex, CombineOptions.RemoveHiddenFacesMethod, CombineOptions.RemoveHiddenSamplingDensity, CombineOptions.bDoubleSidedHiddenRemoval);
}


void OptimizeLODMeshTriangulation(
	FDynamicMesh3& MeshLOD,
	int32 LODIndex,
	TFunction<FIndex3i(const FDynamicMesh3& Mesh, int32 TriangleID)> TriangleGroupingIDFunc,
	bool bWantCoplanarMerging,
	bool bWantPlanarRetriangulation,
	double BaseGeometricTolerance,
	TSet<int32>* SkipMaterialIDs = nullptr)
{
	bool bVerbose = CVarGeometryCombineMeshInstancesVerbose.GetValueOnAnyThread();

	TFunction<FIndex3i(const FDynamicMesh3& Mesh, int32 TriangleID)> GroupingIDFunc = TriangleGroupingIDFunc;
	if (!GroupingIDFunc)
	{
		GroupingIDFunc = [](const FDynamicMesh3&, int32) { return FIndex3i::Zero(); };
	}

	PostProcessHiddenFaceRemovedMesh(MeshLOD, BaseGeometricTolerance, bWantCoplanarMerging, bWantPlanarRetriangulation,	GroupingIDFunc, SkipMaterialIDs);
}

void OptimizeLODMeshTriangulation(
	FDynamicMesh3& MeshLOD,
	int32 LODIndex,
	IGeometryProcessing_CombineMeshInstances::FOptions CombineOptions,
	double BaseGeometricTolerance,
	TSet<int32>* SkipMaterialIDs = nullptr)
{
	bool bWantCoplanarMerging = (CombineOptions.bMergeCoplanarFaces)
		&& (LODIndex >= CombineOptions.MergeCoplanarFacesStartLOD)
		&& (LODIndex > CombineOptions.PreserveUVLODLevel);
	bool bWantPlanarRetriangulation = (bWantCoplanarMerging)
		&& (CombineOptions.PlanarPolygonRetriangulationStartLOD >= 0)
		&& (LODIndex >= CombineOptions.PlanarPolygonRetriangulationStartLOD);

	OptimizeLODMeshTriangulation(MeshLOD, LODIndex, CombineOptions.TriangleGroupingIDFunc, bWantCoplanarMerging, bWantPlanarRetriangulation, BaseGeometricTolerance, SkipMaterialIDs);
}





void ProcessCombinedLODChain(
	TArray<FCombinedMeshLOD>& MeshLODs,
	const TArray<double>& OptimizationTolerances,
	int32 FirstVoxWrappedIndex,
	int32 NumLODs,
	bool bRemoveHiddenFaces,
	TFunctionRef<bool(int32)> LODRemoveHidden,
	TFunctionRef<bool(int32)> LODWantCoplanarMerging,
	TFunctionRef<bool(int32)> LODWantPlanarRetriangulation,
	TFunctionRef<IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode(int32)> LODRemoveHiddenFacesMethod,
	TFunctionRef<double(int32)> LODRemoveHiddenSamplingDensity,
	TFunctionRef<bool(int32)> LODDoubleSidedHiddenRemoval,
	TFunction<FIndex3i(const FDynamicMesh3& Mesh, int32 TriangleID)> TriangleGroupingIDFunc,

	IGeometryProcessing_CombineMeshInstances::ECoarseApproximationStrategy CoarseLODStrategy,
	double CoarseApproximationDetailSize,
	TFunctionRef<int32(int32)> GetCoarseLODMaxTriCount,
	double CoarseLODBaseTolerance,
	double HardNormalAngleDeg,
	bool bAutoGenerateMissingUVs,
	bool bAutoGenerateTangents,
	TSet<int32>* PreserveTopologyMaterialIDs = nullptr
)
{
	using namespace UE::Geometry;
	bool bVerbose = CVarGeometryCombineMeshInstancesVerbose.GetValueOnAnyThread();

	TArray<UE::Tasks::FTask> PendingRemoveHiddenTasks;
	if (bRemoveHiddenFaces)
	{
		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			if (!LODRemoveHidden(LODIndex) || MeshLODs[LODIndex].Mesh.TriangleCount() == 0)
			{
				continue;
			}

			if (bVerbose)
			{
				UE_LOG(LogGeometry, Log, TEXT("  Optimizing LOD%d - Tris %6d Verts %6d"), LODIndex, MeshLODs[LODIndex].Mesh.TriangleCount(), MeshLODs[LODIndex].Mesh.VertexCount());
			}

			double UseTolerance = OptimizationTolerances[LODIndex];
			bool bWantCoplanarMerging = LODWantCoplanarMerging(LODIndex), bWantPlanarRetriangulation = LODWantPlanarRetriangulation(LODIndex);
			UE::Tasks::FTask RemoveHiddenTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, 
				[&MeshLODs, LODIndex, UseTolerance, PreserveTopologyMaterialIDs,
				LODRemoveHiddenFacesMethod, LODRemoveHiddenSamplingDensity, LODDoubleSidedHiddenRemoval,
				TriangleGroupingIDFunc, bWantCoplanarMerging, bWantPlanarRetriangulation]()
			{
				ComputeHiddenRemovalForLOD(MeshLODs[LODIndex].Mesh, LODIndex, LODRemoveHiddenFacesMethod(LODIndex), LODRemoveHiddenSamplingDensity(LODIndex), LODDoubleSidedHiddenRemoval(LODIndex));
				OptimizeLODMeshTriangulation(MeshLODs[LODIndex].Mesh, LODIndex, TriangleGroupingIDFunc, bWantCoplanarMerging, bWantPlanarRetriangulation, UseTolerance, PreserveTopologyMaterialIDs);
			});
			PendingRemoveHiddenTasks.Add(RemoveHiddenTask);

			if (bVerbose)
			{
				RemoveHiddenTask.Wait();
			}
		}
	}


	//
	// Process VoxWrapped LODs 
	//
	bool bUsingCoarseSweepApproximation = false;
	if ( FirstVoxWrappedIndex < MeshLODs.Num() && (MeshLODs[FirstVoxWrappedIndex].Mesh.TriangleCount() > 0))
	{
		FDynamicMesh3 SourceVoxWrapMesh = MoveTemp(MeshLODs[FirstVoxWrappedIndex].Mesh);
		FDynamicMeshAABBTree3 SourceSpatial(&SourceVoxWrapMesh, true);

		FDynamicMesh3 InitialCoarseApproximation;

		double VoxelDimension = 2.0;	// may be modified by ComputeVoxWrapMesh call
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ComputeVoxWrap);
			
			if (CoarseLODStrategy == IGeometryProcessing_CombineMeshInstances::ECoarseApproximationStrategy::VoxelBasedSolidApproximation)
			{
				ComputeVoxWrapMesh(SourceVoxWrapMesh, SourceSpatial, InitialCoarseApproximation, CoarseApproximationDetailSize, VoxelDimension);
				bUsingCoarseSweepApproximation = false;
			}
			else if (CoarseLODStrategy == IGeometryProcessing_CombineMeshInstances::ECoarseApproximationStrategy::SweptPlanarProjection)
			{
				ComputeBestFullProjectionMesh(SourceVoxWrapMesh, SourceSpatial, InitialCoarseApproximation, CoarseApproximationDetailSize);
				bUsingCoarseSweepApproximation = true;
			}
			else if (CoarseLODStrategy == IGeometryProcessing_CombineMeshInstances::ECoarseApproximationStrategy::IntersectSweptPlanarProjections)
			{
				ComputeProjectionMeshIntersection(SourceVoxWrapMesh, SourceSpatial, InitialCoarseApproximation, CoarseApproximationDetailSize);
				bUsingCoarseSweepApproximation = true;
			}
			else  // Automatic
			{
				// try swept-planar-projection as it is cheaper and generally better. If it deviates too much, fall back to voxel
				FDynamicMesh3 SweptPlanarCoarseMesh;
				ComputeBestFullProjectionMesh(SourceVoxWrapMesh, SourceSpatial, SweptPlanarCoarseMesh, CoarseApproximationDetailSize);
				FVector2d SweepDeviation = ComputeGeometricDeviation(SweptPlanarCoarseMesh, SourceSpatial);
				bUsingCoarseSweepApproximation = (SweepDeviation.Y < 2.0 * CoarseApproximationDetailSize);
				if (bUsingCoarseSweepApproximation)
				{
					InitialCoarseApproximation = MoveTemp(SweptPlanarCoarseMesh);
				}
				else
				{
					FDynamicMesh3 VoxWrapCoarseMesh;
					ComputeVoxWrapMesh(SourceVoxWrapMesh, SourceSpatial, VoxWrapCoarseMesh, CoarseApproximationDetailSize, VoxelDimension);
					InitialCoarseApproximation = MoveTemp(VoxWrapCoarseMesh);
				}
			}
			
			// currently need to re-sort output to remove non-determinism...
			SortMesh(InitialCoarseApproximation);

			//UE_LOG(LogGeometry, Warning, TEXT("VoxWrapMesh has %d triangles %d vertices"), InitialCoarseApproximation.TriangleCount(), InitialCoarseApproximation.VertexCount());
		}

		if ( bVerbose )
		{
			UE_LOG(LogGeometry, Log, TEXT("  Generated Base Coarse Mesh - Tris %8d Verts %8d - CellSize is %4.3f"), InitialCoarseApproximation.TriangleCount(), InitialCoarseApproximation.VertexCount(), VoxelDimension);
		}

		InitialCoarseApproximation.DiscardAttributes();
		const int FastCollapseToTriCount = 50000;
		if (InitialCoarseApproximation.TriangleCount() > FastCollapseToTriCount+500)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FastCollapsePrePass);
			FVolPresMeshSimplification Simplifier(&InitialCoarseApproximation);
			Simplifier.bAllowSeamCollapse = false;
			Simplifier.FastCollapsePass(VoxelDimension * 0.5, 10, false, 50000);
		}

		if ( bVerbose )
		{
			UE_LOG(LogGeometry, Log, TEXT("         FastCollapse         - Tris %8d Verts %8d"), InitialCoarseApproximation.TriangleCount(), InitialCoarseApproximation.VertexCount());
		}

		int32 MaxTriCount = GetCoarseLODMaxTriCount(FirstVoxWrappedIndex);
		double SimplifyTolerance = CoarseLODBaseTolerance;

		// Note for very simple parts it can be the case that the last approximate LOD is
		// lower tri-count than the first coarse approximation. To handle such cases,
		// we rely on the BuildOutputSubAssembly to propagate simpler LODs down the chain.
		// (We don't use the non-coarse LODs as a starting point because they tend not
		// to simplify down as well for subsequent coarse LODs)
		UE::Tasks::Wait(PendingRemoveHiddenTasks);

		// Current state of InitialCoarseApproximation is our initial voxel LOD. To ensure
		// that voxel LODs have compatible UVs (to allow baking), we compute UVs on
		// the first LOD and allow them to propagate (and currently normals as well)
		InitialCoarseApproximation.DiscardAttributes();
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimplifyVoxWrap);
			if (InitialCoarseApproximation.TriangleCount() > MaxTriCount)
			{
				ComputeSimplifiedVoxWrapMesh(InitialCoarseApproximation, &SourceVoxWrapMesh, &SourceSpatial,
					SimplifyTolerance, MaxTriCount);
			}
		}
		InitialCoarseApproximation.EnableAttributes();
		InitializeNormalsFromAngleThreshold(InitialCoarseApproximation, HardNormalAngleDeg);
		ComputeVoxWrapMeshAutoUV(InitialCoarseApproximation);
		MeshLODs[FirstVoxWrappedIndex].Mesh = MoveTemp(InitialCoarseApproximation);

		// iterate simplification criteria to next level
		SimplifyTolerance *= 1.5;

		for (int32 LODIndex = FirstVoxWrappedIndex+1; LODIndex < NumLODs; ++LODIndex)
		{
			MaxTriCount = GetCoarseLODMaxTriCount(LODIndex);
			// need to simplify from previous level to preserve UVs/etc
			MeshLODs[LODIndex].Mesh = MeshLODs[LODIndex-1].Mesh;

			if (MeshLODs[LODIndex].Mesh.TriangleCount() > MaxTriCount)
			{
				DoSimplifyMesh<FAttrMeshSimplification>(MeshLODs[LODIndex].Mesh, MaxTriCount, nullptr, SimplifyTolerance);
			}

			SimplifyTolerance *= 1.5;
		}

		// Project colors and materials after mesh simplification to avoid constraining it.
		// If they /should/ constrain simplification, then they should be projected onto the first
		// mesh (InitialCoarseApproximation above) and they will automatically transfer
		for (int32 LODIndex = FirstVoxWrappedIndex; LODIndex < NumLODs; ++LODIndex)
		{
			ProjectAttributes(MeshLODs[LODIndex].Mesh, &SourceVoxWrapMesh, &SourceSpatial);
		}
	}

	// wait...
	UE::Tasks::Wait(PendingRemoveHiddenTasks);

	// parallel regenerate UVs and potentially tangents for any areas of LODs that are missing UVs
	TArray<UE::Tasks::FTask> PendingAutoUVTasks;
	bool bComputeTangents = (bAutoGenerateMissingUVs && bAutoGenerateTangents);
	if (bAutoGenerateMissingUVs)
	{
		for (int32 LODIndex = 0; LODIndex < NumLODs && LODIndex < FirstVoxWrappedIndex; ++LODIndex)
		{
			if (MeshLODs[LODIndex].Mesh.TriangleCount() == 0) continue;

			UE::Tasks::FTask AutoUVTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&MeshLODs, LODIndex, bComputeTangents]()
			{
				ComputeMissingUVs(MeshLODs[LODIndex].Mesh);
				if (bComputeTangents)
				{
					FMeshTangentsd::ComputeDefaultOverlayTangents(MeshLODs[LODIndex].Mesh);
				}
			});
			PendingAutoUVTasks.Add(AutoUVTask);
		}
	}

	// remove hidden faces on voxel LODs
	if (bRemoveHiddenFaces && bUsingCoarseSweepApproximation == false)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RemoveHidden);
		ParallelFor(NumLODs, [&](int32 LODIndex)
		{
			if (MeshLODs[LODIndex].Mesh.TriangleCount() == 0) return;

			if ( LODIndex >= FirstVoxWrappedIndex )
			{ 
				if ( bVerbose )
				{
					UE_LOG(LogGeometry, Log, TEXT("  Optimizing LOD%d - Tris %6d Verts %6d"), LODIndex, MeshLODs[LODIndex].Mesh.TriangleCount(), MeshLODs[LODIndex].Mesh.VertexCount());
				}

				ComputeHiddenRemovalForLOD(MeshLODs[LODIndex].Mesh, LODIndex, LODRemoveHiddenFacesMethod(LODIndex), LODRemoveHiddenSamplingDensity(LODIndex), LODDoubleSidedHiddenRemoval(LODIndex));
			}
		}, (bVerbose) ? EParallelForFlags::ForceSingleThread :  EParallelForFlags::None );
	}


	// make sure AutoUV is done
	UE::Tasks::Wait(PendingAutoUVTasks);
}

void ProcessCombinedLODChain(
	TArray<FCombinedMeshLOD>& MeshLODs,
	const TArray<double>& OptimizationTolerances,
	int32 FirstVoxWrappedIndex,
	IGeometryProcessing_CombineMeshInstances::FOptions CombineOptions,
	TSet<int32>* PreserveTopologyMaterialIDs = nullptr
)
{
	bool bRemoveHiddenFaces =
		(CombineOptions.RemoveHiddenFacesMethod != IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode::None
			&& CVarGeometryCombineMeshInstancesRemoveHidden.GetValueOnAnyThread() > 0);
	ProcessCombinedLODChain(MeshLODs, OptimizationTolerances, FirstVoxWrappedIndex,
		CombineOptions.NumLODs,
		bRemoveHiddenFaces,
		[&CombineOptions, FirstVoxWrappedIndex](int32 LODIndex) { return LODIndex >= CombineOptions.RemoveHiddenStartLOD && LODIndex < FirstVoxWrappedIndex; },
		[&CombineOptions](int32 LODIndex) { return CombineOptions.bMergeCoplanarFaces && LODIndex >= CombineOptions.MergeCoplanarFacesStartLOD && LODIndex >= CombineOptions.PreserveUVLODLevel; },
		[&CombineOptions](int32 LODIndex)
		{
			return CombineOptions.bMergeCoplanarFaces && LODIndex >= CombineOptions.MergeCoplanarFacesStartLOD && LODIndex >= CombineOptions.PreserveUVLODLevel
				&& CombineOptions.PlanarPolygonRetriangulationStartLOD >= 0 && LODIndex >= CombineOptions.PlanarPolygonRetriangulationStartLOD;
		},
		[&CombineOptions](int32) { return CombineOptions.RemoveHiddenFacesMethod; },
		[&CombineOptions](int32) { return CombineOptions.RemoveHiddenSamplingDensity; },
		[&CombineOptions](int32) { return CombineOptions.bDoubleSidedHiddenRemoval; },
		CombineOptions.TriangleGroupingIDFunc,
		CombineOptions.CoarseLODStrategy,
		CombineOptions.CoarseApproximationDetailSize,
		[&CombineOptions, FirstVoxWrappedIndex](int32 LODIndex)
		{
			int32 MaxCoarseTri = CombineOptions.CoarseLODMaxTriCountBase;
			for (int32 Idx = FirstVoxWrappedIndex; Idx < LODIndex; ++Idx)
			{
				MaxCoarseTri /= 2;
			}
			return MaxCoarseTri;
		},
		CombineOptions.CoarseLODBaseTolerance,
		CombineOptions.HardNormalAngleDeg,
		CombineOptions.bAutoGenerateMissingUVs,
		CombineOptions.bAutoGenerateTangents,
		PreserveTopologyMaterialIDs
	);
}


struct FCombinedSubAssembly
{
	TArray<UE::Geometry::FDynamicMesh3> MeshLODs;
	int32 SubAssemblyID = 0;
};


static void BuildOutputSubAssembly(
	TArray<FCombinedMeshLOD>& MeshLODs,
	int SubAssemblyID,
	int32 FirstVoxWrappedIndex,
	FCombinedSubAssembly& OutputSubAssembly
)
{
	OutputSubAssembly.SubAssemblyID = SubAssemblyID;

	// collect output meshes
	int MaxReplaceLOD = MeshLODs.Num();
	for (int32 LODLevel = 0; LODLevel < MeshLODs.Num(); ++LODLevel)
	{
		FDynamicMesh3 LODMesh = MoveTemp(MeshLODs[LODLevel].Mesh);

		// If we ended up larger than the mesh in the previous LOD, we should use that instead!
		// This can happen particular with VoxWrap LODs
		if (LODLevel > 0 && LODLevel < MaxReplaceLOD)
		{
			if (LODMesh.TriangleCount() > OutputSubAssembly.MeshLODs.Last().TriangleCount())
			{
				LODMesh = OutputSubAssembly.MeshLODs.Last();
			}
		}
		OutputSubAssembly.MeshLODs.Add(MoveTemp(LODMesh));
	}
}


void BuildCombinedMesh(
	const FMeshPartsAssembly& Assembly,
	IGeometryProcessing_CombineMeshInstances::FOptions CombineOptions,
	TArray<FCombinedSubAssembly>& CombinedResults)
{
	using namespace UE::Geometry;
	bool bVerbose = CVarGeometryCombineMeshInstancesVerbose.GetValueOnAnyThread();

	int32 NumLODs = CombineOptions.NumLODs;
	TArray<FCombinedMeshLOD> MeshLODs;
	MeshLODs.SetNum(NumLODs);

	int FirstVoxWrappedIndex = MAX_int32;
	TArray<ECombinedLODType> LODTypes;
	LODTypes.Init(ECombinedLODType::Approximated, NumLODs); // Note Approximated is the default here, and will cover the span between simplified and voxwrapped
	for (int32 LODLevel = 0; LODLevel < NumLODs; ++LODLevel)
	{
		if (LODLevel < CombineOptions.NumCopiedLODs)
		{
			LODTypes[LODLevel] = ECombinedLODType::Copied;
		}
		else if (LODLevel < CombineOptions.NumCopiedLODs + CombineOptions.NumSimplifiedLODs)
		{
			LODTypes[LODLevel] = ECombinedLODType::Simplified;
		}
		else if (LODLevel >= NumLODs - CombineOptions.NumCoarseLODs)
		{
			LODTypes[LODLevel] = ECombinedLODType::VoxWrapped;
			FirstVoxWrappedIndex = FMath::Min(LODLevel, FirstVoxWrappedIndex);
		}
	}

	int32 NumParts = Assembly.Parts.Num();

	// determine maximum number of UV channels on any Part LOD0, and configure the output combined
	// LOD meshes to have that many UV channels (clamping to at least 1). Triangles from Parts that
	// have fewer UV channels will end up with (0,0) UVs in the extra channels.
	int32 MaxNumUVChannels = 1;
	for (int32 SetIndex = 0; SetIndex < NumParts; ++SetIndex)
	{
		const FDynamicMesh3& SourceMesh = Assembly.SourceMeshGeometry[SetIndex].SourceMeshLODs[0];
		MaxNumUVChannels = FMath::Max(MaxNumUVChannels, (SourceMesh.HasAttributes() ? SourceMesh.Attributes()->NumUVLayers() : 0));
	}
	for (FCombinedMeshLOD& LODMeshData : MeshLODs)
	{
		LODMeshData.Mesh.Attributes()->SetNumUVLayers(MaxNumUVChannels);
	}


	// determine if we have multiple part subsets. In this case we need to be able to split the mesh
	// by part later, which we will do by appending a polygroup layer
	TArray<int32> SubsetIDs;
	for (const TUniquePtr<FMeshPart>& Part : Assembly.Parts)
	{
		for (const FMeshPartInstance& Instance : Part->Instances)
		{
			SubsetIDs.AddUnique(Instance.SubsetID);
		}
	}
	bool bHaveMultiplePartSubsets = (SubsetIDs.Num() > 1);
	if (bHaveMultiplePartSubsets)
	{
		for (FCombinedMeshLOD& LODMeshData : MeshLODs)
		{
			LODMeshData.Mesh.Attributes()->SetNumPolygroupLayers(1);
			LODMeshData.SubsetIDs = LODMeshData.Mesh.Attributes()->GetPolygroupLayer(0);
		}
	}


	// iterate over part sets, then for each part, over part LODs, and
	// for each instance append the part LOD to the accumulate LOD mesh
	for ( int32 SetIndex = 0; SetIndex < NumParts; ++SetIndex )
	{
		const TUniquePtr<FMeshPart>& Part = Assembly.Parts[SetIndex];
		const FSourceGeometry& SourceGeometry = Assembly.SourceMeshGeometry[SetIndex];
		const FOptimizedGeometry& OptimizedGeometry = Assembly.OptimizedMeshGeometry[SetIndex];

		check(Part->bAllowMerging == true);		// do not support this yet

		FMeshIndexMappings Mappings;

		for (int32 LODLevel = 0; LODLevel < NumLODs; ++LODLevel)
		{
			const FDynamicMesh3* ApproximateAppendMesh = nullptr;
			const FDynamicMesh3* UseAppendMesh = nullptr;

			// default approximate mesh to lowest-quality approximation (box), need to do this
			// so that we always have something to swap to for Decorative parts
			ApproximateAppendMesh = OptimizedGeometry.ApproximateMeshLODs.Num() > 0 ? &OptimizedGeometry.ApproximateMeshLODs.Last() : nullptr;

			ECombinedLODType LevelLODType = LODTypes[LODLevel];
			if (LevelLODType == ECombinedLODType::Copied)
			{
				UseAppendMesh = (LODLevel < SourceGeometry.SourceMeshLODs.Num()) ?
					&SourceGeometry.SourceMeshLODs[LODLevel] : &SourceGeometry.SourceMeshLODs.Last();
			}
			else if (LevelLODType == ECombinedLODType::Simplified)
			{
				int32 SimplifiedLODIndex = LODLevel - CombineOptions.NumCopiedLODs;
				UseAppendMesh = &OptimizedGeometry.SimplifiedMeshLODs[SimplifiedLODIndex];;
			}
			else if (LevelLODType == ECombinedLODType::VoxWrapped)
			{
				UseAppendMesh = &SourceGeometry.SourceMeshLODs.Last();;
			}
			else // ECombinedLODType::Approximated
			{
				int32 ApproxLODIndex = LODLevel - CombineOptions.NumCopiedLODs - CombineOptions.NumSimplifiedLODs;
				ApproximateAppendMesh = &OptimizedGeometry.ApproximateMeshLODs[ApproxLODIndex];
				UseAppendMesh = ApproximateAppendMesh;
			}

			FCombinedMeshLOD& CombinedMeshLODData = MeshLODs[LODLevel];

			for ( const FMeshPartInstance& Instance : Part->Instances )
			{
				const FDynamicMesh3* InstanceAppendMesh = UseAppendMesh;

				if (Instance.FilterLODLevel >= 0 && LODLevel >= Instance.FilterLODLevel)
				{
					continue;
				}

				bool bIsDecorativePart = (Instance.DetailLevel == EMeshDetailLevel::Decorative);
				if (bIsDecorativePart)
				{
					// filter out detail parts at higher LODs, or if we are doing VoxWrap LOD
					if ( LODLevel >= CombineOptions.FilterDecorativePartsLODLevel || LevelLODType == ECombinedLODType::VoxWrapped )
					{
						continue;
					}
					// at last detail part LOD, switch to approximate mesh
					if (LODLevel >= (CombineOptions.FilterDecorativePartsLODLevel - CombineOptions.ApproximateDecorativePartLODs) )
					{
						check(ApproximateAppendMesh)
						InstanceAppendMesh = ApproximateAppendMesh;
					}
				}

				// If approximation is disabled for this instance, fall back to last available simplified LOD.
				// TODO: if part budget was applied, the mesh in this slot might actually be an approximation.
				//   This is difficult to fix because the mesh LOD chains are per-part and not per-instance,
				//   need some way to keep the original copied & simplified LOD chain around...
				bool bAllowApproximation = (Part->bAllowApproximation && Instance.bAllowApproximation);
				if (bAllowApproximation == false && LevelLODType == ECombinedLODType::Approximated)
				{
					InstanceAppendMesh = &OptimizedGeometry.SimplifiedMeshLODs.Last();
				}

				// need to make a copy to run pre-process func
				FDynamicMesh3 TempAppendMesh(*InstanceAppendMesh);
				if (Assembly.PreProcessInstanceMeshFunc)
				{
					Assembly.PreProcessInstanceMeshFunc(TempAppendMesh, Instance);
				}

				// determine if we should be keeping UVs around for this Part
				bool bPreserveUVs = (LODLevel == 0)
					|| (LODLevel <= CombineOptions.PreserveUVLODLevel)
					|| ((int)LevelLODType <= (int)ECombinedLODType::Simplified && CombineOptions.bSimplifyPreserveUVs)
					|| Part->bPreserveUVs;

				// if part does not require UVs, but still has them, discard them here to encourage merging downstream
				// (todo: is this ever possible now? should we skip this if we are not going to do the merging?)
				if (bPreserveUVs == false && TempAppendMesh.HasAttributes())
				{
					for (int32 UVLayer = 0; UVLayer < TempAppendMesh.Attributes()->NumUVLayers(); ++UVLayer)
					{
						TempAppendMesh.Attributes()->GetUVLayer(UVLayer)->ClearElements();
					}
					// if we have no UVs then tangents are invalid
					TempAppendMesh.Attributes()->DisableTangents();
				}

				Mappings.Reset();
				CombinedMeshLODData.Editor.AppendMesh(&TempAppendMesh, Mappings,
					[&](int, const FVector3d& Pos) { return Instance.WorldTransform.TransformPosition(Pos); },
					[&](int, const FVector3d& Normal) { return Instance.WorldTransform.TransformNormal(Normal); });

				// transfer part IDs etc
				if (CombinedMeshLODData.SubsetIDs != nullptr)
				{
					for (int32 tid : TempAppendMesh.TriangleIndicesItr())
					{
						CombinedMeshLODData.SubsetIDs->SetValue(Mappings.GetNewTriangle(tid), Instance.SubsetID);
					}
				}

				// transfer Material IDs if part mesh has them
				FDynamicMeshMaterialAttribute* AppendMaterialAttrib = TempAppendMesh.HasAttributes() ? TempAppendMesh.Attributes()->GetMaterialID() : nullptr;
				for (int32 tid : TempAppendMesh.TriangleIndicesItr())
				{
					int32 SourceMaterialID = (AppendMaterialAttrib != nullptr) ? AppendMaterialAttrib->GetValue(tid) : 0;
					UMaterialInterface* UseMaterial = Instance.Materials.IsValidIndex(SourceMaterialID) ? Instance.Materials[SourceMaterialID] : nullptr;
					const int32* FoundMaterialIndex = Assembly.MaterialMap.Find(UseMaterial);
					int32 AssignMaterialIndex = (FoundMaterialIndex != nullptr) ? *FoundMaterialIndex : 0;

					CombinedMeshLODData.MaterialIDs->SetValue( Mappings.GetNewTriangle(tid), AssignMaterialIndex );
				}
			}
		}
	}

	// Some Material regions may need to be explicitly preserved, this set will be passed on later
	TSet<int32> PreserveTopologyMaterialIDSet;
	for (UMaterialInterface* Material : CombineOptions.PreventMergingMaterialSet)
	{
		if (const int32* FoundMaterialIndex = Assembly.MaterialMap.Find(Material))
		{
			PreserveTopologyMaterialIDSet.Add(*FoundMaterialIndex);
		}
	}

	// make a list of per-LOD geometric tolerances tha will drive additional optimization. 
	// For copied and first simplified LODs, use Simplify Base Tolerance, and then increment
	// for each successive LOD.  (todo: have a separate initial tolerance for Approx LODs?)
	double CurTolerance = CombineOptions.SimplifyBaseTolerance;
	TArray<double> OptimizationTolerances;
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		if (LODTypes[LODIndex] == ECombinedLODType::Simplified && LODTypes[LODIndex - 1] == ECombinedLODType::Simplified)
		{
			CurTolerance *= 2.0;
		}
		else if (LODTypes[LODIndex] == ECombinedLODType::Approximated)
		{
			CurTolerance *= 2.0;
		}
		OptimizationTolerances.Add(CurTolerance);
	}


	if (bHaveMultiplePartSubsets)
	{
		TArray<int32> OrderedSubsetIDs(SubsetIDs);

		int32 NumSubsets = SubsetIDs.Num();
		TArray<TArray<FCombinedMeshLOD>> SubsetMeshLODChains;
		SubsetMeshLODChains.SetNum(NumSubsets);
		for (int32 SubsetIndex = 0; SubsetIndex < NumSubsets; ++SubsetIndex)
		{
			SubsetMeshLODChains[SubsetIndex].SetNum(NumLODs);
		}

		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			// split the LOD by subset ID
			FDynamicMesh3& LODMesh = MeshLODs[LODIndex].Mesh;
			FDynamicMeshPolygroupAttribute* SubsetIDAttrib = MeshLODs[LODIndex].SubsetIDs;
			TArray<FDynamicMesh3> SplitMeshes;
			FDynamicMeshEditor::SplitMesh(&LODMesh, SplitMeshes, [&](int32 tid) { return OrderedSubsetIDs.IndexOfByKey(SubsetIDAttrib->GetValue(tid)); });

			// code below assumes this. If it's not the case, then we have some more complex processing to figure out...
			check(SplitMeshes.Num() == NumSubsets);		

			// give each subset submesh to the 
			for (int32 SubsetIndex = 0; SubsetIndex < NumSubsets; ++SubsetIndex)
			{
				TArray<FCombinedMeshLOD>& LODChain = SubsetMeshLODChains[SubsetIndex];
				FDynamicMesh3& Submesh = SplitMeshes[SubsetIndex];
				LODChain[LODIndex].SetMesh( MoveTemp(Submesh) );
			}
		}

		TArray<UE::Tasks::FTask> PendingSubsetTasks;

		for (int32 SubsetIndex = 0; SubsetIndex < NumSubsets; ++SubsetIndex)
		{
			TArray<FCombinedMeshLOD>& LODChain = SubsetMeshLODChains[SubsetIndex];
			UE::Tasks::FTask ProcessSubsetTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&LODChain, &CombineOptions, &OptimizationTolerances, &PreserveTopologyMaterialIDSet, FirstVoxWrappedIndex]()
			{
				ProcessCombinedLODChain(LODChain, OptimizationTolerances, FirstVoxWrappedIndex, CombineOptions, &PreserveTopologyMaterialIDSet);
			});
			PendingSubsetTasks.Add(ProcessSubsetTask);
			if (bVerbose)
			{
				ProcessSubsetTask.Wait();
			}
		}

		// wait for all subsets to finish processing
		UE::Tasks::Wait(PendingSubsetTasks);

		CombinedResults.SetNum(NumSubsets);
		for (int32 SubsetIndex = 0; SubsetIndex < NumSubsets; ++SubsetIndex)
		{
			TArray<FCombinedMeshLOD>& LODChain = SubsetMeshLODChains[SubsetIndex];
			BuildOutputSubAssembly(LODChain, OrderedSubsetIDs[SubsetIndex], FirstVoxWrappedIndex, CombinedResults[SubsetIndex]);
		}

	}
	else
	{
		ProcessCombinedLODChain(MeshLODs, OptimizationTolerances, FirstVoxWrappedIndex, CombineOptions, &PreserveTopologyMaterialIDSet);

		CombinedResults.SetNum(1);
		BuildOutputSubAssembly(MeshLODs, 0, FirstVoxWrappedIndex, CombinedResults[0]);
	}

}


void BuildCombinedMeshFromPrecomputedMeshes(
	const FMeshPartsAssembly& Assembly,
	const IGeometryProcessing_CombineMeshInstances::FCombineMeshInstancesOptionsGeneral& AllLODOptions,
	TConstArrayView<IGeometryProcessing_CombineMeshInstances::FCombineMeshInstancesOptionsPerLOD> PerLODOptions,
	TArray<FCombinedSubAssembly>& CombinedResults)
{
	using namespace UE::Geometry;
	using FLODOpts = IGeometryProcessing_CombineMeshInstances::FCombineMeshInstancesOptionsPerLOD;
	using FAllOpts = IGeometryProcessing_CombineMeshInstances::FCombineMeshInstancesOptionsGeneral;

	bool bVerbose = CVarGeometryCombineMeshInstancesVerbose.GetValueOnAnyThread();

	int32 NumLODs = PerLODOptions.Num();
	TArray<FCombinedMeshLOD> MeshLODs;
	MeshLODs.SetNum(NumLODs);

	int32 FirstVoxWrappedIndex = MAX_int32;

	// LOD types tracks the optimization method per LOD. We enforce that this is increasing, e.g. must always use at-least-or-more aggressive optimization methods for higher LODs
	TArray<ECombinedLODType> LODTypes;
	LODTypes.Reserve(NumLODs);
	IGeometryProcessing_CombineMeshInstances::EMeshOptimizationMethod LastLODMethod = IGeometryProcessing_CombineMeshInstances::EMeshOptimizationMethod::None;
	for (const FLODOpts& Opts : PerLODOptions)
	{
		IGeometryProcessing_CombineMeshInstances::EMeshOptimizationMethod UseMethod = Opts.OptimizationMethod;
		if ((int32)UseMethod < (int32)LastLODMethod)
		{
			UE_LOG(LogGeometry, Warning, TEXT("LOD optimization methods must be increasing in optimization level."));
			UseMethod = LastLODMethod;
		}
		switch (UseMethod)
		{
		case IGeometryProcessing_CombineMeshInstances::EMeshOptimizationMethod::None:
			LODTypes.Add(ECombinedLODType::Copied);
			break;
		case IGeometryProcessing_CombineMeshInstances::EMeshOptimizationMethod::SimplifyOrApproximate:
			LODTypes.Add(ECombinedLODType::Simplified);
			break;
		case IGeometryProcessing_CombineMeshInstances::EMeshOptimizationMethod::ApproximateOnly:
			LODTypes.Add(ECombinedLODType::Approximated);
			break;
		case IGeometryProcessing_CombineMeshInstances::EMeshOptimizationMethod::VoxelizeAndDecimate:
			int32 LODIdx = LODTypes.Add(ECombinedLODType::VoxWrapped);
			FirstVoxWrappedIndex = FMath::Min(LODIdx, FirstVoxWrappedIndex);
			break;
		}
		LastLODMethod = UseMethod;
	}
	check(LODTypes.Num() == NumLODs);

	int32 NumParts = Assembly.Parts.Num();

	// determine maximum number of UV channels on any Part LOD0, and configure the output combined
	// LOD meshes to have that many UV channels (clamping to at least 1). Triangles from Parts that
	// have fewer UV channels will end up with (0,0) UVs in the extra channels.
	int32 MaxNumUVChannels = 1;
	for (int32 SetIndex = 0; SetIndex < NumParts; ++SetIndex)
	{
		const IGeometryProcessing_CombineMeshInstances::FSinglePartMeshSet& PartMeshes = *Assembly.Parts[SetIndex]->PrecomputedMeshes;
		const FDynamicMesh3& SourceMesh = PartMeshes.Source[0];
		
		MaxNumUVChannels = FMath::Max(MaxNumUVChannels, (SourceMesh.HasAttributes() ? SourceMesh.Attributes()->NumUVLayers() : 0));
	}
	for (FCombinedMeshLOD& LODMeshData : MeshLODs)
	{
		LODMeshData.Mesh.Attributes()->SetNumUVLayers(MaxNumUVChannels);
	}


	// determine if we have multiple part subsets. In this case we need to be able to split the mesh
	// by part later, which we will do by appending a polygroup layer
	TArray<int32> SubsetIDs;
	for (const TUniquePtr<FMeshPart>& Part : Assembly.Parts)
	{
		for (const FMeshPartInstance& Instance : Part->Instances)
		{
			SubsetIDs.AddUnique(Instance.SubsetID);
		}
	}
	bool bHaveMultiplePartSubsets = (SubsetIDs.Num() > 1);
	if (bHaveMultiplePartSubsets)
	{
		for (FCombinedMeshLOD& LODMeshData : MeshLODs)
		{
			LODMeshData.Mesh.Attributes()->SetNumPolygroupLayers(1);
			LODMeshData.SubsetIDs = LODMeshData.Mesh.Attributes()->GetPolygroupLayer(0);
		}
	}

	TArray<int32> PartSourcesPerLOD; // array of precomputed mesh indices per LOD per Part, ordered as [ Lod0[Part0 Part1 ...], Lod1[Part0 Part1 ...], ...]
	PartSourcesPerLOD.SetNumZeroed(NumParts * NumLODs);
	// Get the category of approximation as quality-ordered integer -- source == 0, simplified == 1, approximated == 2
	auto SourceIndexToMeshCategory = [&Assembly](int32 PartIdx, int32 SourceIdx, int32& WithinCategoryIdx, bool& bCanPromoteWithinCategory, bool& bCanPromoteAtAll, double& PromotedAvgError)
		{
			const IGeometryProcessing_CombineMeshInstances::FSinglePartMeshSet& PartMeshes = *Assembly.Parts[PartIdx]->PrecomputedMeshes;
			int32 MeshesCount = PartMeshes.Source.Num() + PartMeshes.Simplified.Num() + PartMeshes.Approximated.Num();
			PromotedAvgError = 0;
			bCanPromoteAtAll = SourceIdx + 1 < MeshesCount;
			if (!bCanPromoteAtAll)
			{
				SourceIdx = MeshesCount - 1;
			}
			if (SourceIdx < PartMeshes.Source.Num())
			{
				WithinCategoryIdx = SourceIdx;
				bCanPromoteWithinCategory = SourceIdx + 1 < PartMeshes.Source.Num();
				if (!bCanPromoteWithinCategory && bCanPromoteAtAll)
				{
					PromotedAvgError = PartMeshes.SimplifiedMeshErrors.IsEmpty() ? PartMeshes.ApproximatedMeshErrors[0].AverageError : PartMeshes.SimplifiedMeshErrors[0].AverageError;
				}
				return 0;
			}
			SourceIdx -= PartMeshes.Source.Num();
			if (SourceIdx < PartMeshes.Simplified.Num())
			{
				WithinCategoryIdx = SourceIdx;
				bCanPromoteWithinCategory = SourceIdx + 1 < PartMeshes.Simplified.Num();
				if (bCanPromoteWithinCategory)
				{
					PromotedAvgError = PartMeshes.SimplifiedMeshErrors[SourceIdx + 1].AverageError;
				}
				else if (bCanPromoteAtAll)
				{
					PromotedAvgError = PartMeshes.ApproximatedMeshErrors[0].AverageError;
				}
				return 1;
			}
			SourceIdx -= PartMeshes.Simplified.Num();
			WithinCategoryIdx = SourceIdx;
			bCanPromoteWithinCategory = SourceIdx + 1 < PartMeshes.Approximated.Num();
			if (bCanPromoteWithinCategory)
			{
				PromotedAvgError = PartMeshes.ApproximatedMeshErrors[SourceIdx + 1].AverageError;
			}
			return 2;
		};
	auto SourceIndexToMesh = [&Assembly](int32 PartIdx, int32 SourceIdx)
		{
			const IGeometryProcessing_CombineMeshInstances::FSinglePartMeshSet& PartMeshes = *Assembly.Parts[PartIdx]->PrecomputedMeshes;
			if (SourceIdx < PartMeshes.Source.Num())
			{
				return &PartMeshes.Source[SourceIdx];
			}
			SourceIdx -= PartMeshes.Source.Num();
			if (SourceIdx < PartMeshes.Simplified.Num())
			{
				return &PartMeshes.Simplified[SourceIdx];
			}
			SourceIdx -= PartMeshes.Simplified.Num();
			if (PartMeshes.Approximated.Num() > 0)
			{
				return &PartMeshes.Approximated[FMath::Min(SourceIdx, PartMeshes.Approximated.Num() - 1)];
			}
			else // If there weren't approximated meshes, fall back to the last existing mesh
			{
				if (PartMeshes.Simplified.Num() > 0)
				{
					return &PartMeshes.Simplified.Last();
				}
				else
				{
					check(!PartMeshes.Source.IsEmpty()); // there must at least be source meshes available for every part
					return &PartMeshes.Source.Last();
				}
			}
		};
	auto GetPrecomputedMesh = [&PartSourcesPerLOD, &SourceIndexToMesh, &Assembly, &PerLODOptions, NumParts](int32 LODLevel, int32 PartIdx, int32 InstIdx) -> const FDynamicMesh3*
		{
			int32 SourceIdx = PartSourcesPerLOD[LODLevel * NumParts + PartIdx];
			bool bIsDecorative = Assembly.Parts[PartIdx]->Instances[InstIdx].DetailLevel == EMeshDetailLevel::Decorative;
			if (bIsDecorative)
			{
				if (PerLODOptions[LODLevel].Decorations == IGeometryProcessing_CombineMeshInstances::EDecorationHandling::Remove)
				{
					return nullptr;
				}
				else if (PerLODOptions[LODLevel].Decorations == IGeometryProcessing_CombineMeshInstances::EDecorationHandling::Approximate)
				{
					SourceIdx = MAX_int32; // approximate decorations with the coarsest available approximation
				}
			}
			// respect whether part/instance allows approximation also
			bool bAllowApproximation = (Assembly.Parts[PartIdx]->bAllowApproximation && Assembly.Parts[PartIdx]->Instances[InstIdx].bAllowApproximation);
			if (!bAllowApproximation)
			{
				const IGeometryProcessing_CombineMeshInstances::FSinglePartMeshSet& PartMeshes = *Assembly.Parts[PartIdx]->PrecomputedMeshes;
				SourceIdx = FMath::Min(SourceIdx, PartMeshes.Source.Num() + PartMeshes.Simplified.Num() - 1);
			}
			const FDynamicMesh3* Mesh = SourceIndexToMesh(PartIdx, SourceIdx);
			return Mesh;
		};
	auto GetLODTriCount = [&PartSourcesPerLOD, &GetPrecomputedMesh, &Assembly, &PerLODOptions, NumParts, &SourceIndexToMesh](int32 LODLevel)
		{
			int32 TriCount = 0;
			int32 UseLODLevel = FMath::Max(LODLevel, 0);
			for (int32 PartIdx = 0; PartIdx < NumParts; ++PartIdx)
			{
				// sum tris per instance, because some instances (and therefore have fewer tris, or no mesh at all)
				int32 NumInst = Assembly.Parts[PartIdx]->Instances.Num();
				for (int32 InstIdx = 0; InstIdx < NumInst; ++InstIdx)
				{
					if (const FDynamicMesh3* Mesh = GetPrecomputedMesh(LODLevel, PartIdx, InstIdx))
					{
						TriCount += Mesh->TriangleCount();
					}
				}
			}
			return TriCount;
		};

	TArray<int32> TriBudget;
	TriBudget.Init(-1, NumLODs);
	for (int32 LODLevel = 0; LODLevel < NumLODs; ++LODLevel)
	{
		int32 PrevLODLevel = FMath::Max(0, LODLevel - 1);
		ECombinedLODType LODType = LODTypes[LODLevel];
		const FLODOpts& Opts = PerLODOptions[LODLevel];
		if (LODType == ECombinedLODType::Copied)
		{
			for (int32 PartIdx = 0; PartIdx < NumParts; ++PartIdx)
			{
				const IGeometryProcessing_CombineMeshInstances::FSinglePartMeshSet& PartMeshes = *Assembly.Parts[PartIdx]->PrecomputedMeshes;
				PartSourcesPerLOD[LODLevel * NumParts + PartIdx] =
					FMath::Clamp(Opts.PreferredLOD, 0, PartMeshes.Source.Num() - 1);
			}
		}
		else if (LODType == ECombinedLODType::Simplified || LODType == ECombinedLODType::Approximated)
		{
			// first pass: copy forward from the previous source indices, + make all part indices appropriate for the requested LOD type
			for (int32 PartIdx = 0; PartIdx < NumParts; ++PartIdx)
			{
				const IGeometryProcessing_CombineMeshInstances::FSinglePartMeshSet& PartMeshes = *Assembly.Parts[PartIdx]->PrecomputedMeshes;
				// Start from the previous LOD level's source index
				int32 UseSourceIdx = PartSourcesPerLOD[PrevLODLevel * NumParts + PartIdx];
				// If approximated meshes are required, enforce that
				if (LODType == ECombinedLODType::Approximated)
				{
					UseSourceIdx = FMath::Max(UseSourceIdx, PartMeshes.Source.Num() + PartMeshes.Simplified.Num());
				}
				PartSourcesPerLOD[LODLevel * NumParts + PartIdx] = UseSourceIdx;
			}

			if (!Opts.bEnableBudgetStrategy_PartLODPromotion || Opts.TriangleBudgetOptions.Method == IGeometryProcessing_CombineMeshInstances::ETriangleBudgetMethod::NoRestriction)
			{
				continue;
			}

			int32 TargetTriCount = Opts.TriangleBudgetOptions.TriangleBudget;
			if (Opts.TriangleBudgetOptions.Method == IGeometryProcessing_CombineMeshInstances::ETriangleBudgetMethod::UsePercentageOfPreviousLOD)
			{
				int32 PrevTriCount = GetLODTriCount(PrevLODLevel);
				TargetTriCount = Opts.TriangleBudgetOptions.LODReductionPercentage * PrevTriCount;
			}
			TriBudget[LODLevel] = TargetTriCount;
			int32 CurTriCount = GetLODTriCount(LODLevel);
			// While needed, apply part promotion.
			// We first promote within categories, so e.g. if we have a mesh still using a source LOD that we can push to a worse source LOD, we do that before pushing anything to a simplified version
			// We then promote to a 'worse' category, prioritizing the parts where the promoted parts have lowest average error
			while (CurTriCount > TargetTriCount * Opts.PartLODPromotionBudgetMultiplier)
			{
				int32 BestCategory = MAX_int32;
				int32 BestSourceIdx = MAX_int32;
				int32 BestPromotableCategory = MAX_int32;
				int32 BestPromotableWithinCatIdx = MAX_int32;
				double BestWithinCatAvgError = FMathd::MaxReal;
				double BestAvgError = FMathd::MaxReal;
				// track the best-to-promote-within-category part index
				int32 BestPromotablePartIdx = -1;
				// track the best-to-promote-overall part index (which may require crossing categories)
				int32 BestPartIdx = -1;
				for (int32 PartIdx = 0; PartIdx < NumParts; ++PartIdx)
				{
					int32 SourceIdx = PartSourcesPerLOD[LODLevel * NumParts + PartIdx];
					bool bCanPromoteWithinCategory, bCanPromoteAtAll;
					int32 WithinCatIdx;
					double PromotedAvgError = 0;
					int32 Category = SourceIndexToMeshCategory(PartIdx, SourceIdx, WithinCatIdx, bCanPromoteWithinCategory, bCanPromoteAtAll, PromotedAvgError);
					if (!bCanPromoteAtAll)
					{
						continue;
					}

					// Prioritize across-category promotions by lower promoted part average error
					if (Category < BestCategory || (Category == BestCategory && PromotedAvgError < BestAvgError))
					{
						BestCategory = Category;
						BestSourceIdx = SourceIdx;
						BestAvgError = PromotedAvgError;
						BestPartIdx = PartIdx;
					}
					if (bCanPromoteWithinCategory)
					{
						// For source meshes (category 0), promote by LOD index primarily (note that source meshes don't have average error stats)
						// For simplified and approximated meshes, promote primarily by average error
						if (Category < BestPromotableCategory || 
						   (Category == 0 && WithinCatIdx < BestPromotableWithinCatIdx) ||
						   (Category == BestPromotableCategory && Category > 0 && PromotedAvgError < BestWithinCatAvgError))
						{
							BestPromotableCategory = Category;
							BestPromotableWithinCatIdx = WithinCatIdx;
							BestPromotablePartIdx = PartIdx;
							BestWithinCatAvgError = PromotedAvgError;
						}
					}
				}
				if (BestPromotablePartIdx != -1 && BestPromotableCategory <= BestCategory)
				{
					PartSourcesPerLOD[LODLevel * NumParts + BestPromotablePartIdx]++;
				}
				else if (BestPartIdx != -1)
				{
					PartSourcesPerLOD[LODLevel * NumParts + BestPartIdx]++;
				}
				else
				{
					break; // couldn't find a promotable part
				}
				CurTriCount = GetLODTriCount(LODLevel);
			}
		}
		else if (LODType == ECombinedLODType::VoxWrapped)
		{
			int32 TargetTriCount = Opts.TriangleBudgetOptions.TriangleBudget;
			if (Opts.TriangleBudgetOptions.Method == IGeometryProcessing_CombineMeshInstances::ETriangleBudgetMethod::UsePercentageOfPreviousLOD
				|| Opts.TriangleBudgetOptions.Method == IGeometryProcessing_CombineMeshInstances::ETriangleBudgetMethod::NoRestriction) // for CoarseLODs, treat 'no restriction' as keeping the budget constant
			{
				// Note: after the first coarse LOD we set the tri budget with respect to the previous budget, not the previous actual tri count,
				// because we haven't yet computed the coarse meshes so do not have the tri counts
				int32 PrevBudget = TriBudget[PrevLODLevel];
				if (LODLevel == FirstVoxWrappedIndex || PrevBudget == -1)
				{
					PrevBudget = GetLODTriCount(PrevLODLevel);
				}
				TargetTriCount = PrevBudget;
				if (Opts.TriangleBudgetOptions.Method == IGeometryProcessing_CombineMeshInstances::ETriangleBudgetMethod::UsePercentageOfPreviousLOD)
				{
					TargetTriCount *= Opts.TriangleBudgetOptions.LODReductionPercentage;
				}
			}
			TriBudget[LODLevel] = TargetTriCount;

			// For voxwrapped LOD levels, reference the last source LOD
			for (int32 PartIdx = 0; PartIdx < NumParts; ++PartIdx)
			{
				const IGeometryProcessing_CombineMeshInstances::FSinglePartMeshSet& PartMeshes = *Assembly.Parts[PartIdx]->PrecomputedMeshes;
				PartSourcesPerLOD[LODLevel * NumParts + PartIdx] = PartMeshes.Source.Num() - 1;
			}
		}
	}


	// iterate over part sets, then for each part, over part LODs, and
	// for each instance append the part LOD to the accumulate LOD mesh
	for (int32 SetIndex = 0; SetIndex < NumParts; ++SetIndex)
	{
		const TUniquePtr<FMeshPart>& Part = Assembly.Parts[SetIndex];
		const IGeometryProcessing_CombineMeshInstances::FSinglePartMeshSet& PartMeshes = *Assembly.Parts[SetIndex]->PrecomputedMeshes;

		check(Part->bAllowMerging == true);		// do not support this yet

		FMeshIndexMappings Mappings;

		for (int32 LODLevel = 0; LODLevel < NumLODs; ++LODLevel)
		{
			int32 SourceIndex = PartSourcesPerLOD[NumParts * LODLevel + SetIndex];
			const FDynamicMesh3* UseAppendMesh = SourceIndexToMesh(SetIndex, SourceIndex);

			ECombinedLODType LevelLODType = LODTypes[LODLevel];

			FCombinedMeshLOD& CombinedMeshLODData = MeshLODs[LODLevel];

			for (int32 InstIdx = 0; InstIdx < Part->Instances.Num(); ++InstIdx)
			{
				const FMeshPartInstance& Instance = Part->Instances[InstIdx];
				const FDynamicMesh3* InstanceAppendMesh = GetPrecomputedMesh(LODLevel, SetIndex, InstIdx);
				if (!InstanceAppendMesh)
				{
					continue;
				}

				if (Instance.FilterLODLevel >= 0 && LODLevel >= Instance.FilterLODLevel)
				{
					continue;
				}

				// need to make a copy to run pre-process func
				FDynamicMesh3 TempAppendMesh(*InstanceAppendMesh);
				if (Assembly.PreProcessInstanceMeshFunc)
				{
					Assembly.PreProcessInstanceMeshFunc(TempAppendMesh, Instance);
				}

				// determine if we should be keeping UVs around for this Part
				bool bPreserveUVs = PerLODOptions[LODLevel].bPreserveUVs
					|| Part->bPreserveUVs;

				// if part does not require UVs, but still has them, discard them here to encourage merging downstream
				if (bPreserveUVs == false && TempAppendMesh.HasAttributes())
				{
					for (int32 UVLayer = 0; UVLayer < TempAppendMesh.Attributes()->NumUVLayers(); ++UVLayer)
					{
						TempAppendMesh.Attributes()->GetUVLayer(UVLayer)->ClearElements();
					}
					// if we have no UVs then tangents are invalid
					TempAppendMesh.Attributes()->DisableTangents();
				}

				Mappings.Reset();
				CombinedMeshLODData.Editor.AppendMesh(&TempAppendMesh, Mappings,
					[&](int, const FVector3d& Pos) { return Instance.WorldTransform.TransformPosition(Pos); },
					[&](int, const FVector3d& Normal) { return Instance.WorldTransform.TransformNormal(Normal); });

				// transfer part IDs etc
				if (CombinedMeshLODData.SubsetIDs != nullptr)
				{
					for (int32 tid : TempAppendMesh.TriangleIndicesItr())
					{
						CombinedMeshLODData.SubsetIDs->SetValue(Mappings.GetNewTriangle(tid), Instance.SubsetID);
					}
				}

				// transfer Material IDs if part mesh has them
				FDynamicMeshMaterialAttribute* AppendMaterialAttrib = TempAppendMesh.HasAttributes() ? TempAppendMesh.Attributes()->GetMaterialID() : nullptr;
				for (int32 tid : TempAppendMesh.TriangleIndicesItr())
				{
					int32 SourceMaterialID = (AppendMaterialAttrib != nullptr) ? AppendMaterialAttrib->GetValue(tid) : 0;
					UMaterialInterface* UseMaterial = Instance.Materials.IsValidIndex(SourceMaterialID) ? Instance.Materials[SourceMaterialID] : nullptr;
					const int32* FoundMaterialIndex = Assembly.MaterialMap.Find(UseMaterial);
					int32 AssignMaterialIndex = (FoundMaterialIndex != nullptr) ? *FoundMaterialIndex : 0;

					CombinedMeshLODData.MaterialIDs->SetValue(Mappings.GetNewTriangle(tid), AssignMaterialIndex);
				}
			}
		}
	}

	// Some Material regions may need to be explicitly preserved, this set will be passed on later
	TSet<int32> PreserveTopologyMaterialIDSet;
	for (UMaterialInterface* Material : AllLODOptions.PreventMergingMaterialSet)
	{
		if (const int32* FoundMaterialIndex = Assembly.MaterialMap.Find(Material))
		{
			PreserveTopologyMaterialIDSet.Add(*FoundMaterialIndex);
		}
	}

	// make a list of per-LOD geometric tolerances that will drive additional optimization,
	// by taking the value from the per-LOD options and making sure it never decreases
	double LastTolerance = 0;
	TArray<double> OptimizationTolerances;
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		double Tolerance = FMath::Max(LastTolerance, PerLODOptions[LODIndex].SimplificationTolerance);
		OptimizationTolerances.Add(Tolerance);
		LastTolerance = Tolerance;
	}


	if (bHaveMultiplePartSubsets)
	{
		TArray<int32> OrderedSubsetIDs(SubsetIDs);

		int32 NumSubsets = SubsetIDs.Num();
		TArray<TArray<FCombinedMeshLOD>> SubsetMeshLODChains;
		SubsetMeshLODChains.SetNum(NumSubsets);
		for (int32 SubsetIndex = 0; SubsetIndex < NumSubsets; ++SubsetIndex)
		{
			SubsetMeshLODChains[SubsetIndex].SetNum(NumLODs);
		}

		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			// split the LOD by subset ID
			FDynamicMesh3& LODMesh = MeshLODs[LODIndex].Mesh;
			FDynamicMeshPolygroupAttribute* SubsetIDAttrib = MeshLODs[LODIndex].SubsetIDs;
			TArray<FDynamicMesh3> SplitMeshes;
			FDynamicMeshEditor::SplitMesh(&LODMesh, SplitMeshes, [&](int32 tid) { return OrderedSubsetIDs.IndexOfByKey(SubsetIDAttrib->GetValue(tid)); });

			// code below assumes this. If it's not the case, then we have some more complex processing to figure out...
			check(SplitMeshes.Num() == NumSubsets);

			// give each subset submesh to the 
			for (int32 SubsetIndex = 0; SubsetIndex < NumSubsets; ++SubsetIndex)
			{
				TArray<FCombinedMeshLOD>& LODChain = SubsetMeshLODChains[SubsetIndex];
				FDynamicMesh3& Submesh = SplitMeshes[SubsetIndex];
				LODChain[LODIndex].SetMesh(MoveTemp(Submesh));
			}
		}

		TArray<UE::Tasks::FTask> PendingSubsetTasks;

		for (int32 SubsetIndex = 0; SubsetIndex < NumSubsets; ++SubsetIndex)
		{
			TArray<FCombinedMeshLOD>& LODChain = SubsetMeshLODChains[SubsetIndex];
			bool bRemoveHiddenFaces = CVarGeometryCombineMeshInstancesRemoveHidden.GetValueOnAnyThread() > 0;
			UE::Tasks::FTask ProcessSubsetTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&LODChain, &OptimizationTolerances, &PreserveTopologyMaterialIDSet, FirstVoxWrappedIndex,
				bRemoveHiddenFaces, &PerLODOptions, &AllLODOptions, NumLODs, &TriBudget]()
				{
					auto LODRemoveHidden = [&PerLODOptions, FirstVoxWrappedIndex](int32 LODIndex) { return PerLODOptions[LODIndex].RemoveHiddenFacesMethod != IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode::None && LODIndex < FirstVoxWrappedIndex; };
					auto LODMergeCoplanar = [&PerLODOptions](int32 LODIndex) { return PerLODOptions[LODIndex].bMergeCoplanarFaces; };
					auto LODPlanarRetriangulate = [&PerLODOptions](int32 LODIndex)
						{
							return PerLODOptions[LODIndex].bMergeCoplanarFaces && PerLODOptions[LODIndex].bPlanarPolygonRetriangulation;
						};
					auto LODRemoveHiddenFaces = [&PerLODOptions](int32 LODIndex)
						{
							return PerLODOptions[LODIndex].RemoveHiddenFacesMethod;
						};
					auto LODRemoveHiddenSamplingDensity = [&PerLODOptions](int32 LODIndex)
						{
							return PerLODOptions[LODIndex].RemoveHiddenSamplingDensity;
						};
					auto LODDoubleSidedHiddenRemoval = [&PerLODOptions](int32 LODIndex)
						{
							return PerLODOptions[LODIndex].bDoubleSidedHiddenRemoval;
						};
					ProcessCombinedLODChain(LODChain, OptimizationTolerances, FirstVoxWrappedIndex,
						NumLODs,
						bRemoveHiddenFaces,
						LODRemoveHidden,
						LODMergeCoplanar,
						LODPlanarRetriangulate,
						LODRemoveHiddenFaces,
						LODRemoveHiddenSamplingDensity,
						LODDoubleSidedHiddenRemoval,
						AllLODOptions.TriangleGroupingIDFunc,
						AllLODOptions.CoarseLODStrategy,
						AllLODOptions.CoarseApproximationDetailSize,
						[&TriBudget](int32 LODIndex) { return TriBudget[LODIndex]; },
						AllLODOptions.CoarseLODBaseTolerance,
						AllLODOptions.HardNormalAngleDeg,
						AllLODOptions.bAutoGenerateMissingUVs,
						AllLODOptions.bAutoGenerateTangents,
						&PreserveTopologyMaterialIDSet
					);
				});
			PendingSubsetTasks.Add(ProcessSubsetTask);
			if (bVerbose)
			{
				ProcessSubsetTask.Wait();
			}
		}

		// wait for all subsets to finish processing
		UE::Tasks::Wait(PendingSubsetTasks);

		CombinedResults.SetNum(NumSubsets);
		for (int32 SubsetIndex = 0; SubsetIndex < NumSubsets; ++SubsetIndex)
		{
			TArray<FCombinedMeshLOD>& LODChain = SubsetMeshLODChains[SubsetIndex];
			BuildOutputSubAssembly(LODChain, OrderedSubsetIDs[SubsetIndex], FirstVoxWrappedIndex, CombinedResults[SubsetIndex]);
		}

	}
	else
	{
		bool bRemoveHiddenFaces = CVarGeometryCombineMeshInstancesRemoveHidden.GetValueOnAnyThread() > 0;
		auto LODRemoveHidden = [&PerLODOptions, FirstVoxWrappedIndex](int32 LODIndex) { return PerLODOptions[LODIndex].RemoveHiddenFacesMethod != IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode::None && LODIndex < FirstVoxWrappedIndex; };
		auto LODMergeCoplanar = [&PerLODOptions](int32 LODIndex) { return PerLODOptions[LODIndex].bMergeCoplanarFaces; };
		auto LODPlanarRetriangulate = [&PerLODOptions](int32 LODIndex)
			{
				return PerLODOptions[LODIndex].bMergeCoplanarFaces && PerLODOptions[LODIndex].bPlanarPolygonRetriangulation;
			};
		auto LODRemoveHiddenFaces = [&PerLODOptions](int32 LODIndex)
			{
				return PerLODOptions[LODIndex].RemoveHiddenFacesMethod;
			};
		auto LODRemoveHiddenSamplingDensity = [&PerLODOptions](int32 LODIndex)
			{
				return PerLODOptions[LODIndex].RemoveHiddenSamplingDensity;
			};
		auto LODDoubleSidedHiddenRemoval = [&PerLODOptions](int32 LODIndex)
			{
				return PerLODOptions[LODIndex].bDoubleSidedHiddenRemoval;
			};
		ProcessCombinedLODChain(MeshLODs, OptimizationTolerances, FirstVoxWrappedIndex,
			NumLODs,
			bRemoveHiddenFaces,
			LODRemoveHidden,
			LODMergeCoplanar,
			LODPlanarRetriangulate,
			LODRemoveHiddenFaces,
			LODRemoveHiddenSamplingDensity,
			LODDoubleSidedHiddenRemoval,
			AllLODOptions.TriangleGroupingIDFunc,
			AllLODOptions.CoarseLODStrategy,
			AllLODOptions.CoarseApproximationDetailSize,
			[&TriBudget](int32 LODIndex) { return TriBudget[LODIndex]; },
			AllLODOptions.CoarseLODBaseTolerance,
			AllLODOptions.HardNormalAngleDeg,
			AllLODOptions.bAutoGenerateMissingUVs,
			AllLODOptions.bAutoGenerateTangents,
			&PreserveTopologyMaterialIDSet
		);

		CombinedResults.SetNum(1);
		BuildOutputSubAssembly(MeshLODs, 0, FirstVoxWrappedIndex, CombinedResults[0]);
	}

}





static void CombineCollisionShapes(
	FSimpleShapeSet3d& CollisionShapes,
	double AxisToleranceDelta = 0.01)
{
	// only going to merge boxes for now
	TArray<FOrientedBox3d> Boxes;
	for (FBoxShape3d Box : CollisionShapes.Boxes)
	{
		Boxes.Add(Box.Box);
	}

	// want to merge larger-volume boxes first
	Boxes.Sort([&](const FOrientedBox3d& A, const FOrientedBox3d& B)
	{
		return A.Volume() > B.Volume();
	});

	auto CalcOffsetVolume = [](FOrientedBox3d Box, double AxisDelta) -> double
	{
		Box.Extents.X = FMathd::Max(0, Box.Extents.X+AxisDelta);
		Box.Extents.Y = FMathd::Max(0, Box.Extents.Y+AxisDelta);
		Box.Extents.Z = FMathd::Max(0, Box.Extents.Z+AxisDelta);
		return Box.Volume();
	};

	double DotTol = 0.99;
	auto HasMatchingAxis = [DotTol](const FVector3d& Axis, const FOrientedBox3d& Box)
	{
		for (int32 k = 0; k < 3; ++k)
		{
			if (FMathd::Abs(Axis.Dot(Box.GetAxis(k))) > DotTol)
			{
				return true;
			}
		}
		return false;
	};

	bool bFoundMerge = true;
	while (bFoundMerge)
	{
		bFoundMerge = false;

		int32 N = Boxes.Num();
		for (int32 i = 0; i < N; ++i)
		{
			FOrientedBox3d Box1 = Boxes[i];

			for (int32 j = i + 1; j < N; ++j)
			{
				FOrientedBox3d Box2 = Boxes[j];

				// should we just be appending box2 to Box1? prevents getting skewed boxes...
				FOrientedBox3d NewBox = Box1.Merge(Box2);

				// check if newbox is still aligned w/ box2?
				bool bAllAxesAligned = true;
				for (int32 k = 0; k < 3; ++k)
				{
					bAllAxesAligned = bAllAxesAligned && HasMatchingAxis(Box1.GetAxis(k), NewBox) && HasMatchingAxis(Box2.GetAxis(k), NewBox);
				}
				if (!bAllAxesAligned)
				{
					continue;
				}

				double SumVolume = Box1.Volume() + Box2.Volume();
				if ( (CalcOffsetVolume(NewBox, AxisToleranceDelta) > SumVolume) &&
						(CalcOffsetVolume(NewBox, -AxisToleranceDelta) < SumVolume) )
				{
					bFoundMerge = true;
					Boxes[i] = NewBox;
					Boxes.RemoveAtSwap(j);
					j = N;
					N--;
				}
			}
		}
	}

	CollisionShapes.Boxes.Reset();
	for (FOrientedBox3d Box : Boxes)
	{
		CollisionShapes.Boxes.Add(FBoxShape3d(Box));
	}
}


void BuildCombinedCollisionShapes(
	const FMeshPartsAssembly& Assembly,
	TArray<int32> SubsetIDsOrdering,
	TArray<FSimpleShapeSet3d>& CombinedCollisionShapes)
{
	int32 NumParts = Assembly.Parts.Num();
	int32 NumSubsets = SubsetIDsOrdering.Num();
	CombinedCollisionShapes.SetNum(NumSubsets);

	for ( int32 SetIndex = 0; SetIndex < NumParts; ++SetIndex )
	{
		const TUniquePtr<FMeshPart>& Part = Assembly.Parts[SetIndex];
		const FSourceGeometry& SourceGeometry = Assembly.SourceMeshGeometry[SetIndex];
		for ( const FMeshPartInstance& Instance : Part->Instances )
		{
			int32 SubsetIndex = SubsetIDsOrdering.IndexOfByKey(Instance.SubsetID);

			bool bIsDecorativePart = (Instance.DetailLevel == EMeshDetailLevel::Decorative);
			if ( ! bIsDecorativePart )
			{
				CombinedCollisionShapes[SubsetIndex].Append(SourceGeometry.CollisionShapes, Instance.WorldTransform);
			}
		}
	}

	// trivially merge any adjacent boxes that merge to a perfect combined-box
	for (FSimpleShapeSet3d& ShapeSet : CombinedCollisionShapes)
	{
		CombineCollisionShapes(ShapeSet, 0.01);
	}
}





void FilterUnusedMaterials(
	TArray<FDynamicMesh3>& Meshes,
	TArray<UMaterialInterface*>& Materials)
{
	FDynamicMeshMaterialAttribute* MaterialIDs = Meshes[0].Attributes()->GetMaterialID();
	if (!MaterialIDs) return;

	TArray<bool> UsedMaterials;
	UsedMaterials.SetNum(Materials.Num());
	for (int32 tid : Meshes[0].TriangleIndicesItr())
	{
		int32 MaterialID = MaterialIDs->GetValue(tid);
		UsedMaterials[MaterialID] = true;
	}
	if (UsedMaterials.Num() == Materials.Num())
	{
		return;
	}
	
	TArray<UMaterialInterface*> NewMaterials;
	TArray<int32> MaterialMap;
	MaterialMap.SetNum(Materials.Num());
	for (int32 k = 0; k < Materials.Num(); ++k)
	{
		if (UsedMaterials[k])
		{
			MaterialMap[k] = NewMaterials.Num();
			NewMaterials.Add(Materials[k]);
		}
	}

	for (FDynamicMesh3& LODMesh : Meshes)
	{
		FDynamicMeshMaterialAttribute* LODMaterialIDs = LODMesh.Attributes()->GetMaterialID();
		for (int32 tid : LODMesh.TriangleIndicesItr())
		{
			int32 MaterialID = LODMaterialIDs->GetValue(tid);
			int32 NewMaterialID = MaterialMap[MaterialID];
			LODMaterialIDs->SetValue(tid, NewMaterialID);
		}
	}

	Materials = MoveTemp(NewMaterials);
}





IGeometryProcessing_CombineMeshInstances::FOptions FCombineMeshInstancesImpl::ConstructDefaultOptions()
{
	//
	// Construct options for ApproximateActors operation
	//
	FOptions Options;

	Options.NumLODs = 5;

	Options.NumCopiedLODs = 1;

	Options.NumSimplifiedLODs = 3;
	Options.SimplifyBaseTolerance = 0.25;
	Options.SimplifyLODLevelToleranceScale = 2.0;

	Options.OptimizeBaseTriCost = 0.7;
	Options.OptimizeLODLevelTriCostScale = 2.5;

	//// LOD level to filter out detail parts
	Options.FilterDecorativePartsLODLevel = 2;


	Options.RemoveHiddenFacesMethod = ERemoveHiddenFacesMode::Fastest;

	return Options;
}



static void SetConstantVertexColor(FDynamicMesh3& Mesh, FLinearColor LinearColor)
{
	if (Mesh.HasAttributes() == false)
	{
		Mesh.EnableAttributes();
	}
	if (Mesh.Attributes()->HasPrimaryColors() == false)
	{
		Mesh.Attributes()->EnablePrimaryColors();
	}
	FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();
	TArray<int32> ElemIDs;
	ElemIDs.SetNum(Mesh.MaxVertexID());
	for (int32 VertexID : Mesh.VertexIndicesItr())
	{
		ElemIDs[VertexID] = Colors->AppendElement( (FVector4f)LinearColor );
	}
	for (int32 TriangleID : Mesh.TriangleIndicesItr())
	{
		FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
		Colors->SetTriangle(TriangleID, FIndex3i(ElemIDs[Triangle.A], ElemIDs[Triangle.B], ElemIDs[Triangle.C]) );
	}
}

// Common compute part meshes code -- to be called after source LOD meshes are populated
static void ComputeSinglePartMeshSet_Helper(
	const IGeometryProcessing_CombineMeshInstances::FComputePartMeshesOptions& Options,
	const IGeometryProcessing_CombineMeshInstances::FComputePartMeshesSinglePartOptions& PartOptions,
	IGeometryProcessing_CombineMeshInstances::FSinglePartMeshSet& Result)
{
	using namespace UE::Geometry;
	const int32 NumSimplifiedLODs = Options.NumSimplifiedLODs;
	const int32 NumApproxLODs = Options.ApproximationTriCosts.Num();
	const double AngleThresholdDeg = Options.HardNormalAngleDeg;

	int32 NumSourceLODs = Result.Source.Num();

	const FDynamicMesh3* SimplificationSourceMesh = Options.SimplificationSourceLOD < Result.Source.Num() ?
		&Result.Source[Options.SimplificationSourceLOD] : &Result.Source.Last();
	FDynamicMeshAABBTree3 SimplificationSourceMeshSpatial(SimplificationSourceMesh, true);
	Result.Simplified.Reserve(NumSimplifiedLODs);
	double UseSimplifyTolerance = Options.SimplifyBaseTolerance;
	for (int32 SimplifyIdx = 0; SimplifyIdx < NumSimplifiedLODs; ++SimplifyIdx)
	{
		TUniquePtr<FDynamicMesh3> ToAdd = MakeUnique<FDynamicMesh3>(*SimplificationSourceMesh);
		FDynamicMesh3& ToSimplify = *ToAdd;
		SimplifyPartMesh(ToSimplify, UseSimplifyTolerance, AngleThresholdDeg, Options.bSimplifyPreserveCorners,
			Options.bSimplifyPreserveUVs || PartOptions.bPreserveUVs,
			Options.bSimplifyPreserveVertexColors, Options.SimplifySharpEdgeAngleDeg, Options.SimplifyMinSalientDimension);
		UseSimplifyTolerance *= Options.SimplifyLODLevelToleranceScale;
		// Add the simplification if it's simpler than the lowest-tri source LOD
		if (ToAdd->TriangleCount() < Result.Source.Last().TriangleCount())
		{
			Result.Simplified.Add(ToAdd.Release());
		}
	}
		
	const FDynamicMesh3* ApproximationSourceMesh = Options.ApproximationSourceLOD < Result.Source.Num() ?
		&Result.Source[Options.ApproximationSourceLOD] : &Result.Simplified[FMath::Clamp(Options.ApproximationSourceLOD - Result.Source.Num(), 0, Result.Simplified.Num())];

	Result.Approximated.Reserve(NumApproxLODs);
	TArray<EApproximatePartMethod> SelectedMethodID; SelectedMethodID.Reserve(NumApproxLODs);		// useful for debugging
	for (int32 ApproxIdx = 0; ApproxIdx < NumApproxLODs; ++ApproxIdx)
	{
		double UseTriCost = Options.ApproximationTriCosts[ApproxIdx];
		Result.Approximated.Add(new FDynamicMesh3(*ApproximationSourceMesh));
		FDynamicMesh3& ToApprox = Result.Approximated.Last();
		EApproximatePartMethod UsedMethod;
		SelectBestFittingMeshApproximation(*ApproximationSourceMesh, SimplificationSourceMeshSpatial,
			PartOptions.ApproximationConstraint, ToApprox, UsedMethod,
			Options.SimplifyBaseTolerance, UseTriCost, Options.MaxAllowableApproximationDeviation);
			
		// update enabled attribs (is this good?)
		ToApprox.EnableMatchingAttributes(*ApproximationSourceMesh);

		// recompute normals
		FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&ToApprox, ToApprox.Attributes()->PrimaryNormals(), AngleThresholdDeg);
		FMeshNormals::QuickRecomputeOverlayNormals(ToApprox);

		// stop making approximations if we are already down to a box
		if (ToApprox.TriangleCount() <= 12)
		{
			break;
		}
	}

	// Compute error metrics for simplified and approximated meshes
	Result.SimplifiedMeshErrors.Reserve(Result.Simplified.Num());
	for (int32 SimplifiedIdx = 0; SimplifiedIdx < Result.Simplified.Num(); ++SimplifiedIdx)
	{
		FVector2d Metric = DeviationMetric(Result.Simplified[SimplifiedIdx], SimplificationSourceMeshSpatial);
		Result.SimplifiedMeshErrors.Add({ Metric[0], Metric[1] });
	}
	Result.ApproximatedMeshErrors.Reserve(Result.Approximated.Num());
	for (int32 ApproxIdx = 0; ApproxIdx < Result.Approximated.Num(); ++ApproxIdx)
	{
		FVector2d Metric = DeviationMetric(Result.Approximated[ApproxIdx], SimplificationSourceMeshSpatial);
		Result.ApproximatedMeshErrors.Add({ Metric[0], Metric[1] });
	}

	if (!Result.Approximated.IsEmpty() && !Result.Simplified.IsEmpty())
	{
		double FirstApproxMetric = FPartApproxSelector::ComputeMetricFromDeviation(
			FVector2d(Result.ApproximatedMeshErrors[0].AverageError, Result.ApproximatedMeshErrors[0].MaxError), 2, Result.Approximated[0].TriangleCount(), Options.MaxAllowableApproximationDeviation);
		for (int32 SimplifiedIdx = Result.Simplified.Num() - 1; SimplifiedIdx >= 0; --SimplifiedIdx)
		{
			double SimplifiedMetric = FPartApproxSelector::ComputeMetricFromDeviation(
				FVector2d(Result.SimplifiedMeshErrors[SimplifiedIdx].AverageError, Result.SimplifiedMeshErrors[SimplifiedIdx].MaxError), 1, Result.Simplified[SimplifiedIdx].TriangleCount(), Options.MaxAllowableApproximationDeviation);
			if (SimplifiedMetric > FirstApproxMetric)
			{
				// simplification is worse than approximation, so delete it
				Result.Simplified.RemoveAt(SimplifiedIdx);
				Result.SimplifiedMeshErrors.RemoveAt(SimplifiedIdx);
			}
			else
			{
				break;
			}
		}
	}

	// optionally planar-remesh the requested Source LODs to reduce their triangle count by removing spurious geometry
	// Note: May invalidate SimplificationSourceMeshSpatial, so we do this last
	if (Options.bRetriangulateSourceLODs)
	{
		for (int32 SourceLODIndex = Options.StartRetriangulateSourceLOD; SourceLODIndex < Result.Source.Num(); ++SourceLODIndex)
		{
			if (!(PartOptions.bPreserveUVs || Options.bSimplifyPreserveUVs || (SourceLODIndex <= Options.PreserveUVLODLevel)))
			{
				PlanarRetriangulatePartMesh(Result.Source[SourceLODIndex], Options.SimplifyBaseTolerance, AngleThresholdDeg);
			}
		}
	}

	// Enforce that simplified and approximated meshes must have decreasing triangle counts
	int32 LastTriCount = Result.Source.Last().TriangleCount();
	auto EnforceDecreasingTriCount = [&LastTriCount](TIndirectArray<FDynamicMesh3>& Meshes, TArray<IGeometryProcessing_CombineMeshInstances::FSinglePartMeshSet::FErrorStats>& MeshErrors)
	{
		for (int32 Idx = 0, Shift = 0; Idx < Meshes.Num(); ++Idx)
		{
			// Shift forward to the first mesh with smaller tri count
			while (Idx + Shift < Meshes.Num() && Meshes[Idx + Shift].TriangleCount() >= LastTriCount)
			{
				Shift++;
			}

			// If there are no meshes to copy back, remove the rest of the array
			if (Idx + Shift >= Meshes.Num())
			{
				Meshes.RemoveAtSwap(Idx, Meshes.Num() - Idx);
				MeshErrors.RemoveAtSwap(Idx, MeshErrors.Num() - Idx);
				break;
			}

			// Update the last tri count and shift if needed
			LastTriCount = Meshes[Idx + Shift].TriangleCount();
			if (Shift > 0)
			{
				Meshes[Idx] = MoveTemp(Meshes[Idx + Shift]);
				MeshErrors[Idx] = MeshErrors[Idx + Shift];
			}
		}
	};
	EnforceDecreasingTriCount(Result.Simplified, Result.SimplifiedMeshErrors);
	EnforceDecreasingTriCount(Result.Approximated, Result.ApproximatedMeshErrors);

}

int32 AddSourceLODs(TIndirectArray<UE::Geometry::FDynamicMesh3>& OutSourceLODs, int32 NumLODs, TFunctionRef<const FMeshDescription*(int)> GetLOD)
{
	OutSourceLODs.Reset(NumLODs);
	for (int32 Idx = 0; Idx < NumLODs; ++Idx)
	{
		const FMeshDescription* Source = GetLOD(Idx);
		if (Source)
		{
			UE::Geometry::FDynamicMesh3* OutputLODMesh = new UE::Geometry::FDynamicMesh3();
			FMeshDescriptionToDynamicMesh Converter;
			Converter.bEnableOutputGroups = true;
			Converter.bTransformVertexColorsLinearToSRGB = true;		// possibly this should be false...
			Converter.Convert(Source, *OutputLODMesh);
			OutSourceLODs.Add(OutputLODMesh);
		}
	}
	return OutSourceLODs.Num();
}

void FCombineMeshInstancesImpl::ComputeSinglePartMeshSet(
	TConstArrayView<const FMeshDescription*> SourceMeshLODs,
	const FComputePartMeshesOptions& Options,
	const FComputePartMeshesSinglePartOptions& PartOptions,
	FSinglePartMeshSet& ResultMeshes)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ComputePartMeshSet_MeshDescriptions);

	int32 NumSources = AddSourceLODs(ResultMeshes.Source, SourceMeshLODs.Num(), [&SourceMeshLODs](int32 LOD) {return SourceMeshLODs[LOD];});

	if (NumSources <= 0)
	{
		return;
	}
	
	TRACE_CPUPROFILER_EVENT_SCOPE(ComputePartMeshSet_PartApprox);
	ComputeSinglePartMeshSet_Helper(Options, PartOptions, ResultMeshes);
}

void FCombineMeshInstancesImpl::ComputeSinglePartMeshSet(
	UStaticMesh* SourceMesh,
	const FComputePartMeshesOptions& Options,
	const FComputePartMeshesSinglePartOptions& PartOptions,
	FSinglePartMeshSet& ResultMeshes
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ComputePartMeshSet_StaticMesh);

	check(SourceMesh);
	int32 NumSources = AddSourceLODs(ResultMeshes.Source, SourceMesh->GetNumSourceModels(), [&SourceMesh](int32 LOD) {return SourceMesh->GetMeshDescription(LOD);});

	if (NumSources <= 0)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(ComputePartMeshSet_PartApprox);
	ComputeSinglePartMeshSet_Helper(Options, PartOptions, ResultMeshes);
}

void FCombineMeshInstancesImpl::ComputePartMeshSets(
	FSourceInstanceList& SourceInstanceList,
	const FComputePartMeshesOptions& Options,
	bool bKeepExistingPartMeshes,
	TArray<TSharedPtr<FSinglePartMeshSet>>& ResultMeshSets
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ComputePartMeshSet_Instances);

	TMap<UStaticMesh*, TSharedPtr<FSinglePartMeshSet>> StaticMeshToPartMap;
	TMap<const IGeometryProcessing_CombineMeshInstances::FMeshLODSet*, TSharedPtr<FSinglePartMeshSet>> MeshLODSetToPartMap;

	int32 NumStaticMeshInstances = SourceInstanceList.StaticMeshInstances.Num();
	int32 NumMeshLODSetInstances = SourceInstanceList.MeshLODSetInstances.Num();

	if (!bKeepExistingPartMeshes)
	{
		ResultMeshSets.Empty();
		for (int32 Index = 0; Index < NumStaticMeshInstances; ++Index)
		{
			IGeometryProcessing_CombineMeshInstances::FStaticMeshInstance& SourceMeshInstance = SourceInstanceList.StaticMeshInstances[Index];
			SourceMeshInstance.PrecomputedMeshes.Reset();
		}
		for (int32 Index = 0; Index < NumMeshLODSetInstances; ++Index)
		{
			IGeometryProcessing_CombineMeshInstances::FMeshLODSetInstance& SourceMeshInstance = SourceInstanceList.MeshLODSetInstances[Index];
			SourceMeshInstance.PrecomputedMeshes.Reset();
		}
	}
	else
	{
		for (int32 Index = 0; Index < NumStaticMeshInstances; ++Index)
		{
			const IGeometryProcessing_CombineMeshInstances::FStaticMeshInstance& SourceMeshInstance = SourceInstanceList.StaticMeshInstances[Index];
			if (SourceMeshInstance.PrecomputedMeshes.IsValid())
			{
				UStaticMesh* StaticMesh = SourceMeshInstance.SourceMesh;
				StaticMeshToPartMap.Add(StaticMesh, SourceMeshInstance.PrecomputedMeshes);
			}
		}
		for (int32 Index = 0; Index < NumMeshLODSetInstances; ++Index)
		{
			const IGeometryProcessing_CombineMeshInstances::FMeshLODSetInstance& SourceMeshInstance = SourceInstanceList.MeshLODSetInstances[Index];
			if (SourceMeshInstance.PrecomputedMeshes.IsValid())
			{
				int32 MeshSetIndex = SourceMeshInstance.MeshLODSetIndex;
				if (MeshSetIndex < 0 || MeshSetIndex > SourceInstanceList.MeshLODSets.Num())
				{
					ensure(false);
					continue;
				}
				const IGeometryProcessing_CombineMeshInstances::FMeshLODSet* MeshLODSet = &SourceInstanceList.MeshLODSets[MeshSetIndex];
				MeshLODSetToPartMap.Add(MeshLODSet, SourceMeshInstance.PrecomputedMeshes);
			}
		}
	}

	

	// TODO: compute these in parallel?
	for (int32 Index = 0; Index < NumStaticMeshInstances; ++Index)
	{
		IGeometryProcessing_CombineMeshInstances::FStaticMeshInstance& SourceMeshInstance = SourceInstanceList.StaticMeshInstances[Index];
		if (SourceMeshInstance.PrecomputedMeshes.IsValid())
		{
			continue;
		}

		UStaticMesh* StaticMesh = SourceMeshInstance.SourceMesh;
		TSharedPtr<FSinglePartMeshSet>* Found = StaticMeshToPartMap.Find(StaticMesh);
		if (Found)
		{
			SourceMeshInstance.PrecomputedMeshes = *Found;
		}
		else
		{
			TSharedPtr<FSinglePartMeshSet>& NewMeshSet = ResultMeshSets.Add_GetRef(MakeShared<FSinglePartMeshSet>());
			EApproximationType ApproximationConstraint = EApproximationType::NoConstraint;
			bool bPreservePartUVs = false;
			if (SourceInstanceList.InstanceGroupDatas.IsValidIndex(SourceMeshInstance.GroupDataIndex))
			{
				ApproximationConstraint = SourceInstanceList.InstanceGroupDatas[SourceMeshInstance.GroupDataIndex].ApproximationConstraint;
				bPreservePartUVs = SourceInstanceList.InstanceGroupDatas[SourceMeshInstance.GroupDataIndex].bPreserveUVs;
			}
			IGeometryProcessing_CombineMeshInstances::FComputePartMeshesSinglePartOptions PartOptions(ApproximationConstraint, bPreservePartUVs);
			ComputeSinglePartMeshSet(StaticMesh, Options, PartOptions, *NewMeshSet);
			StaticMeshToPartMap.Add(StaticMesh, NewMeshSet);
			SourceMeshInstance.PrecomputedMeshes = NewMeshSet;
		}
	}

	// TODO: compute these in parallel?
	for (int32 Index = 0; Index < NumMeshLODSetInstances; ++Index)
	{
		IGeometryProcessing_CombineMeshInstances::FMeshLODSetInstance& SourceMeshInstance = SourceInstanceList.MeshLODSetInstances[Index];
		if (SourceMeshInstance.PrecomputedMeshes.IsValid())
		{
			continue;
		}

		int32 MeshSetIndex = SourceMeshInstance.MeshLODSetIndex;
		if (MeshSetIndex < 0 || MeshSetIndex > SourceInstanceList.MeshLODSets.Num())
		{
			ensure(false);
			continue;
		}
		const IGeometryProcessing_CombineMeshInstances::FMeshLODSet* MeshLODSet = &SourceInstanceList.MeshLODSets[MeshSetIndex];

		TSharedPtr<FSinglePartMeshSet>* Found = MeshLODSetToPartMap.Find(MeshLODSet);
		if (Found)
		{
			SourceMeshInstance.PrecomputedMeshes = *Found;
		}
		else
		{
			TSharedPtr<FSinglePartMeshSet>& NewMeshSet = ResultMeshSets.Add_GetRef(MakeShared<FSinglePartMeshSet>());
			EApproximationType ApproximationConstraint = EApproximationType::NoConstraint;
			if (SourceInstanceList.InstanceGroupDatas.IsValidIndex(SourceMeshInstance.GroupDataIndex))
			{
				ApproximationConstraint = SourceInstanceList.InstanceGroupDatas[SourceMeshInstance.GroupDataIndex].ApproximationConstraint;
			}
			IGeometryProcessing_CombineMeshInstances::FComputePartMeshesSinglePartOptions PartOptions(ApproximationConstraint);
			ComputeSinglePartMeshSet(MeshLODSet->ReferencedMeshLODs, Options, PartOptions, *NewMeshSet);
			MeshLODSetToPartMap.Add(MeshLODSet, NewMeshSet);

			SourceMeshInstance.PrecomputedMeshes = NewMeshSet;
		}
	}
}

void FCombineMeshInstancesImpl::CombineMeshInstances(
	const FSourceInstanceList& MeshInstances, const FOptions& Options, FResults& ResultsOut)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInstances);

	bool bVerbose = CVarGeometryCombineMeshInstancesVerbose.GetValueOnAnyThread();
	if (bVerbose)
	{
		int32 NumApproxLODs = FMath::Max(0, Options.NumLODs - Options.NumCopiedLODs - Options.NumSimplifiedLODs - Options.NumCoarseLODs);
		UE_LOG(LogGeometry, Log, TEXT("CombineMeshInstances: processing %d Instances into %d LODs (%d Copied, %d Simplified, %d Approx, %d Coarse)"),
			( MeshInstances.StaticMeshInstances.Num() + MeshInstances.MeshLODSetInstances.Num() ),
			Options.NumLODs, Options.NumCopiedLODs, Options.NumSimplifiedLODs, NumApproxLODs, Options.NumCoarseLODs);
	}

	FMeshPartsAssembly PartAssembly;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInst_Setup);
		InitializeMeshPartAssembly(MeshInstances, PartAssembly);
		if (bVerbose)
		{
			UE_LOG(LogGeometry, Log, TEXT("  PartAssembly contains %d Parts, %d Unique Materials"), 
				PartAssembly.Parts.Num(), PartAssembly.UniqueMaterials.Num());
		}

		if (PartAssembly.Parts.Num() == 0)
		{
			// todo: set some kind of error code in ResultsOut...
			return;
		}

		InitializeAssemblySourceMeshesFromLOD(PartAssembly, Options.BaseCopiedLOD, Options.NumCopiedLODs);
		InitializePartAssemblySpatials(PartAssembly);
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInst_PartApprox);
		ComputeMeshApproximations(Options, PartAssembly);
	}


	
	PartAssembly.PreProcessInstanceMeshFunc = [&PartAssembly, &MeshInstances, &Options](FDynamicMesh3& AppendMesh, const FMeshPartInstance& Instance)
	{
		int32 SourceInstance = Instance.ExternalInstanceIndex.A;
		bool bIsStaticMeshInstance = (Instance.ExternalInstanceIndex.B == 0);		// a bit of a hack here but we configured this above

		int GroupDataIdx = (bIsStaticMeshInstance) ?
			MeshInstances.StaticMeshInstances[SourceInstance].GroupDataIndex
			: MeshInstances.MeshLODSetInstances[SourceInstance].GroupDataIndex;

		if (MeshInstances.InstanceGroupDatas[GroupDataIdx].bHasConstantOverrideVertexColor)
		{
			FLinearColor VertexColorLinear(0,0,0,1);
			if (Options.VertexColorMappingMode == EVertexColorMappingMode::TriangleCountMetric)
			{
				const double UseMax = 25.0;
				double TriCountRelToBox = FMath::Clamp((double)AppendMesh.TriangleCount() / (double)12, 1.0, UseMax);		// 12 is num tris in a bounding box
				double T = (TriCountRelToBox) / (UseMax);
				T = FMathd::Sqrt(T);	// improve color mapping somewhat (try better options?)
				VertexColorLinear = FLinearColor::LerpUsingHSV(
					FLinearColor::White, FLinearColor::Red, FMath::Clamp(T, 0.0, 1.0));
			}
			else
			{
				FColor VertexColorSRGB = MeshInstances.InstanceGroupDatas[GroupDataIdx].OverrideVertexColor;
				VertexColorLinear = VertexColorSRGB.ReinterpretAsLinear();
			}

			SetConstantVertexColor(AppendMesh, VertexColorLinear);
		}
	};

	// build combined mesh LOD chains for each sub-assembly
	TArray<FCombinedSubAssembly> CombinedResults;
	TArray<int32> SubsetIDsOrdering;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInst_BuildMeshes);
		BuildCombinedMesh(PartAssembly, Options, CombinedResults);
		for (FCombinedSubAssembly& Assembly : CombinedResults)
		{
			SubsetIDsOrdering.Add(Assembly.SubAssemblyID);
		}
	}

	// build combined collision shapes, respecting sub-assembly ordering
	TArray<FSimpleShapeSet3d> CombinedCollisionShapes;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInst_BuildCollision);
		BuildCombinedCollisionShapes(PartAssembly, SubsetIDsOrdering, CombinedCollisionShapes);

		if (bVerbose && CombinedCollisionShapes.Num() == 1)
		{
			UE_LOG(LogGeometry, Log, TEXT("  CombinedCollisionShapes[0] contains %d Boxes, %d Convexes"), 
				CombinedCollisionShapes[0].Boxes.Num(), CombinedCollisionShapes[0].Convexes.Num());
		}
	}


	// build final results data structure
	ResultsOut.CombinedMeshes.SetNum(CombinedResults.Num());
	for (int32 k = 0; k < CombinedResults.Num(); ++k)
	{
		ResultsOut.CombinedMeshes[k].MeshLODs = MoveTemp(CombinedResults[k].MeshLODs);

		FPhysicsDataCollection PhysicsData;
		PhysicsData.Geometry = CombinedCollisionShapes[k];
		PhysicsData.CopyGeometryToAggregate();		// need FPhysicsDataCollection to convert to agg geom, should fix this
		ResultsOut.CombinedMeshes[k].SimpleCollisionShapes = PhysicsData.AggGeom;

		ResultsOut.CombinedMeshes[k].MaterialSet = PartAssembly.UniqueMaterials;
		if (CombinedResults.Num() > 1)	
		{
			// if we have multiple outputs, they may not each use the full material set, in this case we will filter the materials (should this be optional?)
			FilterUnusedMaterials(ResultsOut.CombinedMeshes[k].MeshLODs, ResultsOut.CombinedMeshes[k].MaterialSet);
		}

		ResultsOut.CombinedMeshes[k].InstanceSubsetID = CombinedResults[k].SubAssemblyID;
	}
}


void FCombineMeshInstancesImpl::CombineMeshInstances(
	const FSourceInstanceList& MeshInstances,
	const FCombineMeshInstancesOptionsGeneral& AllLODOptions,
	TConstArrayView<FCombineMeshInstancesOptionsPerLOD> PerLODOptions,
	FResults& ResultsOut
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInstances);

	bool bVerbose = CVarGeometryCombineMeshInstancesVerbose.GetValueOnAnyThread();
	
	FMeshPartsAssembly PartAssembly;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInst_Setup);
		InitializeMeshPartAssembly(MeshInstances, PartAssembly);
		if (bVerbose)
		{
			UE_LOG(LogGeometry, Log, TEXT("  PartAssembly contains %d Parts, %d Unique Materials"),
				PartAssembly.Parts.Num(), PartAssembly.UniqueMaterials.Num());
		}


		// require that all instances have precomputed part meshes
		for (const TUniquePtr<FMeshPart>& Part : PartAssembly.Parts)
		{
			if (!Part->PrecomputedMeshes.IsValid())
			{
				UE_LOG(LogGeometry, Warning, TEXT("Failed to compute CombineMeshInstances because some instances did not have precomputed part meshes"));
				return;
			}
		}

		// Initialize just the collision shapes on source mesh geometry
		PartAssembly.SourceMeshGeometry.SetNum(PartAssembly.Parts.Num());
		for (int32 Index = 0; Index < PartAssembly.Parts.Num(); ++Index)
		{
			TUniquePtr<FMeshPart>& Part = PartAssembly.Parts[Index];
			FSourceGeometry& Target = PartAssembly.SourceMeshGeometry[Index];

			if (UStaticMesh* StaticMesh = Part->SourceAsset)
			{
				if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
				{
					UE::Geometry::GetShapeSet(BodySetup->AggGeom, Target.CollisionShapes);
				}
			}
			else if (Part->SourceMeshLODSet != nullptr)
			{
				UE::Geometry::GetShapeSet(Part->SourceMeshLODSet->SimpleCollisionShapes, Target.CollisionShapes);
			}

			// sometimes simple collision is a convex when it's actually a box - could try to detect here?

		}
	}

	PartAssembly.PreProcessInstanceMeshFunc = [&PartAssembly, &MeshInstances, &AllLODOptions](FDynamicMesh3& AppendMesh, const FMeshPartInstance& Instance)
	{
		int32 SourceInstance = Instance.ExternalInstanceIndex.A;
		bool bIsStaticMeshInstance = (Instance.ExternalInstanceIndex.B == 0);		// a bit of a hack here but we configured this above

		int GroupDataIdx = (bIsStaticMeshInstance) ?
			MeshInstances.StaticMeshInstances[SourceInstance].GroupDataIndex
			: MeshInstances.MeshLODSetInstances[SourceInstance].GroupDataIndex;

		if (MeshInstances.InstanceGroupDatas[GroupDataIdx].bHasConstantOverrideVertexColor)
		{
			FLinearColor VertexColorLinear(0, 0, 0, 1);
			if (AllLODOptions.VertexColorMappingMode == EVertexColorMappingMode::TriangleCountMetric)
			{
				const double UseMax = 25.0;
				double TriCountRelToBox = FMath::Clamp((double)AppendMesh.TriangleCount() / (double)12, 1.0, UseMax);		// 12 is num tris in a bounding box
				double T = (TriCountRelToBox) / (UseMax);
				T = FMathd::Sqrt(T);	// improve color mapping somewhat (try better options?)
				VertexColorLinear = FLinearColor::LerpUsingHSV(
					FLinearColor::White, FLinearColor::Red, FMath::Clamp(T, 0.0, 1.0));
			}
			else
			{
				FColor VertexColorSRGB = MeshInstances.InstanceGroupDatas[GroupDataIdx].OverrideVertexColor;
				VertexColorLinear = VertexColorSRGB.ReinterpretAsLinear();
			}

			SetConstantVertexColor(AppendMesh, VertexColorLinear);
		}
	};

	// build combined mesh LOD chains for each sub-assembly
	TArray<FCombinedSubAssembly> CombinedResults;
	TArray<int32> SubsetIDsOrdering;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInst_BuildMeshes);
		BuildCombinedMeshFromPrecomputedMeshes(PartAssembly, AllLODOptions, PerLODOptions, CombinedResults);
		for (FCombinedSubAssembly& Assembly : CombinedResults)
		{
			SubsetIDsOrdering.Add(Assembly.SubAssemblyID);
		}
	}

	// build combined collision shapes, respecting sub-assembly ordering
	TArray<FSimpleShapeSet3d> CombinedCollisionShapes;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInst_BuildCollision);
		BuildCombinedCollisionShapes(PartAssembly, SubsetIDsOrdering, CombinedCollisionShapes);

		if (bVerbose && CombinedCollisionShapes.Num() == 1)
		{
			UE_LOG(LogGeometry, Log, TEXT("  CombinedCollisionShapes[0] contains %d Boxes, %d Convexes"),
				CombinedCollisionShapes[0].Boxes.Num(), CombinedCollisionShapes[0].Convexes.Num());
		}
	}


	// build final results data structure
	ResultsOut.CombinedMeshes.SetNum(CombinedResults.Num());
	for (int32 k = 0; k < CombinedResults.Num(); ++k)
	{
		ResultsOut.CombinedMeshes[k].MeshLODs = MoveTemp(CombinedResults[k].MeshLODs);

		FPhysicsDataCollection PhysicsData;
		PhysicsData.Geometry = CombinedCollisionShapes[k];
		PhysicsData.CopyGeometryToAggregate();		// need FPhysicsDataCollection to convert to agg geom, should fix this
		ResultsOut.CombinedMeshes[k].SimpleCollisionShapes = PhysicsData.AggGeom;

		ResultsOut.CombinedMeshes[k].MaterialSet = PartAssembly.UniqueMaterials;
		if (CombinedResults.Num() > 1)
		{
			// if we have multiple outputs, they may not each use the full material set, in this case we will filter the materials (should this be optional?)
			FilterUnusedMaterials(ResultsOut.CombinedMeshes[k].MeshLODs, ResultsOut.CombinedMeshes[k].MaterialSet);
		}

		ResultsOut.CombinedMeshes[k].InstanceSubsetID = CombinedResults[k].SubAssemblyID;
	}
}
