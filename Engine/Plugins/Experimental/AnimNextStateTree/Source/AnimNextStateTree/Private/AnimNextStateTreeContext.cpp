// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextStateTreeContext.h"

#include "TraitCore/TraitStackBinding.h"
#include "TraitInterfaces/IBlendStack.h"
#include "TraitInterfaces/ISmoothBlend.h"

bool FAnimNextStateTreeTraitContext::PushAnimationGraphOntoBlendStack(TNonNullPtr<UAnimNextAnimationGraph> InAnimationGraph, const FAlphaBlendArgs& InBlendArguments) const
{		
	if (Binding && Context)
	{
		UE::AnimNext::TTraitBinding<UE::AnimNext::IBlendStack> BlendStackBinding;
		if(Binding->GetInterface<UE::AnimNext::IBlendStack>(BlendStackBinding))
		{
			UE::AnimNext::IBlendStack::FGraphRequest CurrentRequest;			
			BlendStackBinding.GetActiveGraphRequest(*Context, CurrentRequest);

			if (CurrentRequest.AnimationGraph != nullptr)
			{
				if (CurrentRequest.AnimationGraph == InAnimationGraph)
				{
					return true;
				}
			}
			
			FAnimNextGraphInstancePtr GraphPtr;
			UE::AnimNext::IBlendStack::FGraphRequest GraphRequest;
			GraphRequest.BlendArgs = InBlendArguments;
			GraphRequest.AnimationGraph = InAnimationGraph.Get();
			BlendStackBinding.PushGraph(*Context, GraphRequest, GraphPtr);

			return true;
		}
	}

	return false;
}