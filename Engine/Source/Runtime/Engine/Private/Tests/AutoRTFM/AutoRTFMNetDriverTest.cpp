// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "AutoRTFM/AutoRTFM.h"
#include "AutoRTFMTestActor.h"
#include "Engine/ActorChannel.h"
#include "Engine/DemoNetDriver.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TEST_CHECK_TRUE(b) do \
{ \
	if (!(b)) \
	{ \
		FString String(FString::Printf(TEXT("FAILED: %s:%u"), TEXT(__FILE__), __LINE__)); \
		ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Info, String)); \
		return false; \
	} \
} while(false)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutoRTFMNetDriverTests, "AutoRTFM + FTraceFilter", EAutomationTestFlags::EngineFilter | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext)

bool FAutoRTFMNetDriverTests::RunTest(const FString & Parameters)
{
	if (!AutoRTFM::ForTheRuntime::IsAutoRTFMRuntimeEnabled())
	{
		ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Info, TEXT("SKIPPED 'FAutoRTFMNetDriverTests' test. AutoRTFM disabled.")));
		return true;
	}

	UNetDriver* const Driver = NewObject<UDemoNetDriver>();

	UNetConnection* const Connection = NewObject<UDemoNetConnection>();
	Connection->Driver = Driver;
	Driver->AddClientConnection(Connection);

	UActorChannel* const ActorChannel = NewObject<UActorChannel>();
	ActorChannel->OpenedLocally = true;
	ActorChannel->Connection = Connection;
	Connection->Channels.Add(ActorChannel);

	FString Description;

	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Description = ActorChannel->Describe();
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	TEST_CHECK_TRUE(Description.IsEmpty());

	Result = AutoRTFM::Transact([&]
		{
			Description = ActorChannel->Describe();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::Committed == Result);
	TEST_CHECK_TRUE(!Description.IsEmpty());

	return true;
}

#undef TEST_CHECK_TRUE

#endif //WITH_DEV_AUTOMATION_TESTS
