// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ChaosFleshGenerateSurfaceBindingsNode.h"

#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Chaos/AABBTree.h"
#include "Chaos/BoundingVolumeHierarchy.h"
#include "Chaos/Tetrahedron.h"
#include "Chaos/TriangleMesh.h"
#include "ChaosFlesh/FleshCollectionEngineUtility.h"
#include "ChaosFlesh/TetrahedralCollection.h"
#include "Containers/Map.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryCollection/Facades/CollectionTetrahedralBindingsFacade.h"
#include "IndexTypes.h"
#include "MeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/PrimaryAssetId.h"
#include "Dataflow/ChaosFleshNodesUtility.h"

DEFINE_LOG_CATEGORY(LogMeshBindings);

void
BuildVertexToVertexAdjacencyBuffer(
	const UE::Geometry::FDynamicMesh3 DynamicMesh,
	TArray<TArray<uint32>>& NeighborNodes)
{
	NeighborNodes.SetNum(DynamicMesh.VertexCount());
	for (int i = 0; i < DynamicMesh.TriangleCount(); i++)
	{
		const UE::Geometry::FIndex3i& Tri = DynamicMesh.GetTriangle(i);
		TArray<uint32>& N0 = NeighborNodes[Tri[0]];
		N0.AddUnique(Tri[1]);
		N0.AddUnique(Tri[2]);
		TArray<uint32>& N1 = NeighborNodes[Tri[1]];
		N1.AddUnique(Tri[0]);
		N1.AddUnique(Tri[2]);
		TArray<uint32>& N2 = NeighborNodes[Tri[2]];
		N2.AddUnique(Tri[0]);
		N2.AddUnique(Tri[1]);
	}
}

void
BuildVertexToVertexAdjacencyBuffer(
	const FSkeletalMeshLODRenderData& LodRenderData,
	TArray<TArray<uint32>>& NeighborNodes)
{
	const FRawStaticIndexBuffer16or32Interface* IndexBuffer = LodRenderData.MultiSizeIndexContainer.GetIndexBuffer();
	const uint32 IndexCount = IndexBuffer->Num();

	const FPositionVertexBuffer& VertexBuffer = LodRenderData.StaticVertexBuffers.PositionVertexBuffer;
	const uint32 VertexCount = LodRenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();

	NeighborNodes.SetNum(0); // clear, to init clean
	NeighborNodes.SetNum(VertexCount);

	int32 BaseTriangle = 0;
	int32 BaseVertex = 0;
	for (int32 SectionIndex = 0; SectionIndex < LodRenderData.RenderSections.Num(); ++SectionIndex)
	{
		FSkelMeshRenderSection const& RenderSection = LodRenderData.RenderSections[SectionIndex];
		int32 NumTriangles = RenderSection.NumTriangles;
		int32 NumVertices = RenderSection.NumVertices;

		TArray<uint32> RedirectionArray;
		RedirectionArray.SetNum(VertexCount);
		TMap<FVector, int32 /*UniqueVertexIndex*/> UniqueIndexMap;

		for (int32 TriangleIt = BaseTriangle; TriangleIt < BaseTriangle + NumTriangles; ++TriangleIt)
		{
			const uint32 V[3] =
			{
				IndexBuffer->Get(TriangleIt * 3 + 0),
				IndexBuffer->Get(TriangleIt * 3 + 1),
				IndexBuffer->Get(TriangleIt * 3 + 2)
			};

			const FVector P[3] =
			{
				(FVector)VertexBuffer.VertexPosition(V[0]),
				(FVector)VertexBuffer.VertexPosition(V[1]),
				(FVector)VertexBuffer.VertexPosition(V[2])
			};

			for (int32 i = 0; i < 3; ++i)
			{
				const uint32 VertexIndex = RedirectionArray[V[i]] = UniqueIndexMap.FindOrAdd(P[i], V[i]);
				TArray<uint32>& AdjacentVertices = NeighborNodes[VertexIndex];
				for (int32 a = 1; a < 3; ++a)
				{
					const uint32 AdjacentVertexIndex = V[(i + a) % 3];
					if (VertexIndex != AdjacentVertexIndex)
					{
						AdjacentVertices.AddUnique(AdjacentVertexIndex);
					}
				}
			}
		}

		for (int32 VertexIt = BaseVertex + 1; VertexIt < BaseVertex + NumVertices; ++VertexIt)
		{
			// if this vertex has a sibling we copy the data over
			const int32 SiblingIndex = RedirectionArray[VertexIt];
			if (SiblingIndex != VertexIt)
			{
				for (int32 i = 0; i < NeighborNodes[SiblingIndex].Num(); i++)
				{
					const uint32 OtherNode = NeighborNodes[SiblingIndex][i];
					if (OtherNode != VertexIt)
					{
						NeighborNodes[VertexIt].AddUnique(OtherNode);
					}
				}
			}
		}

		BaseTriangle += NumTriangles;
		BaseVertex += NumVertices;
	}
}



void
FGenerateSurfaceBindings::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		TUniquePtr<FTetrahedralCollection> InCollection(GetValue<FManagedArrayCollection>(Context, &Collection).NewCopy<FTetrahedralCollection>());
		const TManagedArray<FIntVector4>* Tetrahedron = 
			InCollection->FindAttribute<FIntVector4>(
				FTetrahedralCollection::TetrahedronAttribute, FTetrahedralCollection::TetrahedralGroup);
		const TManagedArray<int32>* TetrahedronStart =
			InCollection->FindAttribute<int32>(
				FTetrahedralCollection::TetrahedronStartAttribute, FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>* TetrahedronCount =
			InCollection->FindAttribute<int32>(
				FTetrahedralCollection::TetrahedronCountAttribute, FGeometryCollection::GeometryGroup);
		const TManagedArray<TArray<int32>>* IncidentElements =
			InCollection->FindAttribute<TArray<int32>>(
				FTetrahedralCollection::IncidentElementsAttribute, FGeometryCollection::VerticesGroup);

		const TManagedArray<FIntVector>* Triangle =
			InCollection->FindAttribute<FIntVector>(
				"Indices", FGeometryCollection::FacesGroup);
		const TManagedArray<int32>* FacesStart =
			InCollection->FindAttribute<int32>(
				"FaceStart", FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>* FacesCount =
			InCollection->FindAttribute<int32>(
				"FaceCount", FGeometryCollection::GeometryGroup);

		const TManagedArray<FVector3f>* Vertex = 
			InCollection->FindAttribute<FVector3f>(
				"Vertex", "Vertices");

		TObjectPtr<const USkeletalMesh> SkeletalMesh = GetValue<TObjectPtr<const USkeletalMesh>>(Context, &SkeletalMeshIn);
		TObjectPtr<const UStaticMesh> StaticMesh = GetValue<TObjectPtr<const UStaticMesh>>(Context, &StaticMeshIn);
		if ((SkeletalMesh || StaticMesh) && 
			Tetrahedron && TetrahedronStart && TetrahedronCount &&
			Triangle && FacesStart && FacesCount &&
			Vertex)
		{
			// Extract positions to bind
			FString MeshId;
			TArray<TArray<FVector3f>> MeshVertices;
			TArray<TArray<TArray<uint32>>> MeshNeighborNodes;
			if (SkeletalMesh)
			{
				FPrimaryAssetId Id = SkeletalMesh->GetPrimaryAssetId();
				MeshId = ChaosFlesh::GetMeshId(SkeletalMesh, bUseSkeletalMeshImportModel);

				if (!bUseSkeletalMeshImportModel)
				{
					FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();

					MeshVertices.SetNum(RenderData->LODRenderData.Num());
					MeshNeighborNodes.SetNum(RenderData->LODRenderData.Num());
					for (int32 i = 0; i < RenderData->LODRenderData.Num(); i++)
					{
						FSkeletalMeshLODRenderData* LODRenderData = &RenderData->LODRenderData[i];
						const FPositionVertexBuffer& PositionVertexBuffer =
							LODRenderData->StaticVertexBuffers.PositionVertexBuffer;

						TArray<FVector3f>& Vertices = MeshVertices[i];
						Vertices.SetNumUninitialized(PositionVertexBuffer.GetNumVertices());
						for (uint32 j = 0; j < PositionVertexBuffer.GetNumVertices(); j++)
						{
							const FVector3f& Pos = PositionVertexBuffer.VertexPosition(j);
							Vertices[j] = Pos;
						}

						TArray<TArray<uint32>>& NeighborNodes = MeshNeighborNodes[i];
						BuildVertexToVertexAdjacencyBuffer(*LODRenderData, NeighborNodes);
					}
				}
#if WITH_EDITOR
				else // Import Model
				{
					const int32 LODIndex = 0;
					MeshVertices.SetNum(1);
					MeshNeighborNodes.SetNum(1);

					// Check first if we have bulk data available and non-empty.
					FMeshDescription SourceMesh;
#if WITH_EDITORONLY_DATA
					if (SkeletalMesh->HasMeshDescription(LODIndex))
					{
						// @todo(brice) : Confirm correct in release 5.4
						//FSkeletalMeshImportData SkeletalMeshImportData;
						//SkeletalMesh->LoadLODImportedData(LODIndex, SkeletalMeshImportData);
						//FSkeletalMeshBuildSettings Settings;
						//SkeletalMeshImportData.GetMeshDescription(SkeletalMesh, &Settings, SourceMesh);
						SkeletalMesh->CloneMeshDescription(LODIndex, SourceMesh);
					}
					else
#endif
					{
						// Fall back on the LOD model directly if no bulk data exists. When we commit
						// the mesh description, we override using the bulk data. This can happen for older
						// skeletal meshes, from UE 4.24 and earlier.
						const FSkeletalMeshModel* SkeletalMeshModel = SkeletalMesh->GetImportedModel();
						if (SkeletalMeshModel && SkeletalMeshModel->LODModels.IsValidIndex(LODIndex))
						{
							SkeletalMeshModel->LODModels[LODIndex].GetMeshDescription(SkeletalMesh, LODIndex, SourceMesh);
						}
					}

					UE::Geometry::FDynamicMesh3 DynamicMesh;
					FMeshDescriptionToDynamicMesh Converter;
					Converter.Convert(&SourceMesh, DynamicMesh);

					//const FVertexArray& SourceVertices = SourceMesh.Vertices();
					TArray<FVector3f>& Vertices = MeshVertices[LODIndex];
					Vertices.SetNumUninitialized(DynamicMesh.VertexCount());
					for (int32 j = 0; j < DynamicMesh.VertexCount(); j++)
					{
						const FVector3d& Pos = DynamicMesh.GetVertex(j);
						Vertices[j].Set(Pos[0], Pos[1], Pos[2]);
					}

					TArray<TArray<uint32>>& NeighborNodes = MeshNeighborNodes[LODIndex];
					BuildVertexToVertexAdjacencyBuffer(DynamicMesh, NeighborNodes);
				}
#endif
			}
			else // StaticMesh
			{
				MeshId = ChaosFlesh::GetMeshId(StaticMesh);

				const FStaticMeshRenderData* RenderData = StaticMesh->GetRenderData();
				const int32 NumLOD = RenderData->LODResources.Num();
				MeshVertices.SetNum(NumLOD);
				for (int32 i = 0; i < NumLOD; i++)
				{
					const FStaticMeshLODResources& LODResources = RenderData->LODResources[i];
					const FPositionVertexBuffer& PositionVertexBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
					
					TArray<FVector3f>& Vertices = MeshVertices[i];
					Vertices.SetNumUninitialized(PositionVertexBuffer.GetNumVertices());
					for (uint32 j = 0; j < PositionVertexBuffer.GetNumVertices(); j++)
					{
						const FVector3f& Pos = PositionVertexBuffer.VertexPosition(j);
						Vertices[j] = Pos;
					}
				}
			}
			TArray<FString> GeometryGroupGuidsLocal = GetValue<TArray<FString>>(Context, &GeometryGroupGuidsIn);
			const TManagedArray<FString>* Guids = InCollection->FindAttribute<FString>("Guid", FGeometryCollection::GeometryGroup);

			// Build Tetrahedra
			TArray<Chaos::TTetrahedron<Chaos::FReal>> Tets;			// Index 0 == TetMeshStart
			TArray<Chaos::TTetrahedron<Chaos::FReal>*> BVHTetPtrs;

			//
			// Init boundary mesh for projections.
			//
			Chaos::FTriangleMesh TetBoundaryMesh;
			Chaos::FTriangleMesh::TBVHType<Chaos::FRealDouble> TetBoundaryBVH;
			TArray<Chaos::FVec3> VertexD;
			TConstArrayView<Chaos::TVec3<Chaos::FRealDouble>> VertexDView(VertexD);
			TArray<Chaos::FVec3> PointNormals;

			TArray<int32> GeometryGroupSelected;
			if (IsConnected(&TransformSelection))
			{
				FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);
				GeometryGroupSelected = InCollection->TransformSelectionToGeometryIndices(InTransformSelection.AsArray());
			}
			else
			{
				for (int32 TetMeshIdx = 0; TetMeshIdx < TetrahedronStart->Num(); ++TetMeshIdx)
				{
					GeometryGroupSelected.Add(TetMeshIdx);
				}
			}
			for (const int32& TetMeshIdx: GeometryGroupSelected)
			{
				if (GeometryGroupGuidsLocal.Num() && Guids)
				{
					if (!GeometryGroupGuidsLocal.Contains((*Guids)[TetMeshIdx]))
					{
						continue;
					}
				}
				const int32 TetMeshStart = (*TetrahedronStart)[TetMeshIdx];
				const int32 TetMeshCount = (*TetrahedronCount)[TetMeshIdx];
				if (TetMeshCount > 0)
				{
					for (int32 i = 0; i < TetMeshCount; i++)
					{
						const int32 Idx = TetMeshStart + i;
						const FIntVector4& Tet = (*Tetrahedron)[Idx];
						Tets.Add(Chaos::TTetrahedron<Chaos::FReal>(
							(*Vertex)[Tet[0]],
							(*Vertex)[Tet[1]],
							(*Vertex)[Tet[2]],
							(*Vertex)[Tet[3]]));
					}

					if (bDoSurfaceProjection)
					{
						TetBoundaryMesh.Init(
								reinterpret_cast<const TArray<Chaos::TVec3<int32>>&>(Triangle->GetConstArray()), 0, -1, false);
						
						// Promote vertices to double because that's what FTriangleMesh wants.
						VertexD.SetNumUninitialized(Vertex->Num());
						for (int32 i = 0; i < VertexD.Num(); i++)
						{
							VertexD[i] = Chaos::FVec3((*Vertex)[i][0], (*Vertex)[i][1], (*Vertex)[i][2]);
						}
						VertexDView = TConstArrayView<Chaos::TVec3<Chaos::FRealDouble>>(VertexD);

						PointNormals = TetBoundaryMesh.GetPointNormals(VertexDView, false, true);
						TetBoundaryMesh.BuildBVH(VertexDView, TetBoundaryBVH);
					}
				}
			}
			// Init BVH for tetrahedra.
			BVHTetPtrs.SetNumUninitialized(Tets.Num());
			for (int32 TetIdx = 0; TetIdx < Tets.Num(); ++TetIdx)
			{
				BVHTetPtrs[TetIdx] = &Tets[TetIdx];
			}
			Chaos::TBoundingVolumeHierarchy<
				TArray<Chaos::TTetrahedron<Chaos::FReal>*>,
				TArray<int32>,
				Chaos::FReal,
				3> TetBVH(BVHTetPtrs);

			//
			// Do intersection tests against tets, then the surface.
			//

			TArray<TArray<FIntVector4>> Parents; Parents.SetNum(MeshVertices.Num());
			TArray<TArray<FVector4f>> Weights;	 Weights.SetNum(MeshVertices.Num());
			TArray<TArray<FVector3f>> Offsets;	 Offsets.SetNum(MeshVertices.Num());
			TArray<TArray<float>> Masks;		 Masks.SetNum(MeshVertices.Num());
			TArray<int32> Orphans;
			int32 TetHits = 0;
			int32 TriHits = 0;
			int32 Adoptions = 0;
			int32 NumOrphans = 0;
			for (int32 LOD = 0; LOD < MeshVertices.Num(); LOD++)
			{
				Parents[LOD].SetNumUninitialized(MeshVertices[LOD].Num());
				Weights[LOD].SetNumUninitialized(MeshVertices[LOD].Num());
				Offsets[LOD].SetNumUninitialized(MeshVertices[LOD].Num());
				Masks[LOD].SetNumUninitialized(MeshVertices[LOD].Num());

				TArray<int32> TetIntersections; TetIntersections.Reserve(64);
				for (int32 i = 0; i < MeshVertices[LOD].Num(); i++)
				{
					Parents[LOD][i] = FIntVector4(INDEX_NONE);
					Weights[LOD][i] = FVector4f(0);
					Offsets[LOD][i] = FVector3f(0);
					Masks[LOD][i] = 0.0; // Shader does skinning for this vertex

					const FVector3f& Pos = MeshVertices[LOD][i];
					Chaos::TVec3<Chaos::FReal> PosD(Pos[0], Pos[1], Pos[2]);
					TetIntersections = TetBVH.FindAllIntersections(PosD);
					int32 j = 0;
					for (; j < TetIntersections.Num(); j++)
					{
						const int32 TetIdx = TetIntersections[j];
						if (!Tets[TetIdx].Outside(Pos, 0.f)) // includes boundary
						{
							TetHits++;
							Parents[LOD][i] = (*Tetrahedron)[TetIdx];
							Chaos::TVector<Chaos::FReal, 4> WeightsD = Tets[TetIdx].GetBarycentricCoordinates(Pos);
							Weights[LOD][i] = FVector4f(WeightsD[0], WeightsD[1], WeightsD[2], WeightsD[3]);
							Offsets[LOD][i] = FVector3f(0);
							Masks[LOD][i] = 1.0; // Shader does sim for this vertex

							FVector3f EmbeddedPos =
								(*Vertex)[Parents[LOD][i][0]] * Weights[LOD][i][0] +
								(*Vertex)[Parents[LOD][i][1]] * Weights[LOD][i][1] +
								(*Vertex)[Parents[LOD][i][2]] * Weights[LOD][i][2] +
								(*Vertex)[Parents[LOD][i][3]] * Weights[LOD][i][3];
							check((Pos - EmbeddedPos).SquaredLength() < UE_SMALL_NUMBER);
							break;
						}
					}
					if (j == TetIntersections.Num())
					{
						// This vertex didn't land inside any tetrahedra. Project it to the tet boundary surface.
						int32 TriIdx = INDEX_NONE;
						Chaos::FVec3 TriWeights;
						if (bDoSurfaceProjection &&
							TetBoundaryMesh.SmoothProject(
								TetBoundaryBVH,
								VertexDView,
								PointNormals, Pos,
								TriIdx, TriWeights, SurfaceProjectionIterations))
						{
							TriHits++;
							const FIntVector& Tri = (*Triangle)[TriIdx];
							Parents[LOD][i][0] = Tri[0];
							Parents[LOD][i][1] = Tri[1];
							Parents[LOD][i][2] = Tri[2];
							Parents[LOD][i][3] = INDEX_NONE;

							Weights[LOD][i][0] = TriWeights[0];
							Weights[LOD][i][1] = TriWeights[1];
							Weights[LOD][i][2] = TriWeights[2];
							Weights[LOD][i][3] = 0.0;

							const FVector3f EmbeddedPos =
								TriWeights[0] * Vertex->GetConstArray()[Tri[0]] +
								TriWeights[1] * Vertex->GetConstArray()[Tri[1]] +
								TriWeights[2] * Vertex->GetConstArray()[Tri[2]];
							Offsets[LOD][i] = EmbeddedPos - Pos;

							Masks[LOD][i] = 1.0; // Shader does sim for this vertex
						}
						else
						{
							// Despair...
							Orphans.Add(i);

							Parents[LOD][i][0] = INDEX_NONE;
							Parents[LOD][i][1] = INDEX_NONE;
							Parents[LOD][i][2] = INDEX_NONE;
							Parents[LOD][i][3] = INDEX_NONE;

							Weights[LOD][i][0] = 0.0;
							Weights[LOD][i][1] = 0.0;
							Weights[LOD][i][2] = 0.0;
							Weights[LOD][i][3] = 0.0;

							Offsets[LOD][i][0] = 0.0;
							Offsets[LOD][i][1] = 0.0;
							Offsets[LOD][i][2] = 0.0;

							Masks[LOD][i] = 0.0; // Shader does skinning for this vertex
						} // if !SmoothProject()
					} // if !TetIntersections
				} // end for all vertices

				// 
				// Advancing front orphan reparenting
				//
				if (!MeshNeighborNodes.IsValidIndex(LOD))
				{
					continue;
				}
				const TArray<TArray<uint32>>& NeighborNodes = MeshNeighborNodes[LOD];
				TSet<int32> OrphanSet(Orphans);
				while (bDoOrphanReparenting && Orphans.Num())
				{
					// Find the orphan with the fewest number of orphan neighbors, and the 
					// most non-orphans in their 1 ring.
					int32 Orphan = INDEX_NONE;
					int32 NumOrphanNeighbors = TNumericLimits<int32>::Max();
					int32 NumNonOrphanNeighbors = 0;
					for (int32 i = 0; i < Orphans.Num(); i++)
					{
						int32 CurrOrphan = Orphans[i];
						if (!NeighborNodes.IsValidIndex(CurrOrphan))
						{
							continue;
						}
						const TArray<uint32>& Neighbors = NeighborNodes[CurrOrphan];
						int32 OrphanCount = 0;
						int32 NonOrphanCount = 0;
						for (int32 j = 0; j < Neighbors.Num(); j++)
						{
							if (OrphanSet.Contains(Neighbors[j]))
							{
								OrphanCount++;
							}
							else
							{
								NonOrphanCount++;
							}
						}
						if (OrphanCount <= NumOrphanNeighbors && NonOrphanCount > NumNonOrphanNeighbors)
						{
							Orphan = CurrOrphan;
							NumOrphanNeighbors = OrphanCount;
							NumNonOrphanNeighbors = NonOrphanCount;
						}
					}
					if (Orphan == INDEX_NONE)
					{
						// We only have orphans with no neighbors left.
						break;
					}
					const FVector3f& Pos = MeshVertices[LOD][Orphan];
					Chaos::TVec3<Chaos::FReal> PosD(Pos[0], Pos[1], Pos[2]);

					// Use the parent simplices of non-orphan neighbors as test candidates.
					Chaos::FReal CurrDist = TNumericLimits<Chaos::FReal>::Max();
					const TArray<uint32>& Neighbors = NeighborNodes[Orphan];
					bool FoundBinding = false;
					for (int32 i = 0; i < Neighbors.Num(); i++)
					{
						const uint32 Neighbor = Neighbors[i];
						if (OrphanSet.Contains(Neighbor))
						{
							continue;
						}

						const FIntVector4& P = Parents[LOD][Neighbor];
						int32 NumValid = 0;
						for (int32 j = 0; j < 4; j++)
						{
							NumValid += P[j] != INDEX_NONE ? 1 : 0;
						}

						if (NumValid == 0)
						{
							continue;
						}
						else
						{
							// Find tets that share parent indices
							for (int32 j = 0; j < 4; j++)
							{
								const int32 ParentIdx = P[j];
								if (IncidentElements->GetConstArray().IsValidIndex(ParentIdx))
								{
									const TArray<int32>& NeighborTets = (*IncidentElements)[ParentIdx];
									for (int32 k = 0; k < NeighborTets.Num(); k++)
									{
										const int32 TetIdx = NeighborTets[k];
										if (ensure(Tets.IsValidIndex(TetIdx)))
										{
											const Chaos::FTetrahedron& Tet = Tets[TetIdx];

											Chaos::TVec4<Chaos::FReal> W;
											Chaos::TVec3<Chaos::FReal> EmbeddedPos = Tet.FindClosestPointAndBary(PosD, W, 1.0e-4);
											Chaos::TVec3<Chaos::FReal> O = EmbeddedPos - PosD;
											Chaos::FReal Dist = O.SquaredLength();
											if (Dist < CurrDist)
											{
												CurrDist = Dist;
												Parents[LOD][Orphan] = (*Tetrahedron)[TetIdx];
												Weights[LOD][Orphan] = FVector4f(W[0], W[1], W[2], W[3]);
												Offsets[LOD][Orphan] = FVector3f(O[0], O[1], O[2]);
												Masks[LOD][i] = 1.0; // Shader does sim for this vertex
												FoundBinding = true;
											}
										}
									}
								}
							}
						}
					} // end for all neighbors

					// Whether or not we successfully reparented, remove the orphan from the list.
					OrphanSet.Remove(Orphan);
					Orphans.Remove(Orphan);
					if (FoundBinding)
					{
						Adoptions++;
					}
					else
					{
						NumOrphans++;
					}
				} // end while(Orphans)
				NumOrphans += Orphans.Num();

				//ELogVerbosity::Type Verbosity = Orphans.Num() > 0 ? ELogVerbosity::Error : ELogVerbosity::Display;
				UE_LOG(LogMeshBindings, Display,
					TEXT("'%s' - Generated mesh bindings between tet mesh and %s mesh of '%s' LOD %d - stats:\n"
						"    Render vertices num: %d\n"
						"    Vertices in tetrahedra: %d\n"
						"    Vertices bound to tet surface: %d\n"
						"    Orphaned vertices reparented: %d\n"
						"    Vertices orphaned: %d"),
					*GetName().ToString(),
					bUseSkeletalMeshImportModel ? TEXT("import") : TEXT("render"),
					*MeshId, LOD,
					MeshVertices[LOD].Num(), TetHits, TriHits, Adoptions, NumOrphans);

			} // end for all LOD

			// Stash bindings in the geometry collection
			GeometryCollection::Facades::FTetrahedralBindings TetBindings(*InCollection);
			TetBindings.DefineSchema();
			FName MeshName(*MeshId, MeshId.Len());
			for (int32 LOD = 0; LOD < MeshVertices.Num(); LOD++)
			{
				TetBindings.AddBindingsGroup(/*TetMeshIdx = */ 0, MeshName, LOD);
				TetBindings.SetBindingsData(Parents[LOD], Weights[LOD], Offsets[LOD], Masks[LOD]);
			}
		}
		SetValue<const FManagedArrayCollection&>(Context, *InCollection, &Collection);
	}
}
