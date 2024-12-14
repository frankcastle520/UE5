// Copyright Epic Games, Inc. All Rights Reserved.

#include "ValidationUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Commandlets/Commandlet.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectSystem.h"
#include "RHIGlobals.h"
#include "MuCO/CustomizableObjectPrivate.h"
#include "MuCOE/CustomizableObjectBenchmarkingUtils.h"
#include "UObject/ObjectPtr.h"

void PrepareAssetRegistry()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName);
	UE_LOG(LogMutable,Display,TEXT("Searching all assets (this will take some time)..."));
	
	const double AssetRegistrySearchStartSeconds = FPlatformTime::Seconds();
	AssetRegistryModule.Get().SearchAllAssets(true /* bSynchronousSearch */);
	const double AssetRegistrySearchEndSeconds = FPlatformTime::Seconds() - AssetRegistrySearchStartSeconds;
	UE_LOG(LogMutable, Log, TEXT("(double) asset_registry_search_time_s : %f "), AssetRegistrySearchEndSeconds);

	UE_LOG(LogMutable,Display,TEXT("Asset searching completed in \"%f\" seconds!"), AssetRegistrySearchEndSeconds);
}


void LogGlobalSettings()
{
	// Mutable Settings
	const int32 WorkingMemoryKB = UCustomizableObjectSystem::GetInstanceChecked()->GetWorkingMemory() ;
	UE_LOG(LogMutable,Log, TEXT("(int) working_memory_bytes : %d"), WorkingMemoryKB*1024)
	UE_LOG(LogMutable, Display, TEXT("The mutable updates will use as working memory the value of %d KB"), WorkingMemoryKB)
	
	// Expand this when adding new controls from the .xml file
	
	// RHI Settings
	UE_LOG(LogMutable, Log, TEXT("(string) rhi_adapter_name : %s"), *GRHIAdapterName )
}


void Wait(const double ToWaitSeconds)
{
	check (ToWaitSeconds > 0);
	
	const double EndSeconds = FPlatformTime::Seconds() + ToWaitSeconds;
	UE_LOG(LogMutable,Display,TEXT("Holding test execution for %f seconds."),ToWaitSeconds);
	while (FPlatformTime::Seconds() < EndSeconds)
	{
		// Tick the engine
		CommandletHelpers::TickEngine();

		// Stop if exit was requested
		if (IsEngineExitRequested())
		{
			break;
		}
	}

	UE_LOG(LogMutable,Display,TEXT("Resuming test execution."));
}


FCompilationOptions GetCompilationOptionsForBenchmarking (UCustomizableObject& ReferenceCustomizableObject)
{
	// Override some configurations that may have been changed by the user
	FCompilationOptions CISCompilationOptions = ReferenceCustomizableObject.GetPrivate()->GetCompileOptions();
	CISCompilationOptions.bSilentCompilation = false;										// totally optional
	CISCompilationOptions.OptimizationLevel = CustomizableObjectBenchmarkingUtils::GetOptimizationLevelForBenchmarking();
	CISCompilationOptions.TextureCompression = ECustomizableObjectTextureCompression::Fast;	// Does not affect instance update speed but does compilation

	return CISCompilationOptions;
}



