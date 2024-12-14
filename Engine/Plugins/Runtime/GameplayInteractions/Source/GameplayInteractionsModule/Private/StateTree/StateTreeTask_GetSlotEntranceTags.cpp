// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTask_GetSlotEntranceTags.h"
#include "StateTreeExecutionContext.h"
#include "SmartObjectSubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "StateTreeLinker.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeTask_GetSlotEntranceTags)

#define LOCTEXT_NAMESPACE "GameplayInteractions"

FStateTreeTask_GetSlotEntranceTags::FStateTreeTask_GetSlotEntranceTags()
{
	// No tick needed.
	bShouldCallTick = false;
	// No need to update bound properties after enter state.
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

bool FStateTreeTask_GetSlotEntranceTags::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);
	return true;
}

bool FStateTreeTask_GetSlotEntranceTags::UpdateResult(const FStateTreeExecutionContext& Context) const
{
	const USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.SlotEntranceHandle.IsValid())
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[FStateTreeTask_GetSlotEntranceLocation] Expected valid SlotEntranceHandle handle."));
		return false;
	}

	// Make request without validation to just get the entrance tags.
	FSmartObjectSlotEntranceLocationRequest Request;
	Request.UserActor = nullptr;
	Request.ValidationFilter = USmartObjectSlotValidationFilter::StaticClass();
	Request.bProjectNavigationLocation = false;
	Request.bTraceGroundLocation = false;
	Request.bCheckEntranceLocationOverlap = false;
	Request.bCheckSlotLocationOverlap = false;
	Request.bCheckTransitionTrajectory = false;
	
	FSmartObjectSlotEntranceLocationResult EntryLocation;
	if (SmartObjectSubsystem.UpdateEntranceLocation(InstanceData.SlotEntranceHandle, Request, EntryLocation))
	{
		InstanceData.EntranceTags = EntryLocation.Tags;
		return true;
	}

	return false;
}

EStateTreeRunStatus FStateTreeTask_GetSlotEntranceTags::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (!UpdateResult(Context))
	{
		return EStateTreeRunStatus::Failed;
	}
	
	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FStateTreeTask_GetSlotEntranceTags::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	// Slot
	FText SlotEntranceValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, SlotEntranceHandle)), Formatting);
	if (SlotEntranceValue.IsEmpty())
	{
		SlotEntranceValue = LOCTEXT("None", "None");
	}

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("GetSlotEntranceTagsRich", "<b>Get Entrance Tags</> <s>for slot</> {Slot}")
		: LOCTEXT("GetSlotEntranceTags", "Get Entrance Tags for slot {Slot}");

	return FText::FormatNamed(Format,
		TEXT("Slot"), SlotEntranceValue);
}
#endif

#undef LOCTEXT_NAMESPACE
