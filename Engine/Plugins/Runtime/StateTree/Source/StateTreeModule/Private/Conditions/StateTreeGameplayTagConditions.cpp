// Copyright Epic Games, Inc. All Rights Reserved.

#include "Conditions/StateTreeGameplayTagConditions.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeNodeDescriptionHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeGameplayTagConditions)

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE::StateTree::Conditions
{


}

#endif// WITH_EDITOR


//----------------------------------------------------------------------//
//  FGameplayTagMatchCondition
//----------------------------------------------------------------------//

bool FGameplayTagMatchCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	
	return (bExactMatch ? InstanceData.TagContainer.HasTagExact(InstanceData.Tag) : InstanceData.TagContainer.HasTag(InstanceData.Tag)) ^ bInvert;
}

#if WITH_EDITOR
FText FGameplayTagMatchCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	FText ContainerValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, TagContainer)), Formatting);
	if (ContainerValue.IsEmpty())
	{
		ContainerValue = UE::StateTree::DescHelpers::GetGameplayTagContainerAsText(InstanceData->TagContainer);
	}

	FText TagValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Tag)), Formatting);
	if (TagValue.IsEmpty())
	{
		TagValue = FText::FromString(InstanceData->Tag.ToString());
	}

	const FText InvertText = UE::StateTree::DescHelpers::GetInvertText(bInvert, Formatting);
	const FText ExactMatchText = UE::StateTree::DescHelpers::GetExactMatchText(bExactMatch, Formatting);

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("GameplayTagMatchRich", "{EmptyOrNot}{TagContainer} <s>contains</> {EmptyOrExactly}{Tag}")
		: LOCTEXT("GameplayTagMatch", "{EmptyOrNot}{TagContainer} contains {EmptyOrExactly}{Tag}");

	return FText::FormatNamed(Format,
		TEXT("EmptyOrNot"), InvertText,
		TEXT("TagContainer"), ContainerValue,
		TEXT("EmptyOrExactly"), ExactMatchText,
		TEXT("Tag"), TagValue);
}
#endif

//----------------------------------------------------------------------//
//  FGameplayTagContainerMatchCondition
//----------------------------------------------------------------------//

bool FGameplayTagContainerMatchCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	bool bResult = false;
	switch (MatchType)
	{
	case EGameplayContainerMatchType::Any:
		bResult = bExactMatch ? InstanceData.TagContainer.HasAnyExact(InstanceData.OtherContainer) : InstanceData.TagContainer.HasAny(InstanceData.OtherContainer);
		break;
	case EGameplayContainerMatchType::All:
		bResult = bExactMatch ? InstanceData.TagContainer.HasAllExact(InstanceData.OtherContainer) : InstanceData.TagContainer.HasAll(InstanceData.OtherContainer);
		break;
	default:
		ensureMsgf(false, TEXT("Unhandled match type %s."), *UEnum::GetValueAsString(MatchType));
	}
	
	return bResult ^ bInvert;
}

#if WITH_EDITOR
FText FGameplayTagContainerMatchCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	FText ContainerValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, TagContainer)), Formatting);
	if (ContainerValue.IsEmpty())
	{
		ContainerValue = UE::StateTree::DescHelpers::GetGameplayTagContainerAsText(InstanceData->TagContainer);
	}

	FText OtherContainerValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, OtherContainer)), Formatting);
	if (OtherContainerValue.IsEmpty())
	{
		OtherContainerValue = UE::StateTree::DescHelpers::GetGameplayTagContainerAsText(InstanceData->OtherContainer);
	}

	const FText InvertText = UE::StateTree::DescHelpers::GetInvertText(bInvert, Formatting);
	const FText ExactMatchText = UE::StateTree::DescHelpers::GetExactMatchText(bExactMatch, Formatting);
	const FText MatchTypeText = UEnum::GetDisplayValueAsText(MatchType);

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("GameplayTagContainerMatchRich", "{EmptyOrNot}{TagContainer} <s>contains {AnyOrAll}</> {EmptyOrExactly}{OtherTagContainer}")
		: LOCTEXT("GameplayTagContainerMatch", "{EmptyOrNot}{TagContainer} contains {AnyOrAll} {EmptyOrExactly}{OtherTagContainer}");

	return FText::FormatNamed(Format,
		TEXT("EmptyOrNot"), InvertText,
		TEXT("TagContainer"), ContainerValue,
		TEXT("AnyOrAll"), MatchTypeText,
		TEXT("EmptyOrExactly"), ExactMatchText,
		TEXT("OtherTagContainer"), OtherContainerValue);
}
#endif

//----------------------------------------------------------------------//
//  FGameplayTagQueryCondition
//----------------------------------------------------------------------//

bool FGameplayTagQueryCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	return TagQuery.Matches(InstanceData.TagContainer) ^ bInvert;
}

#if WITH_EDITOR
FText FGameplayTagQueryCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	FText ContainerValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, TagContainer)), Formatting);
	if (ContainerValue.IsEmpty())
	{
		ContainerValue = UE::StateTree::DescHelpers::GetGameplayTagContainerAsText(InstanceData->TagContainer);
	}

	const FText QueryValue = UE::StateTree::DescHelpers::GetGameplayTagQueryAsText(TagQuery);

	const FText InvertText = UE::StateTree::DescHelpers::GetInvertText(bInvert, Formatting);


	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("GameplayTagQueryRich", "{EmptyOrNot}{TagContainer} <s>matches</> {TagQuery}")
		: LOCTEXT("GameplayTagQuery", "{EmptyOrNot}{TagContainer} matches {TagQuery}");

	return FText::FormatNamed(Format,
		TEXT("EmptyOrNot"), InvertText,
		TEXT("TagContainer"), ContainerValue,
		TEXT("TagQuery"), QueryValue);
}
#endif

#if WITH_EDITOR
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR

