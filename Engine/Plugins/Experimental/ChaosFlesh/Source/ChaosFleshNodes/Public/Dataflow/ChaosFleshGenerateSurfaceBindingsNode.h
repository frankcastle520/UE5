// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Dataflow/DataflowSelection.h"
#include "ChaosFleshGenerateSurfaceBindingsNode.generated.h"

class UStaticMesh;
class USkeletalMesh;

DECLARE_LOG_CATEGORY_EXTERN(LogMeshBindings, Verbose, All);

/** Generate barycentric bindings (used by the FleshDeformer deformer graph) of a render surface to a tetrahedral mesh. */
USTRUCT(meta = (DataflowFlesh))
struct FGenerateSurfaceBindings : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGenerateSurfaceBindings, "GenerateSurfaceBindings", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender", FGeometryCollection::StaticType(), "Collection")

public:
	/** Passthrough geometry collection. Bindings are stored as standalone groups in the \p Collection, keyed by the name of the input render mesh and all available LOD's. */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	/** The input mesh, whose render surface is used to generate bindings. */
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (DataflowInput, DisplayName = "StaticMesh"))
	TObjectPtr<const UStaticMesh> StaticMeshIn = nullptr;

	/** The input mesh, whose render surface is used to generate bindings. */
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (DataflowInput, DisplayName = "SkeletalMesh"))
	TObjectPtr<const USkeletalMesh> SkeletalMeshIn = nullptr;

	/** Only meshes with transforms in TransformSelection will be bound. */
	UPROPERTY(meta = (DataflowInput, DisplayName = "(Optional) TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Only meshes with transforms in TransformSelection will be bound. */
	UPROPERTY(meta = (DataflowInput, DisplayName = "(Optional) GeometryGroupGuids"))
	TArray<FString> GeometryGroupGuidsIn;

	/** Use the import geometry of the skeletal mesh. */
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (DisplayName = "UseSkeletalMeshImportModel"))
	bool bUseSkeletalMeshImportModel = false;

	/** Enable binding to the exterior hull of the tetrahedron mesh. */
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (DisplayName = "DoSurfaceProjection"))
	bool bDoSurfaceProjection = true;

	/** The maximum number of iterations to try expanding the domain while looking for surface triangles to bind to. */
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (DisplayName = "SurfaceProjectionIterations", EditCondition = "bDoSurfaceProjection == true"))
	uint32 SurfaceProjectionIterations = 10;

	/** When nodes aren't contained in tetrahedra and surface projection fails, try to find suitable bindings by looking to neighboring parents. */
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (DisplayName = "DoOrphanReparenting"))
	bool bDoOrphanReparenting = true;

	FGenerateSurfaceBindings(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterInputConnection(&StaticMeshIn);
		RegisterInputConnection(&SkeletalMeshIn);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&GeometryGroupGuidsIn);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};