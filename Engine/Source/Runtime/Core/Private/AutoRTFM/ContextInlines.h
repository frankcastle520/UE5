// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Context.h"
#include "TransactionInlines.h"

namespace AutoRTFM
{

UE_AUTORTFM_FORCEINLINE FContext* FContext::Get()
{
	return &FContext::ContextSingleton;
}

AUTORTFM_NO_ASAN UE_AUTORTFM_FORCEINLINE void FContext::RecordWrite(void* LogicalAddress, size_t Size)
{
    CurrentTransaction->RecordWrite(LogicalAddress, Size);
}

template<unsigned SIZE> AUTORTFM_NO_ASAN UE_AUTORTFM_FORCEINLINE void FContext::RecordWrite(void* LogicalAddress)
{
    CurrentTransaction->RecordWrite<SIZE>(LogicalAddress);
}

UE_AUTORTFM_FORCEINLINE void FContext::DidAllocate(void* LogicalAddress, size_t Size)
{
    CurrentTransaction->DidAllocate(LogicalAddress, Size);
}

UE_AUTORTFM_FORCEINLINE void FContext::DidFree(void* LogicalAddress)
{
    // We can do free's in the open within a transaction *during* when the
    // transaction itself is being destroyed, so we need to check for that case.
	if (UNLIKELY(!CurrentTransaction))
	{
		return;
	}

    CurrentTransaction->DidFree(LogicalAddress);
}

UE_AUTORTFM_FORCEINLINE bool FContext::AttemptToCommitTransaction(FTransaction* const Transaction)
{
    ASSERT(EContextStatus::OnTrack == Status);

    Status = EContextStatus::Committing;

    const bool bResult = Transaction->AttemptToCommit();

    if (bResult)
    {
        Status = EContextStatus::OnTrack;    
    }

    return bResult;
}

} // namespace AutoRTFM
