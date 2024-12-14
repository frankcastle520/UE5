// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviors/ScriptableToolDoubleClickBehavior.h"
#include "BaseBehaviors/DoubleClickBehavior.h"

USingleClickInputBehavior* UScriptableToolDoubleClickBehavior::CreateNewBehavior() const
{
	return NewObject<UDoubleClickInputBehavior>();
}
