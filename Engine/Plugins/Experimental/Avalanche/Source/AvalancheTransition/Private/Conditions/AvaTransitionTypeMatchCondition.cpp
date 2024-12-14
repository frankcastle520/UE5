// Copyright Epic Games, Inc. All Rights Reserved.

#include "Conditions/AvaTransitionTypeMatchCondition.h"
#include "AvaTransitionContext.h"
#include "AvaTransitionUtils.h"
#include "StateTreeExecutionContext.h"

#define LOCTEXT_NAMESPACE "AvaTransitionTypeMatchCondition"

#if WITH_EDITOR
FText FAvaTransitionTypeMatchCondition::GetDescription(const FGuid& InId, FStateTreeDataView InInstanceDataView, const IStateTreeBindingLookup& InBindingLookup, EStateTreeNodeFormatting InFormatting) const
{
	const FInstanceDataType& InstanceData = InInstanceDataView.Get<FInstanceDataType>();

	const FText TransitionTypeDesc = UEnum::GetDisplayValueAsText(InstanceData.TransitionType).ToLower();

	return InFormatting == EStateTreeNodeFormatting::RichText
		? FText::Format(LOCTEXT("DescRich", "<s>transitioning</> <b>{0}</>"), TransitionTypeDesc)
		: FText::Format(LOCTEXT("Desc", "transitioning {0}"), TransitionTypeDesc);
}
#endif

void FAvaTransitionTypeMatchCondition::PostLoad(FStateTreeDataView InInstanceDataView)
{
	Super::PostLoad(InInstanceDataView);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (TransitionType_DEPRECATED != EAvaTransitionType::None)
	{
		if (FInstanceDataType* InstanceData = UE::AvaTransition::TryGetInstanceData(*this, InInstanceDataView))
		{
			InstanceData->TransitionType = TransitionType_DEPRECATED;
		}
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

bool FAvaTransitionTypeMatchCondition::TestCondition(FStateTreeExecutionContext& InContext) const
{
	const FAvaTransitionContext& TransitionContext = InContext.GetExternalData(TransitionContextHandle);
	const FInstanceDataType& InstanceData = InContext.GetInstanceData(*this);

	return TransitionContext.GetTransitionType() == InstanceData.TransitionType;
}

#undef LOCTEXT_NAMESPACE
