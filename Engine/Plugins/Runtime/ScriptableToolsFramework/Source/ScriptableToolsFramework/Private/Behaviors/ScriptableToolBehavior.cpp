// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScriptableToolBehavior.h"

void UScriptableToolBehavior::SetDefaultPriority(const FInputCapturePriority& Priority)
{
	GetWrappedBehavior()->SetDefaultPriority(Priority);
}


