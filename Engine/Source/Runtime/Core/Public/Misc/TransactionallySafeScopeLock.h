// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TransactionallySafeCriticalSection.h"

// A transactionally safe scope lock - that uses a transactionally safe critical section to back it.
struct FTransactionallySafeScopeLock final
{
	UE_NODISCARD_CTOR FTransactionallySafeScopeLock(FTransactionallySafeCriticalSection* InSynchObject)
		: SynchObject(InSynchObject)
	{
		check(SynchObject);
		SynchObject->Lock();
	}

	~FTransactionallySafeScopeLock()
	{
		Unlock();
	}

	void Unlock()
	{
		if (SynchObject)
		{
			SynchObject->Unlock();
			SynchObject = nullptr;
		}
	}

private:
	FTransactionallySafeScopeLock() = delete;
	FTransactionallySafeScopeLock(const FTransactionallySafeScopeLock&) = delete;

	FTransactionallySafeCriticalSection* SynchObject;
};
