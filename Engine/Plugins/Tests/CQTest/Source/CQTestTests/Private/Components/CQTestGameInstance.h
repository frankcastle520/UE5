// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/GameInstance.h"

#include "CQTestGameInstance.generated.h"

/** Basic GameInstance with a test value. */
UCLASS()
class UCQGameInstanceClass : public UGameInstance
{
	GENERATED_BODY()

public:
	int TestValue{ 42 };
};
