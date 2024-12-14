// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestDriver.h"
#include "TestUtilities.h"

#include "OnlineSubsystemCatchHelper.h"

#include "Helpers/Identity/IdentityGetUniquePlayerIdHelper.h"
#include "Helpers/Achievements/AchievementsQueryAchievementsHelper.h"
#include "Helpers/Achievements/AchievementsGetCachedAchievementsHelper.h"

#define ACHIEVEMENTS_TAG "[suite_achievements]"
#define EG_ACHIEVEMENTS_GETCACHEDACHIEMENTS_TAG ACHIEVEMENTS_TAG "[getcachedachievements]"

#define ACHIEVEMENTS_TEST_CASE(x, ...) ONLINESUBSYSTEM_TEST_CASE(x, ACHIEVEMENTS_TAG __VA_ARGS__)

ACHIEVEMENTS_TEST_CASE("Verify calling Achievements GetCachedAchievements with valid inputs returns the expected result(Success Case)", EG_ACHIEVEMENTS_GETCACHEDACHIEMENTS_TAG)
{
	int32 LocalUserNum = 0;
	FUniqueNetIdPtr LocalUserId = nullptr;
	TArray<FOnlineAchievement>* Achievenments = nullptr;
	int32 NumUsersToImplicitLogin = 1;

	GetLoginPipeline(NumUsersToImplicitLogin)
		.EmplaceStep<FIdentityGetUniquePlayerIdStep>(LocalUserNum, [&LocalUserId](FUniqueNetIdPtr InUserId) {LocalUserId = InUserId; })
		.EmplaceStep<FAchievementsQueryAchievementsStep>(&LocalUserId)
		.EmplaceStep<FAchievementsGetCachedAchievementsStep>(&LocalUserId, [&Achievenments](TArray<FOnlineAchievement>* InOnlineAchievements) {Achievenments = InOnlineAchievements; });
	
	RunToCompletion();
}
