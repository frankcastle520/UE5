// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ChildActorComponent.h"
#include "AutoRTFMTestChildActorComponent.generated.h"

UCLASS()
class UAutoRTFMTestChildActorComponent : public UChildActorComponent
{
    GENERATED_BODY()

public:
	void ForceActorClass(UClass* const Class)
	{
		SetChildActorClass(Class);
	}
};
