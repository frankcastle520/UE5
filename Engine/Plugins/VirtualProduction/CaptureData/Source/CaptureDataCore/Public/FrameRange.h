// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "FrameRange.generated.h"

USTRUCT(BlueprintType)
struct CAPTUREDATACORE_API FFrameRange
{

public:

	GENERATED_BODY()

	bool operator==(const FFrameRange& InOther) const;

	UPROPERTY(EditAnywhere, Category = "Frame Range")
	FString Name = TEXT("Unnamed");

	UPROPERTY(EditAnywhere, Category = "Frame Range")
	int32 StartFrame = -1;

	UPROPERTY(EditAnywhere, Category = "Frame Range")
	int32 EndFrame = -1;

	static bool ContainsFrame(int32 InFrame, const TArray<FFrameRange>& InFrameRangeArray);
};

UENUM()
enum class EFrameRangeType : uint8
{
	UserExcluded		UMETA(DisplayName = "User Excluded Frame"),
	ProcessingExcluded	UMETA(DisplayName = "Processing Excluded Frame"),
	CaptureExcluded		UMETA(DisplayName = "Capture Excluded Frame"),
	None
};

typedef TMap<EFrameRangeType, TArray<FFrameRange>> FFrameRangeMap;
