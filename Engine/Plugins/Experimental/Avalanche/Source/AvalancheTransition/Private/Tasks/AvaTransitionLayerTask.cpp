// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/AvaTransitionLayerTask.h"
#include "AvaTransitionContext.h"
#include "AvaTransitionLayerUtils.h"
#include "AvaTransitionSubsystem.h"
#include "AvaTransitionUtils.h"
#include "StateTreeExecutionContext.h"

#define LOCTEXT_NAMESPACE "AvaTransitionLayerTask"

#if WITH_EDITOR
FText FAvaTransitionLayerTask::GetDescription(const FGuid& InId, FStateTreeDataView InInstanceDataView, const IStateTreeBindingLookup& InBindingLookup, EStateTreeNodeFormatting InFormatting) const
{
	const FInstanceDataType& InstanceData = InInstanceDataView.Get<FInstanceDataType>();

	FAvaTransitionLayerUtils::FLayerQueryTextParams Params;
	Params.LayerType = InstanceData.LayerType;
	Params.SpecificLayerName = InstanceData.SpecificLayer.ToName();
	Params.LayerTypePropertyName = GET_MEMBER_NAME_CHECKED(FInstanceDataType, LayerType);
	Params.SpecificLayerPropertyName = GET_MEMBER_NAME_CHECKED(FInstanceDataType, SpecificLayer);

	return FAvaTransitionLayerUtils::GetLayerQueryText(MoveTemp(Params), InId, InBindingLookup, InFormatting);
}
#endif

void FAvaTransitionLayerTask::PostLoad(FStateTreeDataView InInstanceDataView)
{
	Super::PostLoad(InInstanceDataView);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (LayerType_DEPRECATED != EAvaTransitionLayerCompareType::None)
	{
		if (FInstanceDataType* InstanceData = UE::AvaTransition::TryGetInstanceData(*this, InInstanceDataView))
		{
			InstanceData->LayerType     = LayerType_DEPRECATED;
			InstanceData->SpecificLayer = SpecificLayer_DEPRECATED;
		}
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

TArray<const FAvaTransitionBehaviorInstance*> FAvaTransitionLayerTask::QueryBehaviorInstances(const FStateTreeExecutionContext& InContext) const
{
	const FInstanceDataType& InstanceData = InContext.GetInstanceData(*this);

	UAvaTransitionSubsystem& TransitionSubsystem   = InContext.GetExternalData(TransitionSubsystemHandle);
	const FAvaTransitionContext& TransitionContext = InContext.GetExternalData(TransitionContextHandle);
	const FAvaTransitionLayerComparator Comparator = FAvaTransitionLayerUtils::BuildComparator(TransitionContext, InstanceData.LayerType, InstanceData.SpecificLayer);

	return FAvaTransitionLayerUtils::QueryBehaviorInstances(TransitionSubsystem, Comparator);
}

#undef LOCTEXT_NAMESPACE
