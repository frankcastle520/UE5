// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraitInterfaces/IContinuousBlend.h"

#include "TraitCore/ExecutionContext.h"

namespace UE::AnimNext
{
	AUTO_REGISTER_ANIM_TRAIT_INTERFACE(IContinuousBlend)

#if WITH_EDITOR
	const FText& IContinuousBlend::GetDisplayName() const
	{
		static FText InterfaceName = NSLOCTEXT("TraitInterfaces", "TraitInterface_ContinuousBlend_Name", "Continuous Blend");
		return InterfaceName;
	}  
	const FText& IContinuousBlend::GetDisplayShortName() const
	{
		static FText InterfaceShortName = NSLOCTEXT("TraitInterfaces", "TraitInterface_ContinuousBlend_ShortName", "CNB");
		return InterfaceShortName;
	}
#endif // WITH_EDITOR

	float IContinuousBlend::GetBlendWeight(const FExecutionContext& Context, const TTraitBinding<IContinuousBlend>& Binding, int32 ChildIndex) const
	{
		TTraitBinding<IContinuousBlend> SuperBinding;
		if (Binding.GetStackInterfaceSuper(SuperBinding))
		{
			return SuperBinding.GetBlendWeight(Context, ChildIndex);
		}

		return -1.0f;
	}
}
