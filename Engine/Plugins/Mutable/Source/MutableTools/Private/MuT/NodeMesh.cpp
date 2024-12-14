// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/NodeMesh.h"

#include "MuR/MeshPrivate.h"
#include "MuR/MutableTrace.h"
#include "Misc/AssertionMacros.h"
#include "Spatial/PointHashGrid3.h"


namespace mu
{

	static FNodeType s_nodeMeshType = FNodeType(Node::EType::Mesh , Node::GetStaticType());


	const FNodeType* NodeMesh::GetType() const
	{
		return GetStaticType();
	}


	const FNodeType* NodeMesh::GetStaticType()
	{
		return &s_nodeMeshType;
	}


	void MeshCreateCollapsedVertexMap(const mu::Mesh* Mesh, TArray<int32>& CollapsedVertices)
	{
		MUTABLE_CPUPROFILER_SCOPE(LayoutUV_CreateCollapsedVertexMap);

		const int32 NumVertices = Mesh->GetVertexCount();
		CollapsedVertices.Reserve(NumVertices);

		UE::Geometry::TPointHashGrid3f<int32> VertHash(0.01f, INDEX_NONE);
		VertHash.Reserve(NumVertices);

		TArray<FVector3f> Vertices;
		Vertices.SetNumUninitialized(NumVertices);

		mu::UntypedMeshBufferIteratorConst ItPosition = mu::UntypedMeshBufferIteratorConst(Mesh->GetVertexBuffers(), mu::MBS_POSITION);

		FVector3f* VertexData = Vertices.GetData();

		for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			*VertexData = ItPosition.GetAsVec3f();
			VertHash.InsertPointUnsafe(VertexIndex, *VertexData);

			++ItPosition;
			++VertexData;
		}

		// Find unique vertices
		CollapsedVertices.Init(INDEX_NONE, NumVertices);

		TArray<int32> NearbyVertices;
		for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			if (CollapsedVertices[VertexIndex] != INDEX_NONE)
			{
				continue;
			}

			const FVector3f& Vertex = Vertices[VertexIndex];

			NearbyVertices.Reset();
			VertHash.FindPointsInBall(Vertex, 0.00001,
				[&Vertex, &Vertices](const int32& Other) -> float {return FVector3f::DistSquared(Vertices[Other], Vertex); },
				NearbyVertices);

			// Find equals
			for (int32 NearbyVertexIndex : NearbyVertices)
			{
				CollapsedVertices[NearbyVertexIndex] = VertexIndex;
			}
		}
	}



	void GetUVIsland(TArray<FTriangleInfo>& InTriangles,
		const uint32 InFirstTriangle,
		TArray<uint32>& OutTriangleIndices,
		const TArray<FVector2f>& InUVs,
		const TMultiMap<int32, uint32>& InVertexToTriangleMap)
	{
		MUTABLE_CPUPROFILER_SCOPE(GetUVIsland);

		const uint32 NumTriangles = (uint32)InTriangles.Num();

		OutTriangleIndices.Reserve(NumTriangles);
		OutTriangleIndices.Add(InFirstTriangle);

		TArray<bool> SkipTriangles;
		SkipTriangles.Init(false, NumTriangles);

		TArray<uint32> PendingTriangles;
		PendingTriangles.Reserve(NumTriangles / 64);
		PendingTriangles.Add(InFirstTriangle);

		while (!PendingTriangles.IsEmpty())
		{
			const uint32 TriangleIndex = PendingTriangles.Pop();

			// Triangle about to be proccessed, mark as skip;
			SkipTriangles[TriangleIndex] = true;

			bool ConnectedEdges[3] = { false, false, false };

			const FTriangleInfo& Triangle = InTriangles[TriangleIndex];

			// Find Triangles connected to edges 0 and 2
			int32 CollapsedVertex1 = Triangle.CollapsedIndices[1];
			int32 CollapsedVertex2 = Triangle.CollapsedIndices[2];

			TArray<uint32> FoundTriangleIndices;
			InVertexToTriangleMap.MultiFind(Triangle.CollapsedIndices[0], FoundTriangleIndices);

			for (uint32 OtherTriangleIndex : FoundTriangleIndices)
			{
				const FTriangleInfo& OtherTriangle = InTriangles[OtherTriangleIndex];

				for (int32 OtherIndex = 0; OtherIndex < 3; ++OtherIndex)
				{
					const int32 OtherCollapsedIndex = OtherTriangle.CollapsedIndices[OtherIndex];
					if (OtherCollapsedIndex == CollapsedVertex1)
					{
						// Check if the vertex is in the same UV Island 
						if (!SkipTriangles[OtherTriangleIndex]
							&& InUVs[Triangle.Indices[1]].Equals(InUVs[OtherTriangle.Indices[OtherIndex]], 0.00001f))
						{
							OutTriangleIndices.Add(OtherTriangleIndex);
							PendingTriangles.Add(OtherTriangleIndex);
							SkipTriangles[OtherTriangleIndex] = true;
						}

						// Connected but already processed or in another island
						break;
					}

					if (OtherCollapsedIndex == CollapsedVertex2)
					{
						// Check if the vertex is in the same UV Island 
						if (!SkipTriangles[OtherTriangleIndex]
							&& InUVs[Triangle.Indices[2]].Equals(InUVs[OtherTriangle.Indices[OtherIndex]], 0.00001f))
						{
							OutTriangleIndices.Add(OtherTriangleIndex);
							PendingTriangles.Add(OtherTriangleIndex);
							SkipTriangles[OtherTriangleIndex] = true;
						}

						// Connected but already processed or in another UV Island
						break;
					}
				}

			}

			// Find the triangle connected to edge 1
			FoundTriangleIndices.Reset();
			InVertexToTriangleMap.MultiFind(CollapsedVertex1, FoundTriangleIndices);

			for (uint32 OtherTriangleIndex : FoundTriangleIndices)
			{
				const FTriangleInfo& OtherTriangle = InTriangles[OtherTriangleIndex];

				for (int32 OtherIndex = 0; OtherIndex < 3; ++OtherIndex)
				{
					const int32 OtherCollapsedIndex = OtherTriangle.CollapsedIndices[OtherIndex];
					if (OtherCollapsedIndex == CollapsedVertex2)
					{
						// Check if the vertex belong to the same UV island
						if (!SkipTriangles[OtherTriangleIndex]
							&& InUVs[Triangle.Indices[2]].Equals(InUVs[OtherTriangle.Indices[OtherIndex]], 0.00001f))
						{
							OutTriangleIndices.Add(OtherTriangleIndex);
							PendingTriangles.Add(OtherTriangleIndex);
							SkipTriangles[OtherTriangleIndex] = true;
						}

						// Connected but already processed or in another island
						break;
					}
				}
			}
		}
	}


}


