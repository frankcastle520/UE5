// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

namespace UE::ConcertInsightsServer
{
	/**
	 * On the server, this module listens for requests to start synchronized session tracing.
	 */
	class CONCERTINSIGHTSSERVER_API IConcertInsightsServerModule : public IModuleInterface
	{
	public:

		/**
		 * Singleton-like access to this module's interface.  This is just for convenience!
		 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
		 *
		 * @return Returns singleton instance, loading the module on demand if needed
		 */
		static inline IConcertInsightsServerModule& Get()
		{
			static const FName ModuleName = "ConcertInsightsServer";
			return FModuleManager::LoadModuleChecked<IConcertInsightsServerModule>(ModuleName);
		}

		/**
		 * Checks to see if this module is loaded and ready.  It is only valid to call Get() during shutdown if IsAvailable() returns true.
		 *
		 * @return True if the module is loaded and ready to use
		 */
		static inline bool IsAvailable()
		{
			static const FName ModuleName = "ConcertInsightsEditor";
			return FModuleManager::Get().IsModuleLoaded(ModuleName);
		}
	};
}

