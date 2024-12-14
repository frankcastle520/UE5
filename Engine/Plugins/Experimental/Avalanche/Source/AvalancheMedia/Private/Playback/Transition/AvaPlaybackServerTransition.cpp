// Copyright Epic Games, Inc. All Rights Reserved.

#include "Playback/Transition/AvaPlaybackServerTransition.h"

#include "Playable/Transition/AvaPlayableTransition.h"
#include "Playback/AvaPlaybackManager.h"
#include "Playback/AvaPlaybackServer.h"
#include "Playback/AvaPlaybackUtils.h"

namespace UE::AvaPlaybackServerTransition::Private
{
	FString GetPrettyPlaybackInstanceInfo(const FAvaPlaybackInstance* InPlaybackInstance)
	{
		if (InPlaybackInstance)
		{
			return FString::Printf(TEXT("Id:%s, Asset:%s, Channel:%s, UserData:\"%s\""),
				*InPlaybackInstance->GetInstanceId().ToString(),
				*InPlaybackInstance->GetSourcePath().GetAssetName(),
				*InPlaybackInstance->GetChannelName(),
				*InPlaybackInstance->GetInstanceUserData());
		}
		return TEXT("");
	}

	UAvaPlayable* GetPlayable(const FAvaPlaybackInstance* InPlaybackInstance)
	{
		if (InPlaybackInstance && InPlaybackInstance->GetPlayback())
		{
			return InPlaybackInstance->GetPlayback()->GetFirstPlayable();
		}
		return nullptr;
	}

	TSharedPtr<FAvaPlaybackInstance> FindInstanceForPlayable(const TArray<TWeakPtr<FAvaPlaybackInstance>>& InPlaybackInstancesWeak, const UAvaPlayable* InPlayable)
	{
		for (const TWeakPtr<FAvaPlaybackInstance>& InstanceWeak : InPlaybackInstancesWeak)
		{
			TSharedPtr<FAvaPlaybackInstance> Instance = InstanceWeak.Pin();
			if (GetPlayable(Instance.Get()) == InPlayable)
			{
				return Instance;
			}
		}
		return nullptr;
	}

	TSharedPtr<FAvaPlaybackInstance> FindInstance(const TArray<TWeakPtr<FAvaPlaybackInstance>>& InPlaybackInstancesWeak, const FGuid& InInstanceId)
	{
		for (const TWeakPtr<FAvaPlaybackInstance>& InstanceWeak : InPlaybackInstancesWeak)
		{
			TSharedPtr<FAvaPlaybackInstance> Instance = InstanceWeak.Pin();
			if (Instance && Instance->GetInstanceId() == InInstanceId)
			{
				return Instance;
			}
		}
		return nullptr;
	}

	
	// Check if the given set of playback instances are preventing the start of the transition.
	bool CanStartTransition(const UAvaPlaybackServerTransition* InTransition,
		const TArray<TWeakPtr<FAvaPlaybackInstance>>& InPlaybackInstancesWeak, bool& bOutShouldDiscard)
	{
		using namespace UE::AvaPlayback::Utils;
		for (const TWeakPtr<FAvaPlaybackInstance>& InstanceWeak : InPlaybackInstancesWeak)
		{
			const TSharedPtr<FAvaPlaybackInstance> Instance = InstanceWeak.Pin();
			if (!Instance)
			{
				// For now, we discard transitions with invalid instance.
				UE_LOG(LogAvaPlaybackServer, Warning,
					TEXT("%s Discarding Playback Transition {%s}. Reason: Invalid Instance. "),
					*GetBriefFrameInfo(), *InTransition->GetPrettyTransitionInfo());

				bOutShouldDiscard = true;
				return false;
			}
			
			const UAvaPlayable* Playable = GetPlayable(Instance.Get());
			if (!Playable)
			{
				bOutShouldDiscard = false;
				return false;	// Playable not yet created.
			}

			const EAvaPlayableStatus PlayableStatus = Playable->GetPlayableStatus();

			if (PlayableStatus == EAvaPlayableStatus::Unknown
				|| PlayableStatus == EAvaPlayableStatus::Error)
			{
				UE_LOG(LogAvaPlaybackServer, Warning,
            		TEXT("%s Discarding Playback Transition {%s}. Reason: Playable status: \"%s\". "),
            		*GetBriefFrameInfo(), *InTransition->GetPrettyTransitionInfo(), *StaticEnumToString(PlayableStatus));

				// Discard the command
				bOutShouldDiscard = true;
				return false;
			}

			// todo: this might cause commands to become stale and fill the pending command list
			if (PlayableStatus == EAvaPlayableStatus::Unloaded)
			{
				UE_LOG(LogAvaPlaybackServer, Warning,
					TEXT("%s Playback Transition {%s}: Playable \"%s\" (Id:%s) is unloaded."),
					*GetBriefFrameInfo(), *InTransition->GetPrettyTransitionInfo(), *Playable->GetSourceAssetPath().GetAssetName(), *Playable->GetInstanceId().ToString());

				return false;
			}

			// Asset status must be visible to run the command.
			// If not visible, the components are not yet added to the world.
			if (PlayableStatus != EAvaPlayableStatus::Visible)
			{
				// Keep the command in the queue for next tick.
				bOutShouldDiscard = false;
				return false;
			}
		}

		bOutShouldDiscard = true;
		return true;
	}
}

UAvaPlaybackServerTransition* UAvaPlaybackServerTransition::MakeNew(const TSharedPtr<FAvaPlaybackServer>& InPlaybackServer)
{
	UAvaPlaybackServerTransition* NewTransition = NewObject<UAvaPlaybackServerTransition>();
	NewTransition->PlaybackServerWeak = InPlaybackServer.ToWeakPtr();
	return NewTransition;
}

void UAvaPlaybackServerTransition::AddPendingEnterInstanceIds(const TArray<FGuid>& InInstanceIds)
{
	PendingEnterInstanceIds.Reserve(InInstanceIds.Num());
	
	for (const FGuid& InstanceId : InInstanceIds)
	{
		PendingEnterInstanceIds.AddUnique(InstanceId);		
	}
}

void UAvaPlaybackServerTransition::AddPendingPlayingInstanceId(const FGuid& InInstanceId)
{
	PendingPlayingInstanceIds.AddUnique(InInstanceId);
}
	
void UAvaPlaybackServerTransition::AddPendingExitInstanceId(const FGuid& InInstanceId)
{
	PendingExitInstanceIds.AddUnique(InInstanceId);
}

void UAvaPlaybackServerTransition::SetEnterValues(const TArray<FAvaPlayableRemoteControlValues>& InEnterValues)
{
	EnterValues.Reserve(InEnterValues.Num());
	for (const FAvaPlayableRemoteControlValues& Values : InEnterValues)
	{
		EnterValues.Add(MakeShared<FAvaPlayableRemoteControlValues>(Values));
	}
}

bool UAvaPlaybackServerTransition::AddEnterInstance(const TSharedPtr<FAvaPlaybackInstance>& InPlaybackInstance)
{
	if (!InPlaybackInstance)
	{
		return false;
	}
	
	// Register this transition as a visibility constraint.
	using namespace UE::AvaPlaybackServerTransition::Private;
	if (const UAvaPlayable* Playable = GetPlayable(InPlaybackInstance.Get()))
	{
		if (UAvaPlayableGroup* PlayableGroup = Playable->GetPlayableGroup())
		{
			PlayableGroup->RegisterVisibilityConstraint(this);
		}
	}
	else if (UAvaPlaybackGraph* Playback = InPlaybackInstance->GetPlayback())
	{
		// If the playable is not created yet, register to the creation event.
		Playback->OnPlayableCreated.AddUObject(this, &UAvaPlaybackServerTransition::OnPlayableCreated);
	}
	
	return AddPlaybackInstance(InPlaybackInstance, EnterPlaybackInstancesWeak);
}

bool UAvaPlaybackServerTransition::AddPlayingInstance(const TSharedPtr<FAvaPlaybackInstance>& InPlaybackInstance)
{
	return AddPlaybackInstance(InPlaybackInstance, PlayingPlaybackInstancesWeak);
}

bool UAvaPlaybackServerTransition::AddExitInstance(const TSharedPtr<FAvaPlaybackInstance>& InPlaybackInstance)
{
	return AddPlaybackInstance(InPlaybackInstance, ExitPlaybackInstancesWeak);
}

void UAvaPlaybackServerTransition::TryResolveInstances(const FAvaPlaybackServer& InPlaybackServer)
{
	for (TArray<FGuid>::TIterator InstanceIdIt(PendingEnterInstanceIds); InstanceIdIt; ++InstanceIdIt)
	{
		if (TSharedPtr<FAvaPlaybackInstance> Instance = InPlaybackServer.FindActivePlaybackInstance(*InstanceIdIt))
		{
			AddEnterInstance(Instance);
			InstanceIdIt.RemoveCurrent();
		}
	}
	
	for (TArray<FGuid>::TIterator InstanceIdIt(PendingPlayingInstanceIds); InstanceIdIt; ++InstanceIdIt)
	{
		if (TSharedPtr<FAvaPlaybackInstance> Instance = InPlaybackServer.FindActivePlaybackInstance(*InstanceIdIt))
		{
			AddPlayingInstance(Instance);
			InstanceIdIt.RemoveCurrent();
		}
	}

	for (TArray<FGuid>::TIterator InstanceIdIt(PendingExitInstanceIds); InstanceIdIt; ++InstanceIdIt)
	{
		if (TSharedPtr<FAvaPlaybackInstance> Instance = InPlaybackServer.FindActivePlaybackInstance(*InstanceIdIt))
		{
			AddExitInstance(Instance);
			InstanceIdIt.RemoveCurrent();
		}
	}
}

bool UAvaPlaybackServerTransition::ContainsInstance(const FGuid& InInstanceId) const
{
	if (PendingEnterInstanceIds.Contains(InInstanceId)
		|| PendingPlayingInstanceIds.Contains(InInstanceId)
		|| PendingExitInstanceIds.Contains(InInstanceId))
	{
		return true;
	}

	using namespace UE::AvaPlaybackServerTransition::Private;
	if (FindInstance(EnterPlaybackInstancesWeak, InInstanceId).IsValid()
		|| FindInstance(PlayingPlaybackInstancesWeak, InInstanceId).IsValid()
		|| FindInstance(ExitPlaybackInstancesWeak, InInstanceId).IsValid())
	{
		return true;
	}
	return false;
}

bool UAvaPlaybackServerTransition::IsVisibilityConstrained(const UAvaPlayable* InPlayable) const
{
	using namespace UE::AvaPlaybackServerTransition::Private;
	bool bAllPlayablesLoaded = true;
	bool bIsPlayableInThisTransition = false;
	
	for (const TWeakPtr<FAvaPlaybackInstance>& InstanceWeak : EnterPlaybackInstancesWeak)
	{
		if (const TSharedPtr<FAvaPlaybackInstance> Instance = InstanceWeak.Pin())
		{
			if (const UAvaPlayable* Playable = GetPlayable(Instance.Get()))
			{
				if (Playable == InPlayable)
				{
					bIsPlayableInThisTransition = true;
				}
				const EAvaPlayableStatus PlayableStatus = Playable->GetPlayableStatus();
				if (PlayableStatus != EAvaPlayableStatus::Loaded && PlayableStatus != EAvaPlayableStatus::Visible)
				{
					bAllPlayablesLoaded = false;
				}
			}
		}
	}
	return bIsPlayableInThisTransition && !bAllPlayablesLoaded;
}


bool UAvaPlaybackServerTransition::CanStart(bool& bOutShouldDiscard) const
{
	using namespace UE::AvaPlaybackServerTransition::Private;

	// Wait for any unresolved instances to be loaded.
	if (!PendingEnterInstanceIds.IsEmpty()
		|| !PendingPlayingInstanceIds.IsEmpty()
		|| !PendingExitInstanceIds.IsEmpty())
	{
		bOutShouldDiscard = false;
		return false;
	}

	if (!CanStartTransition(this, EnterPlaybackInstancesWeak, bOutShouldDiscard))
	{
		return false;
	}

	// Note: need to check the "non-entering" instances too in case the playback commands got delayed
	// causing playables to also need loading/recovering.
	
	if (!CanStartTransition(this, PlayingPlaybackInstancesWeak, bOutShouldDiscard))
	{
		return false;
	}

	if (!CanStartTransition(this, ExitPlaybackInstancesWeak, bOutShouldDiscard))
	{
		return false;
	}

	bOutShouldDiscard = true;
	return true;
}

void UAvaPlaybackServerTransition::Start()
{
	RegisterToPlayableTransitionEvent();

	// May fail if playables are not loaded yet. Playables are loaded
	// when the playback object has ticked at least one.
	MakePlayableTransition();

	bool bTransitionStarted = false;
	
	if (PlayableTransition)
	{
		LogDetailedTransitionInfo();

		// Todo: validate the level streaming playables are finished streaming the asset.
		// Otherwise, transition start must be queued on playable streaming events. 
		bTransitionStarted = PlayableTransition->Start();
	}

	if (!bTransitionStarted)
	{
		Stop();
	}
}

void UAvaPlaybackServerTransition::Stop()
{
	if (PlayableTransition)
	{
		PlayableTransition->Stop();
		PlayableTransition = nullptr;
	}

	for (const TWeakPtr<FAvaPlaybackInstance>& InstanceWeak : EnterPlaybackInstancesWeak)
	{
		if (const TSharedPtr<FAvaPlaybackInstance> Instance = InstanceWeak.Pin())
		{
			if (UAvaPlaybackGraph* Playback = Instance->GetPlayback())
			{
				Playback->OnPlayableCreated.RemoveAll(this);
			}
		}
	}
	
	UnregisterFromPlayableTransitionEvent();
	
	// Remove transition from server.
	if (const TSharedPtr<FAvaPlaybackServer> PlaybackServer = PlaybackServerWeak.Pin())
	{
		if (!PlaybackServer->RemovePlaybackInstanceTransition(TransitionId))
		{
			UE_LOG(LogAvaPlaybackServer, Error,
				TEXT("%s Failed to remove Playback Transition {%s}. Reason: not found in server's active transitions."),
				*UE::AvaPlayback::Utils::GetBriefFrameInfo(), *GetPrettyTransitionInfo());
		}
	}
}

bool UAvaPlaybackServerTransition::IsRunning() const
{
	return PlayableTransition ? PlayableTransition->IsRunning() : false;
}

FString UAvaPlaybackServerTransition::GetPrettyTransitionInfo() const
{
	return FString::Printf(TEXT("Id:%s, Channel:%s, Client:%s"),
		*TransitionId.ToString(), *ChannelName.ToString(), *ClientName);
}

FString UAvaPlaybackServerTransition::GetBriefTransitionDescription() const
{
	auto MakeInstanceIdList = [](const TArray<TWeakPtr<FAvaPlaybackInstance>>& InPlaybackInstancesWeak) -> FString
	{
		FString InstanceIdList;
		for (const TWeakPtr<FAvaPlaybackInstance>& InstanceWeak : InPlaybackInstancesWeak)
		{
			if (const TSharedPtr<FAvaPlaybackInstance> Instance = InstanceWeak.Pin())
			{
				InstanceIdList += FString::Printf(TEXT("%s%s"), InstanceIdList.IsEmpty() ? TEXT("") : TEXT(", "), *Instance->GetInstanceId().ToString());
			}
		}
		return InstanceIdList.IsEmpty() ? TEXT("None") : InstanceIdList;
	};
	
	const FString EnterInstanceList = MakeInstanceIdList(EnterPlaybackInstancesWeak);
	const FString PlayingInstanceList = MakeInstanceIdList(PlayingPlaybackInstancesWeak);
	const FString ExitInstanceList = MakeInstanceIdList(ExitPlaybackInstancesWeak);

	return FString::Printf(TEXT("Enter Instance(s): [%s], Playing Instance(s): [%s], Exit Instance(s): [%s]."), *EnterInstanceList, *PlayingInstanceList, *ExitInstanceList);
}

TSharedPtr<FAvaPlaybackInstance> UAvaPlaybackServerTransition::FindInstanceForPlayable(const UAvaPlayable* InPlayable)
{
	using namespace UE::AvaPlaybackServerTransition;
	if (!InPlayable)
	{
		return nullptr;
	}

	TSharedPtr<FAvaPlaybackInstance> Instance = Private::FindInstanceForPlayable(EnterPlaybackInstancesWeak, InPlayable);
	if (Instance)
	{
		return Instance;
	}
	
	Instance = Private::FindInstanceForPlayable(PlayingPlaybackInstancesWeak, InPlayable);
	if (Instance)
	{
		return Instance;
	}

	Instance = Private::FindInstanceForPlayable(ExitPlaybackInstancesWeak, InPlayable);
	if (Instance)
	{
		return Instance;
	}
	return nullptr;
}

void UAvaPlaybackServerTransition::OnTransitionEvent(UAvaPlayable* InPlayable, UAvaPlayableTransition* InTransition, EAvaPlayableTransitionEventFlags InTransitionFlags)
{
	using namespace UE::AvaPlaybackServerTransition::Private;
	using namespace UE::AvaPlayback::Utils;
	
	// not this transition.
	if (InTransition != PlayableTransition || PlayableTransition == nullptr)
	{
		return;
	}

	const TSharedPtr<FAvaPlaybackServer> PlaybackServer = PlaybackServerWeak.Pin();

	// Find the page player for this playable
	if (const TSharedPtr<FAvaPlaybackInstance> Instance = FindInstanceForPlayable(InPlayable))
	{
		// Relay the transition event back to the client
		if (PlaybackServer)
		{
			PlaybackServer->SendPlayableTransitionEvent(TransitionId, InPlayable->GetInstanceId(), InTransitionFlags, ChannelName, ClientName);
		}
		
		if (EnumHasAnyFlags(InTransitionFlags, EAvaPlayableTransitionEventFlags::StopPlayable))
		{
			// Validating that we are not removing an "enter" playable.
			if (PlayableTransition->IsEnterPlayable(InPlayable))
			{
				UE_LOG(LogAvaPlaybackServer, Error,
					TEXT("%s Playback Transition {%s} Error: An \"enter\" playable is being discarded for instance {%s}."),
					*GetBriefFrameInfo(), *GetPrettyTransitionInfo(), *GetPrettyPlaybackInstanceInfo(Instance.Get()));
			}

			// See UAvaRundownPagePlayer::Stop()
			const EAvaPlaybackStopOptions PlaybackStopOptions = bUnloadDiscardedInstances ?
				EAvaPlaybackStopOptions::Default | EAvaPlaybackStopOptions::Unload : EAvaPlaybackStopOptions::Default;	
			Instance->GetPlayback()->Stop(PlaybackStopOptions);
			
			if (bUnloadDiscardedInstances)
			{
				Instance->Unload();
				// Remove instance from the server.
				if (PlaybackServer)
				{
					if (!PlaybackServer->RemoveActivePlaybackInstance(Instance->GetInstanceId()))
					{
						UE_LOG(LogAvaPlaybackServer, Error,
							TEXT("%s Playback Transition {%s} Error: \"exit\" instance {%s} was not found in server's active instances. "),
							*GetBriefFrameInfo(), *GetPrettyTransitionInfo(), *GetPrettyPlaybackInstanceInfo(Instance.Get()));
					}
				}
			}
			else
			{
				Instance->Recycle();
			}
		}
	}

	if (EnumHasAnyFlags(InTransitionFlags, EAvaPlayableTransitionEventFlags::Finished))
	{
		if (PlaybackServer)
		{
			PlaybackServer->SendPlayableTransitionEvent(TransitionId, FGuid(), InTransitionFlags, ChannelName, ClientName);
		}
		
		Stop();
	}
}

void UAvaPlaybackServerTransition::OnPlayableCreated(UAvaPlaybackGraph* InPlayback, UAvaPlayable* InPlayable)
{
	if (UAvaPlayableGroup* PlayableGroup = InPlayable->GetPlayableGroup())
	{
		PlayableGroup->RegisterVisibilityConstraint(this);
	}
}

void UAvaPlaybackServerTransition::MakePlayableTransition()
{
	using namespace UE::AvaPlaybackServerTransition::Private;
	using namespace UE::AvaPlayback::Utils;
	
	FAvaPlayableTransitionBuilder TransitionBuilder;

	auto AddInstancesToBuilder = [&TransitionBuilder, this](const TArray<TWeakPtr<FAvaPlaybackInstance>>& InPlaybackInstancesWeak,
		const TCHAR* InCategory, EAvaPlayableTransitionEntryRole InEntryRole, bool bInAllowMultipleAdd)
	{
		using namespace UE::AvaPlaybackServerTransition::Private;
		int32 ArrayIndex = 0;
		for (const TWeakPtr<FAvaPlaybackInstance>& InstanceWeak : InPlaybackInstancesWeak)
		{
			if (const TSharedPtr<FAvaPlaybackInstance> Instance = InstanceWeak.Pin())
			{
				if (UAvaPlayable* Playable = GetPlayable(Instance.Get()))
				{
					const bool bPlayableAdded = TransitionBuilder.AddPlayable(Playable, InEntryRole, bInAllowMultipleAdd);
					if (InEntryRole == EAvaPlayableTransitionEntryRole::Enter && bPlayableAdded)
					{
						TransitionBuilder.AddEnterPlayableValues(EnterValues.IsValidIndex(ArrayIndex) ? EnterValues[ArrayIndex] : nullptr);	
					}
				}
				else
				{
					// If this happens, likely the playable is not yet loaded.
					UE_LOG(LogAvaPlaybackServer, Error,
						TEXT("%s Playback Transition {%s} Error: Failed to retrieve \"%s\" playable for instance {%s}."),
						*GetBriefFrameInfo(), *GetPrettyTransitionInfo(), InCategory, *GetPrettyPlaybackInstanceInfo(Instance.Get()));
				}
			}
			++ArrayIndex;
		}
	};

	constexpr bool bAllowMultipleAddEnter = false;
	const bool bAllowMultipleAddPlaying = EnumHasAnyFlags(TransitionFlags, EAvaPlayableTransitionFlags::HasReusedPlayables);
	constexpr bool bAllowMultipleAddExit = false;
 
	AddInstancesToBuilder(EnterPlaybackInstancesWeak, TEXT("Enter"), EAvaPlayableTransitionEntryRole::Enter, bAllowMultipleAddEnter);
	AddInstancesToBuilder(PlayingPlaybackInstancesWeak, TEXT("Playing"), EAvaPlayableTransitionEntryRole::Playing,  bAllowMultipleAddPlaying);
	AddInstancesToBuilder(ExitPlaybackInstancesWeak, TEXT("Exit"), EAvaPlayableTransitionEntryRole::Exit,  bAllowMultipleAddExit);
	PlayableTransition = TransitionBuilder.MakeTransition(this, TransitionId);

	if (PlayableTransition)
	{
		PlayableTransition->SetTransitionFlags(TransitionFlags);
	}
}

void UAvaPlaybackServerTransition::LogDetailedTransitionInfo() const
{
	using namespace UE::AvaPlayback::Utils;
	UE_LOG(LogAvaPlaybackServer, Verbose, TEXT("%s Playback Transition {%s}:"), *GetBriefFrameInfo(), *GetPrettyTransitionInfo());

	auto LogInstances = [](const TArray<TWeakPtr<FAvaPlaybackInstance>>& InPlaybackInstancesWeak, const TCHAR* InCategory)
	{
		using namespace UE::AvaPlaybackServerTransition::Private;

		for (const TWeakPtr<FAvaPlaybackInstance>& InstanceWeak : InPlaybackInstancesWeak)
		{
			if (const TSharedPtr<FAvaPlaybackInstance> Instance = InstanceWeak.Pin())
			{
				UE_LOG(LogAvaPlaybackServer, Verbose, TEXT("- %s Instance: {%s}."), InCategory, *GetPrettyPlaybackInstanceInfo(Instance.Get()));
			}
		}
	};

	LogInstances(EnterPlaybackInstancesWeak, TEXT("Enter"));
	LogInstances(PlayingPlaybackInstancesWeak, TEXT("Playing"));
	LogInstances(ExitPlaybackInstancesWeak, TEXT("Exit"));
}

void UAvaPlaybackServerTransition::RegisterToPlayableTransitionEvent()
{
	UAvaPlayable::OnTransitionEvent().RemoveAll(this);
	UAvaPlayable::OnTransitionEvent().AddUObject(this, &UAvaPlaybackServerTransition::OnTransitionEvent);
}

void UAvaPlaybackServerTransition::UnregisterFromPlayableTransitionEvent() const
{
	UAvaPlayable::OnTransitionEvent().RemoveAll(this);
}

bool UAvaPlaybackServerTransition::AddPlaybackInstance(const TSharedPtr<FAvaPlaybackInstance>& InPlaybackInstance, TArray<TWeakPtr<FAvaPlaybackInstance>>& OutPlaybackInstancesWeak)
{
	if (!InPlaybackInstance)
	{
		return false;	
	}

	OutPlaybackInstancesWeak.Add(InPlaybackInstance);
	UpdateChannelName(InPlaybackInstance.Get());
	return true;
}

void UAvaPlaybackServerTransition::UpdateChannelName(const FAvaPlaybackInstance* InPlaybackInstance)
{
	if (ChannelName.IsNone())
	{
		ChannelName = InPlaybackInstance->GetChannelFName();
	}
	else
	{
		using namespace UE::AvaPlaybackServerTransition::Private;
		using namespace UE::AvaPlayback::Utils;

		// Validate the channel is the same.
		if (ChannelName != InPlaybackInstance->GetChannelFName())
		{
			UE_LOG(LogAvaPlaybackServer, Error,
				TEXT("%s Playback Transition {%s}: Adding Playback Instance {%s} in a different channel than previous playback instance (\"%s\")."),
				*GetBriefFrameInfo(), *GetPrettyTransitionInfo(), *GetPrettyPlaybackInstanceInfo(InPlaybackInstance), *ChannelName.ToString());
		}
	}
}