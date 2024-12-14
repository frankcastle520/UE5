// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "Delegates/Delegate.h"
#include "HAL/Platform.h"
#include "Math/MathFwd.h"
#include "Misc/Optional.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

enum class EVCamTargetViewportID : uint8;
class UVCamComponent;
class UVCamOutputProviderBase;

namespace UE::VCamCore
{
	class IViewportResolutionChanger;

	/** Takes care of setting the right resolution for every viewport that has an assigned output provider.  */
	class FViewportResolutionManager : public FNoncopyable
	{
	public:

		DECLARE_DELEGATE_RetVal_OneParam(bool, FHasViewportOwnership, const UVCamOutputProviderBase&);
		FViewportResolutionManager(
			IViewportResolutionChanger& ResolutionChanger UE_LIFETIMEBOUND,
			FHasViewportOwnership HasViewportOwnershipDelegate
			);

		/** Checks which of the output providers in the given VCam array should lock the viewport. */
		void UpdateViewportLockState(TConstArrayView<TWeakObjectPtr<UVCamComponent>> RegisteredVCams);
		
	private:

		/** Talks to the viewport for changing the resolution. */
		IViewportResolutionChanger& ResolutionChanger;

		/** Looks up whether the given output provider has ownership over the viewport. */
		const FHasViewportOwnership HasViewportOwnershipDelegate;

		struct FViewportData
		{
			TWeakObjectPtr<const UVCamOutputProviderBase> CurrentOutputProvider;
			/** Set if we've overriden the resolution. */
			TOptional<FIntPoint> OverrideResolution;

			bool HasOverriddenResolution() const { return OverrideResolution.IsSet(); }
		} ViewportData[4];

		FViewportData& GetViewportData(EVCamTargetViewportID ViewportID);
		
		void UpdateViewport(TConstArrayView<TWeakObjectPtr<UVCamComponent>> RegisteredVCams, EVCamTargetViewportID ViewportID);
		void UpdateResolutionFor(const UVCamOutputProviderBase& OutputProvider);
	};
}

