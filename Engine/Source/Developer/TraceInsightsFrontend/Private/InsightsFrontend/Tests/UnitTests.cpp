// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTraceInsightsFrontendUnitTest, "System.Insights.Frontend.UnitTest", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FTraceInsightsFrontendUnitTest::RunTest(const FString& Parameters)
{
	return true;
}
