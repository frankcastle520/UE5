// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/AsyncInitBodyHelper.h"
#include "HAL/IConsoleManager.h"

namespace Chaos
{
	namespace CVars
	{
		CHAOS_API bool bEnableAsyncInitBody = false;

		FAutoConsoleVariableRef CVarEnableAsyncInitBody(
		TEXT("p.Chaos.EnableAsyncInitBody"), 
		bEnableAsyncInitBody,
		TEXT("[Experimental] Allow body instances to be initialized outside of game thread (default is false)."),
		ECVF_ReadOnly);
	}
}