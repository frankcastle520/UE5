// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreGlobals.h"
#include "HAL/Platform.h"

AITESTSUITE_API DECLARE_LOG_CATEGORY_EXTERN(LogTestableEnsures, Log, All);

//-----------------------------------------------------------------------------
// support for testable ensure and check
//-----------------------------------------------------------------------------
namespace UE::AITestSuite
{
	AITESTSUITE_API extern int32 TestsInProgress;
}

#define AITEST_SCOPED_CHECK(PartialTextToExpect, NumOccurrences) \
	GetTestRunner().AddExpectedError(PartialTextToExpect, EAutomationExpectedErrorFlags::Contains, NumOccurrences); \
	ON_SCOPE_EXIT \
	{ \
		--UE::AITestSuite::TestsInProgress; \
	}; \
	++UE::AITestSuite::TestsInProgress \

#define testableEnsureMsgf(InExpression, InFormat, ... ) \
	(LIKELY(!!(InExpression)) || ([&]() \
		{ \
			if (UNLIKELY(UE::AITestSuite::TestsInProgress > 0)) \
			{ \
				UE_LOG(LogTestableEnsures, Warning, InFormat, ##__VA_ARGS__); \
			} \
			else \
			{ \
				ensureMsgf(false, InFormat, ##__VA_ARGS__); \
			} \
		return false; \
		} ()))

#define testableCheckf(InExpression, InFormat, ... ) \
	if (UNLIKELY(UE::AITestSuite::TestsInProgress > 0)) \
	{ \
		UE_CLOG(!(InExpression), LogTestableEnsures, Error, InFormat, ##__VA_ARGS__); \
		return; \
	} \
	else \
	{ \
		checkf(InExpression, InFormat, ##__VA_ARGS__); \
	}

#define testableCheckfReturn(InExpression, ReturnExpression, InFormat, ... ) \
	if (UNLIKELY(UE::AITestSuite::TestsInProgress > 0)) \
	{ \
		if (!(InExpression)) \
		{ \
			UE_LOG(LogTestableEnsures, Error, InFormat, ##__VA_ARGS__); \
			ReturnExpression; \
		} \
	} \
	else \
	{ \
		checkf(InExpression, InFormat, ##__VA_ARGS__); \
	}
