// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayMontageStateTreeTask.h"
#include "StateTreeExecutionContext.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PlayMontageStateTreeTask)

#define LOCTEXT_NAMESPACE "GameplayInteractions"

EStateTreeRunStatus FPlayMontageStateTreeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (Montage == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	ACharacter* Character = Cast<ACharacter>(InstanceData.Actor);
	if (Character == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	
	InstanceData.Time = 0.f;

	// Grab the task duration from the montage.
	InstanceData.ComputedDuration = Montage->GetPlayLength();

	Character->PlayAnimMontage(Montage);
	// @todo: listen anim completed event

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FPlayMontageStateTreeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	InstanceData.Time += DeltaTime;
	return InstanceData.ComputedDuration <= 0.0f ? EStateTreeRunStatus::Running : (InstanceData.Time < InstanceData.ComputedDuration ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded);
}

#if WITH_EDITOR
FText FPlayMontageStateTreeTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	// Asset
	const FText MontageValue = FText::FromString(GetNameSafe(Montage));

	// Actor
	FText ActorValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Actor)), Formatting);
	if (ActorValue.IsEmpty())
	{
		ActorValue = LOCTEXT("None", "None");
	}

	if (Formatting == EStateTreeNodeFormatting::RichText)
	{
		// Rich
		return FText::Format(LOCTEXT("PlayMontageRich", "<b>Play Montage</> {0} <s>with </>{1}"), MontageValue, ActorValue);
	}
	
	// Plain
	return FText::Format(LOCTEXT("PlayMontage", "Play Montage {0} with {1}"), MontageValue, ActorValue);
}
#endif

#undef LOCTEXT_NAMESPACE
