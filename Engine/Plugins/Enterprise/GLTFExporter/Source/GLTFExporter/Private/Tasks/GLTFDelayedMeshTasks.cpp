// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/GLTFDelayedMeshTasks.h"
#include "Converters/GLTFMeshUtilities.h"
#include "Converters/GLTFBufferAdapter.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "StaticMeshComponentLODInfo.h"
#include "StaticMeshResources.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshModel.h"

#include "Converters/GLTFMaterialUtilities.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Components/SplineMeshComponent.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "Converters/GLTFMeshAttributesArray.h"
#include "Utilities/GLTFLandscapeComponentDataInterface.h"

namespace
{
	template <typename VectorType>
	void CheckTangentVectors(const void* SourceData, uint32 VertexCount, bool& bOutZeroNormals, bool& bOutZeroTangents)
	{
		bool bZeroNormals = false;
		bool bZeroTangents = false;

		typedef TStaticMeshVertexTangentDatum<VectorType> VertexTangentType;
		const VertexTangentType* VertexTangents = static_cast<const VertexTangentType*>(SourceData);

		for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			const VertexTangentType& VertexTangent = VertexTangents[VertexIndex];
			bZeroNormals |= VertexTangent.TangentZ.ToFVector().IsNearlyZero();
			bZeroTangents |= VertexTangent.TangentX.ToFVector().IsNearlyZero();
		}

		bOutZeroNormals = bZeroNormals;
		bOutZeroTangents = bZeroTangents;
	}

	void ValidateVertexBuffer(FGLTFConvertBuilder& Builder, const FStaticMeshVertexBuffer* VertexBuffer, const TCHAR* MeshName)
	{
		if (VertexBuffer == nullptr)
		{
			return;
		}

		const TUniquePtr<IGLTFBufferAdapter> SourceBuffer = IGLTFBufferAdapter::GetTangents(VertexBuffer);
		const uint8* SourceData = SourceBuffer->GetData();

		if (SourceData == nullptr)
		{
			return;
		}

		const uint32 VertexCount = VertexBuffer->GetNumVertices();
		bool bZeroNormals;
		bool bZeroTangents;

		if (VertexBuffer->GetUseHighPrecisionTangentBasis())
		{
			CheckTangentVectors<FPackedRGBA16N>(SourceData, VertexCount, bZeroNormals, bZeroTangents);
		}
		else
		{
			CheckTangentVectors<FPackedNormal>(SourceData, VertexCount, bZeroNormals, bZeroTangents);
		}

		if (bZeroNormals)
		{
			Builder.LogSuggestion(FString::Printf(
				TEXT("Mesh %s has some nearly zero-length normals which may not be supported in some glTF applications. Consider checking 'Recompute Normals' in the asset settings"),
				MeshName));
		}

		if (bZeroTangents)
		{
			Builder.LogSuggestion(FString::Printf(
				TEXT("Mesh %s has some nearly zero-length tangents which may not be supported in some glTF applications. Consider checking 'Recompute Tangents' in the asset settings"),
				MeshName));
		}
	}

	bool HasVertexColors(const FColorVertexBuffer* VertexBuffer)
	{
		if (VertexBuffer == nullptr)
		{
			return false;
		}

		const TUniquePtr<IGLTFBufferAdapter> SourceBuffer = IGLTFBufferAdapter::GetColors(VertexBuffer);
		const uint8* SourceData = SourceBuffer->GetData();

		if (SourceData == nullptr)
		{
			return false;
		}

		const uint32 VertexCount = VertexBuffer->GetNumVertices();
		const uint32 Stride = VertexBuffer->GetStride();

		for (uint32 VertexIndex = 0; VertexIndex < VertexCount; VertexIndex++)
		{
			const FColor& Color = *reinterpret_cast<const FColor*>(SourceData + Stride * VertexIndex);
			if (Color != FColor::White)
			{
				return true;
			}
		}

		return false;
	}

	template <class T>
	bool DoesBufferHasZeroVector(TArray<T> Buffer, float Tolerance = UE_KINDA_SMALL_NUMBER)
	{
		for (const T& Value : Buffer)
		{
			if (FMath::Abs(Value.X) <= Tolerance
				&& FMath::Abs(Value.Y) <= Tolerance
				&& FMath::Abs(Value.Z) <= Tolerance)
			{
				return true;
			}
		}
		return false;
	}
}

FString FGLTFDelayedStaticAndSplineMeshTask::GetName()
{
	return StaticMeshComponent != nullptr ? FGLTFNameUtilities::GetName(StaticMeshComponent) : (SplineMeshComponent != nullptr ? FGLTFNameUtilities::GetName(SplineMeshComponent) : StaticMesh->GetName());
}

void FGLTFDelayedStaticAndSplineMeshTask::Process()
{
	FGLTFMeshUtilities::FullyLoad(StaticMesh);

	const UMeshComponent* MeshComponent = StaticMeshComponent != nullptr ? StaticMeshComponent : (SplineMeshComponent != nullptr ? SplineMeshComponent : nullptr);

	JsonMesh->Name = MeshComponent != nullptr ? FGLTFNameUtilities::GetName(MeshComponent) : StaticMesh->GetName();

	const TArray<FStaticMaterial>& MaterialSlots = FGLTFMeshUtilities::GetMaterials(StaticMesh);
	
	const FGLTFMeshData* MeshData = Builder.ExportOptions->BakeMaterialInputs == EGLTFMaterialBakeMode::UseMeshData ?
		Builder.AddUniqueMeshData(StaticMesh, StaticMeshComponent, LODIndex) : nullptr;

#if WITH_EDITOR
	if (MeshData != nullptr)
	{
		if (MeshData->Description.IsEmpty())
		{
			// TODO: report warning in case the mesh actually has data, which means we failed to extract a mesh description.
			MeshData = nullptr;
		}
		else if (MeshData->BakeUsingTexCoord < 0)
		{
			// TODO: report warning (about missing texture coordinate for baking with mesh data).
			MeshData = nullptr;
		}
	}
#endif

#if WITH_EDITORONLY_DATA
	if (Builder.ExportOptions->bExportSourceModel)
	{
		ProcessMeshDescription(MaterialSlots, MeshData);
	}
	else
#endif
	{
		ProcessRenderData(MaterialSlots, MeshData);
	}
}

#if WITH_EDITORONLY_DATA
void FGLTFDelayedStaticAndSplineMeshTask::ProcessMeshDescription(const TArray<FStaticMaterial>& MaterialSlots, const FGLTFMeshData* MeshData)
{
	FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LODIndex);
	const FPolygonGroupArray& PolygonGroups = MeshDescription->PolygonGroups();

	if (PolygonGroups.Num() != JsonMesh->Primitives.Num()
		|| PolygonGroups.Num() != MaterialSlots.Num())
	{
		return;
	}

	FStaticMeshConstAttributes Attributes(*MeshDescription);
	TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesConstRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesConstRef<FVector3f> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesConstRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesConstRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
	TVertexInstanceAttributesConstRef<FVector4f> VertexInstanceColors = Attributes.GetVertexInstanceColors();
	TPolygonGroupAttributesConstRef<FName> PolygonGroupMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
	TEdgeAttributesConstRef<bool> EdgeHardnesses = Attributes.GetEdgeHardnesses();

	const int32 VertexCount = MeshDescription->Vertices().Num();
	const int32 VertexInstanceCount = MeshDescription->VertexInstances().Num();

	bool bHasVertexColors = Builder.ExportOptions->bExportVertexColors && MeshDescription->VertexInstanceAttributes().HasAttribute(MeshAttribute::VertexInstance::Color);
	int32 UVCount = VertexInstanceUVs.GetNumChannels();

	int32 PrimitiveIndex = 0;
	int32 NumberOfPrimitives = MeshDescription->PolygonGroups().Num();
	for (const FPolygonGroupID& PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
	{
		const TArrayView<const FTriangleID>& TriangleIDs = MeshDescription->GetPolygonGroupTriangles(PolygonGroupID);

		if (TriangleIDs.Num() == 0)
		{
			PrimitiveIndex++;
			//Do not export empty primitives.
			continue;
		}

		FName MaterialSlotName = PolygonGroupMaterialSlotNames[PolygonGroupID];
		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh->Primitives[PrimitiveIndex];

		int32 MaterialIndex = INDEX_NONE;
		{
			for (size_t MatIndex = 0; MatIndex < MaterialSlots.Num(); MatIndex++)
			{
				if (MaterialSlots[MatIndex].ImportedMaterialSlotName == MaterialSlotName)
				{
					MaterialIndex = MatIndex;
					break;
				}
			}

			if (MaterialIndex == INDEX_NONE)
			{
				MaterialIndex = PolygonGroupID.GetValue();
			}
			if (!MaterialSlots.IsValidIndex(MaterialIndex))
			{
				MaterialIndex = 0;
			}
		}

		TArray<int32> OriginalIndices;
		TArray<FVector3f> OriginalPositions;
		TArray<FColor> OriginalVertexColors;
		TArray<FVector3f> OriginalNormals;
		TArray<FVector4f> OriginalTangents;
		TArray<TArray<FVector2f>> OriginalUVs;

		OriginalIndices.Reserve(VertexInstanceCount);
		OriginalPositions.SetNumZeroed(VertexInstanceCount);
		OriginalVertexColors.SetNumZeroed(VertexInstanceCount);
		OriginalNormals.SetNumZeroed(VertexInstanceCount);
		OriginalTangents.SetNumZeroed(VertexInstanceCount);
		for (size_t UVIndex = 0; UVIndex < UVCount; UVIndex++)
		{
			OriginalUVs.Add(TArray<FVector2f>());
			OriginalUVs[UVIndex].SetNumZeroed(VertexInstanceCount);
		}

		for (const FTriangleID& TriangleID : TriangleIDs)
		{
			TArrayView<const FVertexInstanceID> TriangleVertexInstanceIDs = MeshDescription->GetTriangleVertexInstances(TriangleID);
			for (const FVertexInstanceID& VertexInstanceID : TriangleVertexInstanceIDs)
			{
				OriginalIndices.Add(VertexInstanceID);

				FVector3f Position = VertexPositions[MeshDescription->GetVertexInstanceVertex(VertexInstanceID).GetValue()];
				OriginalPositions[VertexInstanceID] = Position;

				OriginalNormals[VertexInstanceID] = VertexInstanceNormals[VertexInstanceID];
				OriginalTangents[VertexInstanceID] = FVector4f(VertexInstanceTangents[VertexInstanceID], VertexInstanceBinormalSigns[VertexInstanceID]);

				for (size_t UVIndex = 0; UVIndex < UVCount; UVIndex++)
				{
					OriginalUVs[UVIndex][VertexInstanceID] = VertexInstanceUVs.Get(VertexInstanceID, UVIndex);
				}
			}
		}

		if (bHasVertexColors)
		{
			for (const FTriangleID& TriangleID : TriangleIDs)
			{
				TArrayView<const FVertexInstanceID> TriangleVertexInstanceIDs = MeshDescription->GetTriangleVertexInstances(TriangleID);
				for (const FVertexInstanceID& VertexInstanceID : TriangleVertexInstanceIDs)
				{
					const FVector4f& SourceVertexColor = VertexInstanceColors[VertexInstanceID];
					OriginalVertexColors[VertexInstanceID] = FLinearColor(SourceVertexColor).ToFColor(true);
				}
			}
		}

		FGLTFIndexArray Indices;
		FGLTFPositionArray PositionBuffer;
		FGLTFColorArray VertexColorBuffer;
		FGLTFNormalArray Normals;
		FGLTFTangentArray Tangents;
		TArray<FGLTFUVArray> UVs;

		//Remap Containers to contain only used data sets (per primitive)
		// + Fill glTF Containers.
		{
			TArray<int32> SortedIndices = OriginalIndices;
			TMap<int32, int32> IndexRemapper;
			{
				SortedIndices.Sort();

				TSet<int32> SortedUniqueIndices;
				SortedUniqueIndices.Append(SortedIndices);
				SortedIndices.Empty();

				SortedIndices = SortedUniqueIndices.Array();

				for (size_t NewIndex = 0; NewIndex < SortedIndices.Num(); NewIndex++)
				{
					IndexRemapper.Add(SortedIndices[NewIndex], NewIndex);
				}
			}

			int32 PrimtiveVertexCount = IndexRemapper.Num();

			Indices.Reserve(PrimtiveVertexCount);
			PositionBuffer.SetNumZeroed(PrimtiveVertexCount);
			VertexColorBuffer.SetNumZeroed(PrimtiveVertexCount);
			Normals.SetNumZeroed(PrimtiveVertexCount);
			Tangents.SetNumZeroed(PrimtiveVertexCount);

			UVs.AddDefaulted(UVCount);
			for (size_t UVIndex = 0; UVIndex < UVCount; UVIndex++)
			{
				UVs[UVIndex].SetNumZeroed(PrimtiveVertexCount);
			}

			for (const int32& OriginalIndex : OriginalIndices)
			{
				int32 NewIndex = IndexRemapper[OriginalIndex];
				Indices.Add(NewIndex);

				FVector3f Position = OriginalPositions[OriginalIndex];
				if (SplineMeshComponent)
				{
					//SplineMeshComponent provided
					//Fix the Positions for Splines:
					const FTransform3f SliceTransform = FTransform3f(SplineMeshComponent->CalcSliceTransform(USplineMeshComponent::GetAxisValueRef(Position, SplineMeshComponent->ForwardAxis)));
					USplineMeshComponent::GetAxisValueRef(Position, SplineMeshComponent->ForwardAxis) = 0;
					Position = SliceTransform.TransformPosition(Position);
				}

				PositionBuffer[NewIndex] = Position;

				if (bHasVertexColors)
				{
					VertexColorBuffer[NewIndex] = OriginalVertexColors[OriginalIndex];
				}

				Normals[NewIndex] = OriginalNormals[OriginalIndex];
				Tangents[NewIndex] = OriginalTangents[OriginalIndex];

				for (size_t UVIndex = 0; UVIndex < UVCount; UVIndex++)
				{
					UVs[UVIndex][NewIndex] = OriginalUVs[UVIndex][OriginalIndex];
				}
			}
		}

		if (Tangents.Num() > 0 && DoesBufferHasZeroVector(Tangents))
		{
			//Do not Export Tangents list that is zeroed out.
			Tangents.Reset();
		}

		if (Normals.Num() > 0 && DoesBufferHasZeroVector(Normals))
		{
			//Do not Export Normals list that is zeroed out.
			Normals.Reset();
		}

		//Set glTF Primitive:
		JsonPrimitive.Indices = Builder.AddUniqueIndexAccessor(Indices, StaticMesh->GetName() + (NumberOfPrimitives > 1 ? (TEXT("_") + FString::FromInt(PrimitiveIndex)) : TEXT("")));
		JsonPrimitive.Attributes.Position = Builder.AddUniquePositionAccessor(PositionBuffer);
		if (bHasVertexColors) JsonPrimitive.Attributes.Color0 = Builder.AddUniqueColorAccessor(VertexColorBuffer);
		JsonPrimitive.Attributes.Normal = Builder.AddUniqueNormalAccessor(Normals);
		JsonPrimitive.Attributes.Tangent = Builder.AddUniqueTangentAccessor(Tangents);
		JsonPrimitive.Attributes.TexCoords.AddUninitialized(UVCount);
		for (size_t UVIndex = 0; UVIndex < UVCount; UVIndex++)
		{
			JsonPrimitive.Attributes.TexCoords[UVIndex] = Builder.AddUniqueUVAccessor(UVs[UVIndex]);
		}
		//
		const UMaterialInterface* Material = Materials.IsValidIndex(MaterialIndex) ? Materials[MaterialIndex] : MaterialSlots[MaterialIndex].MaterialInterface;
		JsonPrimitive.Material = Builder.AddUniqueMaterial(Material, MeshData, { MaterialIndex });

		//Validations:
		if (JsonPrimitive.Attributes.Position == nullptr)
		{
			Builder.LogError(
				FString::Printf(TEXT("Failed to export vertex positions related to material slot %d (%s) in static mesh %s"),
					0,
					*JsonPrimitive.Material->Name,
					*JsonMesh->Name
				));
		}

		PrimitiveIndex++;
	}
}
#endif

void FGLTFDelayedStaticAndSplineMeshTask::ProcessRenderData(const TArray<FStaticMaterial>& MaterialSlots, const FGLTFMeshData* MeshData)
{
	const FStaticMeshLODResources& RenderData = FGLTFMeshUtilities::GetRenderData(StaticMesh, LODIndex);

	const FPositionVertexBuffer& PositionBuffer = RenderData.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& VertexBuffer = RenderData.VertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer* ColorBuffer = &RenderData.VertexBuffers.ColorVertexBuffer; // TODO: add support for overriding color buffer by component

	if (Builder.ExportOptions->bExportVertexColors && HasVertexColors(ColorBuffer))
	{
		Builder.LogSuggestion(FString::Printf(
			TEXT("Vertex colors in mesh %s will act as a multiplier for base color in glTF, regardless of material, which may produce undesirable results"),
			*StaticMesh->GetName()));
	}
	else
	{
		ColorBuffer = nullptr;
	}

	if (StaticMeshComponent != nullptr && StaticMeshComponent->LODData.IsValidIndex(LODIndex))
	{
		const FStaticMeshComponentLODInfo& LODInfo = StaticMeshComponent->LODData[LODIndex];
		ColorBuffer = LODInfo.OverrideVertexColors != nullptr ? LODInfo.OverrideVertexColors : ColorBuffer;
	}
	else if (SplineMeshComponent != nullptr && SplineMeshComponent->LODData.IsValidIndex(LODIndex))
	{
		const FStaticMeshComponentLODInfo& LODInfo = SplineMeshComponent->LODData[LODIndex];
		ColorBuffer = LODInfo.OverrideVertexColors != nullptr ? LODInfo.OverrideVertexColors : ColorBuffer;
	}

	ValidateVertexBuffer(Builder, &VertexBuffer, *StaticMesh->GetName());

	for (int32 MaterialIndex = 0; MaterialIndex < MaterialSlots.Num(); ++MaterialIndex)
	{
		const FGLTFIndexArray SectionIndices = FGLTFMeshUtilities::GetSectionIndices(RenderData, MaterialIndex);
		const FGLTFMeshSection* ConvertedSection = MeshSectionConverter.GetOrAdd(StaticMesh, LODIndex, SectionIndices);

		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh->Primitives[MaterialIndex];
		JsonPrimitive.Indices = Builder.AddUniqueIndexAccessor(ConvertedSection);

		if (!JsonPrimitive.Indices || JsonPrimitive.Indices->Count == 0)
		{
			//Do not export empty primitives.
			continue;
		}

		if (SplineMeshComponent)
		{
			//fix for Splines:
			FPositionVertexBuffer* TransformedPositionBuffer = new FPositionVertexBuffer();

			TransformedPositionBuffer->Init(PositionBuffer.GetNumVertices(), true);

			const uint32 VertexCount = PositionBuffer.GetNumVertices();
			const uint32 Stride = PositionBuffer.GetStride();

			const TUniquePtr<IGLTFBufferAdapter> SourceBuffer = IGLTFBufferAdapter::GetPositions(&PositionBuffer);
			const uint8* SourceData = SourceBuffer->GetData();

			for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex, SourceData+=Stride)
			{
				FVector3f& VertexPosition = TransformedPositionBuffer->VertexPosition(VertexIndex);
				VertexPosition = *reinterpret_cast<const FVector3f*>(SourceData);

				const FTransform3f SliceTransform = FTransform3f(SplineMeshComponent->CalcSliceTransform(USplineMeshComponent::GetAxisValueRef(VertexPosition, SplineMeshComponent->ForwardAxis)));
				USplineMeshComponent::GetAxisValueRef(VertexPosition, SplineMeshComponent->ForwardAxis) = 0;
				VertexPosition = SliceTransform.TransformPosition(VertexPosition);
			}

			JsonPrimitive.Attributes.Position = Builder.AddUniquePositionAccessor(ConvertedSection, TransformedPositionBuffer);
		}
		else
		{
			JsonPrimitive.Attributes.Position = Builder.AddUniquePositionAccessor(ConvertedSection, &PositionBuffer);
		}

		if (JsonPrimitive.Attributes.Position == nullptr)
		{
			Builder.LogError(
				FString::Printf(TEXT("Failed to export vertex positions related to material slot %d (%s) in static mesh %s"),
					MaterialIndex,
					*MaterialSlots[MaterialIndex].MaterialSlotName.ToString(),
					*ConvertedSection->ToString()
				));
		}

		if (ColorBuffer != nullptr)
		{
			JsonPrimitive.Attributes.Color0 = Builder.AddUniqueColorAccessor(ConvertedSection, ColorBuffer);
		}

		// TODO: report warning if both Mesh Quantization (export options) and Use High Precision Tangent Basis (vertex buffer) are disabled
		JsonPrimitive.Attributes.Normal = Builder.AddUniqueNormalAccessor(ConvertedSection, &VertexBuffer);
		JsonPrimitive.Attributes.Tangent = Builder.AddUniqueTangentAccessor(ConvertedSection, &VertexBuffer);

		const uint32 UVCount = VertexBuffer.GetNumTexCoords();
		// TODO: report warning or option to limit UV channels since most viewers don't support more than 2?
		JsonPrimitive.Attributes.TexCoords.AddUninitialized(UVCount);

		for (uint32 UVIndex = 0; UVIndex < UVCount; ++UVIndex)
		{
			JsonPrimitive.Attributes.TexCoords[UVIndex] = Builder.AddUniqueUVAccessor(ConvertedSection, &VertexBuffer, UVIndex);
		}

		const UMaterialInterface* Material = Materials[MaterialIndex];
		JsonPrimitive.Material = Builder.AddUniqueMaterial(Material, MeshData, SectionIndices);
	}
}


FString FGLTFDelayedSkeletalMeshTask::GetName()
{
	return SkeletalMeshComponent != nullptr ? FGLTFNameUtilities::GetName(SkeletalMeshComponent) : SkeletalMesh->GetName();
}

void FGLTFDelayedSkeletalMeshTask::Process()
{
	FGLTFMeshUtilities::FullyLoad(SkeletalMesh);
	JsonMesh->Name = SkeletalMeshComponent != nullptr ? FGLTFNameUtilities::GetName(SkeletalMeshComponent) : SkeletalMesh->GetName();

	const FGLTFMeshData* MeshData = Builder.ExportOptions->BakeMaterialInputs == EGLTFMaterialBakeMode::UseMeshData ?
		Builder.AddUniqueMeshData(SkeletalMesh, SkeletalMeshComponent, LODIndex) : nullptr;

#if WITH_EDITOR
	if (MeshData != nullptr)
	{
		if (MeshData->Description.IsEmpty())
		{
			// TODO: report warning in case the mesh actually has data, which means we failed to extract a mesh description.
			MeshData = nullptr;
		}
		else if (MeshData->BakeUsingTexCoord < 0)
		{
			// TODO: report warning (about missing texture coordinate for baking with mesh data).
			MeshData = nullptr;
		}
	}
#endif

	const TArray<FSkeletalMaterial>& MaterialSlots = FGLTFMeshUtilities::GetMaterials(SkeletalMesh);

#if WITH_EDITORONLY_DATA
	if (Builder.ExportOptions->bExportSourceModel)
	{
		ProcessSourceModel(MaterialSlots, MeshData);
	}
	else
#endif
	{
		ProcessRenderData(MaterialSlots, MeshData);
	}
}

#if WITH_EDITORONLY_DATA
void FGLTFDelayedSkeletalMeshTask::ProcessSourceModel(const TArray<FSkeletalMaterial>& MaterialSlots, const FGLTFMeshData* MeshData)
{
	FSkeletalMeshModel* ImportedSkeletalMeshModel = SkeletalMesh->GetImportedModel();
	if (!ImportedSkeletalMeshModel->LODModels.IsValidIndex(LODIndex))
	{
		//TODO: Log Error
		return;
	}

	bool bExportVertexColors = Builder.ExportOptions->bExportVertexColors;

	FSkeletalMeshLODModel& SourceModel = ImportedSkeletalMeshModel->LODModels[LODIndex];
	const int32 SectionCount = SourceModel.Sections.Num();
	const int32 NumTexCoords = SourceModel.NumTexCoords;

	if (JsonMesh->Primitives.Num() != SectionCount)
	{
		//TODO: Log Error
		return;
	}

	TArray<FSoftSkinVertex> SoftSkinVertices;
	SourceModel.GetNonClothVertices(SoftSkinVertices);

	int32 ClothSectionVertexRemoveOffset = 0;
	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh->Primitives[SectionIndex];

		const FSkelMeshSection& Section = SourceModel.Sections[SectionIndex];
		//Section.MaterialIndex
		if (Section.HasClothingData() || !(MaterialSlots.IsValidIndex(Section.MaterialIndex)))
		{
			ClothSectionVertexRemoveOffset += Section.GetNumVertices();
			continue;
		}

		int32 TriangleCount = Section.NumTriangles;

		TArray<int32> OriginalIndices;
		OriginalIndices.Reserve(TriangleCount * 3);

		// Copy over the index buffer into the FBX polygons set.
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			for (int32 PointIndex = 0; PointIndex < 3; PointIndex++)
			{
				int32 VertexPositionIndex = SourceModel.IndexBuffer[Section.BaseIndex + ((TriangleIndex * 3) + PointIndex)] - ClothSectionVertexRemoveOffset;
				OriginalIndices.Add(VertexPositionIndex);
			}
		}

		struct UIndexProcessedHelper
		{
			int32 NewIndex;
			bool Processed;
			UIndexProcessedHelper(const int32& Index)
				: NewIndex(Index)
				, Processed(false)
			{

			}
		};

		TMap<int32, UIndexProcessedHelper> IndexRemapper;
		{
			TArray<int32> SortedIndices = OriginalIndices;
			SortedIndices.Sort();

			TSet<int32> SortedUniqueIndices;
			SortedUniqueIndices.Append(SortedIndices);
			SortedIndices.Empty();

			SortedIndices = SortedUniqueIndices.Array();

			for (size_t NewIndex = 0; NewIndex < SortedIndices.Num(); NewIndex++)
			{
				IndexRemapper.Add(SortedIndices[NewIndex], NewIndex);
			}
		}

		FGLTFIndexArray Indices;
		FGLTFPositionArray PositionBuffer;
		FGLTFColorArray VertexColorBuffer;
		FGLTFNormalArray Normals;
		FGLTFTangentArray Tangents;
		TArray<FGLTFUVArray> UVs;
		TArray<FGLTFJointInfluenceArray> JointInfluences; //Per Group
		TArray<FGLTFJointWeightArray> JointWeights; //Per Group

		int32 PrimtiveVertexCount = IndexRemapper.Num();

		Indices.Reserve(OriginalIndices.Num());

		PositionBuffer.SetNumZeroed(PrimtiveVertexCount);
		VertexColorBuffer.SetNumZeroed(PrimtiveVertexCount);
		Normals.SetNumZeroed(PrimtiveVertexCount);
		Tangents.SetNumZeroed(PrimtiveVertexCount);

		UVs.AddDefaulted(NumTexCoords);
		for (size_t UVIndex = 0; UVIndex < NumTexCoords; UVIndex++)
		{
			UVs[UVIndex].SetNumZeroed(PrimtiveVertexCount);
		}

		const uint32 GroupCount = (Section.GetMaxBoneInfluences() + 3) / 4;
		if (Builder.ExportOptions->bExportVertexSkinWeights)
		{
			// TODO: report warning or option to limit groups (of joints and weights) since most viewers don't support more than one?
			JointInfluences.AddDefaulted(GroupCount);
			JointWeights.AddDefaulted(GroupCount);
			for (size_t GroupIndex = 0; GroupIndex < GroupCount; GroupIndex++)
			{
				JointInfluences[GroupIndex].SetNumZeroed(PrimtiveVertexCount);
				JointWeights[GroupIndex].SetNumZeroed(PrimtiveVertexCount);
			}
		}

		for (const int32& OriginalIndex : OriginalIndices)
		{
			UIndexProcessedHelper& IndexProcessedHelper = IndexRemapper[OriginalIndex];
			int32 NewIndex = IndexProcessedHelper.NewIndex;

			Indices.Add(NewIndex);

			if (IndexProcessedHelper.Processed)
			{
				continue;
			}
			IndexProcessedHelper.Processed = true;

			const FSoftSkinVertex& OriginalSoftSkinVertex = SoftSkinVertices[OriginalIndex];


			PositionBuffer[NewIndex] = OriginalSoftSkinVertex.Position;

			if (bExportVertexColors)
			{
				VertexColorBuffer[NewIndex] = OriginalSoftSkinVertex.Color;
			}

			Normals[NewIndex] = OriginalSoftSkinVertex.TangentZ;
			Tangents[NewIndex] = OriginalSoftSkinVertex.TangentX;

			for (size_t UVIndex = 0; UVIndex < NumTexCoords; UVIndex++)
			{
				UVs[UVIndex][NewIndex] = OriginalSoftSkinVertex.UVs[UVIndex];
			}

			if (Builder.ExportOptions->bExportVertexSkinWeights)
			{
				for (size_t GroupIndex = 0; GroupIndex < GroupCount; GroupIndex++)
				{
					for (size_t GroupBoneInfluenceCounter = 0; GroupBoneInfluenceCounter < 4; GroupBoneInfluenceCounter++)
					{
						int8 InfluenceIndex = GroupIndex * 4 + GroupBoneInfluenceCounter;

						JointInfluences[GroupIndex][NewIndex][GroupBoneInfluenceCounter] = Section.BoneMap[OriginalSoftSkinVertex.InfluenceBones[InfluenceIndex]];
						JointWeights[GroupIndex][NewIndex][GroupBoneInfluenceCounter] = OriginalSoftSkinVertex.InfluenceWeights[InfluenceIndex];
					}
				}
			}
		}

		if (Tangents.Num() > 0 && DoesBufferHasZeroVector(Tangents))
		{
			//Do not Export Tangents list that is zeroed out.
			Tangents.Reset();
		}

		if (Normals.Num() > 0 && DoesBufferHasZeroVector(Normals))
		{
			//Do not Export Normals list that is zeroed out.
			Normals.Reset();
		}

		//Set glTF Primitive:
		JsonPrimitive.Indices = Builder.AddUniqueIndexAccessor(Indices, SkeletalMesh->GetName() + (SectionCount > 1 ? (TEXT("_") + FString::FromInt(SectionIndex)) : TEXT("")));
		JsonPrimitive.Attributes.Position = Builder.AddUniquePositionAccessor(PositionBuffer);
		if (bExportVertexColors) JsonPrimitive.Attributes.Color0 = Builder.AddUniqueColorAccessor(VertexColorBuffer);
		JsonPrimitive.Attributes.Normal = Builder.AddUniqueNormalAccessor(Normals);
		JsonPrimitive.Attributes.Tangent = Builder.AddUniqueTangentAccessor(Tangents);
		JsonPrimitive.Attributes.TexCoords.AddUninitialized(NumTexCoords);
		for (size_t UVIndex = 0; UVIndex < NumTexCoords; UVIndex++)
		{
			JsonPrimitive.Attributes.TexCoords[UVIndex] = Builder.AddUniqueUVAccessor(UVs[UVIndex]);
		}

		if (Builder.ExportOptions->bExportVertexSkinWeights)
		{
			JsonPrimitive.Attributes.Joints.AddUninitialized(GroupCount);
			JsonPrimitive.Attributes.Weights.AddUninitialized(GroupCount);
			for (size_t GroupCountIndex = 0; GroupCountIndex < GroupCount; GroupCountIndex++)
			{
				JsonPrimitive.Attributes.Joints[GroupCountIndex] = Builder.AddUniqueJointAccessor(JointInfluences[GroupCountIndex]);
				JsonPrimitive.Attributes.Weights[GroupCountIndex] = Builder.AddUniqueWeightAccessor(JointWeights[GroupCountIndex]);
			}
		}

		//
		const UMaterialInterface* Material = Materials.IsValidIndex(Section.MaterialIndex) ? Materials[Section.MaterialIndex] : MaterialSlots[Section.MaterialIndex].MaterialInterface;
		JsonPrimitive.Material = Builder.AddUniqueMaterial(Material, MeshData, { SectionIndex });

		//Validations:
		if (JsonPrimitive.Attributes.Position == nullptr)
		{
			Builder.LogError(
				FString::Printf(TEXT("Failed to export vertex positions related to material slot %d (%s) in static mesh %s"),
					0,
					*JsonPrimitive.Material->Name,
					*JsonMesh->Name
				));
		}
	}
}
#endif

void FGLTFDelayedSkeletalMeshTask::ProcessRenderData(const TArray<FSkeletalMaterial>& MaterialSlots, const FGLTFMeshData* MeshData)
{
	const FSkeletalMeshLODRenderData& RenderData = FGLTFMeshUtilities::GetRenderData(SkeletalMesh, LODIndex);
	const FPositionVertexBuffer& PositionBuffer = RenderData.StaticVertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& VertexBuffer = RenderData.StaticVertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer* ColorBuffer = &RenderData.StaticVertexBuffers.ColorVertexBuffer; // TODO: add support for overriding color buffer by component
	const FSkinWeightVertexBuffer* SkinWeightBuffer = RenderData.GetSkinWeightVertexBuffer(); // TODO: add support for overriding skin weight buffer by component
	// TODO: add support for skin weight profiles?
	// TODO: add support for morph targets

	if (Builder.ExportOptions->bExportVertexColors && HasVertexColors(ColorBuffer))
	{
		Builder.LogSuggestion(FString::Printf(
			TEXT("Vertex colors in mesh %s will act as a multiplier for base color in glTF, regardless of material, which may produce undesirable results"),
			*SkeletalMesh->GetName()));
	}
	else
	{
		ColorBuffer = nullptr;
	}

	if (SkeletalMeshComponent != nullptr && SkeletalMeshComponent->LODInfo.IsValidIndex(LODIndex))
	{
		const FSkelMeshComponentLODInfo& LODInfo = SkeletalMeshComponent->LODInfo[LODIndex];
		ColorBuffer = LODInfo.OverrideVertexColors != nullptr ? LODInfo.OverrideVertexColors : ColorBuffer;
		SkinWeightBuffer = LODInfo.OverrideSkinWeights != nullptr ? LODInfo.OverrideSkinWeights : SkinWeightBuffer;
	}

	ValidateVertexBuffer(Builder, &VertexBuffer, *SkeletalMesh->GetName());

	const int32 MaterialCount = MaterialSlots.Num();

	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		const FGLTFIndexArray SectionIndices = FGLTFMeshUtilities::GetSectionIndices(RenderData, MaterialIndex);
		const FGLTFMeshSection* ConvertedSection = MeshSectionConverter.GetOrAdd(SkeletalMesh, LODIndex, SectionIndices);

		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh->Primitives[MaterialIndex];
		JsonPrimitive.Indices = Builder.AddUniqueIndexAccessor(ConvertedSection);

		JsonPrimitive.Attributes.Position = Builder.AddUniquePositionAccessor(ConvertedSection, &PositionBuffer);
		if (JsonPrimitive.Attributes.Position == nullptr)
		{
			Builder.LogError(
				FString::Printf(TEXT("Failed to export vertex positions related to material slot %d (%s) in skeletal mesh %s"),
					MaterialIndex,
					*MaterialSlots[MaterialIndex].MaterialSlotName.ToString(),
					*ConvertedSection->ToString()
				));
		}

		if (ColorBuffer != nullptr)
		{
			JsonPrimitive.Attributes.Color0 = Builder.AddUniqueColorAccessor(ConvertedSection, ColorBuffer);
		}

		// TODO: report warning if both Mesh Quantization (export options) and Use High Precision Tangent Basis (vertex buffer) are disabled
		JsonPrimitive.Attributes.Normal = Builder.AddUniqueNormalAccessor(ConvertedSection, &VertexBuffer);
		JsonPrimitive.Attributes.Tangent = Builder.AddUniqueTangentAccessor(ConvertedSection, &VertexBuffer);

		const uint32 UVCount = VertexBuffer.GetNumTexCoords();
		// TODO: report warning or option to limit UV channels since most viewers don't support more than 2?
		JsonPrimitive.Attributes.TexCoords.AddUninitialized(UVCount);

		for (uint32 UVIndex = 0; UVIndex < UVCount; ++UVIndex)
		{
			JsonPrimitive.Attributes.TexCoords[UVIndex] = Builder.AddUniqueUVAccessor(ConvertedSection, &VertexBuffer, UVIndex);
		}

		if (Builder.ExportOptions->bExportVertexSkinWeights)
		{
			const uint32 GroupCount = (SkinWeightBuffer->GetMaxBoneInfluences() + 3) / 4;
			// TODO: report warning or option to limit groups (of joints and weights) since most viewers don't support more than one?
			JsonPrimitive.Attributes.Joints.AddUninitialized(GroupCount);
			JsonPrimitive.Attributes.Weights.AddUninitialized(GroupCount);

			for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
			{
				JsonPrimitive.Attributes.Joints[GroupIndex] = Builder.AddUniqueJointAccessor(ConvertedSection, SkinWeightBuffer, GroupIndex * 4);
				JsonPrimitive.Attributes.Weights[GroupIndex] = Builder.AddUniqueWeightAccessor(ConvertedSection, SkinWeightBuffer, GroupIndex * 4);
			}
		}

		const UMaterialInterface* Material = Materials[MaterialIndex];
		JsonPrimitive.Material = Builder.AddUniqueMaterial(Material, MeshData, SectionIndices);
	}
}


FGLTFDelayedLandscapeTask::FGLTFDelayedLandscapeTask(FGLTFConvertBuilder& Builder, const ULandscapeComponent& LandscapeComponent, FGLTFJsonMesh* JsonMesh, const UMaterialInterface& LandscapeMaterial)
	: FGLTFDelayedTask(EGLTFTaskPriority::Mesh)
	, Builder(Builder)
	, LandscapeComponent(LandscapeComponent)
	, JsonMesh(JsonMesh)
	, LandscapeMaterial(LandscapeMaterial)
{
}

FString FGLTFDelayedLandscapeTask::GetName()
{
	return LandscapeComponent.GetName();
}

void FGLTFDelayedLandscapeTask::Process()
{
	const ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(LandscapeComponent.GetOwner());
	JsonMesh->Name = LandscapeComponent.GetName();

	int32 MinX = MAX_int32, MinY = MAX_int32;
	int32 MaxX = MIN_int32, MaxY = MIN_int32;

	// Create and fill in the vertex position data source.
	int32 ExportLOD = 0;
#if WITH_EDITOR
	ExportLOD = Landscape->ExportLOD;
#endif
	const int32 ComponentSizeQuads = ((Landscape->ComponentSizeQuads + 1) >> ExportLOD) - 1;
	const float ScaleFactor = (float)Landscape->ComponentSizeQuads / (float)ComponentSizeQuads;
	const int32 VertexCount = FMath::Square(ComponentSizeQuads + 1);
	const int32 TriangleCount = FMath::Square(ComponentSizeQuads) * 2;

	FGLTFIndexArray Indices;
	FGLTFPositionArray PositionBuffer;
	FGLTFColorArray VertexColorBuffer;
	FGLTFNormalArray Normals;
	FGLTFTangentArray Tangents;
	FGLTFUVArray UV;

	Indices.Reserve(FMath::Square(ComponentSizeQuads) * 2 * 3);
	PositionBuffer.SetNumZeroed(VertexCount);
	VertexColorBuffer.SetNumZeroed(VertexCount);
	Normals.SetNumZeroed(VertexCount);
	Tangents.SetNumZeroed(VertexCount);
	UV.SetNumZeroed(VertexCount);

	FGLTFJsonPrimitive& JsonPrimitive = JsonMesh->Primitives[0];
	TArray<uint8> VisibilityData;
	VisibilityData.SetNumZeroed(VertexCount);

	int OffsetX = Landscape->LandscapeSectionOffset.X;
	int OffsetY = Landscape->LandscapeSectionOffset.Y;
	
	FGLTFLandscapeComponentDataInterface CDI(LandscapeComponent, ExportLOD);

	TArray<uint8> CompVisData;
	const TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = LandscapeComponent.GetWeightmapLayerAllocations();

	for (int32 AllocIdx = 0; AllocIdx < ComponentWeightmapLayerAllocations.Num(); AllocIdx++)
	{
		const FWeightmapLayerAllocationInfo& AllocInfo = ComponentWeightmapLayerAllocations[AllocIdx];
		//Landscape Visibility Layer is named: __LANDSCAPE_VISIBILITY__
		//based on: Engine/Source/Runtime/Landscape/Private/Materials/MaterialExpressionLandscapeVisibilityMask.cpp
		//		FName UMaterialExpressionLandscapeVisibilityMask::ParameterName = FName("__LANDSCAPE_VISIBILITY__");
		FString LayerName = AllocInfo.LayerInfo->LayerName.ToString();
		if (LayerName == TEXT("__LANDSCAPE_VISIBILITY__"))
		{
			CDI.GetWeightmapTextureData(AllocInfo.LayerInfo, CompVisData);
		}
	}

	if (CompVisData.Num() > 0)
	{
		for (int32 i = 0; i < VertexCount; ++i)
		{
			VisibilityData[i] = CompVisData[CDI.VertexIndexToTexel(i)];
		}
	}

	for (int32 VertexIndex = 0; VertexIndex < VertexCount; VertexIndex++)
	{
		int32 VertX, VertY;
		CDI.VertexIndexToXY(VertexIndex, VertX, VertY);

		FVector3f Position;
		FVector3f Normal;
		FVector2f UVElement;
		CDI.GetPositionNormalUV(VertX, VertY, Position, Normal, UVElement);

		PositionBuffer[VertexIndex] = Position;
		Normals[VertexIndex] = Normal;
		UV[VertexIndex] = UVElement;
	}

	const int32 VisThreshold = 170;
	
	for (int32 Y = 0; Y < ComponentSizeQuads; Y++)
	{
		for (int32 X = 0; X < ComponentSizeQuads; X++)
		{
			if (VisibilityData[Y * (ComponentSizeQuads + 1) + X] < VisThreshold)
			{
				Indices.Push((X + 0) + (Y + 0) * (ComponentSizeQuads + 1));
				Indices.Push((X + 1) + (Y + 1) * (ComponentSizeQuads + 1));
				Indices.Push((X + 1) + (Y + 0) * (ComponentSizeQuads + 1));

				Indices.Push((X + 0) + (Y + 0) * (ComponentSizeQuads + 1));
				Indices.Push((X + 0) + (Y + 1) * (ComponentSizeQuads + 1));
				Indices.Push((X + 1) + (Y + 1) * (ComponentSizeQuads + 1));
			}
		}
	}

	if (Indices.Num())
	{
		JsonPrimitive.Attributes.Position = Builder.AddUniquePositionAccessor(PositionBuffer);
		JsonPrimitive.Attributes.Normal = Builder.AddUniqueNormalAccessor(Normals);
		JsonPrimitive.Attributes.TexCoords.AddUninitialized(1);
		JsonPrimitive.Attributes.TexCoords[0] = Builder.AddUniqueUVAccessor(UV);
		JsonPrimitive.Indices = Builder.AddUniqueIndexAccessor(Indices, JsonMesh->Name);
		JsonPrimitive.Material = Builder.AddUniqueMaterial(&LandscapeMaterial);
	}
}