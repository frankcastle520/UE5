// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntitySubsystem.h"
#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassEntitySubsystem)

namespace UE::Mass::Private
{
	static bool bEnableMassConcurrentReserveRuntime = true;
	static int32 ConcurrentReserveMaxEntityCount = 1 << 27;
	static int32 ConcurrentReserveMaxEntitiesPerPage = 1 << 16;

	namespace
	{
		FAutoConsoleVariableRef CVars[] = {
			{
				TEXT("Mass.ConcurrentReserve.Enable"),
				bEnableMassConcurrentReserveRuntime,
				TEXT("Enable Mass's concurrent reserve feature in runtime"),
				ECVF_Default
			},
			{
				TEXT("Mass.ConcurrentReserve.MaxEntityCount"),
				ConcurrentReserveMaxEntityCount,
				TEXT("Set maximum number of permissible entities.  Must be power of 2."),
				ECVF_Default
			},
			{
				TEXT("Mass.ConcurrentReserve.EntitiesPerPage"),
				ConcurrentReserveMaxEntitiesPerPage,
				TEXT("Set number of entities per page. Must be power of 2. Larger reduces fixed memory overhead of FEntityData page lookup but requires bigger contiguous memory blocks per page"),
				ECVF_Default
			}
		};
	}
}
//////////////////////////////////////////////////////////////////////
// UMassEntitySubsystem

UMassEntitySubsystem::UMassEntitySubsystem()
	: EntityManager(MakeShareable(new FMassEntityManager(this)))
{
	
}

void UMassEntitySubsystem::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	EntityManager->GetResourceSizeEx(CumulativeResourceSize);
}

void UMassEntitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FMassEntityManagerStorageInitParams InitializationParams;
	
	if (UE::Mass::Private::bEnableMassConcurrentReserveRuntime)
	{
		InitializationParams.Emplace<FMassEntityManager_InitParams_Concurrent>(
			FMassEntityManager_InitParams_Concurrent
			{
				.MaxEntityCount = static_cast<uint32>(UE::Mass::Private::ConcurrentReserveMaxEntityCount),
				.MaxEntitiesPerPage = static_cast<uint32>(UE::Mass::Private::ConcurrentReserveMaxEntitiesPerPage)
			});
	}
	else
	{
		InitializationParams.Emplace<FMassEntityManager_InitParams_SingleThreaded>();
	}
	
	EntityManager->Initialize(InitializationParams);
	HandleLateCreation();
}

void UMassEntitySubsystem::PostInitialize()
{
	Super::PostInitialize();
	// this needs to be done after all the subsystems have been initialized since some processors might want to access
	// them during processors' initialization
	EntityManager->PostInitialize();
}

void UMassEntitySubsystem::Deinitialize()
{
	EntityManager->Deinitialize();
	EntityManager.Reset();
	Super::Deinitialize();
}

#if WITH_MASSENTITY_DEBUG
//////////////////////////////////////////////////////////////////////
// Debug commands

FAutoConsoleCommandWithWorldArgsAndOutputDevice GPrintArchetypesCmd(
	TEXT("EntityManager.PrintArchetypes"),
	TEXT("Prints information about all archetypes in the current world"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
		{
			if (const UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr)
			{
				EntitySubsystem->GetEntityManager().DebugPrintArchetypes(Ar);
			}
			else
			{
				Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find Entity Subsystem for world %s"), *GetPathNameSafe(World));
			}
		}));
#endif // WITH_MASSENTITY_DEBUG