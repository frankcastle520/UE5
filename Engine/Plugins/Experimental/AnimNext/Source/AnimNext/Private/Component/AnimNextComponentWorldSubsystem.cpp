// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextComponentWorldSubsystem.h"

#include "Component/AnimNextComponent.h"
#include "Engine/World.h"
#include "Logging/StructuredLog.h"
#include "Module/AnimNextModule.h"
#include "Module/ModuleHandle.h"
#include "Module/ModuleTaskContext.h"

void UAnimNextComponentWorldSubsystem::Register(UAnimNextComponent* InComponent)
{
	using namespace UE::AnimNext;
	check(IsInGameThread());
	check(InComponent);
	RegisterHandle(InComponent->ModuleHandle, InComponent->Module, InComponent, InComponent->InitMethod);
}

void UAnimNextComponentWorldSubsystem::Unregister(UAnimNextComponent* InComponent)
{
	using namespace UE::AnimNext;
	check(IsInGameThread());
	check(InComponent);
	UnregisterHandle(InComponent->ModuleHandle);
}

void UAnimNextComponentWorldSubsystem::SetEnabled(UAnimNextComponent* InComponent, bool bInEnabled)
{
	using namespace UE::AnimNext;
	check(IsInGameThread());
	check(InComponent);
	EnableHandle(InComponent->ModuleHandle, bInEnabled);
}

void UAnimNextComponentWorldSubsystem::QueueTask(UAnimNextComponent* InComponent, FName InModuleEventName, TUniqueFunction<void(const UE::AnimNext::FModuleTaskContext&)>&& InTaskFunction, UE::AnimNext::ETaskRunLocation InLocation)
{
	using namespace UE::AnimNext;
	check(IsInGameThread());
	check(InComponent);
	QueueTaskHandle(InComponent->ModuleHandle, InModuleEventName, MoveTemp(InTaskFunction), InLocation);
}

#if WITH_EDITOR

void UAnimNextComponentWorldSubsystem::OnModuleCompiled(UAnimNextModule* InModule)
{
	Super::OnModuleCompiled(InModule);

	for(FAnimNextModuleInstance& Instance : Instances)
	{
		if(Instance.GetModule() == InModule)
		{
			UAnimNextComponent* AnimNextComponent = CastChecked<UAnimNextComponent>(Instance.Object);
			AnimNextComponent->OnModuleCompiled();
		}
	}
}

#endif