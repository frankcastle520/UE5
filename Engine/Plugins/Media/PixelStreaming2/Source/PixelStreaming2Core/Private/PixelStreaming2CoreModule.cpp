// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "CoreUtils.h"

class FPixelStreaming2CoreModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
		if (!UE::PixelStreaming2::IsStreamingSupported())
		{
			return;
		}
	}

	virtual void ShutdownModule() override
	{
		if (!UE::PixelStreaming2::IsStreamingSupported())
		{
			return;
		}
	}
};

IMPLEMENT_MODULE(FPixelStreaming2CoreModule, PixelStreaming2Core);
