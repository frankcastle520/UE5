// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/AvaInteractiveToolsActorToolBase.h"
#include "GameFramework/Actor.h"

bool UAvaInteractiveToolsActorToolBase::OnBegin()
{
	if (ActorClass == nullptr)
	{
		return false;
	}

	return Super::OnBegin();
}

void UAvaInteractiveToolsActorToolBase::DefaultAction()
{
	if (OnBegin())
	{
		SpawnedActor = SpawnActor(ActorClass, /** Preview */false);

		OnComplete();
	}

	Super::DefaultAction();
}

bool UAvaInteractiveToolsActorToolBase::UseIdentityRotation() const
{
	return ConditionalIdentityRotation();
}
