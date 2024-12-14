// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioPlatformSupportWasapiLog.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAudioPlatformSupportWasapi)


class FAudioPlatformSupportWasapiModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{

	}

	virtual void ShutdownModule() override
	{

	}
};

IMPLEMENT_MODULE(FAudioPlatformSupportWasapiModule, AudioPlatformSupportWasapi);
