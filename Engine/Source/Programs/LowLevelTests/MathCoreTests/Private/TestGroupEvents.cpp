// Copyright Epic Games, Inc. All Rights Reserved.
#include "TestCommon/Initialization.h"

#include <catch2/catch_test_macros.hpp>

GROUP_BEFORE_GLOBAL(Catch::DefaultGroup)
{
	UsePlatformFileStubIfRequired();
}

GROUP_AFTER_GLOBAL(Catch::DefaultGroup)
{
	CleanupLogs();
	CleanupLocalization();
}
