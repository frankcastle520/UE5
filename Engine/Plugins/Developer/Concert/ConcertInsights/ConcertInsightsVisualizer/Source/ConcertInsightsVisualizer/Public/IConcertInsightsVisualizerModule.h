// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

namespace UE::ConcertInsightsVisualizer
{
	/** This module extends and manages all the Concert UI in Unreal Insights. */
	class CONCERTINSIGHTSVISUALIZER_API IConcertInsightsVisualizerModule : public IModuleInterface
	{
	public:

		/**
		 * Singleton-like access to this module's interface.  This is just for convenience!
		 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
		 *
		 * @return Returns singleton instance, loading the module on demand if needed
		 */
		static inline IConcertInsightsVisualizerModule& Get()
		{
			static const FName ModuleName = "ConcertInsightsVisualizer";
			return FModuleManager::LoadModuleChecked<IConcertInsightsVisualizerModule>(ModuleName);
		}

		/**
		 * Checks to see if this module is loaded and ready.  It is only valid to call Get() during shutdown if IsAvailable() returns true.
		 *
		 * @return True if the module is loaded and ready to use
		 */
		static inline bool IsAvailable()
		{
			static const FName ModuleName = "ConcertInsightsVisualizer";
			return FModuleManager::Get().IsModuleLoaded(ModuleName);
		}
	};
}
