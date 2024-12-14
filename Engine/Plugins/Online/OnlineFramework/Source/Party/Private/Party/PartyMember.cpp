// Copyright Epic Games, Inc. All Rights Reserved.

#include "Party/PartyMember.h"
#include "Engine/GameInstance.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EnumRange.h"
#include "OnlineSubsystemUtils.h"
#include "Party/SocialParty.h"
#include "SocialManager.h"
#include "SocialToolkit.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PartyMember)

//////////////////////////////////////////////////////////////////////////
// PartyMemberRepData
//////////////////////////////////////////////////////////////////////////

void FPartyMemberRepData::SetOwningMember(const UPartyMember& InOwnerMember)
{
	OwnerMember = &InOwnerMember;
}

void FPartyMemberRepData::MarkOwnerless()
{
	bAllowOwnerless = true;
}

bool FPartyMemberRepData::CanEditData() const
{
	return bAllowOwnerless || (OwnerMember.IsValid() && OwnerMember->IsLocalPlayer());
}

void FPartyMemberRepData::CompareAgainst(const FOnlinePartyRepDataBase& OldData) const
{
	const FPartyMemberRepData& TypedOldData = static_cast<const FPartyMemberRepData&>(OldData);

	ComparePlatformDataPlatform(TypedOldData);
	ComparePlatformDataUniqueId(TypedOldData);
	ComparePlatformDataSessionId(TypedOldData);
	CompareCrossplayPreference(TypedOldData);
	CompareJoinInProgressDataRequest(TypedOldData);
	CompareJoinInProgressDataResponses(TypedOldData);
}

const USocialParty* FPartyMemberRepData::GetOwnerParty() const
{
	return OwnerMember.IsValid() ? &OwnerMember->GetParty() : nullptr;
}

const UPartyMember* FPartyMemberRepData::GetOwningMember() const
{
	return OwnerMember.IsValid() ? OwnerMember.Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// PartyMember
//////////////////////////////////////////////////////////////////////////

ENUM_CLASS_FLAGS(UPartyMember::EInitializingFlags);

class UPartyMember::FDebugInitializer
{
public:
	FDebugInitializer(UPartyMember& InParent);
	~FDebugInitializer();

	void AddPendingAction(FString&& ActionName);
	void RemovePendingAction(FString&& ActionName);
private:
	void SetupWarningTimers();
	void ClearWarningTimers();
	FString GetWaitingForString() const;

	UPartyMember& Parent;
	FString ParentDebugString;
	FTSTicker::FDelegateHandle TickHandle;
	double StartTime = FPlatformTime::Seconds();
	TArray<FString> PendingActions;
};

namespace
{
// Get all local toolkits that are logged in or in the party
TArray<USocialToolkit*> GetLocalToolkits(const UPartyMember& InPartyMember)
{
	// Ideally, this could use UE::OnlineFramework::GetLocalPartyMemberToolkits(InPartyMember.GetParty()), but due to how we initialize party members one by one, in 
	// multi local player cases, the first local party member initialized is the only local party member and thus the only toolkit returned.
	// This doesn't quite capture the intent of getting all local players in the party, as this assumes all local players will be in the party, but there hasn't been
	// a use case for having a party that doesn't have all local players in it.
	TArray<USocialToolkit*> Toolkits;
	if (UGameInstance* GameInstance = InPartyMember.GetWorld()->GetGameInstance())
	{
		for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
		{
			if (LIKELY(LocalPlayer != nullptr))
			{
				USocialToolkit& SocialToolkit = InPartyMember.GetParty().GetSocialManager().GetSocialToolkit(*LocalPlayer);
				// The game can create parties as part of logging in, so also just check if the toolkit is present in the party
				if (SocialToolkit.IsOwnerLoggedIn() || InPartyMember.GetParty().GetPartyMember(SocialToolkit.GetLocalUserNetId(ESocialSubsystem::Primary)) != nullptr)
				{
					Toolkits.Emplace(&SocialToolkit);
				}
			}
		}
	}
	return Toolkits;
}

// Check if all social users for the party member is initialized - ie all local toolkits are done initializing the user
bool AreAllSocialUsersInitialized(const UPartyMember& InPartyMember)
{
	TArray<USocialToolkit*> SocialToolkits = GetLocalToolkits(InPartyMember);
	if (UNLIKELY(SocialToolkits.Num() == 0))
	{
		return false;
	}
	for (USocialToolkit* SocialToolkit : SocialToolkits)
	{
		if (USocialUser* SocialUser = SocialToolkit->FindUser(InPartyMember.GetPrimaryNetId());
			!ensure(SocialUser != nullptr) || !SocialUser->IsInitialized())
		{
			return false;
		}
	}
	return true;
}
}

UPartyMember::UPartyMember()
{
}

void UPartyMember::BeginDestroy()
{
	Super::BeginDestroy();

	if(!IsTemplate())
	{
		Shutdown();
	}
}

void UPartyMember::InitializePartyMember(const FOnlinePartyMemberConstRef& InOssMember, FSimpleDelegate&& OnInitComplete)
{
	checkf(MemberDataReplicator.IsValid(), TEXT("Child classes of UPartyMember MUST call MemberRepData.EstablishRepDataInstance with a valid FPartyMemberRepData struct instance in their constructor."));
	MemberDataReplicator->SetOwningMember(*this);

	InitializingFlags = EInitializingFlags::InitialMemberData | EInitializingFlags::SocialUsers;
	if (ensureAlways(!OssPartyMember.IsValid()))
	{
		OssPartyMember = InOssMember;
		OssPartyMember->OnMemberConnectionStatusChanged().AddUObject(this, &ThisClass::HandleMemberConnectionStatusChanged);
		OssPartyMember->OnMemberAttributeChanged().AddUObject(this, &ThisClass::HandleMemberAttributeChanged);

		if (bEnableDebugInitializer)
		{
			DebugInitializer = MakeUnique<FDebugInitializer>(*this);
		}

		{
			USocialToolkit* OwnerToolkit = GetParty().GetSocialManager().GetSocialToolkit(OssPartyMember->GetUserId());
			// If we are not a local user then we simply get the first local user's toolkit
			if (OwnerToolkit == nullptr)
			{
				OwnerToolkit = GetParty().GetSocialManager().GetFirstLocalUserToolkit();
			}
			check(OwnerToolkit);

			OwnerToolkit->QueueUserDependentAction(InOssMember->GetUserId(),
				[this] (USocialUser& User)
				{
					DefaultSocialUser = &User;
				}, false);
		}

		// Local player already has all the data they need, everyone else we want to wait for
		if (IsLocalPlayer())
		{
			EnumRemoveFlags(InitializingFlags, EInitializingFlags::InitialMemberData);
		}
		else if (DebugInitializer)
		{
			DebugInitializer->AddPendingAction(TEXT("InitialMemberData"));
		}
		
		OnInitializationComplete().Add(MoveTemp(OnInitComplete));

		TArray<USocialToolkit*> Toolkits = GetLocalToolkits(*this);

		// Initialize social user for all logged in toolkits.
		for (USocialToolkit* Toolkit : Toolkits)
		{
			InitializeSocialUserForToolkit(*Toolkit);
		}

		// Listen for toolkit creations and destructions to keep our initialization and social user states in tact
		GetParty().GetSocialManager().OnSocialToolkitCreated().AddUObject(this, &UPartyMember::OnSocialToolkitCreated);
		GetParty().GetSocialManager().OnSocialToolkitDestroyed().AddUObject(this, &UPartyMember::OnSocialToolkitDestroyed);

		UE_LOG(LogParty, Verbose, TEXT("Created new party member [%s]"), *ToDebugString());
	}
}

void UPartyMember::InitializeLocalMemberRepData()
{
	UE_LOG(LogParty, Verbose, TEXT("Initializing rep data for local member [%s]"), *ToDebugString());

	MemberDataReplicator->SetPlatformDataPlatform(IOnlineSubsystem::GetLocalPlatformName());
	MemberDataReplicator->SetPlatformDataUniqueId(DefaultSocialUser->GetUserId(ESocialSubsystem::Platform));
	
	const USocialParty& CurrentParty = GetParty();
	
	FString JoinMethod;
	if (const USocialManager::FJoinPartyAttempt* JoinAttempt = CurrentParty.GetSocialManager().GetJoinAttemptInProgress(CurrentParty.GetPartyTypeId()))
	{
		JoinMethod = JoinAttempt->JoinMethod.ToString();
		UE_LOG(LogParty, Verbose, TEXT("Join method from join attempt for local member is %s."), *JoinMethod);
	}
	else
	{
		IOnlinePartyPtr PartyInterface = Online::GetPartyInterfaceChecked(GetWorld());
		FOnlinePartyDataConstPtr PartyMemberData = PartyInterface.IsValid() ? PartyInterface->GetPartyMemberData(*CurrentParty.GetOwningLocalUserId(), CurrentParty.GetPartyId(), *GetPrimaryNetId(), DefaultPartyDataNamespace) : nullptr;
		
		if (PartyMemberData.IsValid())
		{
			FVariantData AttrValue;
			if (PartyMemberData->GetAttribute(TEXT("JoinMethod"), AttrValue))
			{
				JoinMethod = AttrValue.ToString();
				UE_LOG(LogParty, Verbose, TEXT("Join method recovered from the Party member data is %s."), *JoinMethod);
			}
		}
	}

	MemberDataReplicator->SetJoinMethod(JoinMethod);
}

void UPartyMember::Shutdown()
{
	MemberDataReplicator.Reset();
}

bool UPartyMember::CanPromoteToLeader(const ULocalPlayer& PerformingPlayer) const
{
	return GetParty().CanPromoteMember(PerformingPlayer, *this);
}

bool UPartyMember::PromoteToPartyLeader(const ULocalPlayer& PerformingPlayer)
{
	return GetParty().TryPromoteMember(PerformingPlayer, *this);
}

bool UPartyMember::CanKickFromParty(const ULocalPlayer& PerformingPlayer) const
{
	return GetParty().CanKickMember(PerformingPlayer, *this);
}

bool UPartyMember::KickFromParty(const ULocalPlayer& PerformingPlayer)
{
	return GetParty().TryKickMember(PerformingPlayer, *this);
}

bool UPartyMember::IsInitialized() const
{
	return InitializingFlags == EInitializingFlags::Done;
}

USocialParty& UPartyMember::GetParty() const
{
	return *GetTypedOuter<USocialParty>();
}

FUniqueNetIdRepl UPartyMember::GetPrimaryNetId() const
{
	check(OssPartyMember.IsValid());
	return OssPartyMember->GetUserId();
}

USocialUser& UPartyMember::GetSocialUser() const
{
	return *DefaultSocialUser;
}

USocialUser* UPartyMember::GetSocialUser(const FUniqueNetIdRepl& InLocalUserId) const
{
	if (USocialToolkit* SocialToolkit = GetParty().GetSocialManager().GetSocialToolkit(InLocalUserId))
	{
		return SocialToolkit->FindUser(GetPrimaryNetId());
	}
	return nullptr;
}

FString UPartyMember::GetDisplayName() const
{
	return OssPartyMember->GetDisplayName(GetRepData().GetPlatformDataPlatform());
}

FName UPartyMember::GetPlatformOssName() const
{
	return MemberDataReplicator->GetPlatformDataUniqueId().GetType();
}

FString UPartyMember::ToDebugString(bool bIncludePartyId) const
{
	FString MemberIdentifierStr;

#if UE_BUILD_SHIPPING
	MemberIdentifierStr = GetPrimaryNetId().ToDebugString();
#else
	// It's a whole lot easier to debug with real names when it's ok to do so
	MemberIdentifierStr = FString::Printf(TEXT("%s (%s)"), *GetDisplayName(), *GetPrimaryNetId().ToDebugString());
#endif
	
	if (bIncludePartyId)
	{
		return FString::Printf(TEXT("%s, Party (%s)"), *MemberIdentifierStr, *GetParty().GetPartyId().ToDebugString());
	}
	return MemberIdentifierStr;
}

bool UPartyMember::IsPartyLeader() const
{
	return GetParty().GetPartyLeader() == this;
}

bool UPartyMember::IsLocalPlayer() const
{
	return GetParty().GetSocialManager().IsLocalUser(GetPrimaryNetId(), ESocialSubsystem::Primary);
}

void UPartyMember::NotifyMemberDataReceived(const FOnlinePartyData& MemberData)
{
	UE_LOG(LogParty, VeryVerbose, TEXT("Received updated rep data for member [%s]"), *ToDebugString());

	check(MemberDataReplicator.IsValid());
	MemberDataReplicator.ProcessReceivedData(MemberData/*, bHasReceivedInitialData*/);

	if (EnumHasAnyFlags(InitializingFlags, EInitializingFlags::InitialMemberData))
	{
		if (DebugInitializer)
		{
			DebugInitializer->RemovePendingAction(TEXT("InitialMemberData"));
		}
		EnumRemoveFlags(InitializingFlags, EInitializingFlags::InitialMemberData);
		if (InitializingFlags == EInitializingFlags::Done)
		{
			FinishInitializing();
		}
	}
}

void UPartyMember::NotifyMemberPromoted()
{
	UE_LOG(LogParty, Verbose, TEXT("Member [%s] promoted to party leader."), *ToDebugString());
	OnMemberPromotedInternal();
}

void UPartyMember::NotifyMemberDemoted()
{
	UE_LOG(LogParty, Verbose, TEXT("Member [%s] is no longer party leader."), *ToDebugString());
	OnMemberDemotedInternal();
}

void UPartyMember::NotifyRemovedFromParty(EMemberExitedReason ExitReason)
{
	UE_LOG(LogParty, Verbose, TEXT("Member [%s] is no longer in the party. Reason = [%s]"), *ToDebugString(), ToString(ExitReason));
	OnRemovedFromPartyInternal(ExitReason);
}

void UPartyMember::FinishInitializing()
{
	check(InitializingFlags == EInitializingFlags::Done);
	DebugInitializer.Reset();
	//@todo DanH Party: The old UFortParty did this. Only used for Switch. Thing is, doesn't this need to be solved for all social users? Not just party members? #suggested
	DefaultSocialUser->SetUserLocalAttribute(ESocialSubsystem::Primary, USER_ATTR_PREFERRED_DISPLAYNAME, OssPartyMember->GetDisplayName());

	if (IsLocalPlayer())
	{
		InitializeLocalMemberRepData();
	}

	UE_LOG(LogParty, Verbose, TEXT("PartyMember [%s] is now fully initialized."), *ToDebugString());
	OnInitializationComplete().Broadcast();
	OnInitializationComplete().Clear();
}

void UPartyMember::OnMemberPromotedInternal()
{
	OnPromotedToLeader().Broadcast();
}

void UPartyMember::OnMemberDemotedInternal()
{
	OnDemoted().Broadcast();
}

void UPartyMember::OnRemovedFromPartyInternal(EMemberExitedReason ExitReason)
{
	OnLeftParty().Broadcast(ExitReason);
}

void UPartyMember::InitializeSocialUserForToolkit(USocialToolkit& Toolkit)
{
	// Ensure we have a social user created for this toolkit, and add it to our initializing status if needed
	USocialUser* ToolkitUser = nullptr;
	Toolkit.QueueUserDependentAction(OssPartyMember->GetUserId(),
		[&ToolkitUser] (USocialUser& User)
		{
			ToolkitUser = &User;
		}, /*bExecutePostInit=*/false);
	check(ToolkitUser);

	FUniqueNetIdRepl LocalUserId = Toolkit.GetLocalUserNetId(ESocialSubsystem::Primary);
	UE_LOG(LogParty, Verbose, TEXT("%hs - QUDA returned SocialUser [%s (%p)] for %s"), __FUNCTION__,
		*GetFullNameSafe(ToolkitUser), ToolkitUser, *LocalUserId.ToDebugString());

	// Only wait for it to complete if we're still initializing
	if (EnumHasAnyFlags(InitializingFlags, EInitializingFlags::SocialUsers))
	{
		UE_LOG(LogParty, Log, TEXT("%hs - Registering Init Complete Handler for [%s (%p)]"), __FUNCTION__,
			*GetFullNameSafe(ToolkitUser), ToolkitUser);
		if (DebugInitializer)
		{
			DebugInitializer->AddPendingAction(LocalUserId.ToString());
		}
		ToolkitUser->RegisterInitCompleteHandler(FOnNewSocialUserInitialized::CreateUObject(this, &UPartyMember::HandleSocialUserInitialized));
	}
}

void UPartyMember::HandleSocialUserInitialized(USocialUser& InitializedUser)
{
	FUniqueNetIdRepl LocalUserId = InitializedUser.GetOwningToolkit().GetLocalUserNetId(ESocialSubsystem::Primary);
	UE_LOG(LogParty, VeryVerbose, TEXT("PartyMember [%s]'s underlying SocialUser has been initialized for local user [%s]"),
	       *ToDebugString(), *LocalUserId.ToDebugString());
	if (EnumHasAnyFlags(InitializingFlags, EInitializingFlags::SocialUsers))
	{
		if (DebugInitializer)
		{
			DebugInitializer->RemovePendingAction(LocalUserId.ToString());
		}
		if (AreAllSocialUsersInitialized(*this))
		{
			EnumRemoveFlags(InitializingFlags, EInitializingFlags::SocialUsers);
			if (InitializingFlags == EInitializingFlags::Done)
			{
				FinishInitializing();
			}
		}
	}
}

EMemberConnectionStatus UPartyMember::GetMemberConnectionStatus() const
{
	if (OssPartyMember.IsValid())
	{
		return OssPartyMember->MemberConnectionStatus;
	}
	return EMemberConnectionStatus::Uninitialized;
}

void UPartyMember::HandleMemberConnectionStatusChanged(const FUniqueNetId& ChangedUserId, const EMemberConnectionStatus NewMemberConnectionStatus, const EMemberConnectionStatus PreviousMemberConnectionStatus)
{
	OnMemberConnectionStatusChanged().Broadcast();
}

void UPartyMember::HandleMemberAttributeChanged(const FUniqueNetId& ChangedUserId, const FString& Attribute, const FString& NewValue, const FString& OldValue)
{
	if (Attribute == USER_ATTR_DISPLAYNAME)
	{
		OnDisplayNameChanged().Broadcast();
	}
}

void UPartyMember::OnSocialToolkitCreated(USocialToolkit& Toolkit)
{
	if (Toolkit.IsOwnerLoggedIn())
	{
		OnSocialToolkitLoggedIn(Toolkit);
	}
	else
	{
		Toolkit.OnLoginChanged().AddWeakLambda(this, [this, Toolkit = TObjectPtr<USocialToolkit>(&Toolkit)](bool bLoggedIn)
		{
			if (bLoggedIn)
			{
				OnSocialToolkitLoggedIn(*Toolkit);
				Toolkit->OnLoginChanged().RemoveAll(this);
			}
		});
	}
}

void UPartyMember::OnSocialToolkitLoggedIn(USocialToolkit& Toolkit)
{
	InitializeSocialUserForToolkit(Toolkit);
}

void UPartyMember::OnSocialToolkitDestroyed(USocialToolkit& Toolkit)
{
	if (EnumHasAnyFlags(InitializingFlags, EInitializingFlags::SocialUsers))
	{
		if (DebugInitializer)
		{
			FUniqueNetIdRepl LocalUserId = Toolkit.GetLocalUserNetId(ESocialSubsystem::Primary);
			DebugInitializer->RemovePendingAction(LocalUserId.ToString());
		}
		if (AreAllSocialUsersInitialized(*this))
		{
			EnumRemoveFlags(InitializingFlags, EInitializingFlags::SocialUsers);
			if (InitializingFlags == EInitializingFlags::Done)
			{
				FinishInitializing();
			}
		}
	}
}

UPartyMember::FDebugInitializer::FDebugInitializer(UPartyMember& InParent)
	: Parent(InParent)
	, ParentDebugString(InParent.ToDebugString())
{
	SetupWarningTimers();
}

UPartyMember::FDebugInitializer::~FDebugInitializer()
{
	ClearWarningTimers();
	if (Parent.InitializingFlags == UPartyMember::EInitializingFlags::Done)
	{
		UE_LOG(LogParty, VeryVerbose, TEXT("%s [%0.2f] Complete"), *ParentDebugString, FPlatformTime::Seconds() - StartTime);
	}
	else
	{
		UE_LOG(LogParty, Verbose, TEXT("%s [%0.2f] destroyed before initializing completed"), *ParentDebugString, FPlatformTime::Seconds() - StartTime);
	}
}

void UPartyMember::FDebugInitializer::AddPendingAction(FString&& InAction)
{
	UE_LOG(LogParty, VeryVerbose, TEXT("%s Waiting for [%s]"), *ParentDebugString, *InAction);
	PendingActions.AddUnique(MoveTemp(InAction));
}

void UPartyMember::FDebugInitializer::RemovePendingAction(FString&& InAction)
{
	UE_LOG(LogParty, VeryVerbose, TEXT("%s No longer waiting for [%s]. Time elapsed %0.2f"), *ParentDebugString, *InAction, FPlatformTime::Seconds() - StartTime);
	PendingActions.Remove(MoveTemp(InAction));
}

void UPartyMember::FDebugInitializer::SetupWarningTimers()
{
	double WarningTimeSeconds = 10.0;
	GConfig->GetDouble(TEXT("/Script/Party.PartyMember"), TEXT("DebugInitializer.WarnSeconds"), WarningTimeSeconds, GGameIni);
	if (WarningTimeSeconds > 0.0)
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("UPartyMember::FDebugInitializer"), WarningTimeSeconds, [this, WarningTimeSeconds](float)->bool
		{
			UE_LOG(LogParty, Warning, TEXT("%s [%0.2f] Initialization not complete. Waiting for: %s"), *ParentDebugString, FPlatformTime::Seconds() - StartTime, *GetWaitingForString());
			double ErrorTimeSeconds = 30.0;
			GConfig->GetDouble(TEXT("/Script/Party.PartyMember"), TEXT("DebugInitializer.ErrorSeconds"), ErrorTimeSeconds, GGameIni);
			if ((ErrorTimeSeconds - WarningTimeSeconds) > 0.0)
			{
				TickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("UPartyMember::FDebugInitializer"), (ErrorTimeSeconds - WarningTimeSeconds), [this](float)->bool
				{
					UE_LOG(LogParty, Error, TEXT("%s [%0.2f] Initialization not complete. Waiting for: %s"), *ParentDebugString, FPlatformTime::Seconds() - StartTime, *GetWaitingForString());
					TickHandle.Reset();
					return false;
				});
			}
			return false;
		});
	}
}

void UPartyMember::FDebugInitializer::ClearWarningTimers()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
}

FString UPartyMember::FDebugInitializer::GetWaitingForString() const
{
	TArray<FString> WaitingForStrings;
	WaitingForStrings.Emplace(TEXT("Flags: ") + FString::JoinBy(MakeFlagsRange(Parent.InitializingFlags), TEXT("|"), [](UPartyMember::EInitializingFlags Flag)
	{
		switch (Flag)
		{
		case UPartyMember::EInitializingFlags::SocialUsers: return TEXT("SocialUsers");
		case UPartyMember::EInitializingFlags::InitialMemberData: return TEXT("InitialMemberData");
		default: return TEXT("Unknown");
		}
	}));
	for (const FString& PendingAction : PendingActions)
	{
		WaitingForStrings.Emplace(PendingAction);
	}
	return FString::Join(WaitingForStrings, TEXT(","));
}

namespace UE::OnlineFramework
{
void OnPartyMemberInitializeComplete(UPartyMember& InPartyMember, FSimpleDelegate&& InDelegate)
{
	if (InPartyMember.IsInitialized())
	{
		InDelegate.ExecuteIfBound();
	}
	else
	{
		InPartyMember.OnInitializationComplete().Add(MoveTemp(InDelegate));
	}
}
}
