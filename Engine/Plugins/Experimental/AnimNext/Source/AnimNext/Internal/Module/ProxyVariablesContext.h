// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FAnimNextPublicVariablesProxy;
struct FInstancedPropertyBag;
struct FAnimNextModuleInstance;

namespace UE::AnimNext
{

// Context passed to module proxy variables flip callback
struct FProxyVariablesContext
{
	// Get the default public variables of the module
	FAnimNextPublicVariablesProxy& GetPublicVariablesProxy() const;

private:
	explicit FProxyVariablesContext(FAnimNextModuleInstance& InModuleInstance)
		: ModuleInstance(InModuleInstance)
	{}

	// The instance we wrap
	FAnimNextModuleInstance& ModuleInstance;

	friend struct ::FAnimNextModuleInstance;
	friend struct FModuleBeginTickFunction;
};

}