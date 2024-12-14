// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextModuleContextData.generated.h"

struct FAnimNextModuleInstance;

USTRUCT()
struct FAnimNextModuleContextData
{
	GENERATED_BODY()

	FAnimNextModuleContextData() = default;

	FAnimNextModuleContextData(FAnimNextModuleInstance* InModuleInstance)
		: ModuleInstance(InModuleInstance)
	{
	}

	FAnimNextModuleInstance& GetModuleInstance() const
	{
		check(ModuleInstance != nullptr);
		return *ModuleInstance;
	}

private:
	// Call this to reset the context to its original state to detect stale usage
	void Reset()
	{
		ModuleInstance = nullptr;
	}

	// Module instance that is currently executing
	FAnimNextModuleInstance* ModuleInstance = nullptr;

	friend class UAnimNextModule;
	friend struct FAnimNextExecuteContext;
};
