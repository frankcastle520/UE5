// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlendStack/BlendStackAnimNodeLibrary.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "BlendStack/AnimNode_BlendStackInput.h"
#include "BlendStack/BlendStackDefines.h"

FBlendStackAnimNodeReference UBlendStackAnimNodeLibrary::ConvertToBlendStackNode(const FAnimNodeReference& Node, EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FBlendStackAnimNodeReference>(Node, Result);
}

UAnimationAsset* UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(const FAnimNodeReference& Node)
{
	if (FAnimNode_BlendStackInput* BlendStackInput = Node.GetAnimNodePtr<FAnimNode_BlendStackInput>())
	{
		if (BlendStackInput->Player && *BlendStackInput->Player )
		{
			return (*BlendStackInput->Player)->GetAnimationAsset();
		}
	}
	return nullptr;
}

float UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(const FAnimNodeReference& Node)
{
	if (FAnimNode_BlendStackInput* BlendStackInput = Node.GetAnimNodePtr<FAnimNode_BlendStackInput>())
	{
		if (BlendStackInput->Player && *BlendStackInput->Player )
		{
			return (*BlendStackInput->Player)->GetAccumulatedTime();
		}
	}
	return 0.f;
}

void UBlendStackAnimNodeLibrary::BlendTo(const FAnimUpdateContext& Context, 
										const FBlendStackAnimNodeReference& BlendStackNode, 
										UAnimationAsset* AnimationAsset,
										float AnimationTime,
										bool bLoop,
										bool bMirrored,
										float BlendTime,
										FVector BlendParameters,
										float WantedPlayRate,
										float ActivationDelay)
{
	if (AnimationAsset != nullptr)
	{
		if (FAnimNode_BlendStack* BlendStackNodePtr = BlendStackNode.GetAnimNodePtr<FAnimNode_BlendStack>())
		{
			if (const FAnimationUpdateContext* AnimationUpdateContext = Context.GetContext())
			{
				BlendStackNodePtr->BlendTo(
					*AnimationUpdateContext,
					AnimationAsset,
					AnimationTime,
					bLoop,
					bMirrored,
					BlendStackNodePtr->MirrorDataTable,
					BlendTime,
					BlendStackNodePtr->BlendProfile,
					BlendStackNodePtr->BlendOption,
					BlendStackNodePtr->bUseInertialBlend,
					BlendParameters,
					WantedPlayRate,
					ActivationDelay,
					BlendStackNodePtr->GetGroupName(), BlendStackNodePtr->GetGroupRole(), BlendStackNodePtr->GetGroupMethod());
			}
			else
			{
				UE_LOG(LogBlendStack, Warning, TEXT("UBlendStackAnimNodeLibrary::BlendTo called with an invalid context."));
			}
		}
		else
		{
			UE_LOG(LogBlendStack, Warning, TEXT("UBlendStackAnimNodeLibrary::BlendTo called with an invalid type."));
		}
	}
}

void UBlendStackAnimNodeLibrary::BlendToWithSettings(const FAnimUpdateContext& Context,
													const FBlendStackAnimNodeReference& BlendStackNode, 
													UAnimationAsset* AnimationAsset /*= nullptr*/, 
													float AnimationTime /*= 0.f*/, 
													bool bLoop /*= false*/, 
													bool bMirrored /*= false*/, 
													float BlendTime /*= 0.2f*/, 
													UBlendProfile* BlendProfile /*= nullptr*/, 
													EAlphaBlendOption BlendOption /*= EAlphaBlendOption::HermiteCubic*/,
													bool bInertialBlend /*= false*/, 
													FVector BlendParameters /*= FVector::ZeroVector*/, 
													float WantedPlayRate /*= 1.f*/, 
													float ActivationDelay /*= 0.f*/)
{
	if (AnimationAsset != nullptr)
	{
		if (FAnimNode_BlendStack* BlendStackNodePtr = BlendStackNode.GetAnimNodePtr<FAnimNode_BlendStack>())
		{
			if (const FAnimationUpdateContext* AnimationUpdateContext = Context.GetContext())
			{
				BlendStackNodePtr->BlendTo(
					*AnimationUpdateContext,
					AnimationAsset,
					AnimationTime,
					bLoop,
					bMirrored,
					BlendStackNodePtr->MirrorDataTable,
					BlendTime,
					BlendProfile,
					BlendOption,
					bInertialBlend,
					BlendParameters,
					WantedPlayRate,
					ActivationDelay,
					BlendStackNodePtr->GetGroupName(), BlendStackNodePtr->GetGroupRole(), BlendStackNodePtr->GetGroupMethod());
			}
			else
			{
				UE_LOG(LogBlendStack, Warning, TEXT("UBlendStackAnimNodeLibrary::BlendToWithSettings called with an invalid context."));
			}
		}
		else
		{
			UE_LOG(LogBlendStack, Warning, TEXT("UBlendStackAnimNodeLibrary::BlendToWithSettings called with an invalid type."));
		}
	}
}

UAnimationAsset* UBlendStackAnimNodeLibrary::GetCurrentAsset(const FBlendStackAnimNodeReference& BlendStackNode)
{
	if (FAnimNode_BlendStack* BlendStackNodePtr = BlendStackNode.GetAnimNodePtr<FAnimNode_BlendStack>())
	{
		return BlendStackNodePtr->GetAnimAsset();
	}

	UE_LOG(LogBlendStack, Warning, TEXT("UBlendStackAnimNodeLibrary::GetCurrentAsset called with an invalid type."));
	return nullptr;
}

float UBlendStackAnimNodeLibrary::GetCurrentAssetTime(const FBlendStackAnimNodeReference& BlendStackNode)
{
	if (FAnimNode_BlendStack* BlendStackNodePtr = BlendStackNode.GetAnimNodePtr<FAnimNode_BlendStack>())
	{
		return BlendStackNodePtr->GetCurrentAssetTime();
	}

	UE_LOG(LogBlendStack, Warning, TEXT("UBlendStackAnimNodeLibrary::GetCurrentAssetTime called with an invalid type."));
	return 0.0f;
}

float UBlendStackAnimNodeLibrary::GetCurrentAssetTimeRemaining(const FBlendStackAnimNodeReference& BlendStackNode)
{
	if (FAnimNode_BlendStack* BlendStackNodePtr = BlendStackNode.GetAnimNodePtr<FAnimNode_BlendStack>())
	{
		return BlendStackNodePtr->GetCurrentAssetLength() - BlendStackNodePtr->GetCurrentAssetTimePlayRateAdjusted();
	}

	UE_LOG(LogBlendStack, Warning, TEXT("UBlendStackAnimNodeLibrary::GetCurrentAssetTimeRemaining called with an invalid type."));
	return 0.0f;
}

bool UBlendStackAnimNodeLibrary::IsCurrentAssetLooping(const FBlendStackAnimNodeReference& BlendStackNode)
{
	if (FAnimNode_BlendStack* BlendStackNodePtr = BlendStackNode.GetAnimNodePtr<FAnimNode_BlendStack>())
	{
		return BlendStackNodePtr->IsLooping();
	}

	UE_LOG(LogBlendStack, Warning, TEXT("UBlendStackAnimNodeLibrary::IsCurrentAssetLooping called with an invalid type."));
	return false;
}

void UBlendStackAnimNodeLibrary::ForceBlendNextUpdate(const FBlendStackAnimNodeReference& BlendStackNode)
{
	if (FAnimNode_BlendStack* BlendStackNodePtr = BlendStackNode.GetAnimNodePtr<FAnimNode_BlendStack>())
	{
		BlendStackNodePtr->ForceBlendNextUpdate();
	}
	else
	{
		UE_LOG(LogBlendStack, Warning, TEXT("UBlendStackAnimNodeLibrary::ForceBlendNextUpdate called with an invalid type."));
	}
}