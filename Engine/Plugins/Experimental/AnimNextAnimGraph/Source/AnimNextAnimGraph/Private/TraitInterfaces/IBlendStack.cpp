// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraitInterfaces/IBlendStack.h"

#include "TraitCore/ExecutionContext.h"

namespace UE::AnimNext
{
	AUTO_REGISTER_ANIM_TRAIT_INTERFACE(IBlendStack)

#if WITH_EDITOR
	const FText& IBlendStack::GetDisplayName() const
	{
		static FText InterfaceName = NSLOCTEXT("TraitInterfaces", "TraitInterface_IBlendStack_Name", "Sub Graph");
		return InterfaceName;
	}
	const FText& IBlendStack::GetDisplayShortName() const
	{
		static FText InterfaceShortName = NSLOCTEXT("TraitInterfaces", "TraitInterface_IBlendStack_ShortName", "SBG");
		return InterfaceShortName;
	}
#endif // WITH_EDITOR

	void IBlendStack::PushGraph(FExecutionContext& Context, const TTraitBinding<IBlendStack>& Binding, const IBlendStack::FGraphRequest& GraphRequest, FAnimNextGraphInstancePtr& OutGraphInstance) const
	{
		TTraitBinding<IBlendStack> SuperBinding;
		if (Binding.GetStackInterfaceSuper(SuperBinding))
		{
			SuperBinding.PushGraph(Context, GraphRequest, OutGraphInstance);
			return;
		}

		OutGraphInstance.Release();
	}

	void IBlendStack::GetActiveGraphRequest(FExecutionContext& Context, const TTraitBinding<IBlendStack>& Binding, IBlendStack::FGraphRequest& OutRequest) const
	{
		TTraitBinding<IBlendStack> SuperBinding;
		if (Binding.GetStackInterfaceSuper(SuperBinding))
		{
			SuperBinding.GetActiveGraphRequest(Context, OutRequest);
			return;
		}

		OutRequest.AnimationGraph = nullptr;
	}
}
