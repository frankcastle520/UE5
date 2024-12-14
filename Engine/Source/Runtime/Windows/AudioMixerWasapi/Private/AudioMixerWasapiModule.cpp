// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerWasapi.h"
#include "AudioMixerWasapiLog.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAudioMixerWasapi);

class FAudioMixerModuleWasapi : public IAudioDeviceModule
{
public:
	virtual void StartupModule() override
	{
		IAudioDeviceModule::StartupModule();

		FModuleManager::Get().LoadModuleChecked(TEXT("AudioMixer"));
		FModuleManager::Get().LoadModuleChecked(TEXT("AudioMixerCore"));
	}

	virtual bool IsAudioMixerModule() const override
	{
		return true;
	}

	virtual Audio::IAudioMixerPlatformInterface* CreateAudioMixerPlatformInterface() override
	{
		return new Audio::FAudioMixerWasapi();
	}
};

IMPLEMENT_MODULE(FAudioMixerModuleWasapi, AudioMixerWasapi);
