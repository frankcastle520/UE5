// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Timecode.h"
#include "ImgMediaSource.h"

#include "ImageSequenceTimecodeUtils.generated.h"

UCLASS(BlueprintType, Blueprintable)
class CAPTUREDATAUTILS_API UImageSequenceTimecodeUtils
	: public UObject
{
	GENERATED_BODY()

public:

	static const FName TimecodeTagName;
	static const FName TimecodeRateTagName;

	UFUNCTION(BlueprintCallable, Category = "ImageSequence")
	static void SetTimecodeInfo(const FTimecode& InTimecode, const FFrameRate& InFrameRate, UImgMediaSource* InImageSequence);

	UFUNCTION(BlueprintCallable, Category = "ImageSequence")
	static void SetTimecodeInfoString(const FString& InTimecode, const FString& InFrameRate, UImgMediaSource* InImageSequence);

	UFUNCTION(BlueprintCallable, Category = "ImageSequence")
	static FTimecode GetTimecode(UImgMediaSource* InImageSequence);

	UFUNCTION(BlueprintCallable, Category = "ImageSequence")
	static FFrameRate GetFrameRate(UImgMediaSource* InImageSequence);

	UFUNCTION(BlueprintCallable, Category = "ImageSequence")
	static FString GetTimecodeString(UImgMediaSource* InImageSequence);

	UFUNCTION(BlueprintCallable, Category = "ImageSequence")
	static FString GetFrameRateString(UImgMediaSource* InImageSequence);

	static bool IsValidTimecodeInfo(const FTimecode& InTimecode, const FFrameRate& InTimecodeRate);
	static bool IsValidTimecode(const FTimecode& InTimecode);
	static bool IsValidFrameRate(const FFrameRate& InTimecodeRate);
};
