// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsyncLoadingTests_Shared.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Async/ManualResetEvent.h"
#include "UObject/CoreRedirects.h"
#include "UObject/SavePackage.h"
#include "Tasks/Task.h"

#if WITH_DEV_AUTOMATION_TESTS

// All Flush tests should run on zenloader only as the other loaders are not compliant.
typedef FLoadingTests_ZenLoaderOnly_Base FLoadingTests_Flush_Base;

/**
 * This test validates loading an object synchronously during serialize.
 */
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(
	FLoadingTests_Flush_InvalidFromWorker,
	FLoadingTests_Flush_Base,
	TEXT("System.Engine.Loading.Flush.InvalidFromWorker"),
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)
bool FLoadingTests_Flush_InvalidFromWorker::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("is unable to FlushAsyncLoading from the current thread"), EAutomationExpectedErrorFlags::Contains);
	AddExpectedError(TEXT("[Callstack]"), EAutomationExpectedErrorFlags::Contains, 0 /* At least 1 occurrence */);
	
	FLoadingTestsScope LoadingTestScope(this);

	UAsyncLoadingTests_Shared::OnSerialize.BindLambda(
		[this](FArchive& Ar, UAsyncLoadingTests_Shared* Object)
		{
			if (Ar.IsLoading())
			{
				// Use event instead of waiting on the task to prevent retraction as we really want that task
				// to execute on a worker thread instead of being retracted in the serialize thread.
				UE::FManualResetEvent Event;
				UE::Tasks::Launch(TEXT("FlushAsyncLoading"), [&Event]() { FlushAsyncLoading(); Event.Notify(); });
				Event.Wait();
			}
		}
	);

	LoadingTestScope.LoadObjects();

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(
	FLoadingTests_Flush_ValidFromCallback,
	FLoadingTests_Flush_Base,
	TEXT("System.Engine.Loading.Flush.ValidFromCallback"),
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)
bool FLoadingTests_Flush_ValidFromCallback::RunTest(const FString& Parameters)
{
	FLoadingTestsScope LoadingTestScope(this);

	LoadPackageAsync(FLoadingTestsScope::PackagePath1,
		FLoadPackageAsyncDelegate::CreateLambda([](const FName&, UPackage*, EAsyncLoadingResult::Type) { FlushAsyncLoading(); }));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
