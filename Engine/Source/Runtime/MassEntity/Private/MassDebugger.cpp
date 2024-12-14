// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassDebugger.h"
#if WITH_MASSENTITY_DEBUG
#include "MassProcessor.h"
#include "MassEntityManager.h"
#include "MassEntityManagerStorage.h"
#include "MassEntitySubsystem.h"
#include "MassArchetypeTypes.h"
#include "MassArchetypeData.h"
#include "MassRequirements.h"
#include "MassEntityQuery.h"
#include "Misc/OutputDevice.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "MassEntityUtils.h"
#include "MassCommandBuffer.h"


namespace UE::Mass::Debug
{
	bool bAllowProceduralDebuggedEntitySelection = false;
	bool bAllowBreakOnDebuggedEntity = false;
	bool bTestSelectedEntityAgainstProcessorQueries = true;

	FAutoConsoleVariableRef CVars[] =
	{
		{ TEXT("mass.debug.AllowProceduralDebuggedEntitySelection"), bAllowProceduralDebuggedEntitySelection
			, TEXT("Guards whether MASS_SET_ENTITY_DEBUGGED calls take effect."), ECVF_Cheat}
		, {TEXT("mass.debug.AllowBreakOnDebuggedEntity"), bAllowBreakOnDebuggedEntity
			, TEXT("Guards whether MASS_BREAK_IF_ENTITY_DEBUGGED calls take effect."), ECVF_Cheat}
		, {	TEXT("mass.debug.TestSelectedEntityAgainstProcessorQueries"), bTestSelectedEntityAgainstProcessorQueries
			, TEXT("Enabling will result in testing all processors' queries against SelectedEntity (as indicated by")
			TEXT("mass.debug.DebugEntity or the gameplay debugger) and storing potential failure results to be viewed in MassDebugger")
			, ECVF_Cheat }
	};
	

	FString DebugGetFragmentAccessString(EMassFragmentAccess Access)
	{
		switch (Access)
		{
		case EMassFragmentAccess::None:	return TEXT("--");
		case EMassFragmentAccess::ReadOnly:	return TEXT("RO");
		case EMassFragmentAccess::ReadWrite:	return TEXT("RW");
		default:
			ensureMsgf(false, TEXT("Missing string conversion for EMassFragmentAccess=%d"), Access);
			break;
		}
		return TEXT("Missing string conversion");
	}

	void DebugOutputDescription(TConstArrayView<UMassProcessor*> Processors, FOutputDevice& Ar)
	{
		const bool bAutoLineEnd = Ar.GetAutoEmitLineTerminator();
		Ar.SetAutoEmitLineTerminator(false);
		for (const UMassProcessor* Proc : Processors)
		{
			if (Proc)
			{
				Proc->DebugOutputDescription(Ar);
				Ar.Logf(TEXT("\n"));
			}
			else
			{
				Ar.Logf(TEXT("NULL\n"));
			}
		}
		Ar.SetAutoEmitLineTerminator(bAutoLineEnd);
	}

	// First Id of a range of lightweight entity for which we want to activate debug information
	int32 DebugEntityBegin = INDEX_NONE;

	// Last Id of a range of lightweight entity for which we want to activate debug information
	int32 DebugEntityEnd = INDEX_NONE;

	void SetDebugEntityRange(const int32 InDebugEntityBegin, const int32 InDebugEntityEnd)
	{
		DebugEntityBegin = InDebugEntityBegin;
		DebugEntityEnd = InDebugEntityEnd;
	}

	static FAutoConsoleCommand SetDebugEntityRangeCommand(
		TEXT("mass.debug.SetDebugEntityRange"),
		TEXT("Range of lightweight entity IDs that we want to debug.")
		TEXT("Usage: \"mass.debug.SetDebugEntityRange <FirstEntity> <LastEntity>\""),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() != 2)
				{
					UE_LOG(LogConsoleResponse, Display, TEXT("Error: Expecting 2 parameters"));
					return;
				}

				int32 FirstID = INDEX_NONE;
				int32 LastID = INDEX_NONE;
				if (!LexTryParseString<int32>(FirstID, *Args[0]))
				{
					UE_LOG(LogConsoleResponse, Display, TEXT("Error: first parameter must be an integer"));
					return;
				}
			
				if (!LexTryParseString<int32>(LastID, *Args[1]))
				{
					UE_LOG(LogConsoleResponse, Display, TEXT("Error: second parameter must be an integer"));
					return;
				}

				SetDebugEntityRange(FirstID, LastID);
			}));

	static FAutoConsoleCommand ResetDebugEntity(
		TEXT("mass.debug.ResetDebugEntity"),
		TEXT("Disables lightweight entities debugging.")
		TEXT("Usage: \"mass.debug.ResetDebugEntity\""),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				SetDebugEntityRange(INDEX_NONE, INDEX_NONE);
			}));

	bool HasDebugEntities()
	{
		return DebugEntityBegin != INDEX_NONE && DebugEntityEnd != INDEX_NONE;
	}

	bool IsDebuggingSingleEntity()
	{
		return DebugEntityBegin != INDEX_NONE && DebugEntityBegin == DebugEntityEnd;
	}

	bool GetDebugEntitiesRange(int32& OutBegin, int32& OutEnd)
	{
		OutBegin = DebugEntityBegin;
		OutEnd = DebugEntityEnd;
		return DebugEntityBegin != INDEX_NONE && DebugEntityEnd != INDEX_NONE && DebugEntityBegin <= DebugEntityEnd;
	}
	
	bool IsDebuggingEntity(FMassEntityHandle Entity, FColor* OutEntityColor)
	{
		const int32 EntityIdx = Entity.Index;
		const bool bIsDebuggingEntity = (DebugEntityBegin != INDEX_NONE && DebugEntityEnd != INDEX_NONE && DebugEntityBegin <= EntityIdx && EntityIdx <= DebugEntityEnd);
	
		if (bIsDebuggingEntity && OutEntityColor != nullptr)
		{
			*OutEntityColor = GetEntityDebugColor(Entity);
		}

		return bIsDebuggingEntity;
	}

	FColor GetEntityDebugColor(FMassEntityHandle Entity)
	{
		const int32 EntityIdx = Entity.Index;
		return EntityIdx != INDEX_NONE ? GColorList.GetFColorByIndex(EntityIdx % GColorList.GetColorsNum()) : FColor::Black;
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice PrintEntityFragmentsCmd(
		TEXT("mass.PrintEntityFragments"),
		TEXT("Prints all fragment types and values (uproperties) for the specified Entity index"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
			[](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
			{
				check(World);
				if (UMassEntitySubsystem* EntityManager = World->GetSubsystem<UMassEntitySubsystem>())
				{
					int32 Index = INDEX_NONE;
					if (LexTryParseString<int32>(Index, *Params[0]))
					{
						FMassDebugger::OutputEntityDescription(Ar, EntityManager->GetEntityManager(), Index);
					}
					else
					{
						Ar.Logf(ELogVerbosity::Error, TEXT("Entity index parameter must be an integer"));
					}
				}
				else
				{
					Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find MassEntitySubsystem for world %s"), *GetPathNameSafe(World));
				}
			})
	);

	FAutoConsoleCommandWithWorldArgsAndOutputDevice LogArchetypesCmd(
		TEXT("mass.LogArchetypes"),
		TEXT("Dumps description of archetypes to log. Optional parameter controls whether to include or exclude non-occupied archetypes. Defaults to 'include'."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Params, UWorld*, FOutputDevice& Ar)
			{
				const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
				for (const FWorldContext& Context : WorldContexts)
				{
					UWorld* World = Context.World();
					if (World == nullptr || World->IsPreviewWorld())
					{
						continue;
					}

					Ar.Logf(ELogVerbosity::Log, TEXT("Dumping description of archetypes for world: %s (%s - %s)"),
						*GetPathNameSafe(World),
						LexToString(World->WorldType),
						*ToString(World->GetNetMode()));

					if (UMassEntitySubsystem* EntityManager = World->GetSubsystem<UMassEntitySubsystem>())
					{
						bool bIncludeEmpty = true;
						if (Params.Num())
						{
							LexTryParseString(bIncludeEmpty, *Params[0]);
						}
						Ar.Logf(ELogVerbosity::Log, TEXT("Include empty archetypes: %s"), bIncludeEmpty ? TEXT("TRUE") : TEXT("FALSE"));
						EntityManager->GetEntityManager().DebugGetArchetypesStringDetails(Ar, bIncludeEmpty);
					}
					else
					{
						Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find MassEntitySubsystem for world: %s (%s - %s)"),
							*GetPathNameSafe(World),
							LexToString(World->WorldType),
							*ToString(World->GetNetMode()));
					}
				}
			})
	);

	// @todo these console commands will be reparented to "massentities" domain once we rename and shuffle the modules around 
	FAutoConsoleCommandWithWorld RecacheQueries(
		TEXT("mass.RecacheQueries"),
		TEXT("Forces EntityQueries to recache their valid archetypes"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* InWorld)
			{
				check(InWorld);
				if (UMassEntitySubsystem* System = InWorld->GetSubsystem<UMassEntitySubsystem>())
				{
					System->GetMutableEntityManager().DebugForceArchetypeDataVersionBump();
				}
			}
	));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice LogFragmentSizes(
		TEXT("mass.LogFragmentSizes"),
		TEXT("Logs all the fragment types being used along with their sizes."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
			{
				for (const TWeakObjectPtr<const UScriptStruct>& WeakStruct : FMassFragmentBitSet::DebugGetAllStructTypes())
				{
					if (const UScriptStruct* StructType = WeakStruct.Get())
					{
						Ar.Logf(ELogVerbosity::Log, TEXT("%s, size: %d"), *StructType->GetName(), StructType->GetStructureSize());
					}
				}
			})
	);

	FAutoConsoleCommandWithWorldArgsAndOutputDevice LogMemoryUsage(
		TEXT("mass.LogMemoryUsage"),
		TEXT("Logs how much memory the mass entity system uses"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
			{
				check(World);
				if (UMassEntitySubsystem* System = World->GetSubsystem<UMassEntitySubsystem>())
				{
					FResourceSizeEx CumulativeResourceSize;
					System->GetResourceSizeEx(CumulativeResourceSize);
					Ar.Logf(ELogVerbosity::Log, TEXT("MassEntity system uses: %d bytes"), CumulativeResourceSize.GetDedicatedSystemMemoryBytes());
				}
			}));

	FAutoConsoleCommandWithOutputDevice LogFragments(
		TEXT("mass.LogKnownFragments"),
		TEXT("Logs all the known tags and fragments along with their \"index\" as stored via bitsets."),
		FConsoleCommandWithOutputDeviceDelegate::CreateStatic([](FOutputDevice& OutputDevice)
			{
				auto PrintKnownTypes = [&OutputDevice](TConstArrayView<TWeakObjectPtr<const UScriptStruct>> AllStructs) {
					int i = 0;
					for (TWeakObjectPtr<const UScriptStruct> Struct : AllStructs)
					{
						if (Struct.IsValid())
						{
							OutputDevice.Logf(TEXT("\t%d. %s"), i++, *Struct->GetName());
						}
					}
				};

				OutputDevice.Logf(TEXT("Known tags:"));
				PrintKnownTypes(FMassTagBitSet::DebugGetAllStructTypes());

				OutputDevice.Logf(TEXT("Known Fragments:"));
				PrintKnownTypes(FMassFragmentBitSet::DebugGetAllStructTypes());

				OutputDevice.Logf(TEXT("Known Shared Fragments:"));
				PrintKnownTypes(FMassSharedFragmentBitSet::DebugGetAllStructTypes());

				OutputDevice.Logf(TEXT("Known Chunk Fragments:"));
				PrintKnownTypes(FMassChunkFragmentBitSet::DebugGetAllStructTypes());
			}));

	static FAutoConsoleCommandWithWorldAndArgs DestroyEntity(
		TEXT("mass.debug.DestroyEntity"),
		TEXT("ID of a Mass entity that we want to destroy.")
		TEXT("Usage: \"mass.debug.DestoryEntity <Entity>\""),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("Error: Expecting 1 parameter"));
			return;
		}

		int32 ID = INDEX_NONE;
		if (!LexTryParseString<int32>(ID, *Args[0]))
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("Error: parameter must be an integer"));
			return;
		}

		if (!World)
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("Error: invalid world"));
			return;
		}

		FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(*World);
		FMassEntityHandle EntityToDestroy = EntityManager.DebugGetEntityIndexHandle(ID);
		if (!EntityToDestroy.IsSet())
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("Error: cannot find entity for this index"));
			return;
		}

		EntityManager.Defer().DestroyEntity(EntityToDestroy);
	}));

	static FAutoConsoleCommandWithWorldAndArgs SetDebugEntity(
		TEXT("mass.debug.DebugEntity"),
		TEXT("ID of a Mass entity that we want to debug.")
		TEXT("Usage: \"mass.debug.DebugEntity <Entity>\""),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() != 1)
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: Expecting 1 parameter"));
				return;
			}

			int32 ID = INDEX_NONE;
			if (!LexTryParseString<int32>(ID, *Args[0]))
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: parameter must be an integer"));
				return;
			}

			if (!World)
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: invalid world"));
				return;
			}

			SetDebugEntityRange(ID, ID);

			FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(*World);
			FMassEntityHandle EntityToDebug = EntityManager.DebugGetEntityIndexHandle(ID);
			if (!EntityToDebug.IsSet())
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: cannot find entity for this index"));
				return;
			}

			FMassDebugger::SelectEntity(EntityManager, EntityToDebug);
		}
	));

} // namespace UE::Mass::Debug

//----------------------------------------------------------------------//
// FMassDebugger
//----------------------------------------------------------------------//
FMassDebugger::FOnEntitySelected FMassDebugger::OnEntitySelectedDelegate;

FMassDebugger::FOnMassEntityManagerEvent FMassDebugger::OnEntityManagerInitialized;
FMassDebugger::FOnMassEntityManagerEvent FMassDebugger::OnEntityManagerDeinitialized;
FMassDebugger::FOnDebugEvent FMassDebugger::OnDebugEvent;
TArray<FMassDebugger::FEnvironment> FMassDebugger::ActiveEnvironments;
UE::FSpinLock FMassDebugger::EntityManagerRegistrationLock;

TConstArrayView<FMassEntityQuery*> FMassDebugger::GetProcessorQueries(const UMassProcessor& Processor)
{
	return Processor.OwnedQueries;
}

TConstArrayView<FMassEntityQuery*> FMassDebugger::GetUpToDateProcessorQueries(const FMassEntityManager& EntityManager, UMassProcessor& Processor)
{
	for (FMassEntityQuery* Query : Processor.OwnedQueries)
	{
		if (Query)
		{
			Query->CacheArchetypes(EntityManager);
		}
	}

	return Processor.OwnedQueries;
}

UE::Mass::Debug::FQueryRequirementsView FMassDebugger::GetQueryRequirements(const FMassEntityQuery& Query)
{
	UE::Mass::Debug::FQueryRequirementsView View = { Query.FragmentRequirements, Query.ChunkFragmentRequirements, Query.ConstSharedFragmentRequirements, Query.SharedFragmentRequirements
		, Query.RequiredAllTags, Query.RequiredAnyTags, Query.RequiredNoneTags, Query.RequiredOptionalTags
		, Query.RequiredConstSubsystems, Query.RequiredMutableSubsystems };

	return View;
}

void FMassDebugger::GetQueryExecutionRequirements(const FMassEntityQuery& Query, FMassExecutionRequirements& OutExecutionRequirements)
{
	Query.ExportRequirements(OutExecutionRequirements);
}

TArray<FMassArchetypeHandle> FMassDebugger::GetAllArchetypes(const FMassEntityManager& EntityManager)
{
	TArray<FMassArchetypeHandle> Archetypes;

	for (auto& KVP : EntityManager.FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& Archetype : KVP.Value)
		{
			Archetypes.Add(FMassArchetypeHelper::ArchetypeHandleFromData(Archetype));
		}
	}

	return Archetypes;
}

const FMassArchetypeCompositionDescriptor& FMassDebugger::GetArchetypeComposition(const FMassArchetypeHandle& ArchetypeHandle)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	return ArchetypeData.CompositionDescriptor;
}

void FMassDebugger::GetArchetypeEntityStats(const FMassArchetypeHandle& ArchetypeHandle, UE::Mass::Debug::FArchetypeStats& OutStats)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	OutStats.EntitiesCount = ArchetypeData.GetNumEntities();
	OutStats.EntitiesCountPerChunk = ArchetypeData.GetNumEntitiesPerChunk();
	OutStats.ChunksCount = ArchetypeData.GetChunkCount();
	OutStats.AllocatedSize = ArchetypeData.GetAllocatedSize();
	OutStats.BytesPerEntity = ArchetypeData.GetBytesPerEntity();

	SIZE_T ActiveChunksMemorySize = 0;
	SIZE_T ActiveEntitiesMemorySize = 0;
	ArchetypeData.DebugGetEntityMemoryNumbers(ActiveChunksMemorySize, ActiveEntitiesMemorySize);
	OutStats.WastedEntityMemory = ActiveChunksMemorySize - ActiveEntitiesMemorySize;
}

const TConstArrayView<FName> FMassDebugger::GetArchetypeDebugNames(const FMassArchetypeHandle& ArchetypeHandle)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	return ArchetypeData.GetDebugNames();
}

TConstArrayView<struct UMassCompositeProcessor::FDependencyNode> FMassDebugger::GetProcessingGraph(const UMassCompositeProcessor& GraphOwner)
{
	return GraphOwner.FlatProcessingGraph;
}

TConstArrayView<TObjectPtr<UMassProcessor>> FMassDebugger::GetHostedProcessors(const UMassCompositeProcessor& GraphOwner)
{
	return GraphOwner.ChildPipeline.GetProcessors();
}

FString FMassDebugger::GetRequirementsDescription(const FMassFragmentRequirements& Requirements)
{
	TStringBuilder<256> StringBuilder;
	StringBuilder.Append(TEXT("<"));

	bool bNeedsComma = false;
	for (const FMassFragmentRequirementDescription& Requirement : Requirements.FragmentRequirements)
	{
		if (bNeedsComma)
		{
			StringBuilder.Append(TEXT(","));
		}
		StringBuilder.Append(*FMassDebugger::GetSingleRequirementDescription(Requirement));
		bNeedsComma = true;
	}

	StringBuilder.Append(TEXT(">"));
	return StringBuilder.ToString();
}

FString FMassDebugger::GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements& Requirements, const FMassArchetypeHandle& ArchetypeHandle)
{
	if (ArchetypeHandle.IsValid() == false)
	{
		return TEXT("Invalid");
	}

	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	return FMassDebugger::GetArchetypeRequirementCompatibilityDescription(Requirements, ArchetypeData.GetCompositionDescriptor());
}
	
FString FMassDebugger::GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements& Requirements, const FMassArchetypeCompositionDescriptor& ArchetypeComposition)
{
	FStringOutputDevice OutDescription;

	if (Requirements.HasNegativeRequirements())
	{
		if (ArchetypeComposition.Fragments.HasNone(Requirements.RequiredNoneFragments) == false)
		{
			// has some of the fragments required absent
			OutDescription += TEXT("\nHas fragments required absent: ");
			(Requirements.RequiredNoneFragments & ArchetypeComposition.Fragments).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.Tags.HasNone(Requirements.RequiredNoneTags) == false)
		{
			// has some of the tags required absent
			OutDescription += TEXT("\nHas tags required absent: ");
			(Requirements.RequiredNoneTags & ArchetypeComposition.Tags).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.ChunkFragments.HasNone(Requirements.RequiredNoneChunkFragments) == false)
		{
			// has some of the chunk fragments required absent
			OutDescription += TEXT("\nHas chunk fragments required absent: ");
			(Requirements.RequiredNoneChunkFragments & ArchetypeComposition.ChunkFragments).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.SharedFragments.HasNone(Requirements.RequiredNoneSharedFragments) == false)
		{
			// has some of the chunk fragments required absent
			OutDescription += TEXT("\nHas shared fragments required absent: ");
			(Requirements.RequiredNoneSharedFragments & ArchetypeComposition.SharedFragments).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.ConstSharedFragments.HasNone(Requirements.RequiredNoneConstSharedFragments) == false)
		{
			// has some of the chunk fragments required absent
			OutDescription += TEXT("\nHas shared fragments required absent: ");
			(Requirements.RequiredNoneConstSharedFragments & ArchetypeComposition.ConstSharedFragments).DebugGetStringDesc(OutDescription);
		}
	}

	// if we have regular (i.e. non-optional) positive requirements then these are the determining factor, we don't check optionals
	if (Requirements.HasPositiveRequirements())
	{
		if (ArchetypeComposition.Fragments.HasAll(Requirements.RequiredAllFragments) == false)
		{
			// missing one of the strictly required fragments
			OutDescription += TEXT("\nMissing required fragments: ");
			(Requirements.RequiredAllFragments - ArchetypeComposition.Fragments).DebugGetStringDesc(OutDescription);
		}

		if (Requirements.RequiredAnyFragments.IsEmpty() == false && ArchetypeComposition.Fragments.HasAny(Requirements.RequiredAnyFragments) == false)
		{
			// missing all of the "any" fragments
			OutDescription += TEXT("\nMissing all \'any\' fragments: ");
			Requirements.RequiredAnyFragments.DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.Tags.HasAll(Requirements.RequiredAllTags) == false)
		{
			// missing one of the strictly required tags
			OutDescription += TEXT("\nMissing required tags: ");
			(Requirements.RequiredAllTags - ArchetypeComposition.Tags).DebugGetStringDesc(OutDescription);
		}

		if (Requirements.RequiredAnyTags.IsEmpty() == false && ArchetypeComposition.Tags.HasAny(Requirements.RequiredAnyTags) == false)
		{
			// missing all of the "any" tags
			OutDescription += TEXT("\nMissing all \'any\' tags: ");
			Requirements.RequiredAnyTags.DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.ChunkFragments.HasAll(Requirements.RequiredAllChunkFragments) == false)
		{
			// missing one of the strictly required chunk fragments
			OutDescription += TEXT("\nMissing required chunk fragments: ");
			(Requirements.RequiredAllChunkFragments - ArchetypeComposition.ChunkFragments).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.SharedFragments.HasAll(Requirements.RequiredAllSharedFragments) == false)
		{
			// missing one of the strictly required Shared fragments
			OutDescription += TEXT("\nMissing required Shared fragments: ");
			(Requirements.RequiredAllSharedFragments - ArchetypeComposition.SharedFragments).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.ConstSharedFragments.HasAll(Requirements.RequiredAllConstSharedFragments) == false)
		{
			// missing one of the strictly required Shared fragments
			OutDescription += TEXT("\nMissing required Shared fragments: ");
			(Requirements.RequiredAllConstSharedFragments - ArchetypeComposition.ConstSharedFragments).DebugGetStringDesc(OutDescription);
		}
	}
	// else we check if there are any optionals and if so test them
	else if (Requirements.HasOptionalRequirements() && (Requirements.DoesMatchAnyOptionals(ArchetypeComposition) == false))
	{
		// we report that none of the optionals has been met
		OutDescription += TEXT("\nNone of the optionals were safisfied while not having other positive hard requirements: ");

		Requirements.RequiredOptionalTags.DebugGetStringDesc(OutDescription);
		Requirements.RequiredOptionalFragments.DebugGetStringDesc(OutDescription);
		Requirements.RequiredOptionalChunkFragments.DebugGetStringDesc(OutDescription);
		Requirements.RequiredOptionalSharedFragments.DebugGetStringDesc(OutDescription);
		Requirements.RequiredOptionalConstSharedFragments.DebugGetStringDesc(OutDescription);
	}

	return OutDescription.Len() > 0 ? static_cast<FString>(OutDescription) : TEXT("Match");
}

FString FMassDebugger::GetSingleRequirementDescription(const FMassFragmentRequirementDescription& Requirement)
{
	return FString::Printf(TEXT("%s%s[%s]"), Requirement.IsOptional() ? TEXT("?") : (Requirement.Presence == EMassFragmentPresence::None ? TEXT("-") : TEXT("+"))
		, *GetNameSafe(Requirement.StructType), *UE::Mass::Debug::DebugGetFragmentAccessString(Requirement.AccessMode));
}

void FMassDebugger::OutputArchetypeDescription(FOutputDevice& Ar, const FMassArchetypeHandle& ArchetypeHandle)
{
	Ar.Logf(TEXT("%s"), ArchetypeHandle.IsValid() ? *FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle).DebugGetDescription() : TEXT("INVALID"));
}

void FMassDebugger::OutputEntityDescription(FOutputDevice& Ar, const FMassEntityManager& EntityManager, const int32 EntityIndex, const TCHAR* InPrefix)
{
	if (EntityIndex >= EntityManager.DebugGetEntityStorageInterface().Num())
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for out of range index in EntityManager owned by %s"), *GetPathNameSafe(EntityManager.GetOwner()));
		return;
	}
	
	if (!EntityManager.DebugGetEntityStorageInterface().IsValid(EntityIndex))
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in EntityManager owned by %s"), *GetPathNameSafe(EntityManager.GetOwner()));
	}
	
	FMassEntityHandle Entity;
	Entity.Index = EntityIndex;
	Entity.SerialNumber = EntityManager.DebugGetEntityStorageInterface().GetSerialNumber(EntityIndex);
	OutputEntityDescription(Ar, EntityManager, Entity, InPrefix);
}

void FMassDebugger::OutputEntityDescription(FOutputDevice& Ar, const FMassEntityManager& EntityManager, const FMassEntityHandle Entity, const TCHAR* InPrefix)
{
	if (!EntityManager.IsEntityActive(Entity))
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in EntityManager owned by %s"), *GetPathNameSafe(EntityManager.GetOwner()));
	}

	Ar.Logf(ELogVerbosity::Log, TEXT("Listing fragments values for Entity[%s] in EntityManager owned by %s"), *Entity.DebugGetDescription(), *GetPathNameSafe(EntityManager.GetOwner()));

	FMassArchetypeData* Archetype = EntityManager.DebugGetEntityStorageInterface().GetArchetypeAsShared(Entity.Index).Get();
	if (Archetype == nullptr)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in EntityManager owned by %s"), *GetPathNameSafe(EntityManager.GetOwner()));
	}
	else
	{
		Archetype->DebugPrintEntity(Entity, Ar, InPrefix);
	}
}

void FMassDebugger::SelectEntity(const FMassEntityManager& EntityManager, const FMassEntityHandle EntityHandle)
{
	UE::Mass::Debug::SetDebugEntityRange(EntityHandle.Index, EntityHandle.Index);

	const int32 Index = ActiveEnvironments.IndexOfByPredicate([WeakManager = EntityManager.AsWeak()](const FEnvironment& Element)
		{
			return Element.EntityManager == WeakManager;
		});
	if (ensure(Index != INDEX_NONE))
	{
		ActiveEnvironments[Index].SelectedEntity = EntityHandle;
	}

	OnEntitySelectedDelegate.Broadcast(EntityManager, EntityHandle);
}

FMassEntityHandle FMassDebugger::GetSelectedEntity(const FMassEntityManager& EntityManager)
{
	const int32 Index = ActiveEnvironments.IndexOfByPredicate([WeakManager = EntityManager.AsWeak()](const FEnvironment& Element)
		{
			return Element.EntityManager == WeakManager;
		});

	return Index != INDEX_NONE ? ActiveEnvironments[Index].SelectedEntity : FMassEntityHandle();
}

void FMassDebugger::RegisterEntityManager(FMassEntityManager& EntityManager)
{
	UE::TScopeLock<UE::FSpinLock> ScopeLock(EntityManagerRegistrationLock);

	ActiveEnvironments.Emplace(EntityManager);
	OnEntityManagerInitialized.Broadcast(EntityManager);
}

void FMassDebugger::UnregisterEntityManager(FMassEntityManager& EntityManager)
{
	UE::TScopeLock<UE::FSpinLock> ScopeLock(EntityManagerRegistrationLock);

	if (EntityManager.DoesSharedInstanceExist())
	{
		const int32 Index = ActiveEnvironments.IndexOfByPredicate([WeakManager = EntityManager.AsWeak()](const FEnvironment& Element) 
		{
			return Element.EntityManager == WeakManager;
		});
		if (Index != INDEX_NONE)
		{
			ActiveEnvironments.RemoveAt(Index, EAllowShrinking::No);
		}
	}
	else
	{
		ActiveEnvironments.RemoveAll([](const FEnvironment& Item)
			{
				return Item.IsValid() == false;
			});
	}
	OnEntityManagerDeinitialized.Broadcast(EntityManager);
}

bool FMassDebugger::DoesArchetypeMatchRequirements(const FMassArchetypeHandle& ArchetypeHandle, const FMassFragmentRequirements& Requirements, FOutputDevice& OutputDevice)
{
	if (const FMassArchetypeData* Archetype = FMassArchetypeHelper::ArchetypeDataFromHandle(ArchetypeHandle))
	{
		return FMassArchetypeHelper::DoesArchetypeMatchRequirements(*Archetype, Requirements, /*bBailOutOnFirstFail=*/false, &OutputDevice);
	}
	return false;
}

#endif // WITH_MASSENTITY_DEBUG