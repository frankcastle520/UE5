// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementDatabaseEnvironment.h"
#include "TypedElementDatabase.h"

namespace UE::Editor::DataStorage
{
	FEnvironment::FEnvironment(UEditorDataStorage& InDataStorage,
		FMassEntityManager& InMassEntityManager, FMassProcessingPhaseManager& InMassPhaseManager)
		: DataStorage(InDataStorage)
		, DirectDeferredCommands(*this)
		, MementoSystem(InDataStorage)
		, ValueTagManager(DynamicColumnGenerator)
		, MassEntityManager(InMassEntityManager)
		, MassPhaseManager(InMassPhaseManager)
	{
	}

	Legacy::FCommandBuffer& FEnvironment::GetDirectDeferredCommands()
	{
		return DirectDeferredCommands;
	}

	const Legacy::FCommandBuffer& FEnvironment::GetDirectDeferredCommands() const
	{
		return DirectDeferredCommands;
	}

	FIndexTable& FEnvironment::GetIndexTable()
	{
		return IndexTable;
	}

	const FIndexTable& FEnvironment::GetIndexTable() const
	{
		return IndexTable;
	}

	FScratchBuffer& FEnvironment::GetScratchBuffer()
	{
		return ScratchBuffer;
	}

	const FScratchBuffer& FEnvironment::GetScratchBuffer() const
	{
		return ScratchBuffer;
	}

	FExtendedQueryStore& FEnvironment::GetQueryStore()
	{
		return Queries;
	}

	const FExtendedQueryStore& FEnvironment::GetQueryStore() const
	{
		return Queries;
	}

	FMementoSystem& FEnvironment::GetMementoSystem()
	{
		return MementoSystem;
	}

	const FMementoSystem& FEnvironment::GetMementoSystem() const
	{
		return MementoSystem;
	}

	FMassEntityManager& FEnvironment::GetMassEntityManager()
	{
		return MassEntityManager;
	}

	const FMassEntityManager& FEnvironment::GetMassEntityManager() const
	{
		return MassEntityManager;
	}

	FMassArchetypeHandle FEnvironment::LookupMassArchetype(TableHandle Table) const
	{
		return DataStorage.LookupArchetype(Table);
	}

	FMassProcessingPhaseManager& FEnvironment::GetMassPhaseManager()
	{
		return MassPhaseManager;
	}

	const FMassProcessingPhaseManager& FEnvironment::GetMassPhaseManager() const
	{
		return MassPhaseManager;
	}

	const UScriptStruct* FEnvironment::FindDynamicColumn(const UScriptStruct& Template, const FName& Identifier)
	{
		return DynamicColumnGenerator.FindColumn(Template, Identifier);
	}

	const UScriptStruct* FEnvironment::GenerateDynamicColumn(const UScriptStruct& Template, const FName& Identifier)
	{
		return DynamicColumnGenerator.GenerateColumn(Template, Identifier).Type;
	}

	FConstSharedStruct FEnvironment::GenerateValueTag(const FValueTag& Tag, const FName& Value)
	{
		return ValueTagManager.GenerateValueTag(Tag, Value);
	}

	const UScriptStruct* FEnvironment::GenerateColumnType(const FValueTag& Tag)
	{
		return ValueTagManager.GenerateColumnType(Tag);
	}

	void FEnvironment::NextUpdateCycle()
	{
		Queries.UpdateActivatableQueries();
		FlushCommands();
		ScratchBuffer.BatchDelete();
		UpdateCycleId++;
	}

	uint64 FEnvironment::GetUpdateCycleId() const
	{
		return UpdateCycleId;
	}

	void FEnvironment::PushCommands(TConstArrayView<const FEnvironmentCommand> Commands)
	{
		TUniqueLock<FMutex> Lock(CommandQueueMutex);

		CommandQueue.Append(Commands);
	}

	void FEnvironment::FlushCommands()
	{
		for (FEnvironmentCommand& Command : CommandQueue)
		{
			Command.CommandFunction(Command.CommandData);
		}
		CommandQueue.SetNum(0, EAllowShrinking::No);
	}
} // namespace UE::Editor::DataStorage
