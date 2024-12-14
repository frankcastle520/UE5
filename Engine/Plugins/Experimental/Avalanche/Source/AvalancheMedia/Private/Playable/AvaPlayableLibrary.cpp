// Copyright Epic Games, Inc. All Rights Reserved.

#include "Playable/AvaPlayableLibrary.h"

#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Playable/AvaPlayable.h"
#include "Playable/AvaPlayableAssetUserData.h"
#include "Playable/AvaPlayableGroup.h"
#include "Playable/Playables/AvaPlayableLevelStreaming.h"
#include "Playable/Transition/AvaPlayableTransition.h"

namespace UE::AvaPlayableLibrary::Private
{
	ULevel* GetLevel(const UObject* InWorldContextObject)
	{
		if (!InWorldContextObject)
		{
			return nullptr;
		}

		ULevel* Level = InWorldContextObject->GetTypedOuter<ULevel>();
		if (!Level && GEngine)
		{
			if (const UWorld* World = GEngine->GetWorldFromContextObject(InWorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
			{
				Level = World->PersistentLevel;
			}
		}
		return Level;
	}
}


UAvaPlayable* UAvaPlayableLibrary::GetPlayable(const UObject* InWorldContextObject)
{
	using namespace UE::AvaPlayableLibrary::Private;
	ULevel* Level = GetLevel(InWorldContextObject);
	if (!Level)
	{
		return nullptr;
	}

	if (const UAvaPlayableAssetUserData* PlayableUserData = Level->GetAssetUserData<UAvaPlayableAssetUserData>())
	{
		if (UAvaPlayable* Playable = PlayableUserData->PlayableWeak.Get())
		{
			return Playable;
		}
	}

	return nullptr;
}

UAvaPlayableTransition* UAvaPlayableLibrary::GetPlayableTransition(const UAvaPlayable* InPlayable)
{
	if (!InPlayable)
	{
		return nullptr;
	}

	UAvaPlayableGroup* PlayableGroup = InPlayable->GetPlayableGroup();
	if (!PlayableGroup)
	{
		return nullptr;
	}

	UAvaPlayableTransition* FoundTransition = nullptr;
	PlayableGroup->ForEachPlayableTransition([&FoundTransition, InPlayable](UAvaPlayableTransition* InTransition)
	{
		if (InTransition->IsEnterPlayable(InPlayable)
			|| InTransition->IsPlayingPlayable(InPlayable)
			|| InTransition->IsExitPlayable(InPlayable))
		{
			FoundTransition = InTransition;
			return false;
		}		
		return true;
	});

	return FoundTransition;
}

bool UAvaPlayableLibrary::UpdatePlayableRemoteControlValues(const UObject* InWorldContextObject)
{
	if (UAvaPlayable* Playable = GetPlayable(InWorldContextObject))
	{
		if (UAvaPlayableTransition* Transition = GetPlayableTransition(Playable))
		{
			// Assume that if the RC Values need to be updated, it's because of an Enter Playable not yet having its Update RC called
			constexpr bool bIsEnterPlayable = true;

			if (const TSharedPtr<FAvaPlayableRemoteControlValues> RemoteControlValues = Transition->GetValuesForPlayable(Playable, bIsEnterPlayable))
			{
				if (Playable->UpdateRemoteControlCommand(RemoteControlValues.ToSharedRef()) == EAvaPlayableCommandResult::Executed)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool UAvaPlayableLibrary::IsPlayableHidden(const UObject* InWorldContextObject)
{
	const UAvaPlayableLevelStreaming* LevelStreamingPlayable = Cast<UAvaPlayableLevelStreaming>(GetPlayable(InWorldContextObject));
	return LevelStreamingPlayable ? LevelStreamingPlayable->GetShouldBeHidden() : false;
}

bool UAvaPlayableLibrary::SetPlayableHidden(const UObject* InWorldContextObject, bool bInShouldBeHidden)
{
	if (UAvaPlayableLevelStreaming* LevelStreamingPlayable = Cast<UAvaPlayableLevelStreaming>(GetPlayable(InWorldContextObject)))
	{
		LevelStreamingPlayable->SetShouldBeHidden(bInShouldBeHidden);
		return true;
	}
	return false;
}

