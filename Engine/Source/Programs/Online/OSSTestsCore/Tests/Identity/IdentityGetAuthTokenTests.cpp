// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestDriver.h"

#include "OnlineSubsystemCatchHelper.h"

#include "Helpers/Identity/IdentityGetAuthTokenHelper.h"

#define IDENTITY_TAG "[suite_identity]"
#define EG_IDENTITY_GETAUTHTOKEN_TAG IDENTITY_TAG "[getauthtoken]"

#define IDENTITY_TEST_CASE(x, ...) ONLINESUBSYSTEM_TEST_CASE(x, IDENTITY_TAG __VA_ARGS__)

IDENTITY_TEST_CASE("Verify calling Identity GetAuthToken with valid inputs returns the expected result(Success Case)", EG_IDENTITY_GETAUTHTOKEN_TAG)
{
	int32 LocalUserNum = 0;
	int32 NumUsersToImplicitLogin = 1;
	
	GetLoginPipeline(NumUsersToImplicitLogin)
		.EmplaceStep<FIdentityGetAuthTokenStep>(LocalUserNum);
	
	RunToCompletion();
}