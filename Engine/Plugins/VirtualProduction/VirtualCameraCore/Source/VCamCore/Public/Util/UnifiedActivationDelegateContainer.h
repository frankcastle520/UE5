// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Misc/Optional.h"

#include <type_traits>

#include "UnifiedActivationDelegate.h"

class FDelegateHandle;
struct FVCamCoreChangeActivationArgs;
struct FVCamCoreChangeActivationResult;

namespace UE::VCamCore
{
	struct FUnifiedActivationDelegate;
	
	/** Manages multiple FUnifiedActivationDelegates. */
	class VCAMCORE_API FUnifiedActivationDelegateContainer
	{
	public:

		/** Adds a delegate. */
		FDelegateHandle Add(FUnifiedActivationDelegate Delegate);

		/** Removes a previously bound delegate */
		void Remove(const FDelegateHandle& Handle);
		/** Removes all delegates bound to UserObject. */
		void RemoveAll(const void* UserObject);

		enum class EBreakBehavior : uint8 { Continue, Break };

		/** Iterates every delegate. It's unsafe to mutate the container during iteration. */
		template<typename TConsumer> requires std::is_invocable_r_v<EBreakBehavior, TConsumer, const FUnifiedActivationDelegate&>
		void ForEach(TConsumer&& Consumer) const
		{
			for (const TPair<FDelegateHandle, FUnifiedActivationDelegate>& Pair : Delegates)
			{
				if (Consumer(Pair.Value) == EBreakBehavior::Break)
				{
					return;
				}
			}
		}

	private:

		/** The bound delegates */
		TMap<FDelegateHandle, FUnifiedActivationDelegate> Delegates;
	};

	/**
	 * Executes all delegates in the container until the first FVCamCoreChangeActivationResult::bCanPerformOperation is false and returns that result.
	 * @return Result of the first FVCamCoreChangeActivationResult::bCanPerformOperation == false. 
	 */
	VCAMCORE_API TOptional<FVCamCoreChangeActivationResult> ExecuteUntilFailure(const FUnifiedActivationDelegateContainer& Container, const FVCamCoreChangeActivationArgs& Args);
}
