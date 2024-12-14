// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestDriver.h"

#include "OnlineSubsystemCatchHelper.h"

#define IDENTITY_TAG "[suite_identity]"
#define EG_IDENTITY_LOGIN_TAG IDENTITY_TAG "[login]"

#define IDENTITY_TEST_CASE(x, ...) ONLINESUBSYSTEM_TEST_CASE(x, IDENTITY_TAG __VA_ARGS__)

IDENTITY_TEST_CASE("Verify calling Identity Login with valid inputs returns the expected result(Success Case)", EG_IDENTITY_LOGIN_TAG)
{
	int32 NumUsersToImplicitLogin = 2;

	GetLoginPipeline(NumUsersToImplicitLogin);

	RunToCompletion();
}