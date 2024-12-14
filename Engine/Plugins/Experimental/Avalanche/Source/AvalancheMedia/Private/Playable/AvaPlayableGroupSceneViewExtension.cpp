// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaPlayableGroupSceneViewExtension.h"

#include "Playable/AvaPlayableGroup.h"
#include "SceneView.h"

FAvaPlayableGroupSceneViewExtension::FAvaPlayableGroupSceneViewExtension(const FAutoRegister& InAutoReg)
	: FSceneViewExtensionBase(InAutoReg)
{
}

void FAvaPlayableGroupSceneViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	if (!InViewFamily.Scene)
	{
		return;
	}
	
	const UWorld* ViewWorld = InViewFamily.Scene->GetWorld();
	if (!ViewWorld)
	{
		return;
	}

	if (UAvaPlayableGroup* ViewPlayableGroup = UAvaPlayableGroup::FindPlayableGroupForWorld(ViewWorld))
	{
		ViewPlayableGroup->SetupView(InViewFamily, InView);
	}
}