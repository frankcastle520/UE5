// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"

/** Latent Command that waits until the Query evaluates to `true` or the timeout has exceeded. */
class CQTEST_API FWaitUntil : public IAutomationLatentCommand
{
public:
	FWaitUntil(FAutomationTestBase& InTestRunner, TFunction<bool()> Query, FTimespan Timeout = FTimespan::FromSeconds(10), const TCHAR* InDescription = nullptr)
		: TestRunner(InTestRunner)
		, Query(MoveTemp(Query)) 
		, Timeout(Timeout)
		, Description(InDescription)
	{}

	bool Update() override;

	FAutomationTestBase& TestRunner;
	TFunction<bool()> Query;
	FTimespan Timeout;
	FDateTime StartTime;
	const TCHAR* Description;
	bool bHasTimerStarted = false;
};

/** Latent Command that waits a set time frame.
 * Note that using a timed-wait can introduce test flakiness due to variable runtimes. Please consider using `FWaitUntil` and waiting until something happens instead.
 */
class CQTEST_API FWaitDelay : public IAutomationLatentCommand
{
public:
	FWaitDelay(FAutomationTestBase& InTestRunner, FTimespan Timeout, const TCHAR* InDescription = nullptr)
		: TestRunner(InTestRunner)
		, Timeout(Timeout)
		, Description(InDescription)
	{}

	bool Update() override;

	FAutomationTestBase& TestRunner;
	FTimespan Timeout;
	FDateTime EndTime;
	const TCHAR* Description;
	bool bHasTimerStarted = false;
};

enum class ECQTestFailureBehavior
{
	Skip,
	Run
};

/** Latent Command which executes the provided function. */
class CQTEST_API FExecute : public IAutomationLatentCommand
{
public:
	FExecute(FAutomationTestBase& InTestRunner, TFunction<void()> Func, const TCHAR* InDescription = nullptr, ECQTestFailureBehavior InFailureBehavior = ECQTestFailureBehavior::Skip)
		: TestRunner(InTestRunner)
		, Func(MoveTemp(Func)) 
		, Description(InDescription)
		, FailureBehavior(InFailureBehavior)
	{}

	bool Update() override;

	FAutomationTestBase& TestRunner;
	TFunction<void()> Func;
	const TCHAR* Description = nullptr;
	ECQTestFailureBehavior FailureBehavior;
};

/** Latent Command which manages and executes an array of latent commands. */
class CQTEST_API FRunSequence : public IAutomationLatentCommand
{
public:
	FRunSequence(const TArray<TSharedPtr<IAutomationLatentCommand>>& ToAdd)
		: Commands(ToAdd)
	{
	}

	template <class... Cmds>
	FRunSequence(Cmds... Commands)
		: FRunSequence(TArray<TSharedPtr<IAutomationLatentCommand>>{ Commands... })
	{
	}

	void Append(TSharedPtr<IAutomationLatentCommand> ToAdd);
	void AppendAll(TArray < TSharedPtr<IAutomationLatentCommand>> ToAdd);
	void Prepend(TSharedPtr<IAutomationLatentCommand> ToAdd);

	bool IsEmpty() const
	{
		return Commands.IsEmpty();
	}

	bool Update() override;

	TArray<TSharedPtr<IAutomationLatentCommand>> Commands;
};