// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_CameraCalibration.h"
#include "CameraCalibration.h"

FText UAssetDefinition_CameraCalibration::GetAssetDisplayName() const
{
	return NSLOCTEXT("CaptureData", "CameraCalibrationAssetName", "Camera Calibration");
}

FLinearColor UAssetDefinition_CameraCalibration::GetAssetColor() const
{
	return FColor::Turquoise;
}

TSoftClassPtr<UObject> UAssetDefinition_CameraCalibration::GetAssetClass() const
{
	return UCameraCalibration::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_CameraCalibration::GetAssetCategories() const
{
	static FAssetCategoryPath Path(NSLOCTEXT("CaptureData", "CaptureDataAdvancedAssetCategoryLabel", "Advanced"));
	static FAssetCategoryPath Categories[] = { Path };

	return Categories;
}

bool UAssetDefinition_CameraCalibration::CanImport() const
{
	return true;
}