// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/ModuleTaskContext.h"

#include "Module/AnimNextModuleInstance.h"

namespace UE::AnimNext
{

FModuleTaskContext::FModuleTaskContext(FAnimNextModuleInstance& InModuleInstance)
	: ModuleInstance(&InModuleInstance)
{
}

void FModuleTaskContext::QueueInputTraitEvent(FAnimNextTraitEventPtr Event) const
{
	ModuleInstance->QueueInputTraitEvent(MoveTemp(Event));
}

}