// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Misc/Timecode.h"
#include "Misc/FrameRate.h"
#include "Sound/SoundWave.h"

#include "SoundWaveTimecodeUtils.generated.h"

UCLASS(BlueprintType, Blueprintable)
class CAPTUREDATAUTILS_API USoundWaveTimecodeUtils : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Audio")
	static void SetTimecodeInfo(const FTimecode& InTimecode, const FFrameRate& InFrameRate, USoundWave* OutSoundWave);

	UFUNCTION(BlueprintCallable, Category = "Audio")
	static FTimecode GetTimecode(const USoundWave* InSoundWave);

	UFUNCTION(BlueprintCallable, Category = "Audio")
	static FFrameRate GetFrameRate(const USoundWave* InSoundWave);
};
