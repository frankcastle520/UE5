// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowEngine.h"
//#include "Dataflow/DataflowEngine.h"
//#include "GeometryCollection/ManagedArrayCollection.h"
#include "Dataflow/ChaosFleshNodesUtility.h"

#include "ChaosFleshCreateTetrahedralCollectionNode.generated.h"

class USkeletalMesh;
class UStaticMesh;
class FFleshCollection;
namespace UE {
	namespace Geometry {
		class FDynamicMesh3;
	}
}


USTRUCT(meta = (DataflowFlesh, Deprecated = "5.4"))
struct FGenerateTetrahedralCollectionDataflowNodes : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGenerateTetrahedralCollectionDataflowNodes, "GenerateTetrahedralCollection", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender", FGeometryCollection::StaticType(), "Collection")

public:
	typedef FManagedArrayCollection DataType;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	TEnumAsByte<TetMeshingMethod> Method = TetMeshingMethod::IsoStuffing;

	//
	// IsoStuffing
	//

	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (ClampMin = "1", EditCondition = "Method == TetMeshingMethod::IsoStuffing", EditConditionHides))
	int32 NumCells = 32;

	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (ClampMin = "-0.5", ClampMax = "0.5", EditCondition = "Method == TetMeshingMethod::IsoStuffing", EditConditionHides))
	double OffsetPercent = 0.05;

	//
	// TetWild
	//

	//! Energy at which to stop optimizing tet quality and accept the result.
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (ClampMin = "0.0", EditCondition = "Method == TetMeshingMethod::TetWild", EditConditionHides))
	double IdealEdgeLength = 1.0;

	//! Maximum number of optimization iterations.
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (ClampMin = "1", EditCondition = "Method == TetMeshingMethod::TetWild", EditConditionHides))
	int32 MaxIterations = 80;

	//! Energy at which to stop optimizing tet quality and accept the result.
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (ClampMin = "0.0", EditCondition = "Method == TetMeshingMethod::TetWild", EditConditionHides))
	double StopEnergy = 10;

	//! Relative tolerance, controlling how closely the mesh must follow the input surface.
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (ClampMin = "0.0", EditCondition = "Method == TetMeshingMethod::TetWild", EditConditionHides))
	double EpsRel = 1e-3;

	//! Coarsen the tet mesh result.
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (EditCondition = "Method == TetMeshingMethod::TetWild", EditConditionHides))
	bool bCoarsen = false;

	//! Enforce that the output boundary surface should be manifold.
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (EditCondition = "Method == TetMeshingMethod::TetWild", EditConditionHides))
	bool bExtractManifoldBoundarySurface = false;

	//! Skip the initial simplification step.
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (EditCondition = "Method == TetMeshingMethod::TetWild", EditConditionHides))
	bool bSkipSimplification = false;

	//! Invert tetrahedra.
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (EditCondition = "Method == TetMeshingMethod::TetWild", EditConditionHides))
	bool bInvertOutputTets = false;

	//
	// Common
	//

	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (DataflowInput, DisplayName = "StaticMesh"))
	TObjectPtr<const UStaticMesh> StaticMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (DataflowInput, DisplayName = "SkeletalMesh"))
	TObjectPtr<const USkeletalMesh> SkeletalMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	bool bComputeByComponent = false;

	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	bool bDiscardInteriorTriangles = true;

	FGenerateTetrahedralCollectionDataflowNodes(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&StaticMesh);
		RegisterInputConnection(&SkeletalMesh);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

protected:
	void EvaluateIsoStuffing(UE::Dataflow::FContext& Context, TUniquePtr<FFleshCollection>& InCollection, const UE::Geometry::FDynamicMesh3& DynamicMesh) const;
	void EvaluateTetWild(UE::Dataflow::FContext& Context, TUniquePtr<FFleshCollection>& InCollection, const UE::Geometry::FDynamicMesh3& DynamicMesh) const;
};




