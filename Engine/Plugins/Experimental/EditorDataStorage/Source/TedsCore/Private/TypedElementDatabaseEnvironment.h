// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Mutex.h"
#include "DynamicColumnGenerator.h"
#include "MassEntityManager.h"
#include "MassProcessingPhaseManager.h"
#include "TypedElementDatabaseCommandBuffer.h"
#include "TypedElementDatabaseScratchBuffer.h"
#include "TypedElementDatabaseIndexTable.h"
#include "Memento/TypedElementMementoSystem.h"
#include "Queries/TypedElementExtendedQueryStore.h"

class UEditorDataStorage;

namespace UE::Editor::DataStorage
{
	class FEnvironment final
	{
	public:
		FEnvironment(UEditorDataStorage& InDataStorage,
			FMassEntityManager& InMassEntityManager, FMassProcessingPhaseManager& InMassPhaseManager);

		Legacy::FCommandBuffer& GetDirectDeferredCommands();
		const Legacy::FCommandBuffer& GetDirectDeferredCommands() const;

		FIndexTable& GetIndexTable();
		const FIndexTable& GetIndexTable() const;

		FScratchBuffer& GetScratchBuffer();
		const FScratchBuffer& GetScratchBuffer() const;

		FExtendedQueryStore& GetQueryStore();
		const FExtendedQueryStore& GetQueryStore() const;

		FMementoSystem& GetMementoSystem();
		const FMementoSystem& GetMementoSystem() const;

		FMassEntityManager& GetMassEntityManager();
		const FMassEntityManager& GetMassEntityManager() const;

		FMassArchetypeHandle LookupMassArchetype(TableHandle Table) const;

		FMassProcessingPhaseManager& GetMassPhaseManager();
		const FMassProcessingPhaseManager& GetMassPhaseManager() const;

		// Finds the type information for the dynamic column
		// Dynamic columns are specified by a template layout and a FName Identifier
		const UScriptStruct* FindDynamicColumn(const UScriptStruct& Template, const FName& Identifier);
		// Generates or returns an existing type for the dynamic column
		// Dynamic columns are specified by a template layout and a FName Identifier
		const UScriptStruct* GenerateDynamicColumn(const UScriptStruct& Template, const FName& Identifier);
		
		// Creates or Finds the column type associated with the value tag
		const UScriptStruct* GenerateColumnType(const FValueTag& Tag);
		
		// Creates an instance of a value tag
		FConstSharedStruct GenerateValueTag(const FValueTag& Tag, const FName& Value);

		void NextUpdateCycle();
		uint64 GetUpdateCycleId() const;

		struct FEnvironmentCommand
		{
			void (*CommandFunction)(void*);
			// If this is not static data or null, it should be a pointer into the scratch buffer
			void* CommandData = nullptr;
		};

		// Commands are flushed on NextUpdateCycle
		void PushCommands(TConstArrayView<const FEnvironmentCommand> Commands);

	private:
		void FlushCommands();
		
		UEditorDataStorage& DataStorage;
		Legacy::FCommandBuffer DirectDeferredCommands;
		FIndexTable IndexTable;
		FScratchBuffer ScratchBuffer;
		FExtendedQueryStore Queries;
		FMementoSystem MementoSystem;
		FDynamicColumnGenerator DynamicColumnGenerator;
		FValueTagManager ValueTagManager;

		FMutex CommandQueueMutex;
		TArray<FEnvironmentCommand> CommandQueue;

		FMassEntityManager& MassEntityManager;
		FMassProcessingPhaseManager& MassPhaseManager;

		uint64 UpdateCycleId = 0;
	};
} // namespace UE::Editor::DataStorage
