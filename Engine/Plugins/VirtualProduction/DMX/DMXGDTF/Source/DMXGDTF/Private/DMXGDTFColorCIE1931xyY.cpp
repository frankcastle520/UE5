// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXGDTFColorCIE1931xyY.h"

FString FDMXGDTFColorCIE1931xyY::ToString() const
{
	constexpr int32 SignificantDigits = 6;
	const FString XString = FString::SanitizeFloat(X, SignificantDigits);
	const FString YString = FString::SanitizeFloat(Y, SignificantDigits);
	const FString ZString = FString::SanitizeFloat(YY, SignificantDigits);

	return FString::Printf(TEXT("%s,%s,%s"), *XString, *YString, *ZString);
}
