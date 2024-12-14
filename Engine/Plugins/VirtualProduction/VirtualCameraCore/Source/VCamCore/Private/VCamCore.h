// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IVCamCoreModule.h"
#include "Modules/ModuleManager.h"
#include "Util/DeferredCleanupHandler.h"
#include "Util/UnifiedActivationDelegateContainer.h"
#include "Util/Viewport/ViewportManager.h"

namespace UE::VCamCore::WidgetSnapshotUtils
{
	struct FWidgetSnapshotSettings;
}

namespace UE::VCamCore
{
	class FVCamCoreModule : public IVCamCoreModule
	{
	public:

		static FVCamCoreModule& Get() { return FModuleManager::Get().GetModuleChecked<FVCamCoreModule>("VCamCore"); }

		//~ Begin IModuleInterface Interface
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
		//~ End IModuleInterface Interface

		//~ Begin IVCamCoreModule Interface
		virtual const FUnifiedActivationDelegateContainer& OnCanActivateOutputProvider() const override { return CanActivateDelegateContainer; }
		virtual FUnifiedActivationDelegateContainer& OnCanActivateOutputProvider() override { return CanActivateDelegateContainer; }
		//~ End IVCamCoreModule Interface

		/** @return Gets the object that manages locking and adjusting resolution of viewports. Keeps track of viewport ownership. */
		FViewportManager& GetViewportManager() { return ViewportManager; }
		/** @return The object responsible for cleaning up rendering resources created by VCams when UWorld is destroyed. */
		FDeferredCleanupHandler& GetDeferredCleanup() { return DeferredCleanup; }
		/** @return Gets the settings to use for snapshotting widgets in the VCam HUD. */
		WidgetSnapshotUtils::FWidgetSnapshotSettings GetSnapshotSettings() const;
		
	private:

		/**
		 * Manages interaction with the editor and game viewport system.
		 * 
		 * UVCamComponents and UVCamOutputProviders use this to affect viewports.
		 * Ideally we'd pass the required objects to UVCamComponents and UVCamOutputProviders directly but UObjects do not support injection.
		 * Hence, we are forced to use the service-locator pattern for accessing the FViewportManager.
		 */
		FViewportManager ViewportManager;

		/** Handles cleaning up rendering resources created by VCams when UWorld is destroyed. */
		FDeferredCleanupHandler DeferredCleanup;
		
		/** The delegate container for determining whether an output provider can be activated. */
		FUnifiedActivationDelegateContainer CanActivateDelegateContainer;
		
		/** Register the module's settings object. */
		void RegisterSettings();
		/** Unregister the module's settings object. */
		void UnregisterSettings();
	};
}

