// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstancePropertyOverrideSamplePolicy.h"

#if WITH_EDITOR
bool ULevelInstancePropertyOverrideSamplePolicy::CanOverridePropertyImpl(const FProperty* InProperty) const
{
	// Add code here to prevent edit on properties
	return Super::CanOverridePropertyImpl(InProperty);
}
#endif