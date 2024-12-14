// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "SVGActorEditorComponent.h"
#include "SVGActor.h"

ASVGActor* USVGActorEditorComponent::GetSVGActor() const
{
	if (ASVGActor* SVGActor = GetTypedOuter<ASVGActor>())
	{
		return SVGActor;
	}

	return nullptr;
}
#endif
