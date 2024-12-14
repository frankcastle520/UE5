// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/AnimNextWorldSubsystem.h"

#include "Engine/World.h"
#include "Logging/StructuredLog.h"
#include "Module/AnimNextModule.h"
#include "Module/ModuleHandle.h"
#include "Module/ModuleTaskContext.h"

namespace UE::AnimNext
{

FModulePendingAction::FModulePendingAction(EType InType, FModuleHandle InHandle)
	: Handle(InHandle)
	, Type(InType)
{
}

}

UAnimNextWorldSubsystem::UAnimNextWorldSubsystem()
{
	if(!HasAnyFlags(RF_ClassDefaultObject))
	{
#if WITH_EDITOR
		UAnimNextModule::OnModuleCompiled().AddUObject(this, &UAnimNextWorldSubsystem::OnModuleCompiled);
#endif

		// Kick off root task at the start of each world tick
		OnWorldPreActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddLambda([this](UWorld* InWorld, ELevelTick InTickType, float InDeltaSeconds)
		{
			if (InTickType == LEVELTICK_All || InTickType == LEVELTICK_ViewportsOnly)
			{
				// Flush actions here as they require game thread callbacks (e.g. to reconfigure tick functions)
				FlushPendingActions();
				DeltaTime = InDeltaSeconds;
			}
		});
	}
}

UAnimNextWorldSubsystem::~UAnimNextWorldSubsystem()
{
	if(!HasAnyFlags(RF_ClassDefaultObject))
	{
#if WITH_EDITOR
		UAnimNextModule::OnModuleCompiled().RemoveAll(this);
#endif

		FWorldDelegates::OnWorldPreActorTick.Remove(OnWorldPreActorTickHandle);
	}
}

void UAnimNextWorldSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	UAnimNextWorldSubsystem* This = CastChecked<UAnimNextWorldSubsystem>(InThis);
	for(FAnimNextModuleInstance& Instance : This->Instances)
	{
		Collector.AddPropertyReferences(FAnimNextModuleInstance::StaticStruct(), &Instance, InThis);
	}
}

bool UAnimNextWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	switch (WorldType)
	{
	case EWorldType::Game:
	case EWorldType::Editor:
	case EWorldType::PIE:
	case EWorldType::EditorPreview:
	case EWorldType::GamePreview:
		return true;
	}

	return false;
}

bool UAnimNextWorldSubsystem::IsValidHandle(UE::AnimNext::FModuleHandle InHandle) const
{
	return Instances.IsValidHandle(InHandle);
}

void UAnimNextWorldSubsystem::FlushPendingActions()
{
	using namespace UE::AnimNext;

	FRWScopeLock PendingLockScope(PendingLock, SLT_Write);

	if (PendingActions.Num() > 0)
	{
		FRWScopeLock InstancesLockScope(InstancesLock, SLT_Write);

		for (FModulePendingAction& PendingAction : PendingActions)
		{
			switch (PendingAction.Type)
			{
			case FModulePendingAction::EType::ReleaseHandle:
				if (IsValidHandle(PendingAction.Handle))
				{
					Instances.Release(PendingAction.Handle);
				}
				break;
			case FModulePendingAction::EType::EnableHandle:
				if (IsValidHandle(PendingAction.Handle))
				{
					FAnimNextModuleInstance& Instance = Instances.Get(PendingAction.Handle);
					Instance.Enable(true);
				}
				break;
			case FModulePendingAction::EType::DisableHandle:
				if (IsValidHandle(PendingAction.Handle))
				{
					FAnimNextModuleInstance& Instance = Instances.Get(PendingAction.Handle);
					Instance.Enable(false);
				}
				break;
			default:
				break;
			}
		}

		PendingActions.Reset();
	}
}

void UAnimNextWorldSubsystem::RegisterHandle(UE::AnimNext::FModuleHandle& InOutHandle, UAnimNextModule* InModule, UObject* InObject, EAnimNextModuleInitMethod InitMethod)
{
	using namespace UE::AnimNext;
	check(IsInGameThread());
	FRWScopeLock InstancesLockScope(InstancesLock, SLT_Write);

	InOutHandle = Instances.Emplace(InModule, InObject, InitMethod);
	FAnimNextModuleInstance& Instance = Instances.Get(InOutHandle);
	Instance.Handle = InOutHandle;
	Instance.Initialize();
}

void UAnimNextWorldSubsystem::UnregisterHandle(UE::AnimNext::FModuleHandle& InOutHandle)
{
	using namespace UE::AnimNext;
	check(IsInGameThread());

	if(IsValidHandle(InOutHandle))
	{
		FRWScopeLock InstancesLockScope(InstancesLock, SLT_Write);

		Instances.Release(InOutHandle);
		InOutHandle.Reset();

		// TODO: do not allow immediate handle releases outside of schedule runs - we can either defer or assert
	//	PendingActions.Emplace(FModulePendingAction::EType::ReleaseHandle, InOutHandle);
	}
}

void UAnimNextWorldSubsystem::EnableHandle(UE::AnimNext::FModuleHandle InOutHandle, bool bInEnabled)
{
	using namespace UE::AnimNext;
	check(IsInGameThread());
	if (IsValidHandle(InOutHandle))
	{
		PendingActions.Emplace(bInEnabled ? FModulePendingAction::EType::EnableHandle : FModulePendingAction::EType::DisableHandle, InOutHandle);
	}
}

void UAnimNextWorldSubsystem::QueueTaskHandle(UE::AnimNext::FModuleHandle InOutHandle, FName InModuleEventName, TUniqueFunction<void(const UE::AnimNext::FModuleTaskContext&)>&& InTaskFunction, UE::AnimNext::ETaskRunLocation InLocation)
{
	using namespace UE::AnimNext;
	check(IsInGameThread());
	if (IsValidHandle(InOutHandle))
	{
		FAnimNextModuleInstance& Instance = Instances.Get(InOutHandle);

		TSpscQueue<TUniqueFunction<void(const FModuleTaskContext&)>>* Queue = nullptr;
		FModuleEventTickFunction* FoundTickFunction = nullptr;
		if(Instance.TickFunctions.Num() > 0)
		{
			if(InModuleEventName == NAME_None)
			{
				// passing NAME_None means 'any tick function' in this context, so just use the first one
				FoundTickFunction = &Instance.TickFunctions[0];
			}
			else
			{
				FoundTickFunction = Instance.TickFunctions.FindByPredicate([InModuleEventName](const FModuleEventTickFunction& InTickFunction)
				{
					return InTickFunction.EventName == InModuleEventName;
				});
			}
		}

		if (FoundTickFunction)
		{
			switch (InLocation)
			{
			case ETaskRunLocation::Before:
				Queue = &FoundTickFunction->PreExecuteTasks;
				break;
			case ETaskRunLocation::After:
				Queue = &FoundTickFunction->PostExecuteTasks;
				break;
			}
		}

		if (Queue)
		{
			Queue->Enqueue(MoveTemp(InTaskFunction));
		}
		else
		{
			UE_LOGFMT(LogAnimation, Warning, "QueueTask: Could not find event '{EventName}' in module '{ModuleName}'", InModuleEventName, Instance.GetDataInterfaceName());
		}
	}
}

#if WITH_EDITOR

void UAnimNextWorldSubsystem::OnModuleCompiled(UAnimNextModule* InModule)
{
	// Cant do this while we are running in a world tick
	check(!GetWorld()->bInTick); 

	for(FAnimNextModuleInstance& Instance : Instances)
	{
		if(Instance.GetModule() == InModule)
		{
			Instance.OnModuleCompiled();
		}
	}
}

#endif