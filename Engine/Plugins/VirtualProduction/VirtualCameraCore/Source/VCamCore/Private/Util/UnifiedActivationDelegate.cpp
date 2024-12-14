// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/UnifiedActivationDelegate.h"

namespace UE::VCamCore
{
	FVCamCoreChangeActivationResult FUnifiedActivationDelegate::Execute(const FVCamCoreChangeActivationArgs& Args) const
	{
		switch (VariantDelegate.GetIndex())
		{
		case FActivationDelegateVariant::IndexOfType<FCanChangeActiviationVCamDelegate>():
			{
				const FCanChangeActiviationVCamDelegate& Delegate = VariantDelegate.Get<FCanChangeActiviationVCamDelegate>();
				return Delegate.IsBound() ? Delegate.Execute(Args) : FVCamCoreChangeActivationResult{};
			}
		case FActivationDelegateVariant::IndexOfType<FCanChangeActiviationDynamicVCamDelegate>():
			{
				const FCanChangeActiviationDynamicVCamDelegate& Delegate = VariantDelegate.Get<FCanChangeActiviationDynamicVCamDelegate>();
				return Delegate.IsBound() ? Delegate.Execute(Args) : FVCamCoreChangeActivationResult{};
			}
		default:
			return {};
		}
	}

	bool FUnifiedActivationDelegate::IsBound() const
	{
		switch (VariantDelegate.GetIndex())
		{
		case FActivationDelegateVariant::IndexOfType<FCanChangeActiviationVCamDelegate>():
			return VariantDelegate.Get<FCanChangeActiviationVCamDelegate>().IsBound();
		case FActivationDelegateVariant::IndexOfType<FCanChangeActiviationDynamicVCamDelegate>():
			return VariantDelegate.Get<FCanChangeActiviationDynamicVCamDelegate>().IsBound();
		default:
			return false;
		}
	}

	bool FUnifiedActivationDelegate::IsBoundToObject(void const* InUserObject) const
	{
		switch (VariantDelegate.GetIndex())
		{
		case FActivationDelegateVariant::IndexOfType<FCanChangeActiviationVCamDelegate>():
			return VariantDelegate.Get<FCanChangeActiviationVCamDelegate>().IsBoundToObject(InUserObject);
		case FActivationDelegateVariant::IndexOfType<FCanChangeActiviationDynamicVCamDelegate>():
			return VariantDelegate.Get<FCanChangeActiviationDynamicVCamDelegate>().IsBoundToObject(InUserObject);
		default:
			return false;
		}
	}
}
