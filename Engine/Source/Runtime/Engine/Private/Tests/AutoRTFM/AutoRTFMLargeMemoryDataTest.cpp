// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AutoRTFM/AutoRTFM.h"
#include "Serialization/LargeMemoryData.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutoRTFMLargeMemoryDataTest, "AutoRTFM + FPooledLargeMemoryData", EAutomationTestFlags::EngineFilter | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext)

bool FAutoRTFMLargeMemoryDataTest::RunTest(const FString & Parameters)
{
	if (!AutoRTFM::ForTheRuntime::IsAutoRTFMRuntimeEnabled())
	{
		ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Info, TEXT("SKIPPED 'FAutoRTFMLargeMemoryDataTest' test. AutoRTFM disabled.")));
		return true;
	}

	AutoRTFM::Transact([&]
	{
		FPooledLargeMemoryData Data;
	});

	AutoRTFM::Transact([&]
	{
		FPooledLargeMemoryData Data;
		AutoRTFM::AbortTransaction();
	});

	AutoRTFM::Transact([&]
	{
		{
			FPooledLargeMemoryData Data;
		}
		AutoRTFM::AbortTransaction();
	});

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
