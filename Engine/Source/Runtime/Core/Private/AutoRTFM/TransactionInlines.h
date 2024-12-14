// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Context.h"
#include "Transaction.h"

#include "HAL/Platform.h"
#include "Utils.h"

namespace AutoRTFM
{

UE_AUTORTFM_FORCEINLINE bool FTransaction::IsOnStack(const void* LogicalAddress) const
{ 
    return StackRange.Contains(LogicalAddress);
}

UE_AUTORTFM_FORCEINLINE bool FTransaction::ShouldRecordWrite(void* LogicalAddress) const
{
    // We cannot record writes to stack memory used within the transaction, as
    // undoing the writes may corrupt stack memory that has been unwound or
    // is now being used for a different variable from the one the write was
    // made.
    if (!IsOnStack(LogicalAddress))
    {
        return true;
    }

    // Writes to the stack under a scoped-transaction can be safely ignored,
    // because the values on the stack are not visible outside of the scope of
    // the transaction. In other words, if a scoped-transaction aborts that
    // memory will cease to be meaningful anyway.

    // Non-scoped transactions, as the name implies, do not impose a lexical
    // scope that encompasses the transaction. Instead a non-scoped transaction
    // is started with a call to StartTransaction() and ended with a call to
    // either AbortTransaction() or CommitTransaction(). Unlike a
    // scoped transaction, there's no precise stack range for a non-scoped 
    // transaction, as the scope can freely grow or shrink between the calls to
    // [Start|Abort|Commit]Transaction() and any recorded writes. The only
    // guarantee we have is that a non-scoped transaction cannot shrink past the
    // outer scoped transaction. For this reason, non-scoped transactions
    // adopt the stack range of the outer transaction, as this is guaranteed
    // to encompass the non-scoped transaction's scope range.

    // For non-scoped transactions, we assert that we're not writing to a
    // memory address that's in the transaction's stack range as this cannot be
    // safely undone, and stack variables may be visible once the transaction is
    // aborted. We make an exception for stack variables declared within the
    // scope of a Close(), as writing to these stack variables can be safely
    // ignored (they have the same constrained visibility as stack variables in
    // a scoped transaction).

    // Hitting this assert? 
    // Consider moving the variable being written to an inner scoped 
    // transaction, or move the variable outside of the nearest parent 
    // scoped-transaction.
    ASSERT(bIsStackScoped || LogicalAddress < FContext::Get()->GetClosedStackAddress()); 

    return false;
}

AUTORTFM_NO_ASAN UE_AUTORTFM_FORCEINLINE void FTransaction::RecordWriteMaxPageSized(void* LogicalAddress, size_t Size)
{
    void* CopyAddress = WriteLogBumpAllocator.Allocate(Size);
    memcpy(CopyAddress, LogicalAddress, Size);

    WriteLog.Push(FWriteLogEntry(LogicalAddress, Size, CopyAddress));
}

AUTORTFM_NO_ASAN UE_AUTORTFM_FORCEINLINE void FTransaction::RecordWrite(void* LogicalAddress, size_t Size)
{
    if (UNLIKELY(0 == Size))
    {
        return;
    }

    if (!ShouldRecordWrite(LogicalAddress))
    {
        Stats.Collect<EStatsKind::HitSetSkippedBecauseOfStackLocalMemory>();
        return;
    }

	// The cutoff here is arbitrarily any number less than UINT16_MAX, but its a
	// weigh up what a good size is. Because the hitset doesn't detect when you
	// are trying to write to a subregion of a previous hit (like memset something,
	// then write to an individual element), we've got to balance the cost of
	// recording meaningless hits, against the potential to hit again.
    if (Size <= 16)
    {
        FMemoryLocation Key(LogicalAddress);
        Key.SetTopTag(static_cast<uint16_t>(Size));

        if (!HitSet.Insert(Key))
        {
            Stats.Collect<EStatsKind::HitSetHit>();
            return;
        }
    
        Stats.Collect<EStatsKind::HitSetMiss>();
    }

	if (NewMemoryTracker.Contains(LogicalAddress, Size))
	{
		Stats.Collect<EStatsKind::NewMemoryTrackerHit>();
		return;
	}

	Stats.Collect<EStatsKind::NewMemoryTrackerMiss>();

    uint8_t* const Address = reinterpret_cast<uint8_t*>(LogicalAddress);

    size_t I = 0;

    for (; (I + FWriteLogBumpAllocator::MaxSize) < Size; I += FWriteLogBumpAllocator::MaxSize)
    {
        RecordWriteMaxPageSized(Address + I, FWriteLogBumpAllocator::MaxSize);
    }

    // Remainder at the end of the memcpy.
    RecordWriteMaxPageSized(Address + I, Size - I);
}

template<unsigned SIZE> AUTORTFM_NO_ASAN UE_AUTORTFM_FORCEINLINE void FTransaction::RecordWrite(void* LogicalAddress)
{
    static_assert(SIZE <= 8);

    if (!ShouldRecordWrite(LogicalAddress))
    {
        Stats.Collect<EStatsKind::HitSetSkippedBecauseOfStackLocalMemory>();
        return;
    }

    FMemoryLocation Key(LogicalAddress);
    Key.SetTopTag(static_cast<uint16_t>(SIZE));

    if (!HitSet.Insert(Key))
    {
        Stats.Collect<EStatsKind::HitSetHit>();
        return;
    }

    Stats.Collect<EStatsKind::HitSetMiss>();

	if (NewMemoryTracker.Contains(LogicalAddress, SIZE))
	{
		Stats.Collect<EStatsKind::NewMemoryTrackerHit>();
		return;
	}

	Stats.Collect<EStatsKind::NewMemoryTrackerMiss>();

	WriteLog.Push(FWriteLogEntry::CreateSmall<SIZE>(LogicalAddress));
}

UE_AUTORTFM_FORCEINLINE void FTransaction::DidAllocate(void* LogicalAddress, const size_t Size)
{
	if (0 == Size)
	{
		return;
	}

    const bool DidInsert = NewMemoryTracker.Insert(LogicalAddress, Size);
    ASSERT(DidInsert);
}

UE_AUTORTFM_FORCEINLINE void FTransaction::DidFree(void* LogicalAddress)
{
    ASSERT(bTrackAllocationLocations);
    
    // Checking if one byte is in the interval map is enough to ascertain if it
    // is new memory and we should be worried.
    ASSERT(!NewMemoryTracker.Contains(LogicalAddress, 1));
}

UE_AUTORTFM_FORCEINLINE void FTransaction::DeferUntilCommit(TFunction<void()>&& Callback)
{
	// We explicitly must copy the function here because the original was allocated
	// within a transactional context, and thus the memory is allocating under
	// transactionalized conditions. By copying, we create an open copy of the callback.
	TFunction<void()> Copy(Callback);
    CommitTasks.Add(MoveTemp(Copy));
}

UE_AUTORTFM_FORCEINLINE void FTransaction::DeferUntilAbort(TFunction<void()>&& Callback)
{
	// We explicitly must copy the function here because the original was allocated
	// within a transactional context, and thus the memory is allocating under
	// transactionalized conditions. By copying, we create an open copy of the callback.
	TFunction<void()> Copy(Callback);
    AbortTasks.Add(MoveTemp(Copy));
}

UE_AUTORTFM_FORCEINLINE void FTransaction::PushDeferUntilAbortHandler(const void* Key, TFunction<void()>&& Callback)
{
	// We explicitly must copy the function here because the original was allocated
	// within a transactional context, and thus the memory is allocating under
	// transactionalized conditions. By copying, we create an open copy of the callback.
	TFunction<void()> Copy(Callback);
    AbortTasks.AddKeyed(Key, MoveTemp(Copy));
}

UE_AUTORTFM_FORCEINLINE bool FTransaction::PopDeferUntilAbortHandler(const void* Key)
{
	return AbortTasks.DeleteKey(Key);
}

UE_AUTORTFM_FORCEINLINE bool FTransaction::PopAllDeferUntilAbortHandlers(const void* Key)
{
	return AbortTasks.DeleteAllMatchingKeys(Key);
}

UE_AUTORTFM_FORCEINLINE void FTransaction::CollectStats() const
{
    Stats.Collect<EStatsKind::AverageWriteLogEntries>(WriteLog.Num());
    Stats.Collect<EStatsKind::MaximumWriteLogEntries>(WriteLog.Num());

    Stats.Collect<EStatsKind::AverageWriteLogBytes>(WriteLogBumpAllocator.StatTotalSize);
    Stats.Collect<EStatsKind::MaximumWriteLogBytes>(WriteLogBumpAllocator.StatTotalSize);

    Stats.Collect<EStatsKind::AverageCommitTasks>(CommitTasks.Num());
    Stats.Collect<EStatsKind::MaximumCommitTasks>(CommitTasks.Num());

    Stats.Collect<EStatsKind::AverageAbortTasks>(AbortTasks.Num());
    Stats.Collect<EStatsKind::MaximumAbortTasks>(AbortTasks.Num());

    Stats.Collect<EStatsKind::AverageHitSetSize>(HitSet.GetSize());
    Stats.Collect<EStatsKind::AverageHitSetCapacity>(HitSet.GetCapacity());
}

} // namespace AutoRTFM
