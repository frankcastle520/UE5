// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "AutoRTFM/AutoRTFM.h"
#include "GameplayTagsManager.h"
#include "GameplayTagContainer.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutoRTFMGameplayTagTests, "AutoRTFM + FGameplayTag", EAutomationTestFlags::EngineFilter | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext)

bool FAutoRTFMGameplayTagTests::RunTest(const FString & Parameters)
{
	if (!AutoRTFM::ForTheRuntime::IsAutoRTFMRuntimeEnabled())
	{
		ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Info, TEXT("SKIPPED 'FAutoRTFMGameplayTagTests' test. AutoRTFM disabled.")));
		return true;
	}

	FGameplayTag Tag;
	FGameplayTag Other;

	bool bResult = true;

	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			bResult = Tag.MatchesTag(Other);
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::Committed == Result);
	TEST_CHECK_TRUE(!bResult);

	TArray<FGameplayTag> Parents;

	Result = AutoRTFM::Transact([&]
		{
			UGameplayTagsManager::Get().ExtractParentTags(Tag, Parents);
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::Committed == Result);
	TEST_CHECK_TRUE(Parents.IsEmpty());

	Result = AutoRTFM::Transact([&]
		{
			bResult = UGameplayTagsManager::Get().RequestGameplayTagParents(Tag).IsEmpty();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::Committed == Result);
	TEST_CHECK_TRUE(bResult);
	
	return true;
}

#undef TEST_CHECK_TRUE

#endif //WITH_DEV_AUTOMATION_TESTS
