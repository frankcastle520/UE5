// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/ModuleTickFunction.h"

#include "RigVMRuntimeDataRegistry.h"
#include "Algo/TopologicalSort.h"
#include "Module/AnimNextModule.h"
#include "Module/ModuleTaskContext.h"
#include "Module/ProxyVariablesContext.h"
#include "Module/RigUnit_AnimNextModuleEvents.h"
#include "Module/ModuleEvents.h"
#include "TraitCore/TraitEventList.h"

namespace UE::AnimNext
{

void FModuleEndTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	Run();
}

void FModuleEndTickFunction::Run()
{
	// Decrement the remaining lifetime of the input events we processed and queue up any remaining events
	DecrementLifetimeAndPurgeExpired(ModuleInstance->InputEventList, ModuleInstance->OutputEventList);

	// Filter out our schedule action events, we'll hand them off to the main thread to execute
	FTraitEventList MainThreadActionEventList;
	if (!ModuleInstance->OutputEventList.IsEmpty())
	{
		for (FAnimNextTraitEventPtr& Event : ModuleInstance->OutputEventList)
		{
			if (!Event->IsValid())
			{
				continue;
			}

			if (FAnimNextModule_ActionEvent* ActionEvent = Event->AsType<FAnimNextModule_ActionEvent>())
			{
				if (ActionEvent->IsThreadSafe())
				{
					// Execute this action now
					ActionEvent->Execute();
				}
				else
				{
					// Defer this action and execute it on the main thread
					MainThreadActionEventList.Push(Event);
				}
			}
		}

		// Reset our list of output events, we don't retain any
		ModuleInstance->OutputEventList.Reset();
	}
	
	auto RunTaskOnGameThread = [](TUniqueFunction<void(void)>&& InFunction)
	{
		if(IsInGameThread())
		{
			InFunction();
		}
		else
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(InFunction), TStatId(), nullptr, ENamedThreads::GameThread);
		}
	};

	auto DisableInitializeEvent = [this]()
	{
		if(ModuleInstance->TickFunctions.Num() > 0 && ModuleInstance->TickFunctions[0].EventName == FRigUnit_AnimNextInitializeEvent::EventName)
		{
			// Disable the initialize event as we have already run it
			ModuleInstance->TickFunctions[0].SetTickFunctionEnable(false);
		}
	};

	if(ModuleInstance->RunState == FAnimNextModuleInstance::ERunState::PendingInitialUpdate)
	{
		if(ModuleInstance->InitMethod == EAnimNextModuleInitMethod::InitializeAndPause
#if WITH_EDITOR
			|| (ModuleInstance->InitMethod == EAnimNextModuleInitMethod::InitializeAndPauseInEditor && ModuleInstance->bIsEditor)
#endif
			)
		{
			// Queue task to disable our tick functions now we have performed our initial update
			RunTaskOnGameThread([this, &DisableInitializeEvent]()
			{
				check(IsInGameThread());
				DisableInitializeEvent();
				ModuleInstance->Enable(false);
			});
		}
	}
	else if(ModuleInstance->RunState != FAnimNextModuleInstance::ERunState::Running)
	{
		RunTaskOnGameThread([this, &DisableInitializeEvent]()
		{
			check(IsInGameThread());
			DisableInitializeEvent();
			ModuleInstance->TransitionToRunState(FAnimNextModuleInstance::ERunState::Running);
		});
	}

	if (!MainThreadActionEventList.IsEmpty())
	{
		RunTaskOnGameThread([MainThreadActionEventList = MoveTemp(MainThreadActionEventList)]()
			{
				check(IsInGameThread());
				for (const FAnimNextTraitEventPtr& Event : MainThreadActionEventList)
				{
					FAnimNextModule_ActionEvent* ActionEvent = Event->AsType<FAnimNextModule_ActionEvent>();
					ActionEvent->Execute();
				}
			});
	}
}

FString FModuleEndTickFunction::DiagnosticMessage()
{
	return TEXT("AnimNext: ModuleEnd");
}

void FModuleEventTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	Run(DeltaTime);
}

void FModuleEventTickFunction::Run(float InDeltaTime)
{
	while (!PreExecuteTasks.IsEmpty())
	{
		TOptional<TUniqueFunction<void(const FModuleTaskContext&)>> Function = PreExecuteTasks.Dequeue();
		check(Function.IsSet());
		Function.GetValue()(FModuleTaskContext(*ModuleInstance));
	}

	if (URigVM* VM = ModuleInstance->GetModule()->RigVM)
	{
		FRigVMExtendedExecuteContext& Context = ModuleInstance->GetExtendedExecuteContext();
		check(Context.VMHash == VM->GetVMHash());

		FAnimNextExecuteContext& AnimNextContext = Context.GetPublicDataSafe<FAnimNextExecuteContext>();

		// RigVM setup
		AnimNextContext.SetDeltaTime(InDeltaTime);

		// Module setup
		AnimNextContext.SetContextData<FAnimNextModuleContextData>(ModuleInstance);

		// Run the VM for this event
		VM->ExecuteVM(Context, EventName);

		// Reset the context to avoid issues if we forget to reset it the next time we use it
		AnimNextContext.DebugReset<FAnimNextModuleContextData>();
	}

	while (!PostExecuteTasks.IsEmpty())
	{
		TOptional<TUniqueFunction<void(const FModuleTaskContext&)>> Function = PostExecuteTasks.Dequeue();
		check(Function.IsSet());
		Function.GetValue()(FModuleTaskContext(*ModuleInstance));
	}
}

#if WITH_EDITOR

void FModuleEventTickFunction::InitializeAndRunModule(FAnimNextModuleInstance& InModuleInstance)
{
	// Sort tick functions topologically
	TArray<FModuleEventTickFunction*> TickFunctionPtrs;
	TickFunctionPtrs.Reserve(InModuleInstance.TickFunctions.Num());
	Algo::Transform(InModuleInstance.TickFunctions, TickFunctionPtrs, [](const FModuleEventTickFunction& InTickFunction){ return const_cast<FModuleEventTickFunction*>(&InTickFunction); });
	TArray<FModuleEventTickFunction*> DependencyPtrs;
	Algo::TopologicalSort(
		TickFunctionPtrs,
		[&DependencyPtrs, &InModuleInstance](FModuleEventTickFunction* InTickFunction)
		{
			DependencyPtrs.Reset();

			// Add direct prereqs
			for(const FTickPrerequisite& Prerequisite : InTickFunction->GetPrerequisites())
			{
				DependencyPtrs.Add(static_cast<FModuleEventTickFunction*>(Prerequisite.PrerequisiteTickFunction));
			}

			// Also add any tick functions in prior tick groups that we don't already have
			for(FModuleEventTickFunction& TickFunction : InModuleInstance.TickFunctions)
			{
				if(TickFunction.TickGroup < InTickFunction->TickGroup && !DependencyPtrs.Contains(&TickFunction))
				{
					DependencyPtrs.Add(&TickFunction);
				}
			}

			return DependencyPtrs;
		},
		Algo::ETopologicalSort::None);

	// Run sorted tick functions
	for(FModuleEventTickFunction* TickFunction : TickFunctionPtrs)
	{
		TickFunction->Run(0.0f);
	}
	InModuleInstance.EndTickFunction.Run();
}

#endif

FString FModuleEventTickFunction::DiagnosticMessage()
{
	TStringBuilder<256> Builder;
	Builder.Append(TEXT("AnimNext: "));
	EventName.AppendString(Builder);
	return Builder.ToString();
}

}
