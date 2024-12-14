// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGDynamicMeshData.h"

#include "PCGContext.h"
#include "PCGModule.h"
#include "Data/PCGPointData.h"
#include "Elements/PCGVolumeSampler.h"

#include "UDynamicMesh.h"
#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshQueryFunctions.h"
#include "Helpers/PCGHelpers.h"
#include "Serialization/ArchiveCrc32.h"

UPCGDynamicMeshData::UPCGDynamicMeshData(const FObjectInitializer& ObjectInitializer)
	: UPCGSpatialData(ObjectInitializer)
{
	DynamicMesh = ObjectInitializer.CreateDefaultSubobject<UDynamicMesh>(this, TEXT("DynamicMesh"));
}

void UPCGDynamicMeshData::Initialize(UDynamicMesh* InMesh, bool bCanTakeOwnership, const TArray<UMaterialInterface*>& InMaterials)
{
	check(DynamicMesh);
	if (bCanTakeOwnership)
	{
		Initialize(std::move(InMesh->GetMeshRef()), InMaterials);
	}
	else
	{
		// Make a copy
		Initialize(UE::Geometry::FDynamicMesh3(InMesh->GetMeshRef()), InMaterials);
	}
}

void UPCGDynamicMeshData::Initialize(UE::Geometry::FDynamicMesh3&& InMesh, const TArray<UMaterialInterface*>& InMaterials)
{
	check(DynamicMesh);
	DynamicMesh->SetMesh(std::move(InMesh));
	ResetBounds();
	
	bDynamicMeshOctreeIsDirty = true;
	
	SetMaterials(InMaterials);
}

void UPCGDynamicMeshData::ResetBounds() const
{
	check(DynamicMesh);
	CachedBounds = static_cast<FBox>(DynamicMesh->GetMeshRef().GetBounds());
	bDynamicMeshBoundsAreDirty = false;
}

FBox UPCGDynamicMeshData::GetBounds() const
{
	if (bDynamicMeshBoundsAreDirty)
	{
		DynamicMeshBoundsLock.Lock();
		if (bDynamicMeshBoundsAreDirty)
		{
			ResetBounds();
		}
		DynamicMeshBoundsLock.Unlock();
	}

	return CachedBounds;
}

void UPCGDynamicMeshData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);

	if (bFullDataCrc)
	{
		// Implementation note: metadata not supported at this point.

		uint32 UniqueTypeID = StaticClass()->GetDefaultObject()->GetUniqueID();
		Ar << UniqueTypeID;

		Ar << DynamicMesh->GetMeshRef();
	}
	else
	{
		AddUIDToCrc(Ar);
	}
}

bool UPCGDynamicMeshData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	if (!DynamicMesh)
	{
		return false;
	}
	
	const FVector BoxCenter = InTransform.TransformPosition(InBounds.GetCenter());
	const FVector HalfScaledExtents(InBounds.GetExtent() * InTransform.GetScale3D());
	const UE::Geometry::FAxisAlignedBox3d ScaledBounds(BoxCenter - HalfScaledExtents, BoxCenter + HalfScaledExtents);
	
	if (!ScaledBounds.Intersects(GetBounds()))
	{
		return false;
	}

	const UE::Geometry::FDynamicMeshOctree3& Octree = GetDynamicMeshOctree();
	const UE::Geometry::FDynamicMesh3& MeshRef = DynamicMesh->GetMeshRef();
	

	// TODO: Awful way to do this, need to implement a triangle AABB box intersection if we want more precision. But not even sure that is the right approach using the Octree
	auto TriangleIntersectionTest = [this, &ScaledBounds, MeshRef](int32 TriangleID) -> bool
	{
		return ScaledBounds.Intersects(MeshRef.GetTriBounds(TriangleID));
	};

	const bool bIntersect = Octree.ParallelOverlapAnyQuery(ScaledBounds, TriangleIntersectionTest) != INDEX_NONE;

	if (bIntersect)
	{
		OutPoint.Transform = InTransform;
		OutPoint.SetLocalBounds(InBounds);
		OutPoint.Density = 1.0f;
		return true;
	}
	else
	{
		return false;
	}
}

const UE::Geometry::FDynamicMeshOctree3& UPCGDynamicMeshData::GetDynamicMeshOctree() const
{
	if (DynamicMesh && bDynamicMeshOctreeIsDirty)
	{
		FScopeLock Lock(&DynamicMeshOctreeLock);

		if (bDynamicMeshOctreeIsDirty)
		{
			if (DynamicMeshOctree.Mesh != DynamicMesh->GetMeshPtr())
			{
				// Was never initialized, or with another mesh, reset it
				if (DynamicMeshOctree.Mesh)
				{
					DynamicMeshOctree = UE::Geometry::FDynamicMeshOctree3{};
				}
				
				DynamicMeshOctree.Initialize(&DynamicMesh->GetMeshRef());
			}
			else
			{
				// We can re-insert all triangles to avoid recomputing the whole octree
				DynamicMeshOctree.ReinsertTriangles(DynamicMesh->GetMeshRef().TriangleIndicesItr());
			}
			
			bDynamicMeshOctreeIsDirty = false;
		}
	}

	return DynamicMeshOctree;
}

const UPCGPointData* UPCGDynamicMeshData::ToPointData(FPCGContext* Context, const FBox& InBounds) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGPrimitiveData::CreatePointData);

	check(DynamicMesh);

	FGeometryScriptVectorList VerticesList;
	bool Dummy;
	UGeometryScriptLibrary_MeshQueryFunctions::GetAllVertexPositions(DynamicMesh, VerticesList, /*bSkipGaps=*/false, /*bHasVertexIDGaps=*/Dummy);

	TArray<FVector>& Vertices = *VerticesList.List.Get();
	
	UPCGPointData* Data = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
	TArray<FPCGPoint>& Points = Data->GetMutablePoints();
	Points.Reserve(Vertices.Num());
	for (const FVector& Vertex : Vertices)
	{
		if (InBounds.IsValid && !InBounds.IsInside(Vertex))
		{
			continue;
		}
		
		FPCGPoint& Point = Points.Emplace_GetRef();
		Point.Transform.SetLocation(Vertex);
		PCGHelpers::ComputeSeedFromPosition(Vertex);
	}
	
	UE_LOG(LogPCG, Verbose, TEXT("Dynamic Mesh extracted %d points"), Points.Num());

	return Data;
}

void UPCGDynamicMeshData::SetMaterials(const TArray<UMaterialInterface*>& InMaterials)
{
	Materials.Reset(InMaterials.Num());
	Materials.Append(InMaterials);
}

void UPCGDynamicMeshData::InitializeDynamicMeshComponentFromData(UDynamicMeshComponent* InComponent) const
{
	check(InComponent);

	InComponent->SetMesh(UE::Geometry::FDynamicMesh3(DynamicMesh->GetMeshRef()));
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		InComponent->SetMaterial(i, Materials[i]);
	}
}

UPCGSpatialData* UPCGDynamicMeshData::CopyInternal(FPCGContext* Context) const
{
	UPCGDynamicMeshData* NewDynamicMeshData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
	
	NewDynamicMeshData->Initialize(DynamicMesh, /*bCanTakeOwnership=*/false);
	NewDynamicMeshData->Materials = Materials;

	return NewDynamicMeshData;
}
