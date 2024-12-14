// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "AutoRTFM/AutoRTFM.h"
#include "HAL/Platform.h"
#include "Containers/LockFreeFixedSizeAllocator.h"

#include <vector>

#if WITH_DEV_AUTOMATION_TESTS

#define CHECK_EQ(A, B) \
	do { TestEqual(TEXT(__FILE__ ":" UE_STRINGIZE(__LINE__) ": TestEqual(" #A ", " #B ")"), (A), (B)); } while (0)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutoRTFMLockFreeFixedSizeAllocator, "AutoRTFM + LockFreeFixedSizeAllocator", \
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::ClientContext | \
	EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext)

bool FAutoRTFMLockFreeFixedSizeAllocator::RunTest(const FString& Parameters)
{
	if (!AutoRTFM::ForTheRuntime::IsAutoRTFMRuntimeEnabled())
	{
		ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Info,
			TEXT("SKIPPED 'FAutoRTFMLockFreeFixedSizeAllocator' test. AutoRTFM disabled.")));
		return true;
	}

	constexpr int BlockSize = 32;
	using BasicAllocator = TLockFreeFixedSizeAllocator<BlockSize, PLATFORM_CACHE_LINE_SIZE, FThreadSafeCounter>;

	AutoRTFM::Transact([&]
		{
			// It should be safe to instantiate a lock-free fixed-size allocator in a transaction.
			BasicAllocator Allocator;
			CHECK_EQ(Allocator.GetNumUsed().GetValue(), 0);
			CHECK_EQ(Allocator.GetNumFree().GetValue(), 0);

			// It should be safe to allocate from a lock-free fixed-size allocator inside a transaction.
			void* Data = Allocator.Allocate();

			// It should be safe to free the allocated object while still inside a transaction.
			Allocator.Free(Data);
		});

	// It should be safe to instantiate a lock-free fixed-size allocator outside of a transaction, then
	// use it inside a transaction.
	{
		BasicAllocator Allocator;

		// It should be safe to allocate inside a transaction.
		void* Data = nullptr;
		AutoRTFM::Transact([&]
			{
				Data = Allocator.Allocate();
			});
		CHECK_EQ(Allocator.GetNumUsed().GetValue(), 1);
		CHECK_EQ(Allocator.GetNumFree().GetValue(), 0);

		// It should be safe to free data inside a transaction. Items that are freed inside of a transaction
		// are freed immediately, instead of being added to the free-list.
		AutoRTFM::Transact([&]
			{
				Allocator.Free(Data);
			});
		CHECK_EQ(Allocator.GetNumUsed().GetValue(), 0);
		CHECK_EQ(Allocator.GetNumFree().GetValue(), 0);
	}

	// It should be safe to allocate an object inside a transaction, then free it outside of the transaction.
	// These items will be added to the free-list, like normal.
	{
		BasicAllocator Allocator;
		void* Data = nullptr;
		AutoRTFM::Transact([&]
			{
				Data = Allocator.Allocate();
			});
		Allocator.Free(Data);
		CHECK_EQ(Allocator.GetNumUsed().GetValue(), 0);
		CHECK_EQ(Allocator.GetNumFree().GetValue(), 1);
	}

	// It should be safe to allocate an object inside a transaction, then abort the transaction.
	// This should cause the allocation to be rolled back automatically by AutoRTFM, not be placed on the free-list.
	{
		BasicAllocator Allocator;
		AutoRTFM::Transact([&]
			{
				[[maybe_unused]] void* Data = Allocator.Allocate();
				AutoRTFM::AbortTransaction();
			});
		CHECK_EQ(Allocator.GetNumUsed().GetValue(), 0);
		CHECK_EQ(Allocator.GetNumFree().GetValue(), 0);
	}

	// It should be safe to allocate an object inside a transaction, free it, then abort the transaction.
	// This should cause the allocation to be rolled back automatically by AutoRTFM, not be placed on the free-list.
	{
		BasicAllocator Allocator;
		AutoRTFM::Transact([&]
			{
				void* Data = Allocator.Allocate();
				Allocator.Free(Data);
				AutoRTFM::AbortTransaction();
			});
		CHECK_EQ(Allocator.GetNumUsed().GetValue(), 0);
		CHECK_EQ(Allocator.GetNumFree().GetValue(), 0);
	}

	// It should be safe to allocate an object outside of a transaction, free it inside a transaction, and then 
	// abort the transaction. The allocator should return a use-count of one and the object should be unchanged.
	{
		BasicAllocator Allocator;
		void* Data = Allocator.Allocate();
		memset(Data, 0x44, BlockSize);
		AutoRTFM::Transact([&]
			{
				Allocator.Free(Data);
				AutoRTFM::AbortTransaction();
			});
		CHECK_EQ(Allocator.GetNumUsed().GetValue(), 1);
		CHECK_EQ(Allocator.GetNumFree().GetValue(), 0);

		// The bytes inside Data shouldn't have been modified at all.
		std::vector<char> ExpectedData(BlockSize, 0x44);
		CHECK_EQ(memcmp(Data, ExpectedData.data(), BlockSize), 0);

		Allocator.Free(Data);
	}

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
