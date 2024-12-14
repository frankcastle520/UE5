// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayInteractionGetSlotActorTask.h"
#include "StateTreeExecutionContext.h"
#include "SmartObjectSubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "StateTreeLinker.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameplayInteractionGetSlotActorTask)

#define LOCTEXT_NAMESPACE "GameplayInteractions"

FGameplayInteractionGetSlotActorTask::FGameplayInteractionGetSlotActorTask()
{
	// No tick needed.
	bShouldCallTick = false;
	// No need to update bound properties after enter state.
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

bool FGameplayInteractionGetSlotActorTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);
	return true;
}

EStateTreeRunStatus FGameplayInteractionGetSlotActorTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.TargetSlot.IsValid())
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[GameplayInteractionGetSlotActorTask] Expected valid TargetSlot handle."));
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ResultActor = nullptr;
	
	const FSmartObjectSlotView SlotView = SmartObjectSubsystem.GetSlotView(InstanceData.TargetSlot);
	if (SlotView.IsValid())
	{
		if (const FGameplayInteractionSlotUserData* UserData = SlotView.GetStateDataPtr<FGameplayInteractionSlotUserData>())
		{
			InstanceData.ResultActor = UserData->UserActor.Get();
		}
	}
	
	if (bFailIfNotFound && !IsValid(InstanceData.ResultActor))
	{
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;  
}

#if WITH_EDITOR
FText FGameplayInteractionGetSlotActorTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	// Slot
	FText SlotValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, TargetSlot)), Formatting);
	if (SlotValue.IsEmpty())
	{
		SlotValue = LOCTEXT("None", "None");
	}

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("GetSlotActorRich", "<b>Get Actor</> <s>from slot</> {Slot}")
		: LOCTEXT("GetSlotActor", "Get Actor from slot {Slot}");

	return FText::FormatNamed(Format,
		TEXT("Slot"), SlotValue);
}
#endif

#undef LOCTEXT_NAMESPACE
