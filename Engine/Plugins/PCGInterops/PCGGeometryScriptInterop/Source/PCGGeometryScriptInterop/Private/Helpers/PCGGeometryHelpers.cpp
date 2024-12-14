// Copyright Epic Games, Inc. All Rights Reserved.

#include "Helpers/PCGGeometryHelpers.h"

#include "ConversionUtils/SceneComponentToDynamicMesh.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Utils/PCGLogErrors.h"

namespace PCGGeometryHelpers
{
	void GeometryScriptDebugToPCGLog(FPCGContext* Context, const UGeometryScriptDebug* Debug)
	{
		check(Context && Debug);

		for (const FGeometryScriptDebugMessage& Message : Debug->Messages)
		{
			if (Message.MessageType == EGeometryScriptDebugMessageType::ErrorMessage)
			{
				PCGLog::LogErrorOnGraph(Message.Message, Context);
			}
			else
			{
				PCGLog::LogWarningOnGraph(Message.Message, Context);
			}
		}
	}

	UE::Conversion::EMeshLODType SafeConversionLODType(const EGeometryScriptLODType LODType)
	{
		// Make sure the LOD is valid, and make the conversion.
		UE::Conversion::EMeshLODType RequestedLODType{};

		switch (LODType)
		{
		case EGeometryScriptLODType::MaxAvailable:
			RequestedLODType = UE::Conversion::EMeshLODType::MaxAvailable;
			break;
		case EGeometryScriptLODType::HiResSourceModel:
			RequestedLODType = UE::Conversion::EMeshLODType::HiResSourceModel;
			break;
		case EGeometryScriptLODType::SourceModel:
			RequestedLODType = UE::Conversion::EMeshLODType::SourceModel;
			break;
		case EGeometryScriptLODType::RenderData:
			RequestedLODType = UE::Conversion::EMeshLODType::RenderData;
			break;
		default:
			break;
		}

		return RequestedLODType;
	}

	void RemapMaterials(UE::Geometry::FDynamicMesh3& InMesh, const TArray<UMaterialInterface*>& FromMaterials, TArray<TObjectPtr<UMaterialInterface>>& ToMaterials, const UE::Geometry::FMeshIndexMappings* OptionalMappings)
	{
		if (FromMaterials.IsEmpty() || ToMaterials.IsEmpty() || !InMesh.HasAttributes() || !InMesh.Attributes()->HasMaterialID())
		{
			return;
		}
		
		TMap<int32, int32> MaterialIDRemap;
		MaterialIDRemap.Reserve(FromMaterials.Num());
		
		for (int32 FromMaterialIndex = 0; FromMaterialIndex < FromMaterials.Num(); ++FromMaterialIndex)
		{
			UMaterialInterface* FromMaterial = FromMaterials[FromMaterialIndex];
			int32 ToMaterialIndex = ToMaterials.IndexOfByKey(FromMaterial);
			if (ToMaterialIndex == INDEX_NONE)
			{
				ToMaterialIndex = ToMaterials.Add(FromMaterial);
			}

			if (ToMaterialIndex != FromMaterialIndex)
			{
				MaterialIDRemap.Emplace(FromMaterialIndex, ToMaterialIndex);
			}
		}

		if (!MaterialIDRemap.IsEmpty())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGGeometryHelpers::RemapMaterials);
			UE::Geometry::FDynamicMeshMaterialAttribute* MaterialAttribute = InMesh.Attributes()->GetMaterialID();

			auto Remap = [&MaterialAttribute, &MaterialIDRemap](const int32 TriangleID)
			{
				const int32 OriginalMaterialID = MaterialAttribute->GetValue(TriangleID);
				if (const int32* RemappedMaterialID = MaterialIDRemap.Find(OriginalMaterialID))
				{
					MaterialAttribute->SetValue(TriangleID, *RemappedMaterialID);
				}
			};
			
			// TODO: Could be parallelized
			if (OptionalMappings)
			{
				for (const TPair<int32, int32>& MapTriangleID : OptionalMappings->GetTriangleMap().GetForwardMap())
				{
					Remap(MapTriangleID.Value);
				}
			}
			else
			{
				for (const int32 TriangleID : InMesh.TriangleIndicesItr())
				{
					Remap(TriangleID);
				}
			}
		}
	}
}