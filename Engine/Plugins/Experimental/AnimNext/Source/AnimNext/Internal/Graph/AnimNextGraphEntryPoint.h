// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextGraphState.h"
#include "TraitCore/EntryPointHandle.h"
#include "AnimNextGraphEntryPoint.generated.h"

USTRUCT()
struct FAnimNextGraphEntryPoint
{
	GENERATED_BODY()

	// The name of the entry point
	UPROPERTY()
	FName EntryPointName;

	// This is a handle to the root trait for a graph
	UPROPERTY()
	FAnimNextEntryPointHandle RootTraitHandle;

	// Default state for this entry point
	UPROPERTY()
	FAnimNextGraphState DefaultState;
};

