// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ChaosFleshSetVertexTrianglePositionTargetBindingNode.h"

#include "Chaos/Utilities.h"
#include "Chaos/HierarchicalSpatialHash.h"
#include "Chaos/TriangleCollisionPoint.h"
#include "GeometryCollection/Facades/CollectionMeshFacade.h"
#include "GeometryCollection/Facades/CollectionPositionTargetFacade.h"
#include "ChaosFlesh/TetrahedralCollection.h"
#include "ChaosFlesh/ChaosFleshCollectionFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosFleshSetVertexTrianglePositionTargetBindingNode)

void FSetVertexTrianglePositionTargetBindingDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		DataType InCollection = GetValue<DataType>(Context, &Collection);

		TUniquePtr<FFleshCollection> InFleshCollection(GetValue<DataType>(Context, &Collection).NewCopy<FFleshCollection>());

		Chaos::FFleshCollectionFacade TetCollection(*InFleshCollection);
		if (TManagedArray<FVector3f>* Vertices = InCollection.FindAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup))
		{
			if (TManagedArray<FIntVector>* Indices = InCollection.FindAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup))
			{
				if (TetCollection.IsTetrahedronValid())
				{
					TArray<FVector3f> Vertex = TetCollection.Vertex.Get().GetConstArray();
					TetCollection.ComponentSpaceVertices(Vertex);
					GeometryCollection::Facades::FCollectionMeshFacade MeshFacade(InCollection);
					TArray<int32> ComponentIndex = MeshFacade.GetGeometryGroupIndexArray();
					TArray<Chaos::TVector<float, 3>> IndicesPositions; 
					TArray<Chaos::TVector<int32, 3>> IndicesArray;
					for (int32 i = 0; i < Indices->Num(); i++)
					{
						Chaos::TVector<int32, 3> CurrentIndices(0);
						for (int32 j = 0; j < 3; j++) 
						{
							CurrentIndices[j] = (*Indices)[i][j];
						}
						if (CurrentIndices[0] != INDEX_NONE
							&& CurrentIndices[1] != INDEX_NONE
							&& CurrentIndices[2] != INDEX_NONE)
						{
							IndicesArray.Emplace(CurrentIndices);
						}
					}
					TArray<TArray<int32>> LocalIndex;
					TArray<TArray<int32>>* LocalIndexPtr = &LocalIndex;
					TArray<TArray<int>> GlobalIndex = Chaos::Utilities::ComputeIncidentElements(IndicesArray, LocalIndexPtr);
					int32 ActualParticleCount = 0;
					for (int32 l = 0; l < GlobalIndex.Num(); l++)
					{
						if (GlobalIndex[l].Num() > 0)
						{
							ActualParticleCount += 1;
						}
					}
					
					IndicesPositions.SetNum(ActualParticleCount);
					TArray<int32> IndicesMap;
					IndicesMap.SetNum(ActualParticleCount);
					int32 CurrentParticleIndex = 0;
					for (int32 i = 0; i < GlobalIndex.Num(); i++)
					{
						if (GlobalIndex[i].Num() > 0)
						{
							IndicesPositions[CurrentParticleIndex] = (*Vertices)[(*Indices)[GlobalIndex[i][0]][LocalIndex[i][0]]];
							IndicesMap[CurrentParticleIndex] = (*Indices)[GlobalIndex[i][0]][LocalIndex[i][0]];
							CurrentParticleIndex += 1;
						}
					}

					//Compute max bound and determine detection radius
					Chaos::TVec3<float> CoordMaxs(-FLT_MAX);
					Chaos::TVec3<float> CoordMins(FLT_MAX);
					for (int32 i = 0; i < Vertex.Num(); i++)
					{
						for (int32 j = 0; j < 3; j++)
						{
							if (Vertex[i][j] > CoordMaxs[j])
							{
								CoordMaxs[j] = Vertex[i][j];
							}
							if (Vertex[i][j] < CoordMins[j])
							{
								CoordMins[j] = Vertex[i][j];
							}
						}
					}
					Chaos::TVec3<float> CoordDiff = (CoordMaxs - CoordMins) * VertexRadiusRatio;
					Chaos::FReal SphereRadius = Chaos::FReal(FGenericPlatformMath::Max(CoordDiff[0], FGenericPlatformMath::Max(CoordDiff[1], CoordDiff[2])));
					
					if (IsConnected(&VertexSelection))
					{
						FDataflowVertexSelection InDataflowVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);
						IndicesMap = InDataflowVertexSelection.AsArray();

					}

					GeometryCollection::Facades::FPositionTargetFacade PositionTargets(InCollection);
					PositionTargets.DefineSchema();

					TArray<Chaos::TVec3<Chaos::FReal>> VertexTVec3;
					VertexTVec3.SetNum(Vertex.Num());
					for (int32 VertexIdx = 0; VertexIdx < Vertex.Num(); VertexIdx++)
					{
						VertexTVec3[VertexIdx] = Chaos::TVec3<Chaos::FReal>(Vertex[VertexIdx]);
					}
					Chaos::FTriangleMesh TriangleMesh;
					TriangleMesh.Init(IndicesArray);
					Chaos::FTriangleMesh::TSpatialHashType<Chaos::FReal> SpatialHash;
					TConstArrayView<Chaos::TVec3<Chaos::FReal>> ConstArrayViewVertex(VertexTVec3);
					TriangleMesh.BuildSpatialHash(ConstArrayViewVertex, SpatialHash, SphereRadius);
					for (int32 PointIndex: IndicesMap)
					{
						TArray<Chaos::TTriangleCollisionPoint<Chaos::FReal>> Result;
						if (TriangleMesh.PointClosestTriangleQuery(SpatialHash, ConstArrayViewVertex,
							PointIndex, VertexTVec3[PointIndex], SphereRadius / 2.f, SphereRadius / 2.f,
							[this, &ComponentIndex, &IndicesArray](const int32 PointIndex, const int32 TriangleIndex)->bool
							{
								return ComponentIndex[PointIndex] != ComponentIndex[IndicesArray[TriangleIndex][0]];
							},
							Result))
						{
							for (const Chaos::TTriangleCollisionPoint<Chaos::FReal>& CollisionPoint : Result)
							{
								GeometryCollection::Facades::FPositionTargetsData DataPackage;
								DataPackage.TargetIndex.Init(PointIndex, 1);
								DataPackage.TargetWeights.Init(1.f, 1);
								DataPackage.SourceWeights.Init(1.f, 3);
								DataPackage.SourceIndex.Init(-1, 3);
								DataPackage.SourceIndex[0] = IndicesArray[CollisionPoint.Indices[1]][0];
								DataPackage.SourceIndex[1] = IndicesArray[CollisionPoint.Indices[1]][1];
								DataPackage.SourceIndex[2] = IndicesArray[CollisionPoint.Indices[1]][2];
								DataPackage.SourceWeights[0] = CollisionPoint.Bary[1]; //convention: Bary[0] point, Bary[1:3] triangle
								DataPackage.SourceWeights[1] = CollisionPoint.Bary[2];
								DataPackage.SourceWeights[2] = CollisionPoint.Bary[3];
								if (TManagedArray<float>* Mass = InCollection.FindAttribute<float>("Mass", FGeometryCollection::VerticesGroup))
								{
									DataPackage.Stiffness = 0.f;
									for (int32 k = 0; k < 3; k++)
									{
										DataPackage.Stiffness += DataPackage.SourceWeights[k] * PositionTargetStiffness * (*Mass)[DataPackage.SourceIndex[k]];
									}
									DataPackage.Stiffness += DataPackage.TargetWeights[0] * PositionTargetStiffness * (*Mass)[DataPackage.TargetIndex[0]];
								}
								else
								{
									DataPackage.Stiffness = PositionTargetStiffness;
								}
								PositionTargets.AddPositionTarget(DataPackage);
							}
						}
					}
				}
			}
		}
		SetValue(Context, MoveTemp(InCollection), &Collection);
	}
}
