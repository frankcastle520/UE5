// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Harmonix/HarmonixDeveloperSettings.h"
#include "HarmonixDsp/FusionSampler/FusionVoicePool.h"

#include "FusionSamplerConfig.generated.h"

UCLASS(Config = Engine, defaultconfig, meta = (DisplayName = "Fusion Sampler Settings"))
class HARMONIXDSP_API UFusionSamplerConfig : public UHarmonixDeveloperSettings
{
	GENERATED_BODY()

public:

	UPROPERTY(config, EditDefaultsOnly, Category = "Voice Pools")
	FFusionVoiceConfig DefaultVoicePoolConfig;

	UPROPERTY(config, EditDefaultsOnly, Category = "Voice Pools")
	TMap<FName, FFusionVoiceConfig> VoicePoolConfigs;

	FFusionVoiceConfig GetVoiceConfigForPoolName(FName PoolName) const;
};