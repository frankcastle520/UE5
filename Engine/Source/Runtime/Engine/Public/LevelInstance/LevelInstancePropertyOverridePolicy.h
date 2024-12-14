// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreFwd.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "WorldPartition/WorldPartitionPropertyOverride.h"
#include "LevelInstancePropertyOverridePolicy.generated.h"

UCLASS(Abstract, NotBlueprintable)
class ULevelInstancePropertyOverridePolicy : public UWorldPartitionPropertyOverridePolicy
{
	GENERATED_BODY()
public:
	ULevelInstancePropertyOverridePolicy() {}
	virtual ~ULevelInstancePropertyOverridePolicy() {}

#if WITH_EDITOR
	ENGINE_API virtual bool CanOverrideProperty(const FProperty* InProperty) const override final;

protected:
	virtual bool CanOverridePropertyImpl(const FProperty* InProperty) const { return true; }
#endif
};