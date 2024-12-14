// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

namespace UE::AnimNext::AnimGraph::UncookedOnly
{
	class FModule : public IModuleInterface
	{
	};
}

IMPLEMENT_MODULE(UE::AnimNext::AnimGraph::UncookedOnly::FModule, AnimNextAnimGraphUncookedOnly);
