// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traits/SynchronizeUsingGroups.h"
#include "Graph/SyncGroup_GraphInstanceComponent.h"

namespace UE::AnimNext
{
	AUTO_REGISTER_ANIM_TRAIT(FSynchronizeUsingGroupsTrait)

	// Trait implementation boilerplate
	#define TRAIT_INTERFACE_ENUMERATOR(GeneratorMacro) \
		GeneratorMacro(IGroupSynchronization) \
		GeneratorMacro(ITimeline) \
		GeneratorMacro(IUpdate) \

	GENERATE_ANIM_TRAIT_IMPLEMENTATION(FSynchronizeUsingGroupsTrait, TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_EVENT_ENUMERATOR)
	#undef TRAIT_INTERFACE_ENUMERATOR

	void FSynchronizeUsingGroupsTrait::PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		TTraitBinding<IGroupSynchronization> GroupSyncTrait;
		Binding.GetStackInterface(GroupSyncTrait);

		const FName GroupName = GroupSyncTrait.GetGroupName(Context);
		const bool bHasGroupName = !GroupName.IsNone();

		// If we have a group name, we are active
		// Freeze the timeline, our sync group will control it
		InstanceData->bFreezeTimeline = bHasGroupName;

		// Forward the PreUpdate call, if the timeline attempts to update, we'll do nothing if we are frozen
		IUpdate::PreUpdate(Context, Binding, TraitState);

		if (!bHasGroupName)
		{
			// If no group name is specified, this trait is inactive
			return;
		}

		const EAnimGroupRole::Type GroupRole = GroupSyncTrait.GetGroupRole(Context);

		// Append this trait to our group, we'll need to synchronize it
		FSyncGroupGraphInstanceComponent& Component = Context.GetComponent<FSyncGroupGraphInstanceComponent>();
		Component.RegisterWithGroup(GroupName, GroupRole, Binding.GetTraitPtr(), TraitState);
	}

	FName FSynchronizeUsingGroupsTrait::GetGroupName(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		return SharedData->GroupName;
	}

	EAnimGroupRole::Type FSynchronizeUsingGroupsTrait::GetGroupRole(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		return SharedData->GroupRole;
	}

	FTimelineProgress FSynchronizeUsingGroupsTrait::AdvanceBy(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding, float DeltaTime) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		// When the group advances the timeline, we thaw it to advance
		InstanceData->bFreezeTimeline = false;

		TTraitBinding<ITimeline> TimelineTrait;
		Binding.GetStackInterface(TimelineTrait);

		const FTimelineProgress Progress = TimelineTrait.AdvanceBy(Context, DeltaTime);

		InstanceData->bFreezeTimeline = true;

		return Progress;
	}

	void FSynchronizeUsingGroupsTrait::AdvanceToRatio(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding, float ProgressRatio) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		// When the group advances the timeline, we thaw it to advance
		InstanceData->bFreezeTimeline = false;

		TTraitBinding<ITimeline> TimelineTrait;
		Binding.GetStackInterface(TimelineTrait);

		TimelineTrait.AdvanceToRatio(Context, ProgressRatio);

		InstanceData->bFreezeTimeline = true;
	}

	FTimelineProgress FSynchronizeUsingGroupsTrait::SimulateAdvanceBy(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float DeltaTime) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->bFreezeTimeline)
		{
			return ITimeline::GetProgress(Context, Binding);	// If the timeline is frozen, we don't advance, return the previous value
		}

		return ITimeline::SimulateAdvanceBy(Context, Binding, DeltaTime);
	}

	FTimelineProgress FSynchronizeUsingGroupsTrait::AdvanceBy(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float DeltaTime) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->bFreezeTimeline)
		{
			return ITimeline::GetProgress(Context, Binding);	// If the timeline is frozen, we don't advance, return the previous value
		}

		return ITimeline::AdvanceBy(Context, Binding, DeltaTime);
	}

	void FSynchronizeUsingGroupsTrait::AdvanceToRatio(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float ProgressRatio) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->bFreezeTimeline)
		{
			return;			// If the timeline is frozen, we don't advance
		}

		ITimeline::AdvanceToRatio(Context, Binding, ProgressRatio);
	}
}
