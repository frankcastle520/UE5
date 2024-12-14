// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreFwd.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "LevelInstance/LevelInstancePropertyOverridePolicy.h"
#include "LevelInstancePropertyOverrideSamplePolicy.generated.h"

UCLASS(MinimalAPI)
class ULevelInstancePropertyOverrideSamplePolicy : public ULevelInstancePropertyOverridePolicy
{
	GENERATED_BODY()
public:
	ULevelInstancePropertyOverrideSamplePolicy() {}
	virtual ~ULevelInstancePropertyOverrideSamplePolicy() {}

#if WITH_EDITOR
	virtual bool CanOverridePropertyImpl(const FProperty* InProperty) const override;
#endif
};