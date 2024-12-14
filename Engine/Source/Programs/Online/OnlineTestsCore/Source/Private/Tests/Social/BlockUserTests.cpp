// Copyright Epic Games, Inc. All Rights Reserved.

#include "Helpers/Social/BlockUserHelper.h"
#include "Helpers/Auth/AuthLogout.h"

#define SOCIAL_TAG "[suite_social]"
#define EG_SOCIAL_BLOCKUSER_TAG SOCIAL_TAG "[blockuser]"
#define EG_SOCIAL_BLOCKUSEREOS_TAG SOCIAL_TAG "[blockuser][.EOS]"

#define SOCIAL_TEST_CASE(x, ...) ONLINE_TEST_CASE(x, SOCIAL_TAG __VA_ARGS__)

SOCIAL_TEST_CASE("Verify that BlockUser returns a error if call with an invalid local user account id", EG_SOCIAL_BLOCKUSER_TAG)
{
	FBlockUser::Params OpBlockUserParams;
	FBlockUserHelper::FHelperParams BlockUserHelperHelperParams;
	BlockUserHelperHelperParams.OpParams = &OpBlockUserParams;
	BlockUserHelperHelperParams.OpParams->LocalAccountId = FAccountId();
	BlockUserHelperHelperParams.ExpectedError = TOnlineResult<FBlockUser>(Errors::InvalidParams());


	GetPipeline()
		.EmplaceStep<FBlockUserHelper>(MoveTemp(BlockUserHelperHelperParams));

	RunToCompletion();
}

SOCIAL_TEST_CASE("Verify that BlockUser returns a error if call with an target user account id", EG_SOCIAL_BLOCKUSER_TAG)
{
	FAccountId AccountId;

	FBlockUser::Params OpBlockUserParams;
	FBlockUserHelper::FHelperParams BlockUserHelperHelperParams;
	BlockUserHelperHelperParams.OpParams = &OpBlockUserParams;
	BlockUserHelperHelperParams.OpParams->TargetAccountId = FAccountId();
	BlockUserHelperHelperParams.ExpectedError = TOnlineResult<FBlockUser>(Errors::InvalidParams());

	FTestPipeline& LoginPipeline = GetLoginPipeline({ AccountId });

	BlockUserHelperHelperParams.OpParams->LocalAccountId = AccountId;

	LoginPipeline
		.EmplaceStep<FBlockUserHelper>(MoveTemp(BlockUserHelperHelperParams));

	RunToCompletion();
}

SOCIAL_TEST_CASE("Verify that BlockUser returns a fail message if the local user is not logged in", EG_SOCIAL_BLOCKUSEREOS_TAG)
{
	FAccountId FirstAccountId, SecondAccountId;
	
	int32 UserNumToLogin = 1;
	TSharedPtr<FPlatformUserId> FirstAccountPlatformUserId = MakeShared<FPlatformUserId>();
	TSharedPtr<FPlatformUserId> SecondAccountPlatformUserId = MakeShared<FPlatformUserId>();

	bool bLogout = false;

	FBlockUser::Params OpBlockUserParams;
	FBlockUserHelper::FHelperParams BlockUserHelperHelperParams;
	BlockUserHelperHelperParams.OpParams = &OpBlockUserParams;
	BlockUserHelperHelperParams.ExpectedError = TOnlineResult<FBlockUser>(Errors::NotLoggedIn());

	FTestPipeline& LoginPipeline = GetLoginPipeline(UserNumToLogin, { FirstAccountId, SecondAccountId });

	BlockUserHelperHelperParams.OpParams->LocalAccountId = FirstAccountId;
	BlockUserHelperHelperParams.OpParams->TargetAccountId = SecondAccountId;

	LoginPipeline
		.EmplaceLambda([&FirstAccountId, &SecondAccountId, FirstAccountPlatformUserId, SecondAccountPlatformUserId](SubsystemType OnlineSubsystem)
			{
				UE::Online::IAuthPtr OnlineAuthPtr = OnlineSubsystem->GetAuthInterface();
				REQUIRE(OnlineAuthPtr);

				UE::Online::TOnlineResult<UE::Online::FAuthGetLocalOnlineUserByOnlineAccountId> FirstUserPlatfromUserIdResult = OnlineAuthPtr->GetLocalOnlineUserByOnlineAccountId({ FirstAccountId });
				UE::Online::TOnlineResult<UE::Online::FAuthGetLocalOnlineUserByOnlineAccountId> SecondUserPlatfromUserIdResult = OnlineAuthPtr->GetLocalOnlineUserByOnlineAccountId({ SecondAccountId });

				REQUIRE(FirstUserPlatfromUserIdResult.IsOk());
				REQUIRE(SecondUserPlatfromUserIdResult.IsOk());

				CHECK(FirstUserPlatfromUserIdResult.TryGetOkValue() != nullptr);
				CHECK(SecondUserPlatfromUserIdResult.TryGetOkValue() != nullptr);

				*FirstAccountPlatformUserId = FirstUserPlatfromUserIdResult.TryGetOkValue()->AccountInfo->PlatformUserId;
				*SecondAccountPlatformUserId = SecondUserPlatfromUserIdResult.TryGetOkValue()->AccountInfo->PlatformUserId;
			})
		.EmplaceStep<FAuthLogoutStep>(MoveTemp(FirstAccountPlatformUserId))
		.EmplaceStep<FBlockUserHelper>(MoveTemp(BlockUserHelperHelperParams))
		.EmplaceStep<FAuthLogoutStep>(MoveTemp(SecondAccountPlatformUserId));

	RunToCompletion(bLogout);
}

SOCIAL_TEST_CASE("Verify that BlockUser completes successfully if both users are logged in", EG_SOCIAL_BLOCKUSEREOS_TAG)
{
	FAccountId FirstAccountId, SecondAccountId;

	FBlockUser::Params OpBlockUserParams;
	FBlockUserHelper::FHelperParams BlockUserHelperHelperParams;
	BlockUserHelperHelperParams.OpParams = &OpBlockUserParams;

	FTestPipeline& LoginPipeline = GetLoginPipeline({ FirstAccountId, SecondAccountId });

	BlockUserHelperHelperParams.OpParams->LocalAccountId = FirstAccountId;
	BlockUserHelperHelperParams.OpParams->TargetAccountId = SecondAccountId;

	LoginPipeline
		.EmplaceStep<FBlockUserHelper>(MoveTemp(BlockUserHelperHelperParams));

	RunToCompletion();
}
