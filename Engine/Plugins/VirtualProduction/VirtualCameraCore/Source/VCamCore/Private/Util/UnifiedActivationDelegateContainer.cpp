// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/UnifiedActivationDelegateContainer.h"

namespace UE::VCamCore
{
	FDelegateHandle FUnifiedActivationDelegateContainer::Add(FUnifiedActivationDelegate Delegate)
	{
		if (!Delegate.IsBound())
		{
			return {};
		}
		
		const FDelegateHandle Handle(FDelegateHandle::GenerateNewHandle);
		Delegates.Emplace(Handle, MoveTemp(Delegate));
		return Handle;
	}

	void FUnifiedActivationDelegateContainer::Remove(const FDelegateHandle& Handle)
	{
		Delegates.Remove(Handle);
	}

	void FUnifiedActivationDelegateContainer::RemoveAll(const void* UserObject)
	{
		for (auto It = Delegates.CreateIterator(); It; ++It)
		{
			if (It->Value.IsBoundToObject(UserObject))
			{
				It.RemoveCurrent();
			}
		}
	}

	TOptional<FVCamCoreChangeActivationResult> ExecuteUntilFailure(const FUnifiedActivationDelegateContainer& Container, const FVCamCoreChangeActivationArgs& Args)
	{
		TOptional<FVCamCoreChangeActivationResult> Result;
		Container.ForEach([&Args, &Result](const FUnifiedActivationDelegate& Delegate)
		{
			FVCamCoreChangeActivationResult IntermediateResult = Delegate.Execute(Args);
			if (!IntermediateResult.bCanPerformOperation)
			{
				Result.Emplace(MoveTemp(IntermediateResult));
				return FUnifiedActivationDelegateContainer::EBreakBehavior::Break;
			}
			return FUnifiedActivationDelegateContainer::EBreakBehavior::Continue;
		});
		return Result;
	}
}
