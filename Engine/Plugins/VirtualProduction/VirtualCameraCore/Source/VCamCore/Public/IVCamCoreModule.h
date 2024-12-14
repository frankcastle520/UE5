// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

namespace UE::VCamCore
{
	class FUnifiedActivationDelegateContainer;
	
	class VCAMCORE_API IVCamCoreModule : public IModuleInterface
	{
	public:

		static IVCamCoreModule& Get()
		{
			return FModuleManager::Get().GetModuleChecked<IVCamCoreModule>("VCamCore");
		}

		/** @return Gets the delegate container for determining whether an output provider can be activated. */
		virtual const FUnifiedActivationDelegateContainer& OnCanActivateOutputProvider() const = 0;
		/** @return Gets the delegate container for determining whether an output provider can be activated. */
		virtual FUnifiedActivationDelegateContainer& OnCanActivateOutputProvider() = 0;
	};
}
