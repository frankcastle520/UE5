// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "Misc/TVariant.h"
#include "UnifiedActivationDelegate.generated.h"

class UVCamOutputProviderBase;

/** Input struct for deciding whether an output provider can change its activation state. */
USTRUCT(BlueprintType, meta = (DisplayName = "Can Activate Args"))
struct FVCamCoreChangeActivationArgs
{
	GENERATED_BODY()

	/** The output provider that is about to be changed */
	UPROPERTY(BlueprintReadWrite, Category = "Virtual Camera")
	TObjectPtr<UVCamOutputProviderBase> OutputProvider;
};

/** Output struct for deciding whether an output provider can change its activation state. */
USTRUCT(BlueprintType, meta = (DisplayName = "Can Activate Args"))
struct FVCamCoreChangeActivationResult
{
	GENERATED_BODY()

	/** Whether the activation change can take place */
	UPROPERTY(BlueprintReadWrite, Category = "Virtual Camera")
	bool bCanPerformOperation = true;

	/** Optional reason to display if the operation is not valid. */
	UPROPERTY(BlueprintReadWrite, Category = "Virtual Camera")
	FText Reason = FText::GetEmpty();
};

DECLARE_DELEGATE_RetVal_OneParam(FVCamCoreChangeActivationResult, FCanChangeActiviationVCamDelegate, const FVCamCoreChangeActivationArgs&);
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(FVCamCoreChangeActivationResult, FCanChangeActiviationDynamicVCamDelegate, const FVCamCoreChangeActivationArgs&, Args);

namespace UE::VCamCore
{
	/** Delegate for deciding whether an output provider can change its activation state. */
	struct VCAMCORE_API FUnifiedActivationDelegate
	{
		using FActivationDelegateVariant = TVariant<TYPE_OF_NULLPTR, FCanChangeActiviationVCamDelegate, FCanChangeActiviationDynamicVCamDelegate>;

		FActivationDelegateVariant VariantDelegate;

		FUnifiedActivationDelegate() = default;
		FUnifiedActivationDelegate(FCanChangeActiviationVCamDelegate Delegate) : VariantDelegate(TInPlaceType<FCanChangeActiviationVCamDelegate>(), MoveTemp(Delegate)) {}
		FUnifiedActivationDelegate(FCanChangeActiviationDynamicVCamDelegate Delegate) : VariantDelegate(TInPlaceType<FCanChangeActiviationDynamicVCamDelegate>(), MoveTemp(Delegate)) {}

		FVCamCoreChangeActivationResult Execute(const FVCamCoreChangeActivationArgs& Args) const;
		bool IsBound() const;
		bool IsBoundToObject(void const* InUserObject) const;

		void Unbind() { VariantDelegate.Set<TYPE_OF_NULLPTR>(nullptr); }
	};
}
