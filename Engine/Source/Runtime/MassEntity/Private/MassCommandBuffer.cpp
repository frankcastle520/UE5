// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCommandBuffer.h"
#include "Containers/AnsiString.h"
#include "MassObserverManager.h"
#include "MassEntityUtils.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "VisualLogger/VisualLogger.h"


CSV_DEFINE_CATEGORY(MassEntities, true);
CSV_DEFINE_CATEGORY(MassEntitiesCounters, true);
DECLARE_CYCLE_STAT(TEXT("Mass Flush Commands"), STAT_Mass_FlushCommands, STATGROUP_Mass);

namespace UE::Mass::Command {

#if CSV_PROFILER_STATS
bool bEnableDetailedStats = false;

FAutoConsoleVariableRef CVarEnableDetailedCommandStats(TEXT("massentities.EnableCommandDetailedStats"), bEnableDetailedStats,
	TEXT("Set to true create a dedicated stat per type of command."), ECVF_Default);

/** CSV stat names */
static FString DefaultBatchedName = TEXT("BatchedCommand");
static TMap<FName, TPair<FString, FAnsiString>> CommandBatchedFNames;

/** CSV custom stat names (ANSI) */
static FAnsiString DefaultANSIBatchedName = "BatchedCommand";

/**
 * Provides valid names for CSV profiling.
 * @param Command is the command instance
 * @param OutName is the name to use for csv custom stats
 * @param OutANSIName is the name to use for csv stats
 */
void GetCommandStatNames(FMassBatchedCommand& Command, FString*& OutName, FAnsiString*& OutANSIName)
{
	OutANSIName = &DefaultANSIBatchedName;
	OutName     = &DefaultBatchedName;
	if (!bEnableDetailedStats)
	{
		return;
	}

	const FName CommandFName = Command.GetFName();

	TPair<FString, FAnsiString>& Names = CommandBatchedFNames.FindOrAdd(CommandFName);
	OutName     = &Names.Get<FString>();
	OutANSIName = &Names.Get<FAnsiString>();
	if (OutName->IsEmpty())
	{
		*OutName     = CommandFName.ToString();
		*OutANSIName = **OutName;
	}
}

#endif
} // UE::Mass::Command

//////////////////////////////////////////////////////////////////////
// FMassBatchedCommand
std::atomic<uint32> FMassBatchedCommand::CommandsCounter;

//////////////////////////////////////////////////////////////////////
// FMassCommandBuffer

FMassCommandBuffer::FMassCommandBuffer()
	: OwnerThreadId(FPlatformTLS::GetCurrentThreadId())
{	
}

FMassCommandBuffer::~FMassCommandBuffer()
{
	ensureMsgf(HasPendingCommands() == false, TEXT("Destroying FMassCommandBuffer while there are still unprocessed commands. These operations will never be performed now."));

	CleanUp();
}

void FMassCommandBuffer::ForceUpdateCurrentThreadID()
{
	OwnerThreadId = FPlatformTLS::GetCurrentThreadId();
}

bool FMassCommandBuffer::Flush(FMassEntityManager& EntityManager)
{
	check(!bIsFlushing);
	TGuardValue FlushingGuard(bIsFlushing, true);

	// short-circuit exit
	if (HasPendingCommands() == false)
	{
		return false;
	}

	{
		UE_MT_SCOPED_WRITE_ACCESS(PendingBatchCommandsDetector);
		LLM_SCOPE_BYNAME(TEXT("Mass/FlushCommands"));
		SCOPE_CYCLE_COUNTER(STAT_Mass_FlushCommands);

		// array used to group commands depending on their operations. Based on EMassCommandOperationType
		constexpr int32 CommandTypeOrder[] =
		{
			MAX_int32 - 1, // None
			0, // Create
			3, // Add
			1, // Remove
			2, // ChangeComposition
			4, // Set
			5, // Destroy
		};
		static_assert(UE_ARRAY_COUNT(CommandTypeOrder) == (int)EMassCommandOperationType::MAX, "CommandTypeOrder needs to correspond to all EMassCommandOperationType\'s entries");

		struct FBatchedCommandsSortedIndex
		{
			FBatchedCommandsSortedIndex(const int32 InIndex, const int32 InGroupOrder)
				: Index(InIndex), GroupOrder(InGroupOrder)
			{}

			const int32 Index = -1;
			const int32 GroupOrder = MAX_int32;
			bool IsValid() const { return GroupOrder < MAX_int32; }
			bool operator<(const FBatchedCommandsSortedIndex& Other) const { return GroupOrder < Other.GroupOrder; }
		};
		
		TArray<FBatchedCommandsSortedIndex> CommandsOrder;
		const int32 OwnedCommandsCount = CommandInstances.Num();

		CommandsOrder.Reserve(OwnedCommandsCount);
		for (int32 i = 0; i < OwnedCommandsCount; ++i)
		{
			const TUniquePtr<FMassBatchedCommand>& Command = CommandInstances[i];
			CommandsOrder.Add(FBatchedCommandsSortedIndex(i, (Command && Command->HasWork())? CommandTypeOrder[(int)Command->GetOperationType()] : MAX_int32));
		}
		for (int32 i = 0; i < AppendedCommandInstances.Num(); ++i)
		{
			const TUniquePtr<FMassBatchedCommand>& Command = AppendedCommandInstances[i];
			CommandsOrder.Add(FBatchedCommandsSortedIndex(i + OwnedCommandsCount, (Command && Command->HasWork()) ? CommandTypeOrder[(int)Command->GetOperationType()] : MAX_int32));
		}
		CommandsOrder.StableSort();
				
		for (int32 k = 0; k < CommandsOrder.Num() && CommandsOrder[k].IsValid(); ++k)
		{
			const int32 CommandIndex = CommandsOrder[k].Index;
			TUniquePtr<FMassBatchedCommand>& Command = CommandIndex < OwnedCommandsCount
				? CommandInstances[CommandIndex]
				: AppendedCommandInstances[CommandIndex - OwnedCommandsCount];
			check(Command)

#if CSV_PROFILER_STATS
			using namespace UE::Mass::Command;

			// Extract name (default or detailed)
			FAnsiString* ANSIName = nullptr;
			FString*     Name     = nullptr;
			GetCommandStatNames(*Command, Name, ANSIName);

			// Push stats
			FScopedCsvStat ScopedCsvStat(**ANSIName, CSV_CATEGORY_INDEX(MassEntities));
			FCsvProfiler::RecordCustomStat(**Name, CSV_CATEGORY_INDEX(MassEntitiesCounters), Command->GetNumOperationsStat(), ECsvCustomStatOp::Accumulate);
#endif // CSV_PROFILER_STATS

			Command->Execute(EntityManager);
			Command->Reset();
		}

		AppendedCommandInstances.Reset();

		ActiveCommandsCounter = 0;
	}

	return true;
}
 
void FMassCommandBuffer::CleanUp()
{
	CommandInstances.Reset();
	AppendedCommandInstances.Reset();

	ActiveCommandsCounter = 0;
}

void FMassCommandBuffer::MoveAppend(FMassCommandBuffer& Other)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MassCommandBuffer_MoveAppend);

	// @todo optimize, there surely a way to do faster then this.
	UE_MT_SCOPED_READ_ACCESS(Other.PendingBatchCommandsDetector);
	if (Other.HasPendingCommands())
	{
		FScopeLock Lock(&AppendingCommandsCS);
		UE_MT_SCOPED_WRITE_ACCESS(PendingBatchCommandsDetector);
		AppendedCommandInstances.Append(MoveTemp(Other.CommandInstances));
		AppendedCommandInstances.Append(MoveTemp(Other.AppendedCommandInstances));
		ActiveCommandsCounter += Other.ActiveCommandsCounter;
		Other.ActiveCommandsCounter = 0;
	}
}

SIZE_T FMassCommandBuffer::GetAllocatedSize() const
{
	SIZE_T TotalSize = 0;
	for (const TUniquePtr<FMassBatchedCommand>& Command : CommandInstances)
	{
		TotalSize += Command ? Command->GetAllocatedSize() : 0;
	}
	for (const TUniquePtr<FMassBatchedCommand>& Command : AppendedCommandInstances)
	{
		TotalSize += Command ? Command->GetAllocatedSize() : 0;
	}

	TotalSize += CommandInstances.GetAllocatedSize();
	
	return TotalSize;
}

