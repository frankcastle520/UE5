// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "IAnimNextVariableProxyHost.generated.h"

struct FAnimNextModuleInstance;

namespace UE::AnimNext
{
	struct FProxyVariablesContext;
}

/** Interface used for hosting proxy variables */
UINTERFACE()
class UAnimNextVariableProxyHost : public UInterface
{
	GENERATED_BODY()
};

class IAnimNextVariableProxyHost
{
	GENERATED_BODY()

	// Flip the public variables proxy buffer
	// Called from any thread to retrieve the latest copy of any public proxy variables
	virtual void FlipPublicVariablesProxy(const UE::AnimNext::FProxyVariablesContext& InContext) = 0;

	friend struct FAnimNextModuleInstance;
};
