// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestDriver.h"

#include "OnlineSubsystemCatchHelper.h"

#include "Helpers/Identity/IdentityLoginHelper.h"
#include "Helpers/Identity/IdentityLogoutHelper.h"
#include "Helpers/Identity/IdentityGetUniquePlayerIdHelper.h"
#include "Helpers/Leaderboards/LeaderboardsWriteLeaderboardsHelper.h"
#include "Helpers/Leaderboards/LeaderboardsReadLeaderboardsForFriendsHelper.h"

#define LEADERBOARDS_TAG "[suite_leaderboards]"
#define EG_LEADERBOARDS_READLEADERBOARDSFORFRINEDS_TAG LEADERBOARDS_TAG "[readleaderboardsforfriends]"

#define EG_LEADERBOARDS_TEST_CASE(x, ...) ONLINESUBSYSTEM_TEST_CASE(x, LEADERBOARDS_TAG __VA_ARGS__)

EG_LEADERBOARDS_TEST_CASE("Verify calling ReadLeaderboardsForFriends with valid inputs returns the expected result(Success Case)", EG_LEADERBOARDS_READLEADERBOARDSFORFRINEDS_TAG)
{
	int32 LocalUserNum = 0;
	int32 SecondLocalUserNum = 1;
	int32 TargetUsersNum = 2;
	FUniqueNetIdPtr LocalUserId = nullptr;

	FName LocalSessionName = TEXT("FakeSessionName");
	const FString LocalNameForLeaderboard = TEXT("Name1");
	FOnlineLeaderboardWrite WriteObject;

	WriteObject.LeaderboardNames = { LocalNameForLeaderboard };
	WriteObject.SortMethod = ELeaderboardSort::Ascending;
	WriteObject.UpdateMethod = ELeaderboardUpdateMethod::KeepBest;
	WriteObject.RatedStat = FString(TEXT("Scores"));

	TArray<FUniqueNetIdRef> LocalPlayers;
	FOnlineLeaderboardReadRef LocalReadObject = MakeShared<FOnlineLeaderboardRead>();
	LocalReadObject.Get().LeaderboardName = { LocalNameForLeaderboard };
	int32 NumUsersToImplicitLogin = 1;

	GetLoginPipeline(NumUsersToImplicitLogin)
		.EmplaceStep<FIdentityGetUniquePlayerIdStep>(LocalUserNum, [&LocalUserId](FUniqueNetIdPtr InPlayerId) {LocalUserId = InPlayerId; })
		.EmplaceStep<FLeaderboardsWriteLeaderboardsStep>(LocalSessionName, &LocalUserId, WriteObject)
		.EmplaceStep<FLeaderboardsReadLeaderboardsForFriendsStep>(SecondLocalUserNum, LocalReadObject);

	RunToCompletion();
}
