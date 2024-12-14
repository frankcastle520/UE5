// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)
#include "Transaction.h"
#include "TransactionInlines.h"
#include "CallNestInlines.h"
#include "GlobalData.h"

namespace AutoRTFM
{

FTransaction::FTransaction(FContext* Context)
    : Context(Context)
{
}

bool FTransaction::IsFresh() const
{
    return HitSet.IsEmpty()
        && NewMemoryTracker.IsEmpty()
        && WriteLog.IsEmpty()
        && CommitTasks.IsEmpty()
        && AbortTasks.IsEmpty()
        && !bIsDone;
}

void FTransaction::AbortWithoutThrowing()
{
	UE_LOG(LogAutoRTFM, Verbose, TEXT("Aborting '%hs'!"), GetContextStatusName(Context->GetStatus()));

    ASSERT(Context->IsAborting());
    ASSERT(Context->GetCurrentTransaction() == this);

    Stats.Collect<EStatsKind::Abort>();
    CollectStats();

    // Call the destructors of all the OnCommit functors before undoing the transactional memory and
    // calling the OnAbort callbacks. This is important as the callback functions may have captured
    // variables that are depending on the allocated memory. 
    CommitTasks.Reset();

    Undo();
	AbortTasks.ForEachBackward([&](TFunction<void()>& Task) -> bool 
    { 
        // Call and then reset each of the tasks in reverse order.
        // This ensures that the task and its destructor are called in reverse chronological order,
        // which is important if the function has captures with non-trivial destructors.
        Task();
        Task.Reset();
        return true; 
    });

    if (IsNested())
    {
		ASSERT(Parent);
    }
    else
    {
		ASSERT(Context->IsAborting());
    }

    Reset();
}

void FTransaction::AbortAndThrow()
{
    AbortWithoutThrowing();
	Context->Throw();
}

bool FTransaction::AttemptToCommit()
{
    ASSERT(Context->GetStatus() == EContextStatus::Committing);
    ASSERT(Context->GetCurrentTransaction() == this);

    Stats.Collect<EStatsKind::Commit>();
    CollectStats();

    bool bResult;
    if (IsNested())
    {
        CommitNested();
        bResult = true;
    }
    else
    {
        bResult = AttemptToCommitOuterNest();
    }
    Reset();
    return bResult;
}

void FTransaction::Undo()
{
	UE_LOG(LogAutoRTFM, Verbose, TEXT("Undoing a transaction..."));

	int VerboseCounter = 0;
	int Num = WriteLog.Num();
	for(auto Iter = WriteLog.rbegin(); Iter != WriteLog.rend(); ++Iter)
    {
		FWriteLogEntry& Entry = *Iter;
		void* const Original = Entry.GetOriginal();

        // No write records should be within the transaction's stack range.
        ensure(!IsOnStack(Original));

		const size_t Size = Entry.GetSize();
        void* const Copy = Entry.GetCopy();

		if (UE_LOG_ACTIVE(LogAutoRTFM, Verbose))
		{
			TStringBuilder<1024> Builder;

			Builder.Appendf(TEXT("%4d [UNDO] %p %4llu : [ "), Num - VerboseCounter - 1, Original, Size);

			unsigned char* Current = (unsigned char*)Original;
			unsigned char* Old = (unsigned char*)Copy;

			for (size_t i = 0; i < Size; i++)
			{
				Builder.Appendf(TEXT("%02X "), Current[i]);
			}

			Builder << TEXT("] -> [ ");

			for (size_t i = 0; i < Size; i++)
			{
				Builder.Appendf(TEXT("%02X "), Old[i]);
			}

			Builder << TEXT("]");

			UE_LOG(LogAutoRTFM, Verbose, TEXT("%s"), Builder.ToString());
		}

        memcpy(Original, Copy, Size);

		VerboseCounter++;
    }

	UE_LOG(LogAutoRTFM, Verbose, TEXT("Undone a transaction!"));
}

void FTransaction::CommitNested()
{
    ASSERT(Parent);

	// We need to pass our write log to our parent transaction, but with care!
	// We need to discard any writes if the memory location is on the parent
	// transaction's stack range.
	for (FWriteLogEntry& Write : WriteLog)
	{
		if (Parent->IsOnStack(Write.GetOriginal()))
		{
			continue;
		}

		Parent->WriteLog.Push(Write);

		FHitSet::Key HitSetEntry(Write.GetOriginal());
		HitSetEntry.SetTopTag(static_cast<uint16_t>(Write.GetSize()));

        Parent->HitSet.Insert(HitSetEntry);
    }

    Parent->WriteLogBumpAllocator.Merge(MoveTemp(WriteLogBumpAllocator));

    Parent->CommitTasks.AddAll(MoveTemp(CommitTasks));
    Parent->AbortTasks.AddAll(MoveTemp(AbortTasks));

    Parent->NewMemoryTracker.Merge(NewMemoryTracker);
}

bool FTransaction::AttemptToCommitOuterNest()
{
    ASSERT(!Parent);

	UE_LOG(LogAutoRTFM, Verbose, TEXT("About to run commit tasks!"));
	Context->DumpState();
	UE_LOG(LogAutoRTFM, Verbose, TEXT("Running commit tasks..."));

    AbortTasks.Reset();

    CommitTasks.ForEachForward([] (TFunction<void()>& Task) -> bool
    { 
        Task(); 
        Task.Reset();
        return true; 
    });

    return true;
}

void FTransaction::Reset()
{
    CommitTasks.Reset();
    AbortTasks.Reset();
	HitSet.Reset();
    NewMemoryTracker.Reset();
	WriteLog.Reset();
	WriteLogBumpAllocator.Reset();
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
