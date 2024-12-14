// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/RigUnit_AnimNextModuleEvents.h"

#include "Module/AnimNextModuleInstance.h"

FRigUnit_AnimNextExecuteBindings_Execute()
{
}

UE::AnimNext::FModuleEventBindingFunction FRigUnit_AnimNextExecuteBindings::GetBindingFunction() const
{
	return [](const UE::AnimNext::FTickFunctionBindingContext& InContext, FTickFunction& InTickFunction)
	{
		InTickFunction.TickGroup = ETickingGroup::TG_PrePhysics;
		InTickFunction.bRunOnAnyThread = false; // TODO: This will need to vary depending on whether the event is deemed thread-safe 

		// Add prerequisite on this for all subsequent tick functions (assumes tick functions are sorted by phase)
		for(int32 TickFunctionIndex = InContext.EventIndex + 1; TickFunctionIndex < InContext.ModuleInstance.TickFunctions.Num(); ++TickFunctionIndex)
		{
			FTickFunction& TickFunctions = InContext.ModuleInstance.TickFunctions[TickFunctionIndex];
			TickFunctions.AddPrerequisite(InContext.ModuleInstance.Object, InContext.ModuleInstance.TickFunctions[InContext.EventIndex]);
		}
	};
}

FRigUnit_AnimNextInitializeEvent_Execute()
{
}

UE::AnimNext::FModuleEventBindingFunction FRigUnit_AnimNextInitializeEvent::GetBindingFunction() const
{
	return [](const UE::AnimNext::FTickFunctionBindingContext& InContext, FTickFunction& InTickFunction)
	{
		InTickFunction.TickGroup = ETickingGroup::TG_PrePhysics;

		// Add prerequisite on this for all subsequent tick functions (assumes tick functions are sorted by phase)
		for(int32 TickFunctionIndex = InContext.EventIndex + 1; TickFunctionIndex < InContext.ModuleInstance.TickFunctions.Num(); ++TickFunctionIndex)
		{
			FTickFunction& TickFunction = InContext.ModuleInstance.TickFunctions[TickFunctionIndex];
			TickFunction.AddPrerequisite(InContext.ModuleInstance.Object, InContext.ModuleInstance.TickFunctions[InContext.EventIndex]);
		}
	};
}

FRigUnit_AnimNextPrePhysicsEvent_Execute()
{
}

UE::AnimNext::FModuleEventBindingFunction FRigUnit_AnimNextPrePhysicsEvent::GetBindingFunction() const
{
	return [](const UE::AnimNext::FTickFunctionBindingContext& InContext, FTickFunction& InTickFunction)
	{
		InTickFunction.TickGroup = ETickingGroup::TG_PrePhysics;
	};
}

FRigUnit_AnimNextPostPhysicsEvent_Execute()
{
}

UE::AnimNext::FModuleEventBindingFunction FRigUnit_AnimNextPostPhysicsEvent::GetBindingFunction() const
{
	return [](const UE::AnimNext::FTickFunctionBindingContext& InContext, FTickFunction& InTickFunction)
	{
		InTickFunction.TickGroup = ETickingGroup::TG_PostPhysics;
	};
}