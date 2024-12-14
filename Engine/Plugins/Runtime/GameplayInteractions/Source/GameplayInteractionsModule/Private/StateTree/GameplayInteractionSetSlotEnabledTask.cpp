// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayInteractionSetSlotEnabledTask.h"
#include "StateTreeExecutionContext.h"
#include "SmartObjectSubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "StateTreeLinker.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameplayInteractionSetSlotEnabledTask)

#define LOCTEXT_NAMESPACE "GameplayInteractions"

FGameplayInteractionSetSlotEnabledTask::FGameplayInteractionSetSlotEnabledTask()
{
	// No tick needed.
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
}

bool FGameplayInteractionSetSlotEnabledTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);

	// Copy properties on exit state if the tags are set then.
	bShouldCopyBoundPropertiesOnExitState = (Modify == EGameplayInteractionTaskModify::OnExitState);
	
	return true;
}

EStateTreeRunStatus FGameplayInteractionSetSlotEnabledTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.TargetSlot.IsValid())
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[GameplayInteractionSetSlotEnabledTask] Expected valid TargetSlot handle."));
		return EStateTreeRunStatus::Failed;
	}

	if (Modify == EGameplayInteractionTaskModify::OnEnterState || Modify == EGameplayInteractionTaskModify::OnEnterStateUndoOnExitState)
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, VeryVerbose, TEXT("[GameplayInteractionSetSlotEnabledTask] %s slot (%s)."),
			bEnableSlot ? TEXT("Enable") : TEXT("Disable"), *LexToString(InstanceData.TargetSlot));

		InstanceData.bInitialState = SmartObjectSubsystem.SetSlotEnabled(InstanceData.TargetSlot, bEnableSlot);
	}

	return EStateTreeRunStatus::Running;
}

void FGameplayInteractionSetSlotEnabledTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.TargetSlot.IsValid())
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[GameplayInteractionSetSlotEnabledTask] Expected valid TargetSlot handle."));
		return;
	}

	if (Modify == EGameplayInteractionTaskModify::OnEnterStateUndoOnExitState)
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, VeryVerbose, TEXT("[GameplayInteractionSetSlotEnabledTask] Undo %s slot (%s)."),
			bEnableSlot ? TEXT("Enable") : TEXT("Disable"), *LexToString(InstanceData.TargetSlot));

		SmartObjectSubsystem.SetSlotEnabled(InstanceData.TargetSlot, InstanceData.bInitialState);
	}
	else
	{
		const bool bLastStateFailed = Transition.CurrentRunStatus == EStateTreeRunStatus::Failed
										|| (bHandleExternalStopAsFailure &&  Transition.CurrentRunStatus == EStateTreeRunStatus::Stopped);

		if (Modify == EGameplayInteractionTaskModify::OnExitState
			|| (bLastStateFailed && Modify == EGameplayInteractionTaskModify::OnExitStateFailed)
			|| (!bLastStateFailed && Modify == EGameplayInteractionTaskModify::OnExitStateSucceeded))
		{
			UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, VeryVerbose, TEXT("[GameplayInteractionSetSlotEnabledTask] %s slot (%s)."),
				bEnableSlot ? TEXT("Enable") : TEXT("Disable"), *LexToString(InstanceData.TargetSlot));

			SmartObjectSubsystem.SetSlotEnabled(InstanceData.TargetSlot, bEnableSlot);
		}
	}
}

#if WITH_EDITOR
FText FGameplayInteractionSetSlotEnabledTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	// Slot
	FText SlotValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, TargetSlot)), Formatting);
	if (SlotValue.IsEmpty())
	{
		SlotValue = LOCTEXT("None", "None");
	}

	const FText StateValue = bEnableSlot ? LOCTEXT("Enable", "Enable") : LOCTEXT("Disable", "Disable"); 

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("SetSlotEnabledRich", "<b>{EnableOrDisable} Slot</> {Slot}")
		: LOCTEXT("SetSlotEnabled", "{EnableOrDisable} Slot {Slot}");

	return FText::FormatNamed(Format,
		TEXT("EnableOrDisable"), StateValue,
		TEXT("Slot"), SlotValue);
}
#endif

#undef LOCTEXT_NAMESPACE
