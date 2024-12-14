// Copyright Epic Games, Inc. All Rights Reserved.

#include "Catch2Includes.h"
#include "Misc/ScopeRWLock.h"
#include <AutoRTFM/AutoRTFM.h>
#include <map>
#include <vector>

TEST_CASE("ReadLock")
{
	FRWLock ReadLock;
	int x = 42;

	auto Transaction = AutoRTFM::Transact([&]()
    {
		FReadScopeLock ScopeLock(ReadLock);
		x = 43;
	});

    REQUIRE(x == 43);
	// the read lock should have been released, we verify that by trying to acquire a write lock here
    REQUIRE(ReadLock.TryWriteLock());
	ReadLock.WriteUnlock();
}

TEST_CASE("ReadLockAbort")
{
	FRWLock ReadLock;
	int x = 42;

	auto Transaction = AutoRTFM::Transact([&]()
    {
		FReadScopeLock ScopeLock(ReadLock);
		x = 43;
    	AutoRTFM::AbortTransaction();
	});

    REQUIRE(
        AutoRTFM::ETransactionResult::AbortedByRequest ==
		Transaction);
    REQUIRE(x == 42);
	// the read lock should have been released, we verify that by trying to acquire a write lock here
    REQUIRE(ReadLock.TryWriteLock());
	ReadLock.WriteUnlock();
}
