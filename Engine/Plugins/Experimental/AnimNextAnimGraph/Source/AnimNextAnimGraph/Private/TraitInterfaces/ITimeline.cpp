// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraitInterfaces/ITimeline.h"

#include "TraitCore/ExecutionContext.h"

namespace UE::AnimNext
{
	AUTO_REGISTER_ANIM_TRAIT_INTERFACE(ITimeline)

#if WITH_EDITOR
	const FText& ITimeline::GetDisplayName() const
	{
		static FText InterfaceName = NSLOCTEXT("TraitInterfaces", "TraitInterface_ITimeline_Name", "Timeline");
		return InterfaceName;
	}
	const FText& ITimeline::GetDisplayShortName() const
	{
		static FText InterfaceShortName = NSLOCTEXT("TraitInterfaces", "TraitInterface_ITimeline_ShortName", "TIM");
		return InterfaceShortName;
	}
#endif // WITH_EDITOR

	float ITimeline::GetPlayRate(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding) const
	{
		TTraitBinding<ITimeline> SuperBinding;
		if (Binding.GetStackInterfaceSuper(SuperBinding))
		{
			return SuperBinding.GetPlayRate(Context);
		}

		return 1.0f;
	}

	FTimelineProgress ITimeline::GetProgress(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding) const
	{
		TTraitBinding<ITimeline> SuperBinding;
		if (Binding.GetStackInterfaceSuper(SuperBinding))
		{
			return SuperBinding.GetProgress(Context);
		}

		return FTimelineProgress();
	}

	FTimelineProgress ITimeline::SimulateAdvanceBy(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float DeltaTime) const
	{
		TTraitBinding<ITimeline> SuperBinding;
		if (Binding.GetStackInterfaceSuper(SuperBinding))
		{
			return SuperBinding.SimulateAdvanceBy(Context, DeltaTime);
		}

		return FTimelineProgress();
	}

	FTimelineProgress ITimeline::AdvanceBy(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float DeltaTime) const
	{
		TTraitBinding<ITimeline> SuperBinding;
		if (Binding.GetStackInterfaceSuper(SuperBinding))
		{
			return SuperBinding.AdvanceBy(Context, DeltaTime);
		}

		return FTimelineProgress();
	}

	void ITimeline::AdvanceToRatio(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float ProgressRatio) const
	{
		TTraitBinding<ITimeline> SuperBinding;
		if (Binding.GetStackInterfaceSuper(SuperBinding))
		{
			SuperBinding.AdvanceToRatio(Context, ProgressRatio);
		}
	}
}
