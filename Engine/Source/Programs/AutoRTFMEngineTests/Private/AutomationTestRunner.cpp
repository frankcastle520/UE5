// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutomationTestRunner.h"

#if WITH_AUTOMATION_WORKER

namespace UE::AutoRTFM
{

FAutomationTestRunner::FAutomationTestRunner()
{
}

bool
FAutomationTestRunner::RunTests(const TCHAR* TestFilter)
{
	constexpr int32 ExpectedTestCount = 2048;

	TArray<FAutomationTestInfo> TestInfos;
	TestInfos.Empty(ExpectedTestCount);

	FAutomationTestFramework& TestFramework = FAutomationTestFramework::Get();
	TestFramework.SetRequestedTestFilter(EAutomationTestFlags::SmokeFilter | EAutomationTestFlags::EngineFilter);
	TestFramework.GetValidTestNames(TestInfos);
	int TestCount = TestInfos.Num();

	if (TestCount <= 0)
	{
		return true;
	}

	// Stack walking doesn't work properly on Windows when omitting frame pointers. See WindowsPlatformStackWalk.cpp.
	const bool bCaptureStack = TestFramework.GetCaptureStack();
	TestFramework.SetCaptureStack(false);

	bool AllPassed = true;

	const double TestStartTime = FPlatformTime::Seconds();

	for (int TestIt = 0; TestIt != TestCount; ++TestIt)
	{
		const FAutomationTestInfo& TestInfo = TestInfos[TestIt];
		const FString& TestFullPath = TestInfo.GetFullTestPath();

		if (!TestFullPath.Contains("AutoRTFM"))
		{
			continue;
		}

		if (TestFilter && !TestFullPath.Contains(TestFilter))
		{
			continue;
		}

		constexpr int32 RoleIndex = 0;
		TestFramework.StartTestByName(TestInfo.GetTestName(), RoleIndex);
		FAutomationTestExecutionInfo ExecutionInfo;

		if (!TestFramework.StopTest(ExecutionInfo))
		{
			for (const FAutomationExecutionEntry& Entry : ExecutionInfo.GetEntries())
			{
				UE_LOG(LogAutoRTFMEngineTests, Display, TEXT("%s"), *Entry.Event.Message);
			}

			AllPassed = false;
		}
	}

	const double TestEndTime = FPlatformTime::Seconds();

	const double TestTime = TestEndTime - TestStartTime;
	UE_LOG(LogAutoRTFMEngineTests, Display, TEXT("Tests took %.3lf seconds to execute"), TestTime);

	TestFramework.SetCaptureStack(bCaptureStack);

	return AllPassed;
}

}

#endif
