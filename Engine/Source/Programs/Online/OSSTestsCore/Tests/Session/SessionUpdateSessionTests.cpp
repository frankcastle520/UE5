// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestDriver.h"
#include "TestUtilities.h"

#include "Online/OnlineSessionNames.h"

#include "OnlineSubsystemCatchHelper.h"

#include "Helpers/Identity/IdentityGetUniquePlayerIdHelper.h"
#include "Helpers/Session/SessionCreateSessionHelper.h"
#include "Helpers/Session/SessionUpdateSessionHelper.h"
#include "Helpers/Session/SessionGetSessionSettingsHelper.h"
#include "Helpers/Session/SessionDestroySessionHelper.h"

#define SESSION_TAG "[suite_session]"
#define EG_SESSION_UPDATESESSION_TAG SESSION_TAG "[updatesession]"

#define SESSION_TEST_CASE(x, ...) ONLINESUBSYSTEM_TEST_CASE(x, SESSION_TAG __VA_ARGS__)

SESSION_TEST_CASE("Verify calling Session UpdateSession with valid inputs returns the expected result(Success Case)", EG_SESSION_UPDATESESSION_TAG)
{
	int32 LocalUserNum = 0;
	int32 PublicConnections = 1;
	int32 NewPublicConnections = 1;
	int32 NumUsersToImplicitLogin = 1;
	FUniqueNetIdPtr UserId = nullptr;
	FName SessionName = FName(FString::Printf(TEXT("TestSession_%s"), *FTestUtilities::GetUniqueTestString()));
	FOnlineSessionSetting GamemodeSetting(TEXT("FORTEMPTY"), EOnlineDataAdvertisementType::ViaOnlineService);

	FOnlineSessionSettings SessionSettings = {};
	SessionSettings.NumPublicConnections = PublicConnections;
	SessionSettings.bAllowInvites = false;
	SessionSettings.bAllowJoinInProgress = false;
	SessionSettings.bAllowJoinViaPresence = false;
	SessionSettings.bAntiCheatProtected = false;
	SessionSettings.Settings.Add(SETTING_GAMEMODE, GamemodeSetting);

	FOnlineSessionSettings NewSessionSettings = {};
	NewSessionSettings.NumPublicConnections = NewPublicConnections;
	NewSessionSettings.bAllowInvites = true;
	NewSessionSettings.bAllowJoinInProgress = true;
	NewSessionSettings.bAllowJoinViaPresence = true;
	NewSessionSettings.bAntiCheatProtected = true;

	GetLoginPipeline(NumUsersToImplicitLogin)
		.EmplaceStep<FIdentityGetUniquePlayerIdStep>(LocalUserNum, [&UserId](FUniqueNetIdPtr InUserId) {UserId = InUserId; })
		.EmplaceStep<FSessionCreateSessionStep>(&UserId, SessionName, SessionSettings)
		.EmplaceStep<FSessionGetSessionSettingsStep>(SessionName, SessionSettings)
		.EmplaceStep<FSessionUpdateSessionStep>(SessionName, NewSessionSettings, true)
		.EmplaceStep<FSessionGetSessionSettingsStep>(SessionName, NewSessionSettings)
		.EmplaceStep<FSessionDestroySessionStep>(SessionName);

	RunToCompletion();
}
