// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementDatabaseCommandBuffer.h"

#include "HAL/UnrealMemory.h"
#include "MassEntityManager.h"
#include "TypedElementDatabaseEnvironment.h"
#include "TypedElementDataStorageSharedColumn.h"

namespace UE::Editor::DataStorage::Legacy
{
	// 
	// Commands section
	//

	FCommandBuffer::FCommandBuffer(FEnvironment& InEnvironment)
		: Environment(InEnvironment)
	{
	}

	void* FCommandBuffer::GetQueuedDataColumn(
		RowHandle Row, const UScriptStruct* ColumnType)
	{
		checkf(ColumnType->IsChildOf(FMassFragment::StaticStruct()),
			TEXT("Trying to get the column '%s' which isn't a data column."), *ColumnType->GetName());

		void* const* StoredData = PendingColumns.Find(PendingColumnMappingKey(Row, ColumnType));
		if (StoredData)
		{
			if (*StoredData != nullptr)
			{
				return *StoredData;
			}
			else
			{
				// If the column was created but had no data assigned to it, create data for it now. If this code path triggers
				// a lot there may be a large number of AddColumn followed by GetColumn calls. These can be more efficiently done
				// with an AddorGetColumn call.
				void* Result = Queue_AddDataColumnCommandUnitialized(Row, ColumnType,
					[](const UScriptStruct& ColumnType, void* Destination, void* Source)
					{
						ColumnType.CopyScriptStruct(Destination, Source);
					});
				return Result;
			}
		}
		else
		{
			return nullptr;
		}
	}

	bool FCommandBuffer::HasColumn(RowHandle Row, const UScriptStruct* ColumnType) const
	{
		return PendingColumns.Contains(PendingColumnMappingKey(Row, ColumnType));
	}

	void FCommandBuffer::Clear(RowHandle Row)
	{
		for (TMap<PendingColumnMappingKey, void*>::TIterator It = PendingColumns.CreateIterator(); It; ++It)
		{
			if (It.Key().Key == Row)
			{
				It.RemoveCurrent();
			}
		}

		for (TArray<FCommand>::TIterator It = Commands.CreateIterator(); It; ++It)
		{
			if (It->Row == Row)
			{
				It.RemoveCurrent(); // Don't swap as the order of commands is important.
			}
		}
	}

	FCommandBuffer::FAddDataColumnCommand::~FAddDataColumnCommand()
	{
		if (ColumnType.IsValid() && (ColumnType->StructFlags & (STRUCT_IsPlainOldData | STRUCT_NoDestructor)) == 0)
		{
			ColumnType->DestroyStruct(Data);
		}
	}

	//
	// Queue section
	//

	void FCommandBuffer::Queue_AddColumnCommand(RowHandle Row, const UScriptStruct* ColumnType)
	{
		AddCommand(Row, FAddColumnCommand
			{
				.ColumnType = ColumnType
			});
		PendingColumns.FindOrAdd(PendingColumnMappingKey(Row, ColumnType), nullptr);
	}

	void* FCommandBuffer::Queue_AddDataColumnCommandUnitialized(
		RowHandle Row, const UScriptStruct* ColumnType, ColumnCopyOrMoveCallback Relocator)
	{
		checkf(ColumnType->IsChildOf(FMassFragment::StaticStruct()),
			TEXT("Trying to queue a data column creation for '%s' which isn't a data column."), *ColumnType->GetName());

		PendingColumnMappingKey Key(Row, ColumnType);
		if (void** StoredData = PendingColumns.Find(Key); StoredData == nullptr || *StoredData == nullptr)
		{
			void* Data = Environment.GetScratchBuffer().Allocate(ColumnType->GetStructureSize(), ColumnType->GetMinAlignment());
			// Initialize to zero to replicate the default from Mass.
			FMemory::Memzero(Data, ColumnType->GetStructureSize());
			PendingColumns.Add(PendingColumnMappingKey(Row, ColumnType), Data);
			AddCommand(Row, FAddDataColumnCommand
				{
					.ColumnType = ColumnType,
					.Relocator = Relocator,
					.Data = Data
				});
			return Data;
		}
		else
		{
			return *StoredData;
		}
	}

	void FCommandBuffer::Queue_AddColumnsCommand(RowHandle Row,
		FMassFragmentBitSet FragmentsToAdd, FMassTagBitSet TagsToAdd)
	{
		auto AddColumn = [this, Row](const UScriptStruct* ColumnType)
		{
			PendingColumns.FindOrAdd(PendingColumnMappingKey(Row, ColumnType), nullptr);
			return true;
		};
		FragmentsToAdd.ExportTypes(AddColumn);
		TagsToAdd.ExportTypes(AddColumn);

		AddCommand(Row, FAddColumnsCommand
			{
				.FragmentsToAdd = MoveTemp(FragmentsToAdd),
				.TagsToAdd = MoveTemp(TagsToAdd)
			});
	}

	void FCommandBuffer::Queue_RemoveColumnCommand(RowHandle Row, const UScriptStruct* ColumnType)
	{
		AddCommand(Row, FRemoveColumnCommand
			{
				.ColumnType = ColumnType
			});
		PendingColumns.Remove(PendingColumnMappingKey(Row, ColumnType));
	}

	void FCommandBuffer::Queue_RemoveColumnsCommand(RowHandle Row,
		FMassFragmentBitSet FragmentsToRemove, FMassTagBitSet TagsToRemove)
	{
		auto RemoveColumn = [this, Row](const UScriptStruct* ColumnType)
		{
			PendingColumns.Remove(PendingColumnMappingKey(Row, ColumnType));
			return true;
		};
		FragmentsToRemove.ExportTypes(RemoveColumn);
		TagsToRemove.ExportTypes(RemoveColumn);

		AddCommand(Row, FRemoveColumnsCommand
			{
				.FragmentsToRemove = MoveTemp(FragmentsToRemove),
				.TagsToRemove = MoveTemp(TagsToRemove)
			});
	}

	//
	// Execute section
	//

	bool FCommandBuffer::Execute_IsRowAvailable(const FMassEntityManager& MassEntityManager, RowHandle Row)
	{
		if (Row == InvalidRowHandle)
		{
			return false;
		}
		return MassEntityManager.IsEntityValid(FMassEntityHandle::FromNumber(Row));
	}

	bool FCommandBuffer::Execute_IsRowAssigned(const FMassEntityManager& MassEntityManager, RowHandle Row)
	{
		if (Row == InvalidRowHandle)
		{
			return false;
		}
		return MassEntityManager.IsEntityActive(FMassEntityHandle::FromNumber(Row));
	}

	void FCommandBuffer::Execute_AddColumnCommand(
		FMassEntityManager& MassEntityManager,
		RowHandle Row,
		const UScriptStruct* ColumnType)
	{
		if (ColumnType)
		{
			FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
			if (ColumnType->IsChildOf(FMassTag::StaticStruct()))
			{
				MassEntityManager.AddTagToEntity(Entity, ColumnType);
			}
			else if (ColumnType->IsChildOf(FMassFragment::StaticStruct()))
			{
				FStructView Column = MassEntityManager.GetFragmentDataStruct(Entity, ColumnType);
				// Only add if not already added to avoid asserts from Mass.
				if (!Column.IsValid())
				{
					MassEntityManager.AddFragmentToEntity(Entity, ColumnType);
				}
			}
		}
	}

	void FCommandBuffer::Execute_AddDataColumnCommand(
		FMassEntityManager& MassEntityManager, RowHandle Row, const UScriptStruct* ColumnType,
		void* Data, ColumnCopyOrMoveCallback Relocator)
	{
		if (ColumnType)
		{
			checkf(ColumnType->IsChildOf(FMassFragment::StaticStruct()),
				TEXT("Trying to create a data column for '%s' from a deferred command that isn't a data column."), *ColumnType->GetName());

			FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
			FStructView Column = MassEntityManager.GetFragmentDataStruct(Entity, ColumnType);
			// Only add if not already added to avoid asserts from Mass.
			if (!Column.IsValid())
			{
				MassEntityManager.AddFragmentToEntity(Entity, ColumnType,
					[Data, Relocator](void* Fragment, const UScriptStruct& FragmentType)
					{
						Relocator(FragmentType, Fragment, Data);
					});
			}
			else
			{
				Relocator(*ColumnType, Column.GetMemory(), Data);
			}
		}
	}

	void FCommandBuffer::Execute_AddSharedColumnCommand(FMassEntityManager& MassEntityManager, RowHandle Row, const FConstSharedStruct& SharedColumn)
	{
		if (SharedColumn.IsValid())
		{
			FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
			MassEntityManager.AddConstSharedFragmentToEntity(Entity, SharedColumn);
		}
	}

	void FCommandBuffer::Execute_RemoveSharedColumnCommand(FMassEntityManager& MassEntityManager, RowHandle Row, const UScriptStruct& ColumnType)
	{
		if (ColumnType.IsChildOf(FTedsSharedColumn::StaticStruct()))
		{
			FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
			MassEntityManager.RemoveConstSharedFragmentFromEntity(Entity, ColumnType);
		}
	}

	void FCommandBuffer::Execute_AddColumnsCommand(
		FMassEntityManager& MassEntityManager,
		RowHandle Row,
		FMassFragmentBitSet FragmentsToAdd,
		FMassTagBitSet TagsToAdd)
	{
		FMassArchetypeCompositionDescriptor AddComposition(
			MoveTemp(FragmentsToAdd), MoveTemp(TagsToAdd), FMassChunkFragmentBitSet(), FMassSharedFragmentBitSet(), FMassConstSharedFragmentBitSet());
		MassEntityManager.AddCompositionToEntity_GetDelta(FMassEntityHandle::FromNumber(Row), AddComposition);
	}

	void FCommandBuffer::Execute_RemoveColumnCommand(
		FMassEntityManager& MassEntityManager,
		RowHandle Row,
		const UScriptStruct* ColumnType)
	{
		if (ColumnType)
		{
			FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
			if (ColumnType->IsChildOf(FMassTag::StaticStruct()))
			{
				MassEntityManager.RemoveTagFromEntity(Entity, ColumnType);
			}
			else if (ColumnType->IsChildOf(FMassFragment::StaticStruct()))
			{
				MassEntityManager.RemoveFragmentFromEntity(Entity, ColumnType);
			}
		}
	}

	void FCommandBuffer::Execute_RemoveColumnsCommand(
		FMassEntityManager& MassEntityManager,
		RowHandle Row,
		FMassFragmentBitSet FragmentsToRemove,
		FMassTagBitSet TagsToRemove)
	{
		FMassArchetypeCompositionDescriptor RemoveComposition(
			MoveTemp(FragmentsToRemove), MoveTemp(TagsToRemove), FMassChunkFragmentBitSet(), FMassSharedFragmentBitSet(), FMassConstSharedFragmentBitSet());
		MassEntityManager.RemoveCompositionFromEntity(FMassEntityHandle::FromNumber(Row), RemoveComposition);
	}

	void FCommandBuffer::ProcessCommands()
	{
		Commands.StableSort(
			[](const FCommand& Lhs, const FCommand& Rhs)
			{
				return Lhs.Row < Rhs.Row;
			});

		struct FProcessor
		{
			FMassEntityManager& EntityManager;
			RowHandle Row;
			void operator()(const FAddColumnCommand& Command) { Execute_AddColumnCommand(EntityManager, Row, Command.ColumnType.Get()); }
			void operator()(const FAddDataColumnCommand& Command) { Execute_AddDataColumnCommand(EntityManager, Row, Command.ColumnType.Get(), Command.Data, Command.Relocator); }
			void operator()(const FAddColumnsCommand& Command) { Execute_AddColumnsCommand(EntityManager, Row, Command.FragmentsToAdd, Command.TagsToAdd); }
			void operator()(const FRemoveColumnCommand& Command) { Execute_RemoveColumnCommand(EntityManager, Row, Command.ColumnType.Get()); }
			void operator()(const FRemoveColumnsCommand& Command) { Execute_RemoveColumnsCommand(EntityManager, Row, Command.FragmentsToRemove, Command.TagsToRemove); }
		};

		FProcessor Processor{ .EntityManager = Environment.GetMassEntityManager() };
		for (FCommand& Command : Commands)
		{
			if (Execute_IsRowAssigned(Environment.GetMassEntityManager(), Command.Row))
			{
				Processor.Row = Command.Row;
				Visit(Processor, Command.Data);
			}
		}

		PendingColumns.Reset();
		Commands.Reset();
	}

	// 
	// misc
	//

	template<typename T>
	void FCommandBuffer::AddCommand(RowHandle Row, T&& Args)
	{
		FCommand Command;
		Command.Row = Row;
		Command.Data.Emplace<T>(Forward<T>(Args));
		Commands.Add(MoveTemp(Command));
	}
} // namespace UE::Editor::DataStorage::Legacy
