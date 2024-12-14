// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestDriver.h"
#include "TestUtilities.h"

#include "Online/OnlineSessionNames.h"

#include "OnlineSubsystemCatchHelper.h"

#include "Helpers/Identity/IdentityGetUniquePlayerIdHelper.h"
#include "Helpers/Session/SessionCreateSessionHelper.h"
#include "Helpers/Session/SessionGetNumSessionsHelper.h"
#include "Helpers/Session/SessionDestroySessionHelper.h"

#define SESSION_TAG "[suite_session]"
#define EG_SESSION_GETNUMSESSIONS_TAG SESSION_TAG "[getnumsessions]"

#define SESSION_TEST_CASE(x, ...) ONLINESUBSYSTEM_TEST_CASE(x, SESSION_TAG __VA_ARGS__)

SESSION_TEST_CASE("Verify calling Session GetNumSessions with valid inputs returns the expected result(Success Case)", EG_SESSION_GETNUMSESSIONS_TAG)
{
	int32 LocalUserNum = 0;
	int32 PublicConnections = 1;
	int32 NumUsersToImplicitLogin = 1;
	int32 ExpectedSessionsNum = 2;
	FUniqueNetIdPtr LocalUserId = nullptr;
	FName FirstSessionName = FName(FString::Printf(TEXT("TestSession_%s"), *FTestUtilities::GetUniqueTestString()));
	FName SecondSessionName = FName(FString::Printf(TEXT("TestSession_%s"), *FTestUtilities::GetUniqueTestString()));
	FOnlineSessionSetting GamemodeSetting(TEXT("FORTEMPTY"), EOnlineDataAdvertisementType::ViaOnlineService);

	FOnlineSessionSettings SessionSettings;
	SessionSettings.NumPublicConnections = PublicConnections;
	SessionSettings.Settings.Add(SETTING_GAMEMODE, GamemodeSetting);

	GetLoginPipeline(NumUsersToImplicitLogin)
		.EmplaceStep<FIdentityGetUniquePlayerIdStep>(LocalUserNum, [&LocalUserId](FUniqueNetIdPtr InUserId) {LocalUserId = InUserId; })
		.EmplaceStep<FSessionCreateSessionStep>(&LocalUserId, FirstSessionName, SessionSettings)
		.EmplaceStep<FSessionCreateSessionStep>(&LocalUserId, SecondSessionName, SessionSettings)
		.EmplaceStep<FSessionGetNumSessionsStep>(ExpectedSessionsNum)
		.EmplaceStep<FSessionDestroySessionStep>(FirstSessionName)
		.EmplaceStep<FSessionDestroySessionStep>(SecondSessionName);

	RunToCompletion();
}
