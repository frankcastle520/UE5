// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#include "OnlineSubsystemCatchHelper.h"

#include "Helpers/Friends/FriendsEnsureFriendshipHelper.h"
#include "Helpers/Identity/IdentityGetUniquePlayerIdHelper.h"
#include "Helpers/Friends/FriendsDeleteFriendAliasHelper.h"
#include "Helpers/Friends/FriendsSetFriendAliasHelper.h"

#define FRIENDS_TAG "[suite_friends]"
#define EG_FRIENDS_SETFRIENDALIAS_TAG FRIENDS_TAG "[setfriendalias]"

#define FRIENDS_TEST_CASE(x, ...) ONLINESUBSYSTEM_TEST_CASE(x, FRIENDS_TAG __VA_ARGS__)

FRIENDS_TEST_CASE("Verify calling SetFriendAlias with valid inputs returns the expected result(Success Case)", EG_FRIENDS_SETFRIENDALIAS_TAG)
{
	int32 LocalUserNum = 0;
	FUniqueNetIdPtr LocalUserId = nullptr;
	int32 TargetUserNum = 1;
	FUniqueNetIdPtr TargetUserId = nullptr;
	FString LocalListName = EFriendsLists::ToString(EFriendsLists::Default);
	FString LocalAlias = TEXT("AliasName");
	bool bLocalIsFriendsListPopulated = true;
	int32 NumUsersToImplicitLogin = 2;

	GetLoginPipeline(NumUsersToImplicitLogin)
		.EmplaceStep<FIdentityGetUniquePlayerIdStep>(LocalUserNum, [&LocalUserId](FUniqueNetIdPtr InUserId) {LocalUserId = InUserId; })
		.EmplaceStep<FIdentityGetUniquePlayerIdStep>(TargetUserNum, [&TargetUserId](FUniqueNetIdPtr InUserId) {TargetUserId = InUserId; })
		.EmplaceStep<FFriendsEnsureFriendshipStep>(LocalUserNum, TargetUserNum, &LocalUserId, &TargetUserId, LocalListName, bLocalIsFriendsListPopulated)
		.EmplaceStep<FFriendsSetFriendAliasStep>(LocalUserNum, &TargetUserId, LocalListName, LocalAlias)
		.EmplaceStep<FFriendsDeleteFriendAliasStep>(LocalUserNum, &TargetUserId, LocalListName);

	RunToCompletion();
}
