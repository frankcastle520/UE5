// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestDriver.h"
#include "TestUtilities.h"

#include "OnlineSubsystemCatchHelper.h"

#include "Helpers/Identity/IdentityGetUniquePlayerIdHelper.h"
#include "Helpers/Achievements/AchievementsQueryAchievementDescriptionsHelper.h"
#include "Helpers/Achievements/AchievementsGetCachedAchievementDescriptionHelper.h"

#define ACHIEVEMENTS_TAG "[suite_achievements]"
#define EG_ACHIEVEMENTS_GETCACHEDACHIEMENTDESCRIPTION_TAG ACHIEVEMENTS_TAG "[getcachedachievementdescription]"

#define ACHIEVEMENTS_TEST_CASE(x, ...) ONLINESUBSYSTEM_TEST_CASE(x, ACHIEVEMENTS_TAG __VA_ARGS__)

ACHIEVEMENTS_TEST_CASE("Verify calling Achievements GetCachedAchievementDescription with valid inputs returns the expected result(Success Case)", EG_ACHIEVEMENTS_GETCACHEDACHIEMENTDESCRIPTION_TAG)
{
	int32 LocalUserNum = 0;
	FUniqueNetIdPtr LocalUserId = nullptr;
	const FString AchievementId = TEXT("null-ach-0");
	int32 NumUsersToImplicitLogin = 1;

	GetLoginPipeline(NumUsersToImplicitLogin)
		.EmplaceStep<FIdentityGetUniquePlayerIdStep>(LocalUserNum, [&LocalUserId](FUniqueNetIdPtr InUserId) {LocalUserId = InUserId; })
		.EmplaceStep<FAchievementsQueryAchievementDescriptionsStep>(&LocalUserId)
		.EmplaceStep<FAchievementsGetCachedAchievementDescriptionStep>(AchievementId);
	
	RunToCompletion();
}
