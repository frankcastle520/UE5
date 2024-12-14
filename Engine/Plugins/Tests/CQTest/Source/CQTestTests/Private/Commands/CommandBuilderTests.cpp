// Copyright Epic Games, Inc. All Rights Reserved.

#include "CQTest.h"
#include "CQTestUnitTestHelper.h"

TEST_CLASS(CommandBuilderTests, "TestFramework.CQTest.Core")
{
	FTestCommandBuilder CommandBuilder{*TestRunner};

	TEST_METHOD(Do_ThenBuild_IncludesCommand)
	{
		bool invoked = false;
		auto command = CommandBuilder.Do([&invoked]() { invoked = true; }).Build();

		ASSERT_THAT(IsTrue(command->Update()));
		ASSERT_THAT(IsTrue(invoked));
	}

	TEST_METHOD(Build_WithoutCommands_ReturnsNullptr)
	{
		auto command = CommandBuilder.Build();
		ASSERT_THAT(IsNull(command));
	}

	TEST_METHOD(StartWhen_CreatesWaitUntilCommand)
	{
		bool done = false;
		auto command = CommandBuilder.StartWhen([&done]() { return done; }).Build();

		ASSERT_THAT(IsFalse(command->Update()));
		done = true;
		ASSERT_THAT(IsTrue(command->Update()));
	}

	TEST_METHOD(WaitDelay_WaitsUntilDurationElapsed)
	{
		bool done = false;
		FTimespan Duration = FTimespan::FromMilliseconds(200);
		FDateTime EndTime = FDateTime::UtcNow() + Duration;
		auto command = CommandBuilder
			.WaitDelay(Duration)
			.Then([this, &EndTime, &done]()
			{
				ASSERT_THAT(IsTrue(FDateTime::UtcNow() >= EndTime));
				done = true;
			}).Build();
		
		while (!done)
		{
			command->Update();
		}
		ASSERT_THAT(IsTrue(done));
	}

	TEST_METHOD(WaitDelay_InterruptOnError)
	{
		const FString ExpectedError = TEXT("Error reported outside WaitDelay");

		FTimespan Duration = FTimespan::FromSeconds(10);
		FDateTime EndTime = FDateTime::UtcNow() + Duration;
		auto command = CommandBuilder
			.WaitDelay(Duration).Build();

		ASSERT_THAT(IsFalse(command->Update()));
		AddError(ExpectedError);
		ASSERT_THAT(IsTrue(command->Update()));
		ASSERT_THAT(IsTrue(FDateTime::UtcNow() < EndTime));

		ClearExpectedError(*this->TestRunner, ExpectedError);
	}

	TEST_METHOD(Build_AfterBuild_ReturnsNullptr)
	{
		auto command = CommandBuilder.Do([]() {}).Build();
		auto secondTime = CommandBuilder.Build();

		ASSERT_THAT(IsNotNull(command));
		ASSERT_THAT(IsNull(secondTime));
	}
};