// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEngineRendering.h"

#include "Dataflow/DataflowConnectionTypes.h"
#include "Dataflow/DataflowEditorModule.h"
#include "Dataflow/DataflowRenderingFactory.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Field/FieldSystemTypes.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/Facades/CollectionRenderingFacade.h"
#include "GeometryCollection/Facades/CollectionExplodedVectorFacade.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "UDynamicMesh.h"

namespace UE::Dataflow
{
	void RenderBasicGeometryCollection(GeometryCollection::Facades::FRenderingFacade& RenderCollection, const UE::Dataflow::FGraphRenderingState& State, TArray<FLinearColor>* VertexColorOverride = nullptr)
	{
		FManagedArrayCollection Default;
		FName PrimaryOutput = State.GetRenderOutputs()[0]; // "Collection"
		const FManagedArrayCollection& Collection = State.GetValue<FManagedArrayCollection>(PrimaryOutput, Default);

		const TManagedArray<int32>& BoneIndex = Collection.GetAttribute<int32>("BoneMap", FGeometryCollection::VerticesGroup);
		const TManagedArray<int32>& Parents = Collection.GetAttribute<int32>(FTransformCollection::ParentAttribute, FTransformCollection::TransformGroup);
		const TManagedArray<FTransform3f>& Transforms = Collection.GetAttribute<FTransform3f>(FTransformCollection::TransformAttribute, FTransformCollection::TransformGroup);

		TArray<FTransform> M;
		GeometryCollectionAlgo::GlobalMatrices(Transforms, Parents, M);

		// If Collection has "ExplodedVector" attribute then use it to modify the global matrices (ExplodedView node creates it)
		GeometryCollection::Facades::FCollectionExplodedVectorFacade ExplodedViewFacade(Collection);
		ExplodedViewFacade.UpdateGlobalMatricesWithExplodedVectors(M);

		auto ToD = [](FVector3f V) { return FVector3d(V.X, V.Y, V.Z); };
		auto ToF = [](FVector3d V) { return FVector3f(V.X, V.Y, V.Z); };


		const TManagedArray<FVector3f>& Vertex = Collection.GetAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
		const TManagedArray<FIntVector>& Faces = Collection.GetAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup);
		const TManagedArray<bool>* FaceVisible = Collection.FindAttribute<bool>("Visible", FGeometryCollection::FacesGroup);

		TArray<FVector3f> Vertices; Vertices.AddUninitialized(Vertex.Num());
		TArray<FIntVector> Tris; Tris.AddUninitialized(Faces.Num());
		TArray<bool> Visited; Visited.Init(false, Vertices.Num());

		int32 Tdx = 0;
		for (int32 FaceIdx = 0; FaceIdx < Faces.Num(); ++FaceIdx)
		{
			if (FaceVisible && !(*FaceVisible)[FaceIdx]) continue;

			const FIntVector& Face = Faces[FaceIdx];

			FIntVector Tri = FIntVector(Face[0], Face[1], Face[2]);
			FTransform Ms[3] = { M[BoneIndex[Tri[0]]], M[BoneIndex[Tri[1]]], M[BoneIndex[Tri[2]]] };

			Tris[Tdx++] = Tri;
			if (!Visited[Tri[0]]) Vertices[Tri[0]] = ToF(Ms[0].TransformPosition(ToD(Vertex[Tri[0]])));
			if (!Visited[Tri[1]]) Vertices[Tri[1]] = ToF(Ms[1].TransformPosition(ToD(Vertex[Tri[1]])));
			if (!Visited[Tri[2]]) Vertices[Tri[2]] = ToF(Ms[2].TransformPosition(ToD(Vertex[Tri[2]])));

			Visited[Tri[0]] = true; Visited[Tri[1]] = true; Visited[Tri[2]] = true;
		}

		Tris.SetNum(Tdx);

		// Maybe these buffers should be shrunk, but there are unused vertices in the buffer. 
		for (int i = 0; i < Visited.Num(); i++) if (!Visited[i]) Vertices[i] = FVector3f(0);

		// Copy VertexNormals from the Collection if exists otherwise compute and set it
		TArray<FVector3f> VertexNormals; VertexNormals.AddUninitialized(Vertex.Num());
		if (const TManagedArray<FVector3f>* VertexNormal = Collection.FindAttribute<FVector3f>("Normal", FGeometryCollection::VerticesGroup))
		{
			for (int32 VertexIdx = 0; VertexIdx < VertexNormals.Num(); ++VertexIdx)
			{
				VertexNormals[VertexIdx] = (*VertexNormal)[VertexIdx];
			}
		}
		else
		{
			for (int32 VertexIdx = 0; VertexIdx < VertexNormals.Num(); ++VertexIdx)
			{
				// TODO: Compute the normal
				VertexNormals[VertexIdx] = FVector3f(0.f);
			}
		}

		// Copy VertexColors from the Collection if exists otherwise set it to FDataflowEditorModule::SurfaceColor
		TArray<FLinearColor> VertexColors; VertexColors.AddUninitialized(Vertex.Num());
		if (VertexColorOverride && VertexColorOverride->Num() == Vertex.Num())
		{
			for (int32 VertexIdx = 0; VertexIdx < VertexColors.Num(); ++VertexIdx)
			{
				VertexColors[VertexIdx] = (*VertexColorOverride)[VertexIdx];
			}
		}
		else
		{
			if (const TManagedArray<FLinearColor>* VertexColorManagedArray = Collection.FindAttribute<FLinearColor>("Color", FGeometryCollection::VerticesGroup))
			{
				for (int32 VertexIdx = 0; VertexIdx < VertexColors.Num(); ++VertexIdx)
				{
					VertexColors[VertexIdx] = (*VertexColorManagedArray)[VertexIdx];
				}
			}
			else
			{
				for (int32 VertexIdx = 0; VertexIdx < VertexColors.Num(); ++VertexIdx)
				{
					VertexColors[VertexIdx] = FLinearColor(FDataflowEditorModule::SurfaceColor);
				}
			}
		}

		// Set the data on the RenderCollection
		int32 GeometryIndex = RenderCollection.StartGeometryGroup(State.GetGuid().ToString());
		RenderCollection.AddSurface(MoveTemp(Vertices), MoveTemp(Tris), MoveTemp(VertexNormals), MoveTemp(VertexColors));
		RenderCollection.EndGeometryGroup(GeometryIndex);

	}

	void RenderMeshIndexedGeometryCollection(GeometryCollection::Facades::FRenderingFacade& RenderCollection, const UE::Dataflow::FGraphRenderingState& State, TArray<FLinearColor>* VertexColorOverride = nullptr )
	{
		auto ToD = [](FVector3f V) { return FVector3d(V.X, V.Y, V.Z); };
		auto ToF = [](FVector3d V) { return FVector3f(V.X, V.Y, V.Z); };

		FManagedArrayCollection Default;
		FName PrimaryOutput = State.GetRenderOutputs()[0]; // "Collection"
		const FManagedArrayCollection& Collection = State.GetValue<FManagedArrayCollection>(PrimaryOutput, Default);

		const TManagedArray<int32>& BoneIndex = Collection.GetAttribute<int32>("BoneMap", FGeometryCollection::VerticesGroup);
		const TManagedArray<int32>& Parents = Collection.GetAttribute<int32>(FTransformCollection::ParentAttribute, FTransformCollection::TransformGroup);
		const TManagedArray<FTransform3f>& Transforms = Collection.GetAttribute<FTransform3f>(FTransformCollection::TransformAttribute, FTransformCollection::TransformGroup);
		const TManagedArray<FString>& BoneNames = Collection.GetAttribute<FString>("BoneName", FGeometryCollection::TransformGroup);
		const TManagedArray<FVector3f>& Vertex = Collection.GetAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
		const TManagedArray<FIntVector>& Faces = Collection.GetAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup);
		const TManagedArray<bool>* FaceVisible = Collection.FindAttribute<bool>("Visible", FGeometryCollection::FacesGroup);

		const TManagedArray<int32>& VertexStart = Collection.GetAttribute<int32>("VertexStart", FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>& VertexCount = Collection.GetAttribute<int32>("VertexCount", FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>& FacesStart = Collection.GetAttribute<int32>("FaceStart", FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>& FacesCount = Collection.GetAttribute<int32>("FaceCount", FGeometryCollection::GeometryGroup);
		int32 TotalVertices = Collection.NumElements(FGeometryCollection::VerticesGroup);

		TArray<FTransform> M;
		GeometryCollectionAlgo::GlobalMatrices(Transforms, Parents, M);
		GeometryCollection::Facades::FCollectionExplodedVectorFacade ExplodedViewFacade(Collection);
		ExplodedViewFacade.UpdateGlobalMatricesWithExplodedVectors(M);

		for (int Gdx = 0; Gdx < Collection.NumElements(FGeometryCollection::GeometryGroup); Gdx++)
		{
			TArray<FVector3f> Vertices; Vertices.AddUninitialized(VertexCount[Gdx]);
			TArray<FIntVector> Tris; Tris.AddUninitialized(FacesCount[Gdx]);
			TArray<bool> Visited; Visited.Init(false, VertexCount[Gdx]);

			int32 Tdx = 0;
			int32 LastFaceIndex = FacesStart[Gdx] + FacesCount[Gdx];
			for (int32 FaceIdx = FacesStart[Gdx]; FaceIdx < LastFaceIndex; ++FaceIdx)
			{
				if (FaceVisible && !(*FaceVisible)[FaceIdx]) continue;

				const FIntVector& Face = Faces[FaceIdx];

				FIntVector Tri = FIntVector(Face[0], Face[1], Face[2]);
				FTransform Ms[3] = { M[BoneIndex[Tri[0]]], M[BoneIndex[Tri[1]]], M[BoneIndex[Tri[2]]] };
				FIntVector MovedTri = FIntVector(Face[0] - VertexStart[Gdx], Face[1] - VertexStart[Gdx], Face[2] - VertexStart[Gdx]);

				Tris[Tdx++] = MovedTri;
				if (!Visited[MovedTri[0]]) Vertices[Tri[0] - VertexStart[Gdx]] = ToF(Ms[0].TransformPosition(ToD(Vertex[Tri[0]])));
				if (!Visited[MovedTri[1]]) Vertices[Tri[1] - VertexStart[Gdx]] = ToF(Ms[1].TransformPosition(ToD(Vertex[Tri[1]])));
				if (!Visited[MovedTri[2]]) Vertices[Tri[2] - VertexStart[Gdx]] = ToF(Ms[2].TransformPosition(ToD(Vertex[Tri[2]])));

				Visited[MovedTri[0]] = true; Visited[MovedTri[1]] = true; Visited[MovedTri[2]] = true;
			}

			Tris.SetNum(Tdx);

			// move the unused points too. Need to keep them for vertex alignment with ediior tools. 
			for (int i = 0; i < Visited.Num(); i++)
			{
				if (!Visited[i])
				{
					Vertices[i] = ToF(M[BoneIndex[i + VertexStart[Gdx]]].TransformPosition(ToD(Vertex[i + VertexStart[Gdx]])));
				}
			}

			// Copy VertexNormals from the Collection if exists otherwise compute and set it
			ensure(VertexCount[Gdx] == Vertices.Num());
			TArray<FVector3f> VertexNormals; VertexNormals.AddUninitialized(Vertices.Num());
			if (const TManagedArray<FVector3f>* VertexNormal = Collection.FindAttribute<FVector3f>("Normal", FGeometryCollection::VerticesGroup))
			{
				int32 LastVertIndex = VertexStart[Gdx] + VertexCount[Gdx];
				for (int32 VertexIdx = VertexStart[Gdx], SrcVertexIdx = 0; VertexIdx < LastVertIndex; ++VertexIdx, ++SrcVertexIdx)
				{
					VertexNormals[SrcVertexIdx] = (*VertexNormal)[VertexIdx];
				}
			}
			else
			{
				for (int32 VertexIdx = 0; VertexIdx < Vertices.Num(); ++VertexIdx)
				{
					// TODO: Compute the normal
					VertexNormals[VertexIdx] = FVector3f(0.f);
				}
			}

			// Copy VertexColors from the Collection if exists otherwise set it to FDataflowEditorModule::SurfaceColor
			TArray<FLinearColor> VertexColors; VertexColors.AddUninitialized(Vertices.Num());
			if (VertexColorOverride && VertexColorOverride->Num() == TotalVertices)
			{
				int32 LastVertIndex = VertexStart[Gdx] + VertexCount[Gdx];
				for (int32 VertexIdx = VertexStart[Gdx], SrcVertexIdx = 0; VertexIdx < LastVertIndex; ++VertexIdx, ++SrcVertexIdx)
				{
					VertexColors[SrcVertexIdx] = (*VertexColorOverride)[VertexIdx];
				}
			}
			else
			{
				if (const TManagedArray<FLinearColor>* VertexColorManagedArray = Collection.FindAttribute<FLinearColor>("Color", FGeometryCollection::VerticesGroup))
				{
					int32 LastVertIndex = VertexStart[Gdx] + VertexCount[Gdx];
					for (int32 VertexIdx = VertexStart[Gdx], SrcVertexIdx = 0; VertexIdx < LastVertIndex; ++VertexIdx, ++SrcVertexIdx)
					{
						VertexColors[SrcVertexIdx] = (*VertexColorManagedArray)[VertexIdx];
					}
				}
				else
				{
					for (int32 VertexIdx = 0; VertexIdx < VertexColors.Num(); ++VertexIdx)
					{
						VertexColors[VertexIdx] = FLinearColor(FDataflowEditorModule::SurfaceColor);
					}
				}
			}

			// Set the data on the RenderCollection
			if (Vertices.Num() && Tris.Num())
			{
				FString GeometryName = State.GetGuid().ToString(); GeometryName.AppendChar('.').AppendInt(Gdx);
				if (BoneIndex[VertexStart[Gdx]] != INDEX_NONE)
				{
					GeometryName = BoneNames[BoneIndex[VertexStart[Gdx]]];
				}
				int32 GeometryIndex = RenderCollection.StartGeometryGroup(GeometryName);
				RenderCollection.AddSurface(MoveTemp(Vertices), MoveTemp(Tris), MoveTemp(VertexNormals), MoveTemp(VertexColors));
				RenderCollection.EndGeometryGroup(GeometryIndex);
			}
		}
	}

	

	class FGeometryCollectionSurfaceRenderCallbacks : public FRenderingFactory::ICallbackInterface
	{
		virtual UE::Dataflow::FRenderKey GetRenderKey() const override
		{
			return { "SurfaceRender", FGeometryCollection::StaticType() };
		}

		virtual bool CanRender(const UE::Dataflow::IDataflowConstructionViewMode& ViewMode) const override
		{
			return (ViewMode.GetName() == FDataflowConstruction3DViewMode::Name);
		}

		virtual void Render(GeometryCollection::Facades::FRenderingFacade& RenderCollection, const FGraphRenderingState& State) override
		{
			if (State.GetRenderOutputs().Num())
			{
				FManagedArrayCollection Default;
				FName PrimaryOutput = State.GetRenderOutputs()[0]; // "Collection"
				const FManagedArrayCollection& Collection = State.GetValue<FManagedArrayCollection>(PrimaryOutput, Default);

				const bool bFoundIndices = Collection.FindAttributeTyped<FIntVector>("Indices", FGeometryCollection::FacesGroup) != nullptr;
				const bool bFoundVertices = Collection.FindAttributeTyped<FVector3f>("Vertex", FGeometryCollection::VerticesGroup) != nullptr;
				const bool bFoundTransforms = Collection.FindAttributeTyped<FTransform3f>(FTransformCollection::TransformAttribute, FTransformCollection::TransformGroup) != nullptr;
				const bool bFoundBoneMap = Collection.FindAttributeTyped<int32>("BoneMap", FGeometryCollection::VerticesGroup) != nullptr;
				const bool bFoundParents = Collection.FindAttributeTyped<int32>(FTransformCollection::ParentAttribute, FTransformCollection::TransformGroup) != nullptr;
				UE_LOG(LogTemp, Warning, TEXT("Render GC with found params = %d %d %d %d %d"), bFoundIndices, bFoundVertices, bFoundTransforms, bFoundBoneMap, bFoundParents);
				bool bFoundRenderData = bFoundIndices && bFoundVertices && bFoundTransforms && bFoundBoneMap && bFoundParents
					&& Collection.NumElements(FTransformCollection::TransformGroup) > 0;

				const bool bFoundVertexStart = Collection.FindAttributeTyped<int32>("VertexStart", FGeometryCollection::GeometryGroup) != nullptr;
				const bool bFoundVertexCount = Collection.FindAttributeTyped<int32>("VertexCount", FGeometryCollection::GeometryGroup) != nullptr;
				const bool bFoundFaceStart = Collection.FindAttributeTyped<int32>("FaceStart", FGeometryCollection::GeometryGroup) != nullptr;
				const bool bFoundFaceCount = Collection.FindAttributeTyped<int32>("FaceCount", FGeometryCollection::GeometryGroup) != nullptr;
				UE_LOG(LogTemp, Warning, TEXT("Render GC with found mesh group params = %d %d %d %d"), bFoundVertexStart, bFoundVertexCount, bFoundFaceStart, bFoundFaceCount);
				bool bFoundGeometryAttributes = bFoundVertexStart && bFoundVertexCount && bFoundFaceStart && bFoundFaceCount
					&& Collection.NumElements(FGeometryCollection::GeometryGroup) > 0;

				if (bFoundRenderData && bFoundGeometryAttributes)
				{
					RenderMeshIndexedGeometryCollection(RenderCollection, State);
				}
				else if (bFoundRenderData)
				{
					RenderBasicGeometryCollection(RenderCollection, State);
				}
			}
		}
	};


	class FGeometryCollectionSurfaceWeightsRenderCallbacks : public FRenderingFactory::ICallbackInterface
	{
		virtual UE::Dataflow::FRenderKey GetRenderKey() const override
		{
			return { "SurfaceWeightsRender", FGeometryCollection::StaticType() };
		}

		virtual bool CanRender(const UE::Dataflow::IDataflowConstructionViewMode& ViewMode) const override
		{
			return (ViewMode.GetName() == FDataflowConstruction3DViewMode::Name);
		}

		virtual void Render(GeometryCollection::Facades::FRenderingFacade& RenderCollection, const FGraphRenderingState& State) override
		{
			if (State.GetRenderOutputs().Num() >= 2)
			{
				FManagedArrayCollection Default;
				FName PrimaryOutput = State.GetRenderOutputs()[0]; // "Collection"
				const FManagedArrayCollection& Collection = State.GetValue<FManagedArrayCollection>(PrimaryOutput, Default);

				const bool bFoundIndices = Collection.FindAttributeTyped<FIntVector>("Indices", FGeometryCollection::FacesGroup) != nullptr;
				const bool bFoundVertices = Collection.FindAttributeTyped<FVector3f>("Vertex", FGeometryCollection::VerticesGroup) != nullptr;
				const bool bFoundTransforms = Collection.FindAttributeTyped<FTransform3f>(FTransformCollection::TransformAttribute, FTransformCollection::TransformGroup) != nullptr;
				const bool bFoundBoneMap = Collection.FindAttributeTyped<int32>("BoneMap", FGeometryCollection::VerticesGroup) != nullptr;
				const bool bFoundParents = Collection.FindAttributeTyped<int32>(FTransformCollection::ParentAttribute, FTransformCollection::TransformGroup) != nullptr;
				UE_LOG(LogTemp, Warning, TEXT("Render GC with found params = %d %d %d %d %d"), bFoundIndices, bFoundVertices, bFoundTransforms, bFoundBoneMap, bFoundParents);
				bool bFoundRenderData = bFoundIndices && bFoundVertices && bFoundTransforms && bFoundBoneMap && bFoundParents
					&& Collection.NumElements(FTransformCollection::TransformGroup) > 0;

				const bool bFoundVertexStart = Collection.FindAttributeTyped<int32>("VertexStart", FGeometryCollection::GeometryGroup) != nullptr;
				const bool bFoundVertexCount = Collection.FindAttributeTyped<int32>("VertexCount", FGeometryCollection::GeometryGroup) != nullptr;
				const bool bFoundFaceStart = Collection.FindAttributeTyped<int32>("FaceStart", FGeometryCollection::GeometryGroup) != nullptr;
				const bool bFoundFaceCount = Collection.FindAttributeTyped<int32>("FaceCount", FGeometryCollection::GeometryGroup) != nullptr;
				UE_LOG(LogTemp, Warning, TEXT("Render GC with found mesh group params = %d %d %d %d"), bFoundVertexStart, bFoundVertexCount, bFoundFaceStart, bFoundFaceCount);
				bool bFoundGeometryAttributes = bFoundVertexStart && bFoundVertexCount && bFoundFaceStart && bFoundFaceCount
					&& Collection.NumElements(FGeometryCollection::GeometryGroup) > 0;

				FCollectionAttributeKey DefaultKey;
				FName SecondaryOutput = State.GetRenderOutputs()[1]; // "AttributeKey"
				const FCollectionAttributeKey& AttributeKey = State.GetValue<FCollectionAttributeKey>(SecondaryOutput, DefaultKey);

				const bool bFoundVertexColor = Collection.FindAttributeTyped<FLinearColor>("Color", FGeometryCollection::VerticesGroup) != nullptr;
				const bool bFoundFloatScalar = Collection.FindAttributeTyped<float>(FName(AttributeKey.Attribute), FName(AttributeKey.Group)) != nullptr;
				bool bFoundVertexScalarAndColors = bFoundVertexColor && bFoundFloatScalar && AttributeKey.Group.Equals(FGeometryCollection::VerticesGroup.ToString());

				TArray<FLinearColor>* Colors = nullptr;
				if (bFoundVertexScalarAndColors)
				{
					auto RangeValue = [](const TManagedArray<float>* FloatArray)
						{
							float Min = FLT_MAX;
							float Max = -FLT_MAX;
							for (int i = 0; i < FloatArray->Num(); i++) {
								Min = FMath::Min(Min, (*FloatArray)[i]);
								Max = FMath::Max(Max, (*FloatArray)[i]);
							}
							return TPair<float, float>(Min, Max);
						};

					const TManagedArray<float>* FloatArray = Collection.FindAttributeTyped<float>(FName(AttributeKey.Attribute), FName(AttributeKey.Group));
					if (FloatArray && FloatArray->Num())
					{
						Colors = new TArray<FLinearColor>();
						Colors->AddUninitialized(FloatArray->Num());

						TPair<float, float> Range = RangeValue(FloatArray);
						float Delta = FMath::Abs(Range.Get<1>() - Range.Get<0>());
						if (Delta > FLT_EPSILON)
						{
							for (int32 VertexIdx = 0; VertexIdx < FloatArray->Num(); ++VertexIdx)
							{
								(*Colors)[VertexIdx] = FLinearColor::White * ((*FloatArray)[VertexIdx] - Range.Get<0>()) / Delta;
							}
						}
						else
						{
							for (int32 VertexIdx = 0; VertexIdx < FloatArray->Num(); ++VertexIdx)
							{
								(*Colors)[VertexIdx] = FLinearColor::Black;
							}
						}
					}
				}

				if (bFoundRenderData && bFoundGeometryAttributes)
				{
					RenderMeshIndexedGeometryCollection(RenderCollection, State, Colors);
				}
				else if (bFoundRenderData)
				{
					RenderBasicGeometryCollection(RenderCollection, State, Colors);
				}

				if (Colors)
				{
					delete Colors;
				}
			}
		}
	};

	class FDynamicMesh3SurfaceRenderCallbacks : public FRenderingFactory::ICallbackInterface
	{
		virtual UE::Dataflow::FRenderKey GetRenderKey() const override
		{
			return { "SurfaceRender", FName("FDynamicMesh3") };
		}

		virtual bool CanRender(const UE::Dataflow::IDataflowConstructionViewMode& ViewMode) const override
		{
			return (ViewMode.GetName() == FDataflowConstruction3DViewMode::Name);
		}

		virtual void Render(GeometryCollection::Facades::FRenderingFacade& RenderCollection, const FGraphRenderingState& State) override
		{
			if (State.GetRenderOutputs().Num())
			{
				FName PrimaryOutput = State.GetRenderOutputs()[0]; // "Mesh"

				TObjectPtr<UDynamicMesh> Default;
				if (const TObjectPtr<UDynamicMesh> Mesh = State.GetValue<TObjectPtr<UDynamicMesh>>(PrimaryOutput, Default))
				{
					const UE::Geometry::FDynamicMesh3& DynamicMesh = Mesh->GetMeshRef();

					const int32 NumVertices = DynamicMesh.VertexCount();
					const int32 NumTriangles = DynamicMesh.TriangleCount();

					if (NumVertices > 0 && NumTriangles > 0)
					{
						// This will contain the valid triangles only
						TArray<FIntVector> Tris; Tris.Reserve(DynamicMesh.TriangleCount());

						// DynamicMesh.TrianglesItr() returns the valid triangles only
						for (UE::Geometry::FIndex3i Tri : DynamicMesh.TrianglesItr())
						{
							Tris.Add(FIntVector(Tri.A, Tri.B, Tri.C));
						}

						// This will contain all the vertices (invalid ones too)
						// Otherwise the IDs need to be remaped
						TArray<FVector3f> Vertices; Vertices.AddZeroed(DynamicMesh.MaxVertexID());

						// DynamicMesh.VertexIndicesItr() returns the valid vertices only
						for (int32 VertexID : DynamicMesh.VertexIndicesItr())
						{
							Vertices[VertexID] = (FVector3f)DynamicMesh.GetVertex(VertexID);
						}

						// Add VertexNormal and VertexColor
						TArray<FVector3f> VertexNormals; VertexNormals.AddUninitialized(Vertices.Num());
						TArray<FLinearColor> VertexColors; VertexColors.AddUninitialized(Vertices.Num());
						for (int32 VertexIdx = 0; VertexIdx < VertexNormals.Num(); ++VertexIdx)
						{
							// TODO: Get the normal from FDynamicMesh3
							VertexNormals[VertexIdx] = FVector3f(0.f);
							VertexColors[VertexIdx] = FLinearColor(FDataflowEditorModule::SurfaceColor);
						}

						int32 GeometryIndex = RenderCollection.StartGeometryGroup(State.GetGuid().ToString());
						RenderCollection.AddSurface(MoveTemp(Vertices), MoveTemp(Tris), MoveTemp(VertexNormals), MoveTemp(VertexColors));
						RenderCollection.EndGeometryGroup(GeometryIndex);
					}
				}
			}
		}
	};


	class FBoxSurfaceRenderCallbacks : public FRenderingFactory::ICallbackInterface
	{
		virtual UE::Dataflow::FRenderKey GetRenderKey() const override
		{
			return { "SurfaceRender", FName("FBox") };
		}

		virtual bool CanRender(const UE::Dataflow::IDataflowConstructionViewMode& ViewMode) const override
		{
			return (ViewMode.GetName() == FDataflowConstruction3DViewMode::Name);
		}

		virtual void Render(GeometryCollection::Facades::FRenderingFacade& RenderCollection, const FGraphRenderingState& State) override
		{
			if (State.GetRenderOutputs().Num())
			{
				FName PrimaryOutput = State.GetRenderOutputs()[0]; // "Box"

				FBox Default(ForceInit);
				const FBox& Box = State.GetValue<FBox>(PrimaryOutput, Default);

				const int32 NumVertices = 8;
				const int32 NumTriangles = 12;

				TArray<FVector3f> Vertices; Vertices.AddUninitialized(NumVertices);
				TArray<FIntVector> Tris; Tris.AddUninitialized(NumTriangles);

				FVector Min = Box.Min;
				FVector Max = Box.Max;

				// Add vertices
				Vertices[0] = FVector3f(Min);
				Vertices[1] = FVector3f(Max.X, Min.Y, Min.Z);
				Vertices[2] = FVector3f(Max.X, Max.Y, Min.Z);
				Vertices[3] = FVector3f(Min.X, Max.Y, Min.Z);
				Vertices[4] = FVector3f(Min.X, Min.Y, Max.Z);
				Vertices[5] = FVector3f(Max.X, Min.Y, Max.Z);
				Vertices[6] = FVector3f(Max);
				Vertices[7] = FVector3f(Min.X, Max.Y, Max.Z);

				// Add triangles
				Tris[0] = FIntVector(0, 1, 3); Tris[1] = FIntVector(1, 2, 3);
				Tris[2] = FIntVector(0, 4, 1); Tris[3] = FIntVector(4, 5, 1);
				Tris[4] = FIntVector(5, 2, 1); Tris[5] = FIntVector(5, 6, 2);
				Tris[6] = FIntVector(3, 2, 6); Tris[7] = FIntVector(7, 3, 6);
				Tris[8] = FIntVector(0, 3, 7); Tris[9] = FIntVector(4, 0, 7);
				Tris[10] = FIntVector(5, 4, 7); Tris[11] = FIntVector(5, 7, 6);

				TArray<FVector3f> VertexNormals; VertexNormals.AddUninitialized(NumVertices);
				// TODO: Compute vertex normals

				// Add VertexNormal and VertexColor
				TArray<FLinearColor> VertexColors; VertexColors.AddUninitialized(Vertices.Num());
				for (int32 VertexIdx = 0; VertexIdx < VertexNormals.Num(); ++VertexIdx)
				{
					VertexNormals[VertexIdx] = FVector3f(0.f);
					VertexColors[VertexIdx] = FLinearColor(FDataflowEditorModule::SurfaceColor);
				}

				int32 GeometryIndex = RenderCollection.StartGeometryGroup(State.GetGuid().ToString());
				RenderCollection.AddSurface(MoveTemp(Vertices), MoveTemp(Tris), MoveTemp(VertexNormals), MoveTemp(VertexColors));
				RenderCollection.EndGeometryGroup(GeometryIndex);
			}
		}
	};


	class FFieldVolumeRenderCallbacks : public FRenderingFactory::ICallbackInterface
	{
		virtual UE::Dataflow::FRenderKey GetRenderKey() const override
		{
			return { "VolumeRender", FFieldCollection::StaticType() };
		}

		virtual bool CanRender(const UE::Dataflow::IDataflowConstructionViewMode& ViewMode) const override
		{
			return (ViewMode.GetName() == FDataflowConstruction3DViewMode::Name);
		}

		virtual void Render(GeometryCollection::Facades::FRenderingFacade& RenderCollection, const FGraphRenderingState& State) override
		{
			if (State.GetRenderOutputs().Num())
			{
				FName PrimaryOutput = State.GetRenderOutputs()[0]; // "VectorField"
				if (PrimaryOutput.IsEqual(FName("VectorField")))
				{
						FFieldCollection Default;
						const FFieldCollection& Collection = State.GetValue<FFieldCollection>(PrimaryOutput, Default);
						TArray<TPair<FVector3f, FVector3f>> VectorField = Collection.GetVectorField();
						TArray<FLinearColor> VertexColors = Collection.GetVectorColor();
						const int32 NumVertices = 3 * VectorField.Num();
						const int32 NumTriangles = VectorField.Num();

						TArray<FVector3f> Vertices; Vertices.AddUninitialized(NumVertices);
						TArray<FIntVector> Tris; Tris.AddUninitialized(NumTriangles);
						TArray<FVector3f> VertexNormals; VertexNormals.AddUninitialized(NumVertices);

						for (int32 i = 0; i < VectorField.Num(); i++)
						{
							
							FVector3f Dir = VectorField[i].Value - VectorField[i].Key;
							FVector3f DirAdd = Dir;
							DirAdd.X += 1.f;
							FVector3f OrthogonalDir = (Dir^ DirAdd).GetSafeNormal();
							Tris[i] = FIntVector(3*i, 3*i+1, 3*i+2);
							Vertices[3*i] = VectorField[i].Key;
							Vertices[3*i+1] = VectorField[i].Value;
							Vertices[3*i+2] = VectorField[i].Key + float(0.1) * Dir.Size() * OrthogonalDir;
							FVector3f TriangleNormal = (OrthogonalDir ^ Dir).GetSafeNormal();
							VertexNormals[3*i] = TriangleNormal;
							VertexNormals[3*i+1] = TriangleNormal;
							VertexNormals[3*i+2] = TriangleNormal;
						}

					int32 GeometryIndex = RenderCollection.StartGeometryGroup(State.GetGuid().ToString());
					RenderCollection.AddSurface(MoveTemp(Vertices), MoveTemp(Tris), MoveTemp(VertexNormals), MoveTemp(VertexColors));
					RenderCollection.EndGeometryGroup(GeometryIndex);
				}
			}
		}
	};

	void RenderingCallbacks()
	{
		using namespace UE::Dataflow;

		FRenderingFactory::GetInstance()->RegisterCallbacks(MakeUnique<FGeometryCollectionSurfaceRenderCallbacks>());
		FRenderingFactory::GetInstance()->RegisterCallbacks(MakeUnique<FGeometryCollectionSurfaceWeightsRenderCallbacks>());
		FRenderingFactory::GetInstance()->RegisterCallbacks(MakeUnique<FDynamicMesh3SurfaceRenderCallbacks>());
		FRenderingFactory::GetInstance()->RegisterCallbacks(MakeUnique<FBoxSurfaceRenderCallbacks>());
		FRenderingFactory::GetInstance()->RegisterCallbacks(MakeUnique<FFieldVolumeRenderCallbacks>());
	}

}
