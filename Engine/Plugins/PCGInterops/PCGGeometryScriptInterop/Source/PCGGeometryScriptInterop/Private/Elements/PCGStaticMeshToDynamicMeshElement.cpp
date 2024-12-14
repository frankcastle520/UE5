// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGStaticMeshToDynamicMeshElement.h"

#include "PCGContext.h"
#include "PCGModule.h"
#include "Data/PCGDynamicMeshData.h"

#include "UDynamicMesh.h"
#include "ConversionUtils/SceneComponentToDynamicMesh.h"
#include "Engine/StaticMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"

#define LOCTEXT_NAMESPACE "PCGStaticMeshToDynamicMeshElementElement"

#if WITH_EDITOR
FName UPCGStaticMeshToDynamicMeshSettings::GetDefaultNodeName() const
{
	return FName(TEXT("StaticMeshToDynamicMeshElement"));
}

FText UPCGStaticMeshToDynamicMeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Static Mesh To Dynamic Mesh Element");
}

FText UPCGStaticMeshToDynamicMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Convert a static mesh into a dynamic mesh data.");
}
#endif // WITH_EDITOR

FPCGElementPtr UPCGStaticMeshToDynamicMeshSettings::CreateElement() const
{
	return MakeShared<FPCGStaticMeshToDynamicMeshElement>();
}

TArray<FPCGPinProperties> UPCGStaticMeshToDynamicMeshSettings::InputPinProperties() const
{
	return {};
}

TArray<FPCGPinProperties> UPCGStaticMeshToDynamicMeshSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, false, false);
	return Properties;
}

bool FPCGStaticMeshToDynamicMeshElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	// Without context, we can't know, so force it in the main thread to be safe.
	return !Context || Context->CurrentPhase == EPCGExecutionPhase::PrepareData;
}

FPCGContext* FPCGStaticMeshToDynamicMeshElement::CreateContext()
{
	return new FPCGStaticMeshToDynamicMeshContext();
}

bool FPCGStaticMeshToDynamicMeshElement::PrepareDataInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshToDynamicMeshElement::Execute);

	FPCGStaticMeshToDynamicMeshContext* Context = static_cast<FPCGStaticMeshToDynamicMeshContext*>(InContext);
	check(Context);

	const UPCGStaticMeshToDynamicMeshSettings* Settings = InContext->GetInputSettings<UPCGStaticMeshToDynamicMeshSettings>();
	check(Settings);
	
	if (Context->WasLoadRequested() || Settings->StaticMesh.IsNull())
	{
		return true;
	}

	TArray<FSoftObjectPath> ObjectsToLoad;
	if (Settings->bExtractMaterials)
	{
		Algo::Transform(Settings->OverrideMaterials, ObjectsToLoad, [](const TSoftObjectPtr<UMaterialInterface>& MaterialSoftPtr) { return MaterialSoftPtr.ToSoftObjectPath(); });
	}

	ObjectsToLoad.Add(Settings->StaticMesh.ToSoftObjectPath());
	
	return Context->RequestResourceLoad(Context, std::move(ObjectsToLoad), !Settings->bSynchronousLoad);
}

bool FPCGStaticMeshToDynamicMeshElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshToDynamicMeshElement::Execute);

	check(InContext);

	const UPCGStaticMeshToDynamicMeshSettings* Settings = InContext->GetInputSettings<UPCGStaticMeshToDynamicMeshSettings>();
	check(Settings);

	UStaticMesh* StaticMesh = Settings->StaticMesh.Get();
	if (!StaticMesh)
	{
		if (!Settings->StaticMesh.IsNull())
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("StaticMeshNull", "Static mesh failed to load."), InContext);
		}
		
		return true;
	}

	if (Settings->bExtractMaterials && !Settings->OverrideMaterials.IsEmpty())
	{
		if (StaticMesh->GetStaticMaterials().Num() != Settings->OverrideMaterials.Num())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MismatchMaterials", "Mismatch number between Static mesh materials ({0}) and override materials ({1})"), StaticMesh->GetStaticMaterials().Num(), Settings->OverrideMaterials.Num()));
			return true;
		}
		else if (const TSoftObjectPtr<UMaterialInterface>* UnloadedMaterial = Settings->OverrideMaterials.FindByPredicate([](const TSoftObjectPtr<UMaterialInterface>& MaterialSoftPtr) -> bool { return !MaterialSoftPtr.Get(); }))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("UnloadedMaterial", "Material {0} failed to load."), FText::FromString(UnloadedMaterial->ToSoftObjectPath().ToString())));
			return true;
		}
	}
	
	UE::Conversion::EMeshLODType LODType = static_cast<UE::Conversion::EMeshLODType>(Settings->RequestedLODType);
	int32 LODIndex = Settings->RequestedLODIndex;

	UE::Conversion::FStaticMeshConversionOptions ConversionOptions{};
	FText ErrorMessage;
	UE::Geometry::FDynamicMesh3 NewMesh;
	const bool bSuccess = UE::Conversion::StaticMeshToDynamicMesh(StaticMesh, NewMesh, ErrorMessage, ConversionOptions, LODType, LODIndex);

	if (bSuccess)
	{
		TArray<UMaterialInterface*> Materials;

		if (Settings->bExtractMaterials)
		{
			if (!Settings->OverrideMaterials.IsEmpty())
			{
				Algo::Transform(Settings->OverrideMaterials, Materials, [](const TSoftObjectPtr<UMaterialInterface>& MaterialSoftPtr) { return MaterialSoftPtr.Get(); });
			}
			else
			{
				TArray<FName> MaterialSlotNames;
				UGeometryScriptLibrary_StaticMeshFunctions::GetMaterialListFromStaticMesh(StaticMesh, Materials, MaterialSlotNames);
			}
		}
		
		UPCGDynamicMeshData* DynMeshData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(InContext);
		DynMeshData->Initialize(std::move(NewMesh), Materials);

		InContext->OutputData.TaggedData.Emplace_GetRef().Data = DynMeshData;
	}
	else
	{
		PCGLog::LogErrorOnGraph(ErrorMessage, InContext);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
