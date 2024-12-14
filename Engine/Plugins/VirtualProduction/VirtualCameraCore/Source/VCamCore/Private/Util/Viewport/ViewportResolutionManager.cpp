// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportResolutionManager.h"

#include "EVCamTargetViewportID.h"
#include "VCamComponent.h"
#include "Interfaces/IViewportResolutionChanger.h"
#include "Output/VCamOutputProviderBase.h"

namespace UE::VCamCore
{
	FViewportResolutionManager::FViewportResolutionManager(IViewportResolutionChanger& ResolutionChanger, FHasViewportOwnership HasViewportOwnershipDelegate)
		: ResolutionChanger(ResolutionChanger)
		, HasViewportOwnershipDelegate(MoveTemp(HasViewportOwnershipDelegate))
	{}

	void FViewportResolutionManager::UpdateViewportLockState(TConstArrayView<TWeakObjectPtr<UVCamComponent>> RegisteredVCams)
	{
		UpdateViewport(RegisteredVCams, EVCamTargetViewportID::Viewport1);
		UpdateViewport(RegisteredVCams, EVCamTargetViewportID::Viewport2);
		UpdateViewport(RegisteredVCams, EVCamTargetViewportID::Viewport3);
		UpdateViewport(RegisteredVCams, EVCamTargetViewportID::Viewport4);
	}
	
	FViewportResolutionManager::FViewportData& FViewportResolutionManager::GetViewportData(EVCamTargetViewportID ViewportID)
	{
		static_assert(static_cast<int32>(EVCamTargetViewportID::Viewport1) == 0, "Update this location");
		return ViewportData[static_cast<int32>(ViewportID)];
	}

	void FViewportResolutionManager::UpdateViewport(TConstArrayView<TWeakObjectPtr<UVCamComponent>> RegisteredVCams, EVCamTargetViewportID ViewportID)
	{
		for (const TWeakObjectPtr<UVCamComponent>& WeakVCamComponent : RegisteredVCams)
		{
			UVCamComponent* VCamComponent = WeakVCamComponent.Get();
			if (!VCamComponent)
			{
				continue;
			}
			
			for (UVCamOutputProviderBase* OutputProvider : VCamComponent->GetOutputProviders())
			{
				if (OutputProvider
					&& OutputProvider->GetTargetViewport() == ViewportID
					&& HasViewportOwnershipDelegate.Execute(*OutputProvider))
				{
					UpdateResolutionFor(*OutputProvider);
					return;
				}
			}
		}
		
		FViewportData& Data = GetViewportData(ViewportID);
		const TWeakObjectPtr<const UVCamOutputProviderBase>& WeakOutput = GetViewportData(ViewportID).CurrentOutputProvider;
		const UVCamOutputProviderBase* CurrentResolutionSource = GetViewportData(ViewportID).CurrentOutputProvider.Get();
		const bool bHasBecomeInvalidated = CurrentResolutionSource && (!HasViewportOwnershipDelegate.Execute(*CurrentResolutionSource) || CurrentResolutionSource->GetTargetViewport() != ViewportID);
		if (bHasBecomeInvalidated || WeakOutput.IsStale())
		{
			ResolutionChanger.RestoreOverrideResolutionForViewport(ViewportID);
			Data.CurrentOutputProvider = nullptr;
			Data.OverrideResolution.Reset();
		}
	}

	void FViewportResolutionManager::UpdateResolutionFor(const UVCamOutputProviderBase& OutputProvider)
	{
		const EVCamTargetViewportID TargetViewportID = OutputProvider.GetTargetViewport();
		const bool bWantsOverride = OutputProvider.bUseOverrideResolution;
		const FIntPoint& TargetResolution = OutputProvider.OverrideResolution;
		
		FViewportData& Data = GetViewportData(TargetViewportID);
		const bool bHasChangedOutputProviders = Data.CurrentOutputProvider != &OutputProvider;
		if (!bWantsOverride && (Data.HasOverriddenResolution() || bHasChangedOutputProviders))
		{
			ResolutionChanger.RestoreOverrideResolutionForViewport(TargetViewportID);
			Data.CurrentOutputProvider = nullptr;
			Data.OverrideResolution.Reset();
		}
		else if (bWantsOverride && (!Data.HasOverriddenResolution() || bHasChangedOutputProviders || *Data.OverrideResolution != TargetResolution))
		{
			ResolutionChanger.ApplyOverrideResolutionForViewport(TargetViewportID, TargetResolution.X, TargetResolution.Y);
			Data.CurrentOutputProvider = &OutputProvider;
			Data.OverrideResolution = TargetResolution;
		}
	}
}
