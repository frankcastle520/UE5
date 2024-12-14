// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class CAPTUREDATACORE_API FImageSequenceUtils
{
public:
	static bool GetImageSequencePathAndFiles(const class UImgMediaSource* InImgSequence, FString& OutFullSequencePath, TArray<FString>& OutImageFiles);
	static bool GetImageSequencePathAndFiles(const FString& InFullSequencePath, TArray<FString>& OutImageFiles);

	static bool GetImageSequenceInfo(const class UImgMediaSource* InImgSequence, FIntVector2& OutDimensions, int32& OutNumImages);
	static bool GetImageSequenceInfo(const FString& InFullSequencePath, FIntVector2& OutDimensions, int32& OutNumImages);
};
