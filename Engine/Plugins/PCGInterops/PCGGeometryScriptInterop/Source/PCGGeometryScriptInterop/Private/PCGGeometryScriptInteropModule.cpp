// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "PCGModule.h"
#include "Data/PCGDynamicMeshData.h"
#include "Elements/PCGGetDynamicMeshData.h"

#if WITH_EDITOR
#include "Editor/PCGDynamicMeshDataVisualization.h"
#endif // WITH_EDITOR

class FPCGGeometryScriptInteropModule final : public IModuleInterface
{
public:
	//~ IModuleInterface implementation
	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}
	
	virtual void StartupModule() override
	{
		FModuleManager::Get().LoadModuleChecked(TEXT("PCG"));

#if WITH_EDITOR
		FPCGDataVisualizationRegistry& DataVisRegistry = FPCGModule::GetMutablePCGDataVisualizationRegistry();
		DataVisRegistry.RegisterPCGDataVisualization(UPCGDynamicMeshData::StaticClass(), MakeUnique<const FPCGDynamicMeshDataVisualization>());
#endif // WITH_EDITOR

		FPCGGetDataFunctionRegistry& PCGDataFunctionRegistry = FPCGModule::MutableGetDataFunctionRegistry();
		GetActorDataFunctionHandle = PCGDataFunctionRegistry.RegisterDataFromActorFunction(&PCGGetDynamicMeshData::GetDynamicMeshDataFromActor);
		GetComponentDataFunctionHandle = PCGDataFunctionRegistry.RegisterDataFromComponentFunction(&PCGGetDynamicMeshData::GetDynamicMeshDataFromComponent);
	}

	virtual void ShutdownModule() override
	{
		// No need to unregister if the PCG module is already dead.
		if (FPCGModule::IsPCGModuleLoaded())
		{
			FPCGGetDataFunctionRegistry& PCGDataFunctionRegistry = FPCGModule::MutableGetDataFunctionRegistry();
			PCGDataFunctionRegistry.UnregisterDataFromActorFunction(GetActorDataFunctionHandle);
			PCGDataFunctionRegistry.UnregisterDataFromComponentFunction(GetComponentDataFunctionHandle);
		
#if WITH_EDITOR
			FPCGDataVisualizationRegistry& DataVisRegistry = FPCGModule::GetMutablePCGDataVisualizationRegistry();
			DataVisRegistry.UnregisterPCGDataVisualization(UPCGDynamicMeshData::StaticClass());
#endif // WITH_EDITOR
		}
	}
	//~ End IModuleInterface implementation
	
private:
	FPCGGetDataFunctionRegistry::FFunctionHandle GetActorDataFunctionHandle = (uint64)(-1);
	FPCGGetDataFunctionRegistry::FFunctionHandle GetComponentDataFunctionHandle = (uint64)(-1);
};

IMPLEMENT_MODULE(FPCGGeometryScriptInteropModule, PCGGeometryScriptInterop);
