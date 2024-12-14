// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"
#include "AutoRTFMEngineTests.h"

#if WITH_AUTOMATION_WORKER

namespace UE::AutoRTFM
{

class FAutomationTestRunner
{
public:
	FAutomationTestRunner();
	
	// Returns true if the tests ran successfully.
	bool RunTests(const TCHAR* TestFilter = nullptr);
};

}
#endif
