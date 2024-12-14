// Copyright Epic Games, Inc. All Rights Reserved.

#include "Catch2Includes.h"
#include "AutoRTFM/AutoRTFM.h"
#include "HAL/PlatformMisc.h"

TEST_CASE("FPlatformMisc.CreateGUID")
{
	AutoRTFM::ForTheRuntime::SetEnsureOnAbortByLanguage(true);
	FGuid Guid;
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		FPlatformMisc::CreateGuid(Guid);
	});
	AutoRTFM::ForTheRuntime::SetEnsureOnAbortByLanguage(false);

	REQUIRE(AutoRTFM::ETransactionResult::Committed == Result);
	REQUIRE(Guid != FGuid());
}
