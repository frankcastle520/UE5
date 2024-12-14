// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "MassEntityTypes.h"
#include "MassProcessingTypes.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "Mass"

class FMassEntityModule : public IModuleInterface
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
};

IMPLEMENT_MODULE(FMassEntityModule, MassEntity)

void FMassEntityModule::StartupModule()
{
#if WITH_UNREAL_DEVELOPER_TOOLS
	FModuleManager::Get().LoadModule("MassEntityTestSuite");
#endif // WITH_UNREAL_DEVELOPER_TOOLS

#if MASS_DO_PARALLEL
	UE_LOG(LogMass, Log, TEXT("MassEntity running with MULTITHREADING support."));
#else
	UE_LOG(LogMass, Log, TEXT("MassEntity running in game thread."));
#endif // MASS_DO_PARALLEL
}

#undef LOCTEXT_NAMESPACE 
