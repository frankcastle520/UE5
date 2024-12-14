// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM/AutoRTFM.h"
#include "ContextStatus.h"
#include "HAL/PlatformTLS.h"
#include "StackRange.h"

namespace AutoRTFM
{

class FLineLock;
class FTransaction;
class FCallNest;

class FContext
{
public:
    static FContext* Get();
    static bool IsTransactional();
	static bool IsCommittingOrAborting();
    
    // This is public API
    ETransactionResult Transact(void (*InstrumentedFunction)(void*), void* Arg);
    
	EContextStatus CallClosedNest(void (*ClosedFunction)(void* Arg), void* Arg);

	void AbortByRequestAndThrow();
	void AbortByRequestWithoutThrowing();

	// Open API - no throw
	bool StartTransaction();

	ETransactionResult CommitTransaction();
	ETransactionResult AbortTransaction(bool bIsClosed, bool bIsCascading);
	void ClearTransactionStatus();
	bool IsAborting() const;

    // Record that a write is about to occur at the given LogicalAddress of Size bytes.
    void RecordWrite(void* LogicalAddress, size_t Size);
    template<unsigned SIZE> void RecordWrite(void* LogicalAddress);

    void DidAllocate(void* LogicalAddress, size_t Size);
    void DidFree(void* LogicalAddress);

    // The rest of this is internalish.
    [[noreturn]] void AbortByLanguageAndThrow();

	inline FTransaction* GetCurrentTransaction() const { return CurrentTransaction; }
	inline FCallNest* GetCurrentNest() const { return CurrentNest; }
	inline EContextStatus GetStatus() const { return CurrentThreadId == FPlatformTLS::GetCurrentThreadId() ? Status : EContextStatus::Idle; }
	[[noreturn]] void Throw();

	// Returns the starting stack address of the innermost call to Closed(), or
	// nullptr if there is no call to Closed. Used to assert that a stack memory
	// write is safe to record.
	// See FTransaction::ShouldRecordWrite()
	inline const void* GetClosedStackAddress() const { return ClosedStackAddress; }

    void DumpState() const;

    static void InitializeGlobalData();

private:
	static FContext ContextSingleton;

	FContext() { Reset(); }
    FContext(const FContext&) = delete;

	void PushCallNest(FCallNest* NewCallNest);
	void PopCallNest();

	void PushTransaction(FTransaction* NewTransaction);
	void PopTransaction();

	ETransactionResult ResolveNestedTransaction(FTransaction* NewTransaction);
	bool AttemptToCommitTransaction(FTransaction* const Transaction);
    
    // All of this other stuff ought to be private?
    void Reset();
    
    FTransaction* CurrentTransaction{nullptr};
	FCallNest* CurrentNest{nullptr};

    FStackRange Stack;
	void* ClosedStackAddress = nullptr;
    EContextStatus Status{EContextStatus::Idle};
	uint32 CurrentThreadId{ FPlatformTLS::InvalidTlsSlot };
};

} // namespace AutoRTFM
