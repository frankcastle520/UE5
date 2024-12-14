// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestDriver.h"
#include "TestUtilities.h"

#include "Online/OnlineSessionNames.h"

#include "OnlineSubsystemCatchHelper.h"

#include "Helpers/Identity/IdentityGetUniquePlayerIdHelper.h"
#include "Helpers/Session/SessionCreateSessionHelper.h"
#include "Helpers/Session/SessionDestroySessionHelper.h"
#include "Helpers/Session/SessionCancelFindSessionHelper.h"

#define SESSION_TAG "[suite_session]"
#define EG_SESSION_CANCELFINDSESSION_TAG SESSION_TAG "[cancelfindsession]"

#define SESSION_TEST_CASE(x, ...) ONLINESUBSYSTEM_TEST_CASE(x, SESSION_TAG __VA_ARGS__)

SESSION_TEST_CASE("Verify calling Session CancelFindSession returns the expected result(Success Case)", EG_SESSION_CANCELFINDSESSION_TAG)
{
	int32 LocalUserNum = 0;
	int32 TargetUserNum = 1;
	int32 PublicConnections = 2;
	int32 PrivateConnections = 1;
	int32 NumBots = 3;
	int32 NumUsersToImplicitLogin = 2;
	FUniqueNetIdPtr LocalUserId = nullptr;
	FUniqueNetIdPtr TargetUserId = nullptr;
	FString SessionKey = TEXT("1234session");
	FName SessionName = FName(FString::Printf(TEXT("TestSession_%s"), *FTestUtilities::GetUniqueTestString()));
	FOnlineSessionSetting GamemodeSetting(TEXT("FORTEMPTY"), EOnlineDataAdvertisementType::ViaOnlineService);
	FOnlineSessionSetting NumbotsSetting(NumBots, EOnlineDataAdvertisementType::ViaOnlineService);
	FOnlineSessionSetting SessionkeySetting(SessionKey, EOnlineDataAdvertisementType::ViaOnlineService);

	FOnlineSessionSettings SessionSettings;
	SessionSettings.bShouldAdvertise = true;
	SessionSettings.bAllowJoinInProgress = true;
	SessionSettings.NumPublicConnections = PublicConnections;
	SessionSettings.NumPrivateConnections = PrivateConnections;
	SessionSettings.Settings.Add(SETTING_GAMEMODE, GamemodeSetting);
	SessionSettings.Settings.Add(SETTING_NUMBOTS, NumbotsSetting);
	SessionSettings.Settings.Add(SETTING_SESSIONKEY, SessionkeySetting);

	TSharedPtr<FNamedOnlineSession> NamedOnlineSession = nullptr;

	FOnlineSessionSearch SearchSetting;
	SearchSetting.QuerySettings.Set(SETTING_NUMBOTS, NumBots, EOnlineComparisonOp::Equals);

	TSharedRef<FOnlineSessionSearch> SearchSettingRef = MakeShared<FOnlineSessionSearch>(SearchSetting);

	GetLoginPipeline(NumUsersToImplicitLogin)
		.EmplaceStep<FIdentityGetUniquePlayerIdStep>(LocalUserNum, [&LocalUserId](FUniqueNetIdPtr InUserId) {LocalUserId = InUserId; })
		.EmplaceStep<FIdentityGetUniquePlayerIdStep>(TargetUserNum, [&TargetUserId](FUniqueNetIdPtr InUserId) {TargetUserId = InUserId; })
		.EmplaceStep<FSessionCreateSessionStep>(&LocalUserId, SessionName, SessionSettings, [&NamedOnlineSession](TSharedPtr<FNamedOnlineSession> InNamedOnlineSession) { NamedOnlineSession = MoveTemp(InNamedOnlineSession); })
		.EmplaceStep<FSessionCancelFindSessionStep>(&LocalUserId, &TargetUserId, SearchSettingRef)
		.EmplaceStep<FSessionDestroySessionStep>(SessionName);

	RunToCompletion();
}