// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CaptureData.h"

#include "Internationalization/Internationalization.h"

namespace UE::CaptureData
{

class CAPTUREDATACORE_API FImageSequencePathChecker
{
public:
	explicit FImageSequencePathChecker(FText InAssetDisplayName);

	void Check(const UFootageCaptureData& InCaptureData);
	void DisplayDialog() const;
	bool HasError() const;

private:
	int32 NumCaptureDataFootageAssets;
	int32 NumInvalidImageSequences;
	FText AssetDisplayName;
};

}
