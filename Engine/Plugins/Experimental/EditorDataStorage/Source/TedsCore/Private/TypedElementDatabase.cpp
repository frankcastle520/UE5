// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementDatabase.h"

#include "Editor.h"
#include "EditorDataStorageSettings.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Engine/World.h"
#include "GlobalLock.h"
#include "MassEntityEditorSubsystem.h"
#include "MassEntityTypes.h"
#include "MassProcessor.h"
#include "MassSubsystemAccess.h"
#include "Processors/TypedElementProcessorAdaptors.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Stats/Stats2.h"
#include "TickTaskManagerInterface.h"
#include "TypedElementDatabaseEnvironment.h"
#include "UObject/UObjectIterator.h"

const FName UEditorDataStorage::TickGroupName_Default(TEXT("Default"));
const FName UEditorDataStorage::TickGroupName_PreUpdate(TEXT("PreUpdate"));
const FName UEditorDataStorage::TickGroupName_Update(TEXT("Update"));
const FName UEditorDataStorage::TickGroupName_PostUpdate(TEXT("PostUpdate"));
const FName UEditorDataStorage::TickGroupName_SyncWidget(TEXT("SyncWidgets"));
const FName UEditorDataStorage::TickGroupName_SyncExternalToDataStorage(TEXT("SyncExternalToDataStorage"));
const FName UEditorDataStorage::TickGroupName_SyncDataStorageToExternal(TEXT("SyncDataStorageToExternal"));

FAutoConsoleCommandWithOutputDevice PrintQueryCallbacksConsoleCommand(
	TEXT("TEDS.PrintQueryCallbacks"),
	TEXT("Prints out a list of all processors."),
	FConsoleCommandWithOutputDeviceDelegate::CreateLambda([](FOutputDevice& Output)
		{
			using namespace UE::Editor::DataStorage;
			if (IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName))
			{
				DataStorage->DebugPrintQueryCallbacks(Output);
			}
		}));

FAutoConsoleCommandWithOutputDevice PrintSupportedColumnsConsoleCommand(
	TEXT("TEDS.PrintSupportedColumns"),
	TEXT("Prints out a list of available Data Storage columns."),
	FConsoleCommandWithOutputDeviceDelegate::CreateLambda([](FOutputDevice& Output)
		{
			Output.Log(TEXT("The Editor Data Storage supports the following columns:"));
			
			UScriptStruct* FragmentTypeInfo = FMassFragment::StaticStruct();
			UScriptStruct* TagTypeInfo = FMassTag::StaticStruct();
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				if (It->IsChildOf(FragmentTypeInfo) || It->IsChildOf(TagTypeInfo))
				{
					int32 StructureSize = It->GetStructureSize();
					if (StructureSize >= 1024 * 1024)
					{
						Output.Logf(TEXT("    [%6.2f mib] %s"), StructureSize / (1024.0f * 1024.0f), *It->GetFullName());
					}
					else if (StructureSize >= 1024)
					{
						Output.Logf(TEXT("    [%6.2f kib] %s"), StructureSize / 1024.0f, *It->GetFullName());
					}
					else
					{
						Output.Logf(TEXT("    [%6i b  ] %s"), StructureSize, *It->GetFullName());
					}
				}
			}
			Output.Log(TEXT("End of Typed Elements Data Storage supported column list."));
		}));

namespace UE::Editor::DataStorage::Private
{
	struct ColumnsToBitSetsResult
	{
		bool bMustUpdateFragments = false;
		bool bMustUpdateTags = false;
		
		bool MustUpdate() const { return bMustUpdateFragments || bMustUpdateTags; }
	};
	ColumnsToBitSetsResult ColumnsToBitSets(TConstArrayView<const UScriptStruct*> Columns, FMassFragmentBitSet& Fragments, FMassTagBitSet& Tags)
	{
		ColumnsToBitSetsResult Result;

		for (const UScriptStruct* ColumnType : Columns)
		{
			if (ColumnType->IsChildOf(FMassFragment::StaticStruct()))
			{
				Fragments.Add(*ColumnType);
				Result.bMustUpdateFragments = true;
			}
			else if (ColumnType->IsChildOf(FMassTag::StaticStruct()))
			{
				Tags.Add(*ColumnType);
				Result.bMustUpdateTags = true;
			}
		}
		return Result;
	}
} // namespace UE::Editor::DataStorage::Private

void UEditorDataStorage::Initialize()
{
	using namespace UE::Editor::DataStorage;

	check(GEditor);
	UMassEntityEditorSubsystem* Mass = GEditor->GetEditorSubsystem<UMassEntityEditorSubsystem>();
	check(Mass);
	OnPreMassTickHandle = Mass->GetOnPreTickDelegate().AddUObject(this, &UEditorDataStorage::OnPreMassTick);
	OnPostMassTickHandle = Mass->GetOnPostTickDelegate().AddUObject(this, &UEditorDataStorage::OnPostMassTick);

	ActiveEditorEntityManager = Mass->GetMutableEntityManager();
	ActiveEditorPhaseManager = Mass->GetMutablePhaseManager();
	if (ActiveEditorEntityManager && ActiveEditorPhaseManager)
	{
		Environment = MakeShared<FEnvironment>(*this, *ActiveEditorEntityManager, *ActiveEditorPhaseManager);

		using PhaseType = std::underlying_type_t<EQueryTickPhase>;
		for (PhaseType PhaseId = 0; PhaseId < static_cast<PhaseType>(EQueryTickPhase::Max); ++PhaseId)
		{
			EQueryTickPhase Phase = static_cast<EQueryTickPhase>(PhaseId);
			EMassProcessingPhase MassPhase = FTypedElementQueryProcessorData::MapToMassProcessingPhase(Phase);

			ActiveEditorPhaseManager->GetOnPhaseStart(MassPhase).AddLambda(
				[this, Phase](float DeltaTime)
				{
					PreparePhase(Phase, DeltaTime);
				});

			ActiveEditorPhaseManager->GetOnPhaseEnd(MassPhase).AddLambda(
				[this, Phase](float DeltaTime)
				{
					FinalizePhase(Phase, DeltaTime);
				});

			// Update external source to TEDS at the start of the phase.
			RegisterTickGroup(GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage),
				Phase, {}, {}, EExecutionMode::Threaded);
			
			// Default group.
			RegisterTickGroup(GetQueryTickGroupName(EQueryTickGroups::Default),
				Phase, {}, GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage), EExecutionMode::Threaded);

			// Order the update groups.
			RegisterTickGroup(GetQueryTickGroupName(EQueryTickGroups::PreUpdate),
				Phase, {}, GetQueryTickGroupName(EQueryTickGroups::Default), EExecutionMode::Threaded);
			RegisterTickGroup(GetQueryTickGroupName(EQueryTickGroups::Update),
				Phase, {}, GetQueryTickGroupName(EQueryTickGroups::PreUpdate), EExecutionMode::Threaded);
			RegisterTickGroup(GetQueryTickGroupName(EQueryTickGroups::PostUpdate),
				Phase, {}, GetQueryTickGroupName(EQueryTickGroups::Update), EExecutionMode::Threaded);

			// After everything has processed sync the data in TEDS to external sources.
			RegisterTickGroup(GetQueryTickGroupName(EQueryTickGroups::SyncDataStorageToExternal),
				Phase, {}, GetQueryTickGroupName(EQueryTickGroups::PostUpdate), EExecutionMode::Threaded);

			// Update any widgets with data from TEDS.
			RegisterTickGroup(GetQueryTickGroupName(EQueryTickGroups::SyncWidgets),
				Phase, {}, GetQueryTickGroupName(EQueryTickGroups::PostUpdate), EExecutionMode::GameThread /* Needs main thread*/);
		}
	}
}

void UEditorDataStorage::SetFactories(TConstArrayView<UClass*> FactoryClasses)
{
	Factories.Reserve(FactoryClasses.Num());

	UClass* BaseFactoryType = UEditorDataStorageFactory::StaticClass();

	for (UClass* FactoryClass : FactoryClasses)
	{
		if (FactoryClass->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}
		if (!FactoryClass->IsChildOf(BaseFactoryType))
		{
			continue;
		}
		UEditorDataStorageFactory* Factory = NewObject<UEditorDataStorageFactory>(this, FactoryClass, NAME_None, EObjectFlags::RF_Transient);
		Factories.Add(FFactoryTypePair
			{
				.Type = FactoryClass,
				.Instance = Factory
			});
	}

	Factories.StableSort(
	[](const FFactoryTypePair& Lhs, const FFactoryTypePair& Rhs)
	{
		return Lhs.Instance->GetOrder() < Rhs.Instance->GetOrder();
	});
	
	for (FFactoryTypePair& Factory : Factories)
	{
		Factory.Instance->PreRegister(*this);
	}
}

void UEditorDataStorage::ResetFactories()
{
	for (int32 Index = Factories.Num() - 1; Index >= 0; --Index)
	{
		const FFactoryTypePair& Factory = Factories[Index];
		Factory.Instance->PreShutdown(*this);
	}
	Factories.Empty();
}

UEditorDataStorage::FactoryIterator UEditorDataStorage::CreateFactoryIterator()
{
	return UEditorDataStorage::FactoryIterator(this);
}

UEditorDataStorage::FactoryConstIterator UEditorDataStorage::CreateFactoryIterator() const
{
	return UEditorDataStorage::FactoryConstIterator(this);
}

const UEditorDataStorageFactory* UEditorDataStorage::FindFactory(const UClass* FactoryType) const
{
	for (const FFactoryTypePair& Factory : Factories)
	{
		if (Factory.Type == FactoryType)
		{
			return Factory.Instance;
		}
	}
	return nullptr;
}

void UEditorDataStorage::Deinitialize()
{
	checkf(Factories.IsEmpty(), TEXT("ResetFactories should have been called before deinitialized"));
	
	Reset();
}

void UEditorDataStorage::OnPreMassTick(float DeltaTime)
{
	checkf(IsAvailable(), TEXT("Typed Element Database was ticked while it's not ready."));
	
	OnUpdateDelegate.Broadcast();
	// Process pending commands after other systems have had a chance to update. Other systems may have executed work needed
	// to complete pending work.
	Environment->GetDirectDeferredCommands().ProcessCommands();
}

void UEditorDataStorage::OnPostMassTick(float DeltaTime)
{
	checkf(IsAvailable(), TEXT("Typed Element Database was ticked while it's not ready."));
	
	Environment->NextUpdateCycle();
	OnUpdateCompletedDelegate.Broadcast();
}

TSharedPtr<FMassEntityManager> UEditorDataStorage::GetActiveMutableEditorEntityManager()
{
	return ActiveEditorEntityManager;
}

TSharedPtr<const FMassEntityManager> UEditorDataStorage::GetActiveEditorEntityManager() const
{
	return ActiveEditorEntityManager;
}

UE::Editor::DataStorage::TableHandle UEditorDataStorage::RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList, const FName Name)
{
	using namespace UE::Editor::DataStorage;

	if (ActiveEditorEntityManager && !TableNameLookup.Contains(Name))
	{
		TableHandle Result = Tables.Num();
		FMassArchetypeCreationParams ArchetypeCreationParams;
		ArchetypeCreationParams.DebugName = Name;
		ArchetypeCreationParams.ChunkMemorySize = GetTableChunkSize(Name);
		Tables.Add(ActiveEditorEntityManager->CreateArchetype(ColumnList, ArchetypeCreationParams));
		if (Name.IsValid())
		{
			TableNameLookup.Add(Name, Result);
		}
		return Result;
	}
	return InvalidTableHandle;
}

UE::Editor::DataStorage::TableHandle UEditorDataStorage::RegisterTable(UE::Editor::DataStorage::TableHandle SourceTable,
	TConstArrayView<const UScriptStruct*> ColumnList, const FName Name)
{
	using namespace UE::Editor::DataStorage;

	if (ActiveEditorEntityManager && SourceTable < Tables.Num() && !TableNameLookup.Contains(Name))
	{
		TableHandle Result = Tables.Num();
		FMassArchetypeCreationParams ArchetypeCreationParams;
		ArchetypeCreationParams.DebugName = Name;
		ArchetypeCreationParams.ChunkMemorySize = GetTableChunkSize(Name);
		Tables.Add(ActiveEditorEntityManager->CreateArchetype(Tables[SourceTable], ColumnList, ArchetypeCreationParams));
		if (Name.IsValid())
		{
			TableNameLookup.Add(Name, Result);
		}
		return Result;
	}
	return InvalidTableHandle;
}

UE::Editor::DataStorage::TableHandle UEditorDataStorage::FindTable(const FName Name)
{
	using namespace UE::Editor::DataStorage;

	TableHandle* TableHandle = TableNameLookup.Find(Name);
	return TableHandle ? *TableHandle : InvalidTableHandle;
}

UE::Editor::DataStorage::RowHandle UEditorDataStorage::ReserveRow()
{
	return ActiveEditorEntityManager 
		? ActiveEditorEntityManager->ReserveEntity().AsNumber()
		: UE::Editor::DataStorage::InvalidRowHandle;
}

void UEditorDataStorage::BatchReserveRows(int32 Count, TFunctionRef<void(RowHandle)> ReservationCallback)
{
	if (ActiveEditorEntityManager)
	{
		TArrayView<FMassEntityHandle> ReservedEntities(Environment->GetScratchBuffer().EmplaceArray<FMassEntityHandle>(Count), Count);
		ActiveEditorEntityManager->BatchReserveEntities(ReservedEntities);

		for (FMassEntityHandle ReservedEntity : ReservedEntities)
		{
			ReservationCallback(ReservedEntity.AsNumber());
		}
	}
}

void UEditorDataStorage::BatchReserveRows(TArrayView<RowHandle> ReservedRows)
{
	if (ActiveEditorEntityManager)
	{
		// Depend on the fact that a row handle is an alias for an entity within the Mass powered backend. This
		// avoids the need for copying to a temporary array;
		static_assert(sizeof(RowHandle) == sizeof(FMassEntityHandle),
			"BatchReserveRows in TEDS requires the row handle and the Mass entity handle to be the same size.");

		TArrayView<FMassEntityHandle>* MassHandles = reinterpret_cast<TArrayView<FMassEntityHandle>*>(&ReservedRows);
		ActiveEditorEntityManager->BatchReserveEntities(*MassHandles);
	}
}

UE::Editor::DataStorage::RowHandle UEditorDataStorage::AddRow(TableHandle Table)
{
	checkf(Table < Tables.Num(), TEXT("Attempting to add a row to a non-existing table."));
	return ActiveEditorEntityManager 
		? ActiveEditorEntityManager->CreateEntity(Tables[Table]).AsNumber() 
		: UE::Editor::DataStorage::InvalidRowHandle;
}

UE::Editor::DataStorage::RowHandle UEditorDataStorage::AddRow(TableHandle Table,
	RowCreationCallbackRef OnCreated)
{
	using namespace UE::Editor::DataStorage;

	OnCreated.CheckCallable();
	if (ActiveEditorEntityManager)
	{
		checkf(Table < Tables.Num(), TEXT("Attempting to a row to a non-existing table."));

		TArray<FMassEntityHandle> Entity;
		Entity.Reserve(1);
		TSharedRef<FMassEntityManager::FEntityCreationContext> Context =
			ActiveEditorEntityManager->BatchCreateEntities(Tables[Table], 1, Entity);

		checkf(!Entity.IsEmpty(), TEXT("Add row tried to create a new row but none were provided by the backend."));
		RowHandle Result = Entity[0].AsNumber();
		OnCreated(Entity[0].AsNumber());
		return Result;
	}
	return InvalidRowHandle;
}

bool UEditorDataStorage::AddRow(RowHandle ReservedRow, TableHandle Table)
{
	checkf(!IsRowAssigned(ReservedRow), TEXT("Attempting to assign a table to row that already has a table assigned."));
	checkf(Table < Tables.Num(), TEXT("Attempting to add a row to a non-existing table."));
	if (ActiveEditorEntityManager)
	{
		ActiveEditorEntityManager->BuildEntity(FMassEntityHandle::FromNumber(ReservedRow), Tables[Table]);
		return true;
	}
	else
	{
		return false;
	}
}

bool UEditorDataStorage::AddRow(RowHandle ReservedRow, TableHandle Table,
	RowCreationCallbackRef OnCreated)
{
	OnCreated.CheckCallable();
	if (ActiveEditorEntityManager)
	{
		checkf(Table < Tables.Num(), TEXT("Attempting to add a row to a non-existing table."));
		
		TSharedRef<FMassEntityManager::FEntityCreationContext> Context =
			ActiveEditorEntityManager->BatchCreateReservedEntities(Tables[Table], { FMassEntityHandle::FromNumber(ReservedRow) });

		OnCreated(ReservedRow);
		return true;
	}
	return false;
}

bool UEditorDataStorage::BatchAddRow(
	TableHandle Table, int32 Count, RowCreationCallbackRef OnCreated)
{
	OnCreated.CheckCallable();
	if (ActiveEditorEntityManager)
	{
		checkf(Table < Tables.Num(), TEXT("Attempting to add multiple rows to a non-existing table."));
	
		TArray<FMassEntityHandle> Entities;
		Entities.Reserve(Count);
		TSharedRef<FMassEntityManager::FEntityCreationContext> Context = 
			ActiveEditorEntityManager->BatchCreateEntities(Tables[Table], Count, Entities);
		
		for (FMassEntityHandle Entity : Entities)
		{
			OnCreated(Entity.AsNumber());
		}

		return true;
	}
	return false;
}

bool UEditorDataStorage::BatchAddRow(TableHandle Table, 
	TConstArrayView<RowHandle> ReservedHandles, RowCreationCallbackRef OnCreated)
{
	using namespace UE::Editor;
	OnCreated.CheckCallable();
	if (ActiveEditorEntityManager)
	{
		checkf(Table < Tables.Num(), TEXT("Attempting to add multiple rows to a non-existing table."));
	
		// Depend on the fact that a row handle is an alias for an entity within the Mass powered backend. This
		// avoids the need for copying to a temporary array;
		static_assert(sizeof(RowHandle) == sizeof(FMassEntityHandle), 
			"BatchAddRow in TEDS requires the row handle and the Mass entity handle to be the same size.");
		
		TConstArrayView<FMassEntityHandle> Entities(
			reinterpret_cast<const FMassEntityHandle*>(ReservedHandles.GetData()), ReservedHandles.Num());
		TSharedRef<FMassEntityManager::FEntityCreationContext> Context =
			ActiveEditorEntityManager->BatchCreateReservedEntities(Tables[Table], Entities);

		for (RowHandle Entity : ReservedHandles)
		{
			OnCreated(Entity);
		}

		return true;
	}
	return false;
}


void UEditorDataStorage::RemoveRow(RowHandle Row)
{
	FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
	if (ActiveEditorEntityManager && ActiveEditorEntityManager->IsEntityValid(Entity))
	{
		Environment->GetIndexTable().RemoveRow(UE::Editor::DataStorage::EGlobalLockScope::Public, Row);
		if (ActiveEditorEntityManager->IsEntityBuilt(FMassEntityHandle::FromNumber(Row)))
		{
			ActiveEditorEntityManager->DestroyEntity(FMassEntityHandle::FromNumber(Row));
		}
		else
		{
			Environment->GetDirectDeferredCommands().Clear(Row);
			ActiveEditorEntityManager->ReleaseReservedEntity(FMassEntityHandle::FromNumber(Row));
		}
	}
}

bool UEditorDataStorage::IsRowAvailable(RowHandle Row) const
{
	return ActiveEditorEntityManager ? UE::Editor::DataStorage::Legacy::FCommandBuffer::Execute_IsRowAvailable(*ActiveEditorEntityManager, Row) : false;
}

bool UEditorDataStorage::IsRowAssigned(RowHandle Row) const
{
	return ActiveEditorEntityManager ? UE::Editor::DataStorage::Legacy::FCommandBuffer::Execute_IsRowAssigned(*ActiveEditorEntityManager, Row) : false;
}

void UEditorDataStorage::AddColumn(RowHandle Row, const UScriptStruct* ColumnType)
{
	if (ColumnType && ActiveEditorEntityManager)
	{
		if (IsRowAssigned(Row))
		{
			UE::Editor::DataStorage::Legacy::FCommandBuffer::Execute_AddColumnCommand(*ActiveEditorEntityManager, Row, ColumnType);
		}
		else
		{
			Environment->GetDirectDeferredCommands().Queue_AddColumnCommand(Row, ColumnType);
		}
	}
}

void UEditorDataStorage::AddColumnData(RowHandle Row, const UScriptStruct* ColumnType,
	const ColumnCreationCallbackRef& Initializer,
	ColumnCopyOrMoveCallback Relocator)
{
	if (ActiveEditorEntityManager && ColumnType && ColumnType->IsChildOf(FMassFragment::StaticStruct()))
	{
		if (IsRowAssigned(Row))
		{
			FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
			FStructView Column = ActiveEditorEntityManager->GetFragmentDataStruct(Entity, ColumnType);
			if (!Column.IsValid())
			{
				ActiveEditorEntityManager->AddFragmentToEntity(Entity, ColumnType, Initializer);
			}
			else
			{
				Initializer(Column.GetMemory(), *ColumnType);
			}
		}
		else
		{
			void* Column = Environment->GetDirectDeferredCommands().Queue_AddDataColumnCommandUnitialized(Row, ColumnType, Relocator);
			Initializer(Column, *ColumnType);
		}
	}
}

void UEditorDataStorage::RemoveColumn(RowHandle Row, const UScriptStruct* ColumnType)
{
	if (ColumnType && ActiveEditorEntityManager)
	{
		if (IsRowAssigned(Row))
		{
			UE::Editor::DataStorage::Legacy::FCommandBuffer::Execute_RemoveColumnCommand(*ActiveEditorEntityManager, Row, ColumnType);
		}
		else
		{
			Environment->GetDirectDeferredCommands().Queue_RemoveColumnCommand(Row, ColumnType);
		}
	}
}

const void* UEditorDataStorage::GetColumnData(RowHandle Row, const UScriptStruct* ColumnType) const
{
	FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
	if (ActiveEditorEntityManager &&
		ColumnType && ColumnType->IsChildOf(FMassFragment::StaticStruct()))
	{
		if (ActiveEditorEntityManager->IsEntityActive(Entity))
		{
			FStructView Column = ActiveEditorEntityManager->GetFragmentDataStruct(Entity, ColumnType);
			if (Column.IsValid())
			{
				return Column.GetMemory();
			}
		}
		else
		{
			return Environment->GetDirectDeferredCommands().GetQueuedDataColumn(Row, ColumnType);
		}
	}
	return nullptr;
}

void* UEditorDataStorage::GetColumnData(RowHandle Row, const UScriptStruct* ColumnType)
{
	return const_cast<void*>(static_cast<const UEditorDataStorage*>(this)->GetColumnData(Row, ColumnType));
}

void UEditorDataStorage::AddColumns(RowHandle Row, TConstArrayView<const UScriptStruct*> Columns)
{
	using namespace UE::Editor::DataStorage;
	if (ActiveEditorEntityManager)
	{
		FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
		FMassArchetypeHandle Archetype = ActiveEditorEntityManager->GetArchetypeForEntity(Entity);

		FMassFragmentBitSet FragmentsToAdd;
		FMassTagBitSet TagsToAdd;
		if (Private::ColumnsToBitSets(Columns, FragmentsToAdd, TagsToAdd).MustUpdate())
		{
			if (ActiveEditorEntityManager->IsEntityActive(Entity))
			{
				Legacy::FCommandBuffer::Execute_AddColumnsCommand(*ActiveEditorEntityManager, Row, FragmentsToAdd, TagsToAdd);
			}
			else
			{
				Environment->GetDirectDeferredCommands().Queue_AddColumnsCommand(Row, FragmentsToAdd, TagsToAdd);
			}
		}
	}
}

void UEditorDataStorage::AddColumn(RowHandle Row, const UE::Editor::DataStorage::FValueTag& Tag, const FName& InValue)
{
	if (ActiveEditorEntityManager)
	{
		const FConstSharedStruct SharedStruct = Environment->GenerateValueTag(Tag, InValue);

		FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
		if (ActiveEditorEntityManager->IsEntityActive(Entity))
		{
			UE::Editor::DataStorage::Legacy::FCommandBuffer::Execute_AddSharedColumnCommand(*ActiveEditorEntityManager, Row, SharedStruct);
		}
	}
}

void UEditorDataStorage::RemoveColumn(RowHandle Row, const UE::Editor::DataStorage::FValueTag& Tag)
{
	if (ActiveEditorEntityManager)
	{
		const UScriptStruct* ValueTagType = Environment->GenerateColumnType(Tag);
		FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
		if (ActiveEditorEntityManager->IsEntityActive(Entity))
		{
			UE::Editor::DataStorage::Legacy::FCommandBuffer::Execute_RemoveSharedColumnCommand(*ActiveEditorEntityManager, Row, *ValueTagType);
		}
	}
}

void UEditorDataStorage::RemoveColumns(RowHandle Row, TConstArrayView<const UScriptStruct*> Columns)
{
	using namespace UE::Editor::DataStorage;

	FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
	if (ActiveEditorEntityManager)
	{
		FMassArchetypeHandle Archetype = ActiveEditorEntityManager->GetArchetypeForEntity(Entity);

		FMassFragmentBitSet FragmentsToRemove;
		FMassTagBitSet TagsToRemove;
		if (Private::ColumnsToBitSets(Columns, FragmentsToRemove, TagsToRemove).MustUpdate())
		{
			if (ActiveEditorEntityManager->IsEntityActive(Entity))
			{
				Legacy::FCommandBuffer::Execute_RemoveColumnsCommand(*ActiveEditorEntityManager, Row, FragmentsToRemove, TagsToRemove);
			}
			else
			{
				Environment->GetDirectDeferredCommands().Queue_RemoveColumnsCommand(Row, FragmentsToRemove, TagsToRemove);
			}
		}
	}
}

void UEditorDataStorage::AddRemoveColumns(RowHandle Row,
	TConstArrayView<const UScriptStruct*> ColumnsToAdd, TConstArrayView<const UScriptStruct*> ColumnsToRemove)
{
	using namespace UE::Editor::DataStorage;
	if (ActiveEditorEntityManager)
	{
		FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
		FMassArchetypeHandle Archetype = ActiveEditorEntityManager->GetArchetypeForEntity(Entity);

		FMassFragmentBitSet FragmentsToAdd;
		FMassTagBitSet TagsToAdd;
		FMassTagBitSet TagsToRemove;
		FMassFragmentBitSet FragmentsToRemove;

		bool bMustAddColumns = Private::ColumnsToBitSets(ColumnsToAdd, FragmentsToAdd, TagsToAdd).MustUpdate();
		bool bMustRemoveColumns = Private::ColumnsToBitSets(ColumnsToRemove, FragmentsToRemove, TagsToRemove).MustUpdate();
		
		if (ActiveEditorEntityManager->IsEntityActive(Entity))
		{
			if (bMustAddColumns)
			{
				Legacy::FCommandBuffer::Execute_AddColumnsCommand(*ActiveEditorEntityManager, Row, FragmentsToAdd, TagsToAdd);
			}
			if (bMustRemoveColumns)
			{
				Legacy::FCommandBuffer::Execute_RemoveColumnsCommand(*ActiveEditorEntityManager, Row, FragmentsToRemove, TagsToRemove);
			}
		}
		else
		{
			if (bMustAddColumns)
			{
				Environment->GetDirectDeferredCommands().Queue_AddColumnsCommand(Row, FragmentsToAdd, TagsToAdd);
			}
			if (bMustRemoveColumns)
			{
				Environment->GetDirectDeferredCommands().Queue_RemoveColumnsCommand(Row, FragmentsToRemove, TagsToRemove);
			}
		}
	}
}

void UEditorDataStorage::BatchAddRemoveColumns(TConstArrayView<RowHandle> Rows, 
	TConstArrayView<const UScriptStruct*> ColumnsToAdd, TConstArrayView<const UScriptStruct*> ColumnsToRemove)
{	
	if (ActiveEditorEntityManager)
	{
		FMassFragmentBitSet FragmentsToAdd;
		FMassFragmentBitSet FragmentsToRemove;

		FMassTagBitSet TagsToAdd;
		FMassTagBitSet TagsToRemove;

		using namespace UE::Editor::DataStorage;

		Private::ColumnsToBitSetsResult AddResult = Private::ColumnsToBitSets(ColumnsToAdd, FragmentsToAdd, TagsToAdd);
		Private::ColumnsToBitSetsResult RemoveResult = Private::ColumnsToBitSets(ColumnsToRemove, FragmentsToRemove, TagsToRemove);
		
		if (AddResult.MustUpdate() || RemoveResult.MustUpdate())
		{
			using EntityHandleArray = TArray<FMassEntityHandle, TInlineAllocator<32>>;
			using EntityArchetypeLookup = TMap<FMassArchetypeHandle, EntityHandleArray, TInlineSetAllocator<32>>;
			using ArchetypeEntityArray = TArray<FMassArchetypeEntityCollection, TInlineAllocator<32>>;

			Legacy::FCommandBuffer& CommandBuffer = Environment->GetDirectDeferredCommands();
			
			// Sort rows (entities) into to matching table (archetype) bucket.
			EntityArchetypeLookup LookupTable;
			for (RowHandle EntityId : Rows)
			{
				FMassEntityHandle Entity = FMassEntityHandle::FromNumber(EntityId);
				if (ActiveEditorEntityManager->IsEntityActive(Entity))
				{
					FMassArchetypeHandle Archetype = ActiveEditorEntityManager->GetArchetypeForEntity(Entity);
					EntityHandleArray& EntityCollection = LookupTable.FindOrAdd(Archetype);
					EntityCollection.Add(Entity);
				}
				else
				{
					if (AddResult.MustUpdate())
					{
						CommandBuffer.Queue_AddColumnsCommand(EntityId, FragmentsToAdd, TagsToAdd);
					}
					if (RemoveResult.MustUpdate())
					{
						CommandBuffer.Queue_RemoveColumnsCommand(EntityId, FragmentsToRemove, TagsToRemove);
					}
				}
			}
		
			// Construct table (archetype) specific row (entity) collections.
			ArchetypeEntityArray EntityCollections;
			EntityCollections.Reserve(LookupTable.Num());
			for (auto It = LookupTable.CreateConstIterator(); It; ++It)
			{
				EntityCollections.Emplace(It.Key(), It.Value(), FMassArchetypeEntityCollection::EDuplicatesHandling::FoldDuplicates);
			}

			// Batch update using the appropriate fragment/bit sets.
			if (AddResult.bMustUpdateFragments || RemoveResult.bMustUpdateFragments)
			{
				ActiveEditorEntityManager->BatchChangeFragmentCompositionForEntities(EntityCollections, FragmentsToAdd, FragmentsToRemove);
			}
			if (AddResult.bMustUpdateTags || RemoveResult.bMustUpdateTags)
			{
				ActiveEditorEntityManager->BatchChangeTagsForEntities(EntityCollections, TagsToAdd, TagsToRemove);
			}
		}
	}
}

bool UEditorDataStorage::HasColumns(RowHandle Row, TConstArrayView<const UScriptStruct*> ColumnTypes) const
{
	if (ActiveEditorEntityManager)
	{
		bool bHasAllColumns = true;
		
		FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
		if (ActiveEditorEntityManager->IsEntityActive(Entity))
		{
			FMassArchetypeHandle Archetype = ActiveEditorEntityManager->GetArchetypeForEntity(Entity);
			const FMassArchetypeCompositionDescriptor& Composition = ActiveEditorEntityManager->GetArchetypeComposition(Archetype);

			const UScriptStruct* const* ColumnTypesEnd = ColumnTypes.end();
			for (const UScriptStruct* const* ColumnType = ColumnTypes.begin(); ColumnType != ColumnTypesEnd && bHasAllColumns; ++ColumnType)
			{
				if ((*ColumnType)->IsChildOf(FMassFragment::StaticStruct()))
				{
					bHasAllColumns = Composition.Fragments.Contains(**ColumnType);
				}
				else if ((*ColumnType)->IsChildOf(FMassTag::StaticStruct()))
				{
					bHasAllColumns = Composition.Tags.Contains(**ColumnType);
				}
				else
				{
					return false;
				}
			}
		}
		else
		{
			const UE::Editor::DataStorage::Legacy::FCommandBuffer& CommandBuffer = Environment->GetDirectDeferredCommands();
			const UScriptStruct* const* ColumnTypesEnd = ColumnTypes.end();
			for (const UScriptStruct* const* ColumnType = ColumnTypes.begin(); ColumnType != ColumnTypesEnd && bHasAllColumns; ++ColumnType)
			{
				bHasAllColumns = CommandBuffer.HasColumn(Row, *ColumnType);
			}
		}

		return bHasAllColumns;
	}
	return false;
}

bool UEditorDataStorage::HasColumns(RowHandle Row, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes) const
{
	if (ActiveEditorEntityManager)
	{
		bool bHasAllColumns = true;
	
		FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
		if (ActiveEditorEntityManager->IsEntityActive(Entity))
		{
			FMassArchetypeHandle Archetype = ActiveEditorEntityManager->GetArchetypeForEntity(Entity);
			const FMassArchetypeCompositionDescriptor& Composition = ActiveEditorEntityManager->GetArchetypeComposition(Archetype);

			const TWeakObjectPtr<const UScriptStruct>* ColumnTypesEnd = ColumnTypes.end();
			for (const TWeakObjectPtr<const UScriptStruct>* ColumnType = ColumnTypes.begin(); ColumnType != ColumnTypesEnd && bHasAllColumns; ++ColumnType)
			{
				if (ColumnType->IsValid())
				{
					if ((*ColumnType)->IsChildOf(FMassFragment::StaticStruct()))
					{
						bHasAllColumns = Composition.Fragments.Contains(**ColumnType);
						continue;
					}
					else if ((*ColumnType)->IsChildOf(FMassTag::StaticStruct()))
					{
						bHasAllColumns = Composition.Tags.Contains(**ColumnType);
						continue;
					}
				}
				return false;
			}
		}
		else
		{
			const UE::Editor::DataStorage::Legacy::FCommandBuffer& CommandBuffer = Environment->GetDirectDeferredCommands();
			const TWeakObjectPtr<const UScriptStruct>* ColumnTypesEnd = ColumnTypes.end();
			for (const TWeakObjectPtr<const UScriptStruct>* ColumnType = ColumnTypes.begin(); ColumnType != ColumnTypesEnd && bHasAllColumns; ++ColumnType)
			{
				bHasAllColumns = CommandBuffer.HasColumn(Row, ColumnType->Get());
			}
		}

		return bHasAllColumns;
	}
	return false;
}

void UEditorDataStorage::ListColumns(RowHandle Row, ColumnListCallbackRef Callback) const
{
	if (ActiveEditorEntityManager)
	{
		FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
		if (ActiveEditorEntityManager->IsEntityActive(Entity))
		{
			FMassArchetypeHandle Archetype = ActiveEditorEntityManager->GetArchetypeForEntity(Entity);
			const FMassArchetypeCompositionDescriptor& Composition = ActiveEditorEntityManager->GetArchetypeComposition(Archetype);
			
			auto CallbackWrapper = [&Callback](const UScriptStruct* ColumnType)
				{
					if (ColumnType)
					{
						Callback(*ColumnType);
					}
					return true;
				};
			Composition.Fragments.ExportTypes(CallbackWrapper);
			Composition.Tags.ExportTypes(CallbackWrapper);

		}
	}
}

void UEditorDataStorage::ListColumns(RowHandle Row, ColumnListWithDataCallbackRef Callback)
{
	if (ActiveEditorEntityManager)
	{
		FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
		if (ActiveEditorEntityManager->IsEntityActive(Entity))
		{
			FMassArchetypeHandle Archetype = ActiveEditorEntityManager->GetArchetypeForEntity(Entity);
			const FMassArchetypeCompositionDescriptor& Composition = ActiveEditorEntityManager->GetArchetypeComposition(Archetype);

			Composition.Fragments.ExportTypes(
				[this, &Callback, Entity](const UScriptStruct* ColumnType)
				{
					if (ColumnType)
					{
						Callback(ActiveEditorEntityManager->GetFragmentDataStruct(Entity, ColumnType).GetMemory(), *ColumnType);
					}
					return true;
				});
			Composition.Tags.ExportTypes(
				[&Callback](const UScriptStruct* ColumnType)
				{
					if (ColumnType)
					{
						Callback(nullptr, *ColumnType);
					}
					return true;
				});

		}
	}
}

bool UEditorDataStorage::MatchesColumns(RowHandle Row, const UE::Editor::DataStorage::Queries::FConditions& Conditions) const
{
	if (ActiveEditorEntityManager)
	{
		checkf(Conditions.IsCompiled(), TEXT("Query Conditions must be compiled before they can be used"));
		
		FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
		if (ActiveEditorEntityManager->IsEntityActive(Entity))
		{
			FMassArchetypeHandle Archetype = ActiveEditorEntityManager->GetArchetypeForEntity(Entity);
			const FMassArchetypeCompositionDescriptor& Composition = ActiveEditorEntityManager->GetArchetypeComposition(Archetype);

			auto Callback = [&Composition](uint8_t ColumnIndex, TWeakObjectPtr<const UScriptStruct> ColumnType)
			{
				if (ColumnType.IsValid())
				{
					if (ColumnType->IsChildOf(FMassFragment::StaticStruct()))
					{
						return Composition.Fragments.Contains(*ColumnType);
					}
					else if (ColumnType->IsChildOf(FMassTag::StaticStruct()))
					{
						return Composition.Tags.Contains(*ColumnType);
					}
				}
				return false;
			};
			return Conditions.Verify(Callback);
		}
		else
		{
			auto Callback = [Row, &CommandBuffer = Environment->GetDirectDeferredCommands()]
				(uint8_t ColumnIndex, TWeakObjectPtr<const UScriptStruct> ColumnType)
			{
				if (ColumnType.IsValid())
				{
					return CommandBuffer.HasColumn(Row, ColumnType.Get());
				}
				return false;
			};
			return Conditions.Verify(Callback);
		}
	}
	return false;
}

const UScriptStruct* UEditorDataStorage::FindDynamicColumn(const UE::Editor::DataStorage::FDynamicColumnDescription& Description) const
{
	return Environment->FindDynamicColumn(*Description.TemplateType, Description.Identifier);
}

const UScriptStruct* UEditorDataStorage::GenerateDynamicColumn(const UE::Editor::DataStorage::FDynamicColumnDescription& Description)
{
	return Environment->GenerateDynamicColumn(*Description.TemplateType, Description.Identifier);
}

void UEditorDataStorage::RegisterTickGroup(
	FName GroupName, EQueryTickPhase Phase, FName BeforeGroup, FName AfterGroup, UE::Editor::DataStorage::EExecutionMode ExecutionMode)
{
	Environment->GetQueryStore().RegisterTickGroup(GroupName, Phase, BeforeGroup, AfterGroup, ExecutionMode);
}

void UEditorDataStorage::UnregisterTickGroup(FName GroupName, EQueryTickPhase Phase)
{
	Environment->GetQueryStore().UnregisterTickGroup(GroupName, Phase);
}

UE::Editor::DataStorage::QueryHandle UEditorDataStorage::RegisterQuery(FQueryDescription&& Query)
{
	return (ActiveEditorEntityManager && ActiveEditorPhaseManager)
		? Environment->GetQueryStore().RegisterQuery(MoveTemp(Query), *Environment, *ActiveEditorEntityManager, *ActiveEditorPhaseManager).Packed()
		: UE::Editor::DataStorage::InvalidQueryHandle;
}

void UEditorDataStorage::UnregisterQuery(QueryHandle Query)
{
	if (ActiveEditorEntityManager && ActiveEditorPhaseManager)
	{
		const UE::Editor::DataStorage::FExtendedQueryStore::Handle StorageHandle(Query);
		Environment->GetQueryStore().UnregisterQuery(StorageHandle, *ActiveEditorEntityManager, *ActiveEditorPhaseManager);
	}
}

const IEditorDataStorageProvider::FQueryDescription& UEditorDataStorage::GetQueryDescription(QueryHandle Query) const
{
	const UE::Editor::DataStorage::FExtendedQueryStore::Handle StorageHandle(Query);
	return Environment->GetQueryStore().GetQueryDescription(StorageHandle);
}

FName UEditorDataStorage::GetQueryTickGroupName(EQueryTickGroups Group) const
{
	switch (Group)
	{
		case EQueryTickGroups::Default:
			return TickGroupName_Default;
		case EQueryTickGroups::PreUpdate:
			return TickGroupName_PreUpdate;
		case EQueryTickGroups::Update:
			return TickGroupName_Update;
		case EQueryTickGroups::PostUpdate:
			return TickGroupName_PostUpdate;
		case EQueryTickGroups::SyncExternalToDataStorage:
			return TickGroupName_SyncExternalToDataStorage;
		case EQueryTickGroups::SyncDataStorageToExternal:
			return TickGroupName_SyncDataStorageToExternal;
		case EQueryTickGroups::SyncWidgets:
			return TickGroupName_SyncWidget;
		default:
			checkf(false, TEXT("EQueryTickGroups value %i can't be translated to a group name by this Data Storage backend."), static_cast<int>(Group));
			return NAME_None;
	}
}

IEditorDataStorageProvider::FQueryResult UEditorDataStorage::RunQuery(QueryHandle Query)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEDS.RunQuery);

	if (ActiveEditorEntityManager)
	{
		const UE::Editor::DataStorage::FExtendedQueryStore::Handle StorageHandle(Query);
		return Environment->GetQueryStore().RunQuery(*ActiveEditorEntityManager, StorageHandle);
	}
	else
	{
		return FQueryResult();
	}
}

IEditorDataStorageProvider::FQueryResult UEditorDataStorage::RunQuery(
	QueryHandle Query, IEditorDataStorageProvider::DirectQueryCallbackRef Callback)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEDS.RunQuery);

	if (ActiveEditorEntityManager)
	{
		const UE::Editor::DataStorage::FExtendedQueryStore::Handle StorageHandle(Query);
		return Environment->GetQueryStore().RunQuery(*ActiveEditorEntityManager, *Environment, StorageHandle, 
			UE::Editor::DataStorage::EDirectQueryExecutionFlags::Default, Callback);
	}
	else
	{
		return FQueryResult();
	}
}

IEditorDataStorageProvider::FQueryResult UEditorDataStorage::RunQuery(
	QueryHandle Query, UE::Editor::DataStorage::EDirectQueryExecutionFlags Flags,
	IEditorDataStorageProvider::DirectQueryCallbackRef Callback)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEDS.RunQuery);

	if (ActiveEditorEntityManager)
	{
		const UE::Editor::DataStorage::FExtendedQueryStore::Handle StorageHandle(Query);
		return Environment->GetQueryStore().RunQuery(*ActiveEditorEntityManager, *Environment, StorageHandle, Flags, Callback);
	}
	else
	{
		return FQueryResult();
	}
}

void UEditorDataStorage::ActivateQueries(FName ActivationName)
{
	if (ActiveEditorEntityManager)
	{
		Environment->GetQueryStore().ActivateQueries(ActivationName);
	}
}

UE::Editor::DataStorage::RowHandle UEditorDataStorage::FindIndexedRow(UE::Editor::DataStorage::IndexHash Index) const
{
	return Environment->GetIndexTable().FindIndexedRow(UE::Editor::DataStorage::EGlobalLockScope::Public, Index);
}

void UEditorDataStorage::IndexRow(UE::Editor::DataStorage::IndexHash Index, RowHandle Row)
{
	Environment->GetIndexTable().IndexRow(UE::Editor::DataStorage::EGlobalLockScope::Public, Index, Row);
}

void UEditorDataStorage::BatchIndexRows(
	TConstArrayView<TPair<UE::Editor::DataStorage::IndexHash, RowHandle>> IndexRowPairs)
{
	Environment->GetIndexTable().BatchIndexRows(UE::Editor::DataStorage::EGlobalLockScope::Public, IndexRowPairs);
}

void UEditorDataStorage::ReindexRow(UE::Editor::DataStorage::IndexHash OriginalIndex, UE::Editor::DataStorage::IndexHash NewIndex, 
	RowHandle RowHandle)
{
	Environment->GetIndexTable().ReindexRow(UE::Editor::DataStorage::EGlobalLockScope::Public, OriginalIndex, NewIndex, RowHandle);
}

void UEditorDataStorage::RemoveIndex(UE::Editor::DataStorage::IndexHash Index)
{
	Environment->GetIndexTable().RemoveIndex(UE::Editor::DataStorage::EGlobalLockScope::Public, Index);
}

FTypedElementOnDataStorageUpdate& UEditorDataStorage::OnUpdate()
{
	return OnUpdateDelegate;
}

FTypedElementOnDataStorageUpdate& UEditorDataStorage::OnUpdateCompleted()
{
	return OnUpdateCompletedDelegate;
}

bool UEditorDataStorage::IsAvailable() const
{
	return bool(ActiveEditorEntityManager);
}

void* UEditorDataStorage::GetExternalSystemAddress(UClass* Target)
{
	if (Target && Target->IsChildOf<USubsystem>())
	{
		return FMassSubsystemAccess::FetchSubsystemInstance(/*World=*/nullptr, Target);
	}
	return nullptr;
}

bool UEditorDataStorage::SupportsExtension(FName Extension) const
{
	return false;
}

void UEditorDataStorage::ListExtensions(TFunctionRef<void(FName)> Callback) const
{
}

void UEditorDataStorage::PreparePhase(EQueryTickPhase Phase, float DeltaTime)
{
	using namespace UE::Editor::DataStorage;

	if (ActiveEditorEntityManager)
	{
		{
			// The preamble queries are all run on the game thread. While this is true it's safe to take a global write lock.
			// If there's a performance loss because this lock is held too long, the work in RunPhasePreambleQueries can be split
			// into a step that runs the queries and uses a shared lock and one that executes the command buffer with an exclusive lock.
			FScopedExclusiveLock Lock(EGlobalLockScope::Public);
			Environment->GetQueryStore().RunPhasePreambleQueries(*ActiveEditorEntityManager, *Environment, Phase, DeltaTime);
		}
		// During the processing of queries no mutation can happen to the structure of the database, just fields being updated. As such
		// it's safe to only take a shared lock
		// TODO: This requires Mass to tell TEDS it's about to flush its deferred commands.
		// FGlobalLock::InternalSharedLock();
	}
}

void UEditorDataStorage::FinalizePhase(EQueryTickPhase Phase, float DeltaTime)
{
	using namespace UE::Editor::DataStorage;

	if (ActiveEditorEntityManager)
	{
		// During the processing of queries no mutation can happen to the structure of the database, just fields being updated. As such
		// it's safe to only take a shared lock
		// TODO: This requires Mass to tell TEDS it's about to flush its deferred commands. Right now this gets called after the
		// deferred commands are run, which require exclusive access.
		//FGlobalLock::InternalSharedUnlock();
		
		// The preamble queries are all run on the game thread. While this is true it's safe to take a global write lock.
		// If there's a performance loss because this lock is held too long, the work in RunPhasePostambleQueries can be split
		// into a step that runs the queries and uses a shared lock and one that executes the command buffer with an exclusive lock.
		FScopedExclusiveLock Lock(EGlobalLockScope::Public);
		Environment->GetQueryStore().RunPhasePostambleQueries(*ActiveEditorEntityManager, *Environment, Phase, DeltaTime);
	}
}

void UEditorDataStorage::Reset()
{
	if (UMassEntityEditorSubsystem* Mass = GEditor->GetEditorSubsystem<UMassEntityEditorSubsystem>())
	{
		Mass->GetOnPostTickDelegate().Remove(OnPostMassTickHandle);
		Mass->GetOnPreTickDelegate().Remove(OnPreMassTickHandle);
	}
	OnPostMassTickHandle.Reset();
	OnPreMassTickHandle.Reset();

	if (ActiveEditorEntityManager && ActiveEditorPhaseManager)
	{
		Environment->GetQueryStore().Clear(*ActiveEditorEntityManager.Get(), *ActiveEditorPhaseManager.Get());
	}
	Tables.Reset();
	TableNameLookup.Reset();
	Environment.Reset();
	ActiveEditorPhaseManager.Reset();
	ActiveEditorEntityManager.Reset();
}

int32 UEditorDataStorage::GetTableChunkSize(FName TableName) const
{
	const UEditorDataStorageSettings* Settings = GetDefault<UEditorDataStorageSettings>();
	if (const EChunkMemorySize* TableSpecificSize = Settings->TableSpecificChunkMemorySize.Find(TableName))
	{
		return static_cast<int32>(*TableSpecificSize);
	}
	else
	{
		return static_cast<int32>(Settings->ChunkMemorySize);
	}
}

TSharedPtr<UE::Editor::DataStorage::FEnvironment> UEditorDataStorage::GetEnvironment()
{
	return Environment;
}

TSharedPtr<const UE::Editor::DataStorage::FEnvironment> UEditorDataStorage::GetEnvironment() const
{
	return Environment;
}

FMassArchetypeHandle UEditorDataStorage::LookupArchetype(UE::Editor::DataStorage::TableHandle InTableHandle) const
{
	const uint32 TableIndex = InTableHandle;
	if (Tables.IsValidIndex(TableIndex))
	{
		return Tables[TableIndex];
	}
	return FMassArchetypeHandle();
}

void UEditorDataStorage::DebugPrintQueryCallbacks(FOutputDevice& Output)
{
	Environment->GetQueryStore().DebugPrintQueryCallbacks(Output);
}

void UEditorDataStorage::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	UEditorDataStorage* Database = static_cast<UEditorDataStorage*>(InThis);

	for (auto& FactoryPair : Database->Factories)
	{
		Collector.AddReferencedObject(FactoryPair.Instance);
		Collector.AddReferencedObject(FactoryPair.Type);
	}
}
