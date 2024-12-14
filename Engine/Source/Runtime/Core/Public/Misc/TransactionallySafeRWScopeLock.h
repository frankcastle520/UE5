// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TransactionallySafeRWLock.h"
#include "ScopeRWLock.h"

// A transactionally safe read scope lock - that uses a transactionally safe read-write lock to back it.
class FTransactionallySafeReadScopeLock
{
public:
	UE_NODISCARD_CTOR explicit FTransactionallySafeReadScopeLock(FTransactionallySafeRWLock& InLock)
		: Lock(InLock)
	{
		Lock.ReadLock();
	}

	~FTransactionallySafeReadScopeLock()
	{
		Lock.ReadUnlock();
	}

private:
	UE_NONCOPYABLE(FTransactionallySafeReadScopeLock);

	FTransactionallySafeRWLock& Lock;
};

// A transactionally safe write scope lock - that uses a transactionally safe read-write lock to back it.
class FTransactionallySafeWriteScopeLock
{
public:
	UE_NODISCARD_CTOR explicit FTransactionallySafeWriteScopeLock(FTransactionallySafeRWLock& InLock)
		: Lock(InLock)
	{
		Lock.WriteLock();
	}

	~FTransactionallySafeWriteScopeLock()
	{
		Lock.WriteUnlock();
	}

private:
	UE_NONCOPYABLE(FTransactionallySafeWriteScopeLock);

	FTransactionallySafeRWLock& Lock;
};

// A transactionally safe read-write scope lock - that uses a transactionally safe read-write lock to back it.
struct FTransactionallySafeRWScopeLock final
{
	UE_NODISCARD_CTOR explicit FTransactionallySafeRWScopeLock(FTransactionallySafeRWLock& InLockObject, FRWScopeLockType InLockType)
		: LockObject(InLockObject), LockType(InLockType)
	{
		if (LockType != SLT_ReadOnly)
		{
			LockObject.WriteLock();
		}
		else
		{
			LockObject.ReadLock();
		}
	}

	~FTransactionallySafeRWScopeLock()
	{
		if (LockType == SLT_ReadOnly)
		{
			LockObject.ReadUnlock();
		}
		else
		{
			LockObject.WriteUnlock();
		}
	}

private:
	UE_NONCOPYABLE(FTransactionallySafeRWScopeLock);

	FTransactionallySafeRWLock& LockObject;
	FRWScopeLockType LockType;
};
