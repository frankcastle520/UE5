// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)
#include "Context.h"
#include "CallNestInlines.h"
#include "ContextInlines.h"
#include "FunctionMap.h"
#include "GlobalData.h"
#include "ScopedGuard.h"
#include "Stats.h"
#include "TransactionInlines.h"
#include "AutoRTFM/AutoRTFMMetrics.h"

#include "Templates/UniquePtr.h"
#include "Containers/StringConv.h"
#include "HAL/PlatformTLS.h"

#if PLATFORM_WINDOWS
extern "C" __declspec(dllimport) void __stdcall GetCurrentThreadStackLimits(void**, void**);
#endif

namespace
{
AutoRTFM::FAutoRTFMMetrics GAutoRTFMMetrics;
}

namespace AutoRTFM
{

void ResetAutoRTFMMetrics()
{
	GAutoRTFMMetrics = FAutoRTFMMetrics{};
}

// get a snapshot of the current internal metrics
FAutoRTFMMetrics GetAutoRTFMMetrics()
{
	return GAutoRTFMMetrics;
}

void FContext::InitializeGlobalData()
{
}

FContext FContext::ContextSingleton;

bool FContext::IsTransactional()
{
    return Get()->GetStatus() == EContextStatus::OnTrack;
}

bool FContext::IsCommittingOrAborting()
{
	switch (Get()->GetStatus())
	{
	default:
		return true;
	case EContextStatus::Idle:
	case EContextStatus::OnTrack:
		return false;
	}
}

bool FContext::StartTransaction()
{
	ensureMsgf(CurrentTransaction, TEXT("FContext::StartTransaction() can only be called within a scoped transaction"));
	FTransaction* NewTransaction = new FTransaction(this);
	NewTransaction->SetStackRange(CurrentTransaction->GetStackRange());

	// This form of transaction is always ultimately within a scoped Transact 
	ASSERT(Status == EContextStatus::OnTrack);
	PushTransaction(NewTransaction);

	GAutoRTFMMetrics.NumTransactionsStarted++;

	return true;
}

ETransactionResult FContext::CommitTransaction()
{
	constexpr bool bVerbose = false;

	ASSERT(Status == EContextStatus::OnTrack);

	// Scoped transactions commit on return, so committing explicitly isn't allowed
	ASSERT(CurrentTransaction->IsScopedTransaction() == false);

	ETransactionResult Result = ETransactionResult::Committed;

	if (CurrentTransaction->IsNested())
	{
		Result = ResolveNestedTransaction(CurrentTransaction);
	}
	else
	{
		UE_LOG(LogAutoRTFM, Verbose, TEXT("About to commit; my state is:"));
		DumpState();
		UE_LOG(LogAutoRTFM, Verbose, TEXT("Committing..."));

		if (AttemptToCommitTransaction(CurrentTransaction))
		{
			Result = ETransactionResult::Committed;
		}
		else
		{
			UE_LOG(LogAutoRTFM, Verbose, TEXT("Commit failed!"));
			ASSERT(Status != EContextStatus::OnTrack);
			ASSERT(Status != EContextStatus::Idle);
		}
	}

	// Parent transaction is now the current transaction
	PopTransaction();

	GAutoRTFMMetrics.NumTransactionsCommitted++;

	return Result;
}

ETransactionResult FContext::AbortTransaction(bool bIsClosed, bool bIsCascading)
{
	GAutoRTFMMetrics.NumTransactionsAborted++;

	ETransactionResult Result = ETransactionResult::AbortedByRequest;
	ASSERT(Status == EContextStatus::OnTrack);
	Status = bIsCascading ? EContextStatus::AbortedByCascade : EContextStatus::AbortedByRequest;

	ASSERT(nullptr != CurrentTransaction);

	// Sort out how aborts work
	CurrentTransaction->AbortWithoutThrowing();

	// Non-scoped transactions are ended immediately, but scoped need to get to the end scope before being popped
	if (!CurrentTransaction->IsScopedTransaction())
	{
		Result = ResolveNestedTransaction(CurrentTransaction);
		PopTransaction();
	}
	
	if (bIsClosed)
	{
		Throw();
	}

	return Result;
}

bool FContext::IsAborting() const
{
	switch (Status)
	{
	default:
		return true;
	case EContextStatus::OnTrack:
	case EContextStatus::Idle:
	case EContextStatus::Committing:
		return false;
	}
}

EContextStatus FContext::CallClosedNest(void (*ClosedFunction)(void* Arg), void* Arg)
{
	TScopedGuard<void*> ClosedStackAddressGuard(ClosedStackAddress, &ClosedStackAddressGuard);
	PushCallNest(new FCallNest(this));

	CurrentNest->Try([&]() { ClosedFunction(Arg); });

	PopCallNest();

	return GetStatus();
}

void FContext::PushCallNest(FCallNest* NewCallNest)
{
	ASSERT(NewCallNest != nullptr);
	ASSERT(NewCallNest->Parent == nullptr);

	NewCallNest->Parent = CurrentNest;
	CurrentNest = NewCallNest;
}

void FContext::PopCallNest()
{
	ASSERT(CurrentNest != nullptr);
	FCallNest* OldCallNest = CurrentNest;
	CurrentNest = CurrentNest->Parent;

	delete OldCallNest;
}

void FContext::PushTransaction(FTransaction* NewTransaction)
{
	ASSERT(NewTransaction != nullptr);
	ASSERT(!NewTransaction->IsDone());
	ASSERT(NewTransaction->GetParent() == nullptr);

	ASSERT(CurrentTransaction == nullptr || !CurrentTransaction->IsDone());
	
	NewTransaction->SetParent(CurrentTransaction);
	CurrentTransaction = NewTransaction;

	// Collect stats that we've got a new transaction.
	Stats.Collect<EStatsKind::Transaction>();
}

void FContext::PopTransaction()
{
	ASSERT(CurrentTransaction != nullptr);
	ASSERT(CurrentTransaction->IsDone());
	FTransaction* OldTransaction = CurrentTransaction;
	CurrentTransaction = CurrentTransaction->GetParent();
	delete OldTransaction;
}

void FContext::ClearTransactionStatus()
{
	switch (Status)
	{
	case EContextStatus::OnTrack:
		break;
	case EContextStatus::AbortedByLanguage:
	case EContextStatus::AbortedByRequest:
	case EContextStatus::AbortedByCascade:
	case EContextStatus::AbortedByFailedLockAcquisition:
		Status = EContextStatus::OnTrack;
		break;
	default:
		AutoRTFM::Unreachable();
	}
}

ETransactionResult FContext::ResolveNestedTransaction(FTransaction* NewTransaction)
{
	// We just use this bit to help assertions for now (though we could use it more strongly). Because of how we use this right now,
	// it's OK that it's set before we commit but after we abort.
	ASSERT(!NewTransaction->IsDone());
	NewTransaction->SetIsDone();

	if (Status == EContextStatus::OnTrack)
	{
		bool bCommitResult = AttemptToCommitTransaction(NewTransaction);
		ASSERT(bCommitResult);
		ASSERT(Status == EContextStatus::OnTrack);
		return ETransactionResult::Committed;
	}

	switch (Status)
	{
	case EContextStatus::AbortedByRequest:
		return ETransactionResult::AbortedByRequest;
	case EContextStatus::AbortedByLanguage:
		return ETransactionResult::AbortedByLanguage;
	case EContextStatus::AbortedByCascade:
		return ETransactionResult::AbortedByCascade;
	default:
		AutoRTFM::Unreachable();
	}
}

ETransactionResult FContext::Transact(void (*InstrumentedFunction)(void*), void* Arg)
{
    constexpr bool bVerbose = false;

	if constexpr (0 != UE_AUTOSTM)
	{
		UE_LOG(LogAutoRTFM, Warning, TEXT("AutoSTM is not implemented in the runtime yet!"));
		return ETransactionResult::AbortedByLanguage;
	}

    if (UNLIKELY(EContextStatus::Committing == Status))
    {
    	return ETransactionResult::AbortedByTransactInOnCommit;
    }

    if (UNLIKELY(IsAborting()))
    {
    	return ETransactionResult::AbortedByTransactInOnAbort;
    }
    
    ASSERT(Status == EContextStatus::Idle || Status == EContextStatus::OnTrack);

    if (!InstrumentedFunction)
    {
		UE_LOG(LogAutoRTFM, Warning, TEXT("Could not find function in AutoRTFM::FContext::Transact."));
        return ETransactionResult::AbortedByLanguage;
    }
    
	FTransaction* NewTransaction = new FTransaction(this);
	FCallNest* NewNest = new FCallNest(this);

	// Transact requires a return from the lambda to commit the results
	NewTransaction->SetIsScopedTransaction();

	void* TransactStackStart = &NewTransaction;

	ETransactionResult Result = ETransactionResult::Committed; // Initialize to something to make the compiler happy.

    if (!CurrentTransaction)
    {
        ASSERT(Status == EContextStatus::Idle);

		ASSERT(FPlatformTLS::InvalidTlsSlot == CurrentThreadId);
		CurrentThreadId = FPlatformTLS::GetCurrentThreadId();

		ASSERT(Stack == FStackRange{});

#if PLATFORM_WINDOWS
		GetCurrentThreadStackLimits(&Stack.Low, &Stack.High);
#elif defined(__APPLE__)         
		Stack.High = pthread_get_stackaddr_np(pthread_self());
		size_t StackSize = pthread_get_stacksize_np(pthread_self());
		StackLow = static_cast<char*>(Stack.High) - StackSize;
#else
		pthread_attr_t Attr;
		pthread_getattr_np(pthread_self(), &Attr);
		size_t StackSize = 0;
		pthread_attr_getstack(&Attr, &Stack.Low, &StackSize);
		Stack.High = static_cast<char*>(Stack.Low) + StackSize;
#endif
		ASSERT(Stack.High > Stack.Low);

		ASSERT(Stack.Contains(TransactStackStart));
		NewTransaction->SetStackRange({Stack.Low, &TransactStackStart});

		PushTransaction(NewTransaction);
		PushCallNest(NewNest);

		bool bTriedToRunOnce = false;

        for (;;)
        {
            Status = EContextStatus::OnTrack;
            ASSERT(CurrentTransaction->IsFresh());
			CurrentNest->Try([&] () { InstrumentedFunction(Arg); });
			ASSERT(CurrentTransaction == NewTransaction); // The transaction lambda should have unwound any nested transactions.
            ASSERT(Status != EContextStatus::Idle);

            if (Status == EContextStatus::OnTrack)
            {
				UE_LOG(LogAutoRTFM, Verbose, TEXT("About to commit; my state is:"));
				DumpState();
				UE_LOG(LogAutoRTFM, Verbose, TEXT("Committing..."));

				if (UNLIKELY(!bTriedToRunOnce && AutoRTFM::ForTheRuntime::ShouldRetryNonNestedTransactions()))
				{
					// We skip trying to commit this time, and instead re-run the transaction.
					Status = EContextStatus::AbortedByFailedLockAcquisition;
					CurrentTransaction->AbortWithoutThrowing();
					ClearTransactionStatus();

					// We've tried to run at least once if we get here!
					bTriedToRunOnce = true;
					continue;
				}

				if (AttemptToCommitTransaction(CurrentTransaction))
				{
					Result = ETransactionResult::Committed;
					break;
				}

				UE_LOG(LogAutoRTFM, Verbose, TEXT("Commit failed!"));

                ASSERT(Status != EContextStatus::OnTrack);
                ASSERT(Status != EContextStatus::Idle);
            }

            if (Status == EContextStatus::AbortedByRequest)
            {
                Result = ETransactionResult::AbortedByRequest;
                break;
            }

            if (Status == EContextStatus::AbortedByLanguage)
            {
                Result = ETransactionResult::AbortedByLanguage;
                break;
            }

            if (Status == EContextStatus::AbortedByCascade)
            {
                Result = ETransactionResult::AbortedByCascade;
                break;
            }

            ASSERT(Status == EContextStatus::AbortedByFailedLockAcquisition);
        }

		NewTransaction->SetIsDone();

		PopCallNest();
		PopTransaction();
		ClearTransactionStatus();

		ASSERT(CurrentNest == nullptr);
		ASSERT(CurrentTransaction == nullptr);

        Reset();
	}
    else
    {
		// This transaction is within another transaction
		ASSERT(Status == EContextStatus::OnTrack);

		ASSERT(CurrentThreadId == FPlatformTLS::GetCurrentThreadId());

		ASSERT(Stack.Contains(TransactStackStart));
		NewTransaction->SetStackRange({Stack.Low, &TransactStackStart});

		PushTransaction(NewTransaction);
		PushCallNest(NewNest);

		bool bTriedToRunOnce = false;

		for (;;)
		{
			CurrentNest->Try([&]() { InstrumentedFunction(Arg); });
			ASSERT(CurrentTransaction == NewTransaction);

			if (Status == EContextStatus::OnTrack)
			{
				if (UNLIKELY(!bTriedToRunOnce && AutoRTFM::ForTheRuntime::ShouldRetryNestedTransactionsToo()))
				{
					// We skip trying to commit this time, and instead re-run the transaction.
					Status = EContextStatus::AbortedByFailedLockAcquisition;
					NewTransaction->AbortWithoutThrowing();
					ClearTransactionStatus();

					// We've tried to run at least once if we get here!
					bTriedToRunOnce = true;

					continue;
				}
			}

			Result = ResolveNestedTransaction(NewTransaction);
			break;
		}

		PopCallNest();
		PopTransaction();

		ASSERT(CurrentNest != nullptr);
		ASSERT(CurrentTransaction != nullptr);

		// Cascading aborts should cause all transactions to abort!
		switch (Result)
		{
		default:
			break;
		case ETransactionResult::AbortedByCascade:
			CurrentTransaction->AbortAndThrow();
			break;
		}

		ClearTransactionStatus();
	}

	return Result;
}

void FContext::AbortByRequestAndThrow()
{
    ASSERT(Status == EContextStatus::OnTrack);
	GAutoRTFMMetrics.NumTransactionsAbortedByRequest++;
    Status = EContextStatus::AbortedByRequest;
    CurrentTransaction->AbortAndThrow();
}

void FContext::AbortByRequestWithoutThrowing()
{
	ASSERT(Status == EContextStatus::OnTrack);
	GAutoRTFMMetrics.NumTransactionsAbortedByRequest++;
	Status = EContextStatus::AbortedByRequest;
	CurrentTransaction->AbortWithoutThrowing();
}

void FContext::AbortByLanguageAndThrow()
{
    ASSERT(Status == EContextStatus::OnTrack);
	GAutoRTFMMetrics.NumTransactionsAbortedByLanguage++;
    Status = EContextStatus::AbortedByLanguage;
    CurrentTransaction->AbortAndThrow();
}

void FContext::Reset()
{
	ASSERT(CurrentThreadId == FPlatformTLS::GetCurrentThreadId() || CurrentThreadId == FPlatformTLS::InvalidTlsSlot);

	CurrentThreadId = FPlatformTLS::InvalidTlsSlot;
	Stack = {};
    CurrentTransaction = nullptr;
	CurrentNest = nullptr;
    Status = EContextStatus::Idle;
}

void FContext::Throw()
{
	GetCurrentNest()->AbortJump.Throw();
}

void FContext::DumpState() const
{
	UE_LOG(LogAutoRTFM, Verbose, TEXT("Context at %p"), this);
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
