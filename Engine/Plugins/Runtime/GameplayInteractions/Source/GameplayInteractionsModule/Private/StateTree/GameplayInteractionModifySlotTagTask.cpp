// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayInteractionModifySlotTagTask.h"
#include "StateTreeExecutionContext.h"
#include "SmartObjectSubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "StateTreeLinker.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameplayInteractionModifySlotTagTask)

#define LOCTEXT_NAMESPACE "GameplayInteractions"

FGameplayInteractionModifySlotTagTask::FGameplayInteractionModifySlotTagTask()
{
	// No tick needed.
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
}

bool FGameplayInteractionModifySlotTagTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);

	// Copy properties on exit state if the tags are set then.
	bShouldCopyBoundPropertiesOnExitState = (Modify == EGameplayInteractionTaskModify::OnExitState);
	
	return true;
}

EDataValidationResult FGameplayInteractionModifySlotTagTask::Compile(FStateTreeDataView InstanceDataView, TArray<FText>& ValidationMessages)
{
	EDataValidationResult Result = EDataValidationResult::Valid;

	if (!Tag.IsValid())
	{
		ValidationMessages.Add(LOCTEXT("MissingTag", "Tag property is empty, expecting valid tag."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

EStateTreeRunStatus FGameplayInteractionModifySlotTagTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.TargetSlot.IsValid())
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[GameplayInteractionModifySlotTagTask] Expected valid TargetSlot handle."));
		return EStateTreeRunStatus::Failed;
	}

	if (Modify == EGameplayInteractionTaskModify::OnEnterState || Modify == EGameplayInteractionTaskModify::OnEnterStateUndoOnExitState)
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, VeryVerbose, TEXT("[GameplayInteractionModifySlotTagTask] %s %s Tag %s to slot (%s)."),
			*UEnum::GetDisplayValueAsText(Modify).ToString(), *UEnum::GetDisplayValueAsText(Operation).ToString(), *Tag.ToString(), *LexToString(InstanceData.TargetSlot));

		if (Operation == EGameplayInteractionModifyGameplayTagOperation::Add)
		{
			SmartObjectSubsystem.AddTagToSlot(InstanceData.TargetSlot, Tag);
		}
		else if (Operation == EGameplayInteractionModifyGameplayTagOperation::Remove)
		{
			InstanceData.bTagRemoved = SmartObjectSubsystem.RemoveTagFromSlot(InstanceData.TargetSlot, Tag);
		}
	}

	return EStateTreeRunStatus::Running;
}

void FGameplayInteractionModifySlotTagTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.TargetSlot.IsValid())
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[GameplayInteractionModifySlotTagTask] Expected valid TargetSlot handle."));
		return;
	}

	if (Modify == EGameplayInteractionTaskModify::OnEnterStateUndoOnExitState)
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, VeryVerbose, TEXT("[GameplayInteractionModifySlotTagTask] Undo %s %s Tag %s to slot (%s)."),
			*UEnum::GetDisplayValueAsText(Modify).ToString(), *UEnum::GetDisplayValueAsText(Operation).ToString(), *Tag.ToString(), *LexToString(InstanceData.TargetSlot));

		// Undo changes done on state enter.
		if (Operation == EGameplayInteractionModifyGameplayTagOperation::Add)
		{
			SmartObjectSubsystem.RemoveTagFromSlot(InstanceData.TargetSlot, Tag);
		}
		else if (Operation == EGameplayInteractionModifyGameplayTagOperation::Remove && InstanceData.bTagRemoved)
		{
			SmartObjectSubsystem.AddTagToSlot(InstanceData.TargetSlot, Tag);
		}
	}
	else
	{
		const bool bLastStateFailed = Transition.CurrentRunStatus == EStateTreeRunStatus::Failed
										|| (bHandleExternalStopAsFailure &&  Transition.CurrentRunStatus == EStateTreeRunStatus::Stopped);

		if (Modify == EGameplayInteractionTaskModify::OnExitState
			|| (bLastStateFailed && Modify == EGameplayInteractionTaskModify::OnExitStateFailed)
			|| (!bLastStateFailed && Modify == EGameplayInteractionTaskModify::OnExitStateSucceeded))
		{
			UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, VeryVerbose, TEXT("[GameplayInteractionModifySlotTagTask] %s %s Tag %s to slot (%s)."),
				*UEnum::GetDisplayValueAsText(Modify).ToString(), *UEnum::GetDisplayValueAsText(Operation).ToString(), *Tag.ToString(), *LexToString(InstanceData.TargetSlot));

			if (Operation == EGameplayInteractionModifyGameplayTagOperation::Add)
			{
				SmartObjectSubsystem.AddTagToSlot(InstanceData.TargetSlot, Tag);
			}
			else if (Operation == EGameplayInteractionModifyGameplayTagOperation::Remove)
			{
				SmartObjectSubsystem.RemoveTagFromSlot(InstanceData.TargetSlot, Tag);
			}
		}
	}
}

#if WITH_EDITOR
FText FGameplayInteractionModifySlotTagTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
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
		? LOCTEXT("ModifySlotTagRich", "<b>{AddOrRemove} Tag</> {Tag} <s>to slot</> {Slot}")
		: LOCTEXT("ModifySlotTag", "{AddOrRemove} Tag {Tag} to slot {Slot}");

	return FText::FormatNamed(Format,
		TEXT("AddOrRemove"), UEnum::GetDisplayValueAsText(Operation),
		TEXT("Tag"), FText::FromString(Tag.ToString()),
		TEXT("Slot"), SlotValue);
}
#endif

#undef LOCTEXT_NAMESPACE
