// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Dataflow/CollectionRenderingPatternUtility.h"

#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowObject.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/NonManifoldMappingSupport.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryCollection/Facades/CollectionRenderingFacade.h"
#include "SkeletalMeshAttributes.h"
#include "ToDynamicMesh.h"

using namespace UE::Geometry;

namespace UE::Dataflow
{
	namespace Conversion
	{
		// Convert a rendering facade to a dynamic mesh
		void RenderingFacadeToDynamicMesh(const GeometryCollection::Facades::FRenderingFacade& Facade, int32 InMeshIndex, FDynamicMesh3& DynamicMesh)
		{
			if (Facade.CanRenderSurface())
			{
				int32 StartTriangles = 0;
				int32 StartVertices = 0;
				int32 NumTriangles = Facade.NumTriangles();
				int32 NumVertices = Facade.NumVertices();

				if (InMeshIndex != INDEX_NONE)
				{
					if (ensure(0 <= InMeshIndex && InMeshIndex < Facade.NumGeometry()))
					{
						StartTriangles = Facade.GetIndicesStart()[InMeshIndex];
						StartVertices = Facade.GetVertexStart()[InMeshIndex];
						NumTriangles = Facade.GetIndicesCount()[InMeshIndex];
						NumVertices = Facade.GetVertexCount()[InMeshIndex];
					}
				}

				TArray<int32> Remapping;
				const TManagedArray<FIntVector>& Indices = Facade.GetIndices();
				const TManagedArray<FVector3f>& Positions = Facade.GetVertices();
				const TManagedArray<FVector3f>& Normals = Facade.GetNormals();
				const TManagedArray<FLinearColor>& Colors = Facade.GetVertexColor();

				int32 LastVertexIndex = StartVertices + NumVertices;
				for (int32 VertexIndex = StartVertices; VertexIndex < LastVertexIndex; ++VertexIndex)
				{
					DynamicMesh.AppendVertex(FVertexInfo(FVector3d(Positions[VertexIndex]), Normals[VertexIndex],
						FVector3f(Colors[VertexIndex].R, Colors[VertexIndex].G, Colors[VertexIndex].B)));
					Remapping.Add(VertexIndex);
				}
				int32 LastTriangleIndex = StartTriangles + NumTriangles;
				for (int32 TriangleIndex = StartTriangles; TriangleIndex < LastTriangleIndex; ++TriangleIndex)
				{
					DynamicMesh.AppendTriangle(FIndex3i(
						Indices[TriangleIndex].X - StartVertices,
						Indices[TriangleIndex].Y - StartVertices,
						Indices[TriangleIndex].Z - StartVertices)
					);
				}
				FMeshNormals::QuickComputeVertexNormals(DynamicMesh);

				DynamicMesh.EnableAttributes();

				// Build Remmaping indices back into the colleciton. 
				if (Remapping.Num() < Facade.NumVertices())
				{
					UE::Geometry::FNonManifoldMappingSupport::AttachNonManifoldVertexMappingData(Remapping, DynamicMesh);
				}

				DynamicMesh.Attributes()->EnablePrimaryColors();
				DynamicMesh.Attributes()->PrimaryColors()->CreateFromPredicate([](int ParentVID, int TriIDA, int TriIDB) {return true; }, 0.f);
				DynamicMesh.EnableVertexColors(FVector3f::Zero());
				FDynamicMeshColorOverlay* const ColorOverlay = DynamicMesh.Attributes()->PrimaryColors();
				auto SetColorsFromWeights = [&](int TriangleID)
				{
					const FIndex3i Tri = DynamicMesh.GetTriangle(TriangleID);
					const FIndex3i ColorElementTri = ColorOverlay->GetTriangle(TriangleID);
					for (int TriVertIndex = 0; TriVertIndex < 3; ++TriVertIndex)
					{
						FVector4f Color(Colors[Remapping[Tri[TriVertIndex]]]); Color.W = 1.0f;
						ColorOverlay->SetElement(ColorElementTri[TriVertIndex], Color);
					}
				};
				for (const int TriangleID : DynamicMesh.TriangleIndicesItr())
				{
					SetColorsFromWeights(TriangleID);
				}
			}
		}

		// Convert a dynamic mesh to a rendering facade
		void DynamicMeshToRenderingFacade(const FDynamicMesh3& DynamicMesh, GeometryCollection::Facades::FRenderingFacade& Facade)
		{
			if (Facade.CanRenderSurface())
			{
				const int32 NumTriangles = Facade.NumTriangles();
				const int32 NumVertices = Facade.NumVertices();

				// We can only override vertices attributes (position, normals, colors)
				if ((NumTriangles == DynamicMesh.TriangleCount()) && (NumVertices == DynamicMesh.VertexCount()))
				{
					TManagedArray<FVector3f>& Positions = Facade.ModifyVertices();
					TManagedArray<FVector3f>& Normals = Facade.ModifyNormals();
					TManagedArray<FLinearColor>& Colors = Facade.ModifyVertexColor();

					for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
					{
						Positions[VertexIndex] = FVector3f(DynamicMesh.GetVertex(VertexIndex));
						Normals[VertexIndex] = DynamicMesh.GetVertexNormal(VertexIndex);
						Colors[VertexIndex] = DynamicMesh.GetVertexColor(VertexIndex);
					}
				}
			}
		}
	}

}	// namespace UE::Dataflow