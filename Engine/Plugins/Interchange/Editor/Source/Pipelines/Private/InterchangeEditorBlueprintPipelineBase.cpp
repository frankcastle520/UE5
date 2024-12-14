// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeEditorBlueprintPipelineBase.h"
#include "Editor.h"

UWorld* UInterchangeEditorPipelineBase::GetWorld() const
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext(false).World();
	}

	return Super::GetWorld();
}
