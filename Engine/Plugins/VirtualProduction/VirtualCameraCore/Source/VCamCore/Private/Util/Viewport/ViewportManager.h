// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OwnershipMapping.h"
#include "VCamComponent.h"
#include "ViewportLockManager.h"
#include "ViewportResolutionManager.h"
#include "Systems/ViewportSystemSwitcher.h"
#include "Util/VCamViewportLocker.h"

#include "HAL/Platform.h"
#include "Templates/UnrealTemplate.h"

class UVCamComponent;
class UVCamOutputProviderBase;
enum class EVCamTargetViewportID : uint8;

namespace UE::VCamCore
{
	/**
	 * Decides the ownership of viewports for multiple output providers.
	 * Ownership is then passed to FViewportLockManager and FViewportResolutionManager, which handle locking the viewport and applying override resolution.
	 *
	 * An output provider has ownership over a viewport when:
	 * 1. Output provider is outputting
	 * 2. Owning UVCamComponent::ViewportLocker is configured to lock OR UVCamOutputProviderBase::NeedsForceLockToViewport.
	 * 
	 * The specific implementations are designed to be injected; this allows unit tests to inject mocks.
	 */
	class FViewportManagerBase : public FNoncopyable
	{
	public:

		/** @return If set, the ownership is overriden with this value. If unset, the default behaviour is used. */
		DECLARE_DELEGATE_RetVal_OneParam(TOptional<bool>, FOverrideShouldHaveOwnership, const UVCamOutputProviderBase&);

		/**
		 * @param ViewportLocker Implementation for locking viewports. Outlives the constructed object. 
		 * @param ResolutionChanger Implementation for changing viewport resolution. Outlives the constructed object. 
		 * @param OverrideShouldHaveOwnership Optional callback for overriding whether an object should have ownership. Useful for unit tests.
		 */
		FViewportManagerBase(
			IViewportLocker& ViewportLocker UE_LIFETIMEBOUND,
			IViewportResolutionChanger& ResolutionChanger UE_LIFETIMEBOUND,
			FOverrideShouldHaveOwnership OverrideShouldHaveOwnership = {}
			);
		~FViewportManagerBase();

		/**
		 * Registers Component, so it is now considered for locking the viewport.
		 * A viewport will only be locked if one of its output providers has acquired ownership.
		 */
		void RegisterVCamComponent(UVCamComponent& Component);
		/** Component will no longer be considered for viewport locking anymore.*/
		void UnregisterVCamComponent(UVCamComponent& Component);
		
		/** Called when something about the lock state has changed and needs refreshing. */
		void RequestLockRefresh() { bHasRequestedLockRefresh = true; }
		/** Called when UVCamOutputProviderBase::OverrideResolution, bUseOverrideResolution, or TargetViewport change. */
		void RequestResolutionRefresh() { bHasRequestedResolutionRefresh = true; }

	private:

		/** Talks to engine for changing the lock state of viewports. Used internally by LockManager only. */
		IViewportLocker& ViewportLocker;
		/** Talks to engine for changing the resolution of viewports. Used internally by ResolutionManager only. */
		IViewportResolutionChanger& ResolutionChanger;
		/** Optional callback for overriding whether an object should have ownership. Useful for unit tests */
		const FOverrideShouldHaveOwnership OverrideShouldHaveOwnership;
		
		/**
		 * Keeps track what output providers the viewports are owned by.
		 * 
		 * An output provider has ownership over a viewport when:
		 * 1. Output provider is outputting
		 * 2. Owning UVCamComponent::ViewportLocker is configured to lock OR UVCamOutputProviderBase::NeedsForceLockToViewport.
		 */
		TOwnershipMapping<EVCamTargetViewportID, TWeakObjectPtr<const UVCamOutputProviderBase>> ViewportOwnership;
		
		/** Handles logic for locking viewports. */
		FViewportLockManager LockManager;
		/** Handles logic for changing viewport resolution. */
		FViewportResolutionManager ResolutionManager;
		
		/** Components that want to affect the viewport(s). */
		TArray<TWeakObjectPtr<UVCamComponent>> RegisteredVCams;

		/** Whether the viewports were set to pilot last tick or not. */
		bool ViewportShouldPilotStates[4] { false, false, false, false };

		/** Whether locks should be refreshed. */
		bool bHasRequestedLockRefresh = false;
		/** Whether viewport resolutions should be refreshed. */
		bool bHasRequestedResolutionRefresh = false;

		/** Processes changes made to registered VCams for the purpose updating ownership and then proceeds updating the viewports. */
		void OnEndOfFrame();

		/** Removes dead registered VCams */
		TInlineComponentArray<TWeakObjectPtr<UVCamComponent>> CleanseRegisteredVCams();
		/** Updates ViewportShouldPilotStates and sets bHasRequestedLockRefresh if it has changed. */
		void UpdatePilotState(bool bAllowRefresh = true);
		void UpdatePilotStateNoRefresh() { UpdatePilotState(false); }
		
		/** Process changes that have occured in this frame and updates ViewportOwnership. Returns whether any changes have been made. */
		void UpdateAllOwnership(const TInlineComponentArray<TWeakObjectPtr<UVCamComponent>>& RemovedThisTick);
		/** Updates ownership for a single OutputProvider. */
		void UpdateOwnershipFor(const FVCamViewportLocker& OwnerLockState, const UVCamOutputProviderBase& OutputProvider);
		bool DetermineOwnershipFor(const UVCamOutputProviderBase& OutputProvider, const FVCamViewportLocker& OwnerLockState) const;
		
		/** Removes the VCam's output providers from the ownership model. */
		void RemoveOwnership(const UVCamComponent& Component);

		/** Callback passed to LockManager and ResolutionManager for deciding whether a given output provider has ownership over a viewport. */
		bool HasViewportOwnership(const UVCamOutputProviderBase& OutputProvider) const;
	};

	/**
	 * Adds look-up functions for special UI constructs (e.g. FSceneViewport, input window, and level editor client) to the manager.
	 * Not unit tested.
	 */
	class FViewportManager : public FNoncopyable
	{
	public:

		FViewportManager() : Implementation(ViewportSystemSwitcher, ViewportSystemSwitcher) {}

		/**
		 * Registers Component, so it is now considered for locking the viewport.
		 * A viewport will only be locked if one of its output providers has acquired ownership.
		 */
		void RegisterVCamComponent(UVCamComponent& Component) { Implementation.RegisterVCamComponent(Component); }
		/** Component will no longer be considered for viewport locking anymore.*/
		void UnregisterVCamComponent(UVCamComponent& Component) { Implementation.UnregisterVCamComponent(Component); }
		
		/** Called when something about the lock state has changed and needs refreshing. */
		void RequestLockRefresh() { Implementation.RequestLockRefresh(); }
		/** Called when UVCamOutputProviderBase::OverrideResolution, bUseOverrideResolution, or TargetViewport change. */
		void RequestResolutionRefresh() { Implementation.RequestResolutionRefresh(); }
		
		/** Gets the scene viewport identified by ViewportID. */
		TSharedPtr<FSceneViewport> GetSceneViewport(EVCamTargetViewportID ViewportID) const { return ViewportSystemSwitcher.GetSceneViewport(ViewportID); }
		/** Gets the window that contains the given viewport. */
		TWeakPtr<SWindow> GetInputWindow(EVCamTargetViewportID ViewportID) const { return ViewportSystemSwitcher.GetInputWindow(ViewportID); }
		
#if WITH_EDITOR
		/**
		 * Gets the FLevelEditorViewportClient that is managing the ViewportID.
		 * This will only return valid in the editor environment (thus not work in PIE or games).
		 * 
		 * If the UVCamOutputProviderBase::DisplayType is EVPWidgetDisplayType::PostProcessWithBlendMaterial, the FEditorViewportClient::ViewModifiers
		 * are used to overlay the widget into the viewport because it plays nicely with other viewports. In games, the output provider simply places
		 * the post process material in the target camera because there is only one viewport.
		 * Hence, there is no implementation for games.
		 */
		FLevelEditorViewportClient* GetEditorViewportClient(EVCamTargetViewportID ViewportID) const { return ViewportSystemSwitcher.GetEditorViewportClient(ViewportID); }
#endif
		
	private:
		
		/**
		 * Decides (at compile-time or dynamically) which viewport system should be used for locking.
		 * In games, the game viewport can be used. In editors, it depends on whether we're in PIE or not.
		 * See the class docu for further info.
		 */
		FViewportSystemSwitcher ViewportSystemSwitcher;

		/**
		 * Composition instead of inheritance so we can fully initialize ViewportSystemSwitcher before passing it down.
		 * 
		 * (Even though it'd be valid C++ passing uninitialized ViewportSystemSwitcher to parent constructor, it's dangerous if somebody in the future
		 * changes that constructor to call a function on it.)
		 */
		FViewportManagerBase Implementation;
	};
}


