// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/GameplayInteractionConditions.h"
#include "StateTreeExecutionContext.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeLinker.h"
#include "VisualLogger/VisualLogger.h"
#include "StateTreeNodeDescriptionHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameplayInteractionConditions)

#define LOCTEXT_NAMESPACE "GameplayInteractions"

#define ST_INTERACTION_LOG(Verbosity, Format, ...) UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Verbosity, TEXT("[%s] ") Format, *StaticStruct()->GetName(), ##__VA_ARGS__)
#define ST_INTERACTION_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG_UELOG((Condition), Context.GetOwner(), LogStateTree, Verbosity, TEXT("[%s] ") Format, *StaticStruct()->GetName(), ##__VA_ARGS__)

namespace UE::GameplayInteraction
{

const FGameplayTagContainer* GetSlotTags(const USmartObjectSubsystem& SmartObjectSubsystem, const FSmartObjectSlotHandle Slot, const EGameplayInteractionMatchSlotTagSource Source)
{
	const FSmartObjectSlotView SlotView = SmartObjectSubsystem.GetSlotView(Slot);
	if (!SlotView.IsValid())
	{
		return nullptr;
	}

	if (Source == EGameplayInteractionMatchSlotTagSource::RuntimeTags)
	{
		return &SlotView.GetTags();
	}
	else if (Source == EGameplayInteractionMatchSlotTagSource::ActivityTags)
	{
		const FSmartObjectSlotDefinition& SlotDefinition = SlotView.GetDefinition();
		return &SlotDefinition.ActivityTags;
	}

	return nullptr;
}



};

//----------------------------------------------------------------------//
//  FGameplayInteractionSlotTagsMatchCondition
//----------------------------------------------------------------------//

bool FGameplayInteractionSlotTagsMatchCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);
	return true;
}

bool FGameplayInteractionSlotTagsMatchCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const FGameplayTagContainer* Container = UE::GameplayInteraction::GetSlotTags(SmartObjectSubsystem, InstanceData.Slot, Source);
	if (Container == nullptr)
	{
		return false;
	}

	bool bResult = false;
	switch (MatchType)
	{
	case EGameplayContainerMatchType::Any:
		bResult = bExactMatch ? Container->HasAnyExact(InstanceData.TagsToMatch) : Container->HasAny(InstanceData.TagsToMatch);
		break;
	case EGameplayContainerMatchType::All:
		bResult = bExactMatch ? Container->HasAllExact(InstanceData.TagsToMatch) : Container->HasAll(InstanceData.TagsToMatch);
		break;
	default:
		ensureMsgf(false, TEXT("Unhandled match type %s."), *UEnum::GetValueAsString(MatchType));
	}
	
	return bResult ^ bInvert;
}

#if WITH_EDITOR
FText FGameplayInteractionSlotTagsMatchCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	// Asset
	FText SlotValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Slot)), Formatting);
	if (SlotValue.IsEmpty())
	{
		SlotValue = LOCTEXT("None", "None");
	}

	FText ContainerValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, TagsToMatch)), Formatting);
	if (ContainerValue.IsEmpty())
	{
		ContainerValue = UE::StateTree::DescHelpers::GetGameplayTagContainerAsText(InstanceData->TagsToMatch);
	}

	const FText InvertText = UE::StateTree::DescHelpers::GetInvertText(bInvert, Formatting);
	const FText ExactMatchText = UE::StateTree::DescHelpers::GetExactMatchText(bExactMatch, Formatting);
	const FText MatchTypeText = UEnum::GetDisplayValueAsText(MatchType);

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("SlotTagsMatchRich", "{EmptyOrNot}<s>Slot</> {Slot} <s>matches {AnyOrAll}</> {EmptyOrExactly}{TagContainer}")
		: LOCTEXT("SlotTagsMatch", "{EmptyOrNot}Slot {Slot} matches {AnyOrAll} {EmptyOrExactly}{TagContainer}");

	return FText::FormatNamed(Format,
		TEXT("EmptyOrNot"), InvertText,
		TEXT("Slot"), SlotValue,
		TEXT("AnyOrAll"), MatchTypeText,
		TEXT("EmptyOrExactly"), ExactMatchText,
		TEXT("TagContainer"), ContainerValue);
}
#endif

//----------------------------------------------------------------------//
//  FGameplayInteractionQuerySlotTagCondition
//----------------------------------------------------------------------//

bool FGameplayInteractionQuerySlotTagCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);
	return true;
}

bool FGameplayInteractionQuerySlotTagCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const FGameplayTagContainer* Container = UE::GameplayInteraction::GetSlotTags(SmartObjectSubsystem, InstanceData.Slot, Source);
	if (Container == nullptr)
	{
		return false;
	}

	return TagQuery.Matches(*Container) ^ bInvert;
}

#if WITH_EDITOR
FText FGameplayInteractionQuerySlotTagCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	// Asset
	FText SlotValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Slot)), Formatting);
	if (SlotValue.IsEmpty())
	{
		SlotValue = LOCTEXT("None", "None");
	}

	const FText QueryValue = UE::StateTree::DescHelpers::GetGameplayTagQueryAsText(TagQuery);

	const FText InvertText = UE::StateTree::DescHelpers::GetInvertText(bInvert, Formatting);


	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("QuerySlotTagRich", "{EmptyOrNot}<s>Slot</> {Slot} <s>matches</> {Query}")
		: LOCTEXT("QuerySlotTag", "{EmptyOrNot}Slot {Slot} matches {Query}");

	return FText::FormatNamed(Format,
		TEXT("EmptyOrNot"), InvertText,
		TEXT("Slot"), SlotValue,
		TEXT("Query"), QueryValue);
}
#endif

//----------------------------------------------------------------------//
//  FGameplayInteractionIsSlotHandleValidCondition
//----------------------------------------------------------------------//

bool FGameplayInteractionIsSlotHandleValidCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);
	return true;
}

bool FGameplayInteractionIsSlotHandleValidCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	return (InstanceData.Slot.IsValid() && SmartObjectSubsystem.IsSmartObjectSlotValid(InstanceData.Slot)) ^ bInvert;
}

#if WITH_EDITOR
FText FGameplayInteractionIsSlotHandleValidCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	// SLot
	FText SlotValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Slot)), Formatting);
	if (SlotValue.IsEmpty())
	{
		SlotValue = LOCTEXT("None", "None");
	}

	const FText InvertText = UE::StateTree::DescHelpers::GetInvertText(bInvert, Formatting);

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("IsSlotHandleValidRich", "{EmptyOrNot}<s>Slot</> {Slot} <s>is valid</>")
		: LOCTEXT("IsSlotHandleValid", "{EmptyOrNot}Slot {Slot} is valid");

	return FText::FormatNamed(Format,
		TEXT("EmptyOrNot"), InvertText,
		TEXT("Slot"), SlotValue);
}
#endif

#undef LOCTEXT_NAMESPACE
