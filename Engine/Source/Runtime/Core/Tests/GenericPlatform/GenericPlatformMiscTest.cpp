// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_TESTS 

#include "GenericPlatform/GenericPlatformMisc.h"
#include "Tests/TestHarnessAdapter.h"

TEST_CASE_NAMED(FGenericPlatformMiscTest, "System::Core::GenericPlatform", "[Core][GenericPlatform][SmokeFilter]")
{
	SECTION("Parse pakchunk index")
	{
		CHECK(1 == FGenericPlatformMisc::GetPakchunkIndexFromPakFile(FString(TEXT("pakchunk1-Windows"))));
		CHECK(12 == FGenericPlatformMisc::GetPakchunkIndexFromPakFile(FString(TEXT("../../../Content/Paks/pakchunk12-Windows"))));
		CHECK(42 == FGenericPlatformMisc::GetPakchunkIndexFromPakFile(FString(TEXT("pakchunk42"))));
		CHECK(INDEX_NONE == FGenericPlatformMisc::GetPakchunkIndexFromPakFile(FString(TEXT("pakchunk"))));
	}
}

#endif // WITH_TESTS
