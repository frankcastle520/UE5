// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaPlayableGroupSubsystem.h"

#include "Playable/AvaPlayableGroupManager.h"
#include "Playable/PlayableGroups/AvaGameViewportPlayableGroup.h"

void UAvaPlayableGroupSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAvaPlayableGroupSubsystem::Deinitialize()
{
	if (PlayableGroupManager)
	{
		TArray<TWeakObjectPtr<UAvaPlayableGroup>> PlayableGroupsWeak = PlayableGroupManager->GetPlayableGroups();
		for (const TWeakObjectPtr<UAvaPlayableGroup>& PlayableGroupWeak : PlayableGroupsWeak)
		{
			UAvaGameViewportPlayableGroup* GameViewportPlayableGroup = Cast<UAvaGameViewportPlayableGroup>(PlayableGroupWeak.Get());
	
			if (GameViewportPlayableGroup && GameViewportPlayableGroup->GetGameInstance() == GetGameInstance())
			{
				GameViewportPlayableGroup->DetachGameInstance();
			}
		}
	}
	
	PlayableGroupManager = nullptr;
	
	Super::Deinitialize();
}
