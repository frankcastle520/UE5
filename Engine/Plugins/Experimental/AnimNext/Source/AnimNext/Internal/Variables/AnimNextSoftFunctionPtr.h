// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/SoftObjectPtr.h"
#include "AnimNextSoftFunctionPtr.generated.h"

/** Struct wrapper around TSoftObjectPtr<UFunction>, used to allow RigVM support */
USTRUCT()
struct ANIMNEXT_API FAnimNextSoftFunctionPtr
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UFunction> SoftObjectPtr;
};