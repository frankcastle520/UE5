// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "Templates/SubclassOf.h"
#include "ActorModifierCoreBlueprintFactory.generated.h"

class UActorModifierCoreBlueprintBase;

/** Factory responsible to create a UActorModifierCoreBlueprint object */
UCLASS()
class UActorModifierCoreBlueprintFactory : public UFactory
{
	GENERATED_BODY()

public:
	UActorModifierCoreBlueprintFactory();

	//~ Begin UFactory
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn, FName InCallingContext) override;
	//~ End UFactory

	UPROPERTY()
	TSubclassOf<UActorModifierCoreBlueprintBase> ParentClass;
};
