// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Blueprint.h"
#include "ActorModifierCoreBlueprint.generated.h"

/**
 * Blueprint object that defines a user modifier
 */
UCLASS(MinimalAPI)
class UActorModifierCoreBlueprint : public UBlueprint
{
	GENERATED_BODY()

	//~ Begin UBlueprint
#if WITH_EDITOR
	virtual UClass* GetBlueprintClass() const override;
	virtual void GetReparentingRules(TSet<const UClass*>& OutAllowedChildrenOfClasses, TSet<const UClass*>& OutDisallowedChildrenOfClasses) const override;
	virtual bool SupportedByDefaultBlueprintFactory() const override;
#endif
	//~ End UBlueprint
};
