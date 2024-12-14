// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestDriver.h"

#include "OnlineSubsystemCatchHelper.h"

#include "Helpers/Identity/IdentityGetAuthTypeHelper.h"

#define IDENTITY_TAG "[suite_identity]"
#define EG_IDENTITY_GETAUTHTYPE_TAG IDENTITY_TAG "[getauthtype]"

#define IDENTITY_TEST_CASE(x, ...) ONLINESUBSYSTEM_TEST_CASE(x, IDENTITY_TAG __VA_ARGS__)

IDENTITY_TEST_CASE("Verify calling Identity GetAuthType with valid inputs returns the expected result(Success Case)", IDENTITY_TAG)
{
	int32 LocalUserNum = 0;
	int32 NumUsersToImplicitLogin = 1;

	GetLoginPipeline(NumUsersToImplicitLogin)
		.EmplaceStep<FIdentityGetAuthTypeStep>();
	
	RunToCompletion();
}
