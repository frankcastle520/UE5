// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "EditorFramework/AssetImportData.h"

#include "CameraCalibration.generated.h"


USTRUCT(BlueprintType)
struct FExtendedLensFile
{
	GENERATED_BODY()


public:
	UPROPERTY(EditAnywhere, Category = Calibration)
	FString Name;

	UPROPERTY(EditAnywhere, Category = Calibration)
	bool IsDepthCamera{};

	UPROPERTY(EditAnywhere, Category = Calibration)
	TObjectPtr<class ULensFile> LensFile{};
};

USTRUCT(BlueprintType)
struct FStereoPair
{
	GENERATED_BODY()


public:
	UPROPERTY(EditAnywhere, Category = Calibration)
	uint32 CameraIndex1 {};

	UPROPERTY(EditAnywhere, Category = Calibration)
	uint32 CameraIndex2 {};
};

struct CAPTUREDATACORE_API FCameraCalibration
{
public:
	FString Name;
	FString Camera;
	int32 ImageSizeX = 0;
	int32 ImageSizeY = 0;
	double FX = 0;
	double FY = 0;
	double CX = 0;
	double CY = 0;
	double K1 = 0;
	double K2 = 0;
	double P1 = 0;
	double P2 = 0;
	double K3 = 0;
	double K4 = 0;
	double K5 = 0;
	double K6 = 0;
	FMatrix Transform;
};

/** Camera Calibration Asset
*
*   Contains the parameters for calibrating the camera
*   used in footage for MetaHuman Identity and Performance assets.
* 
**/
UCLASS(BlueprintType)
class CAPTUREDATACORE_API UCameraCalibration : public UObject
{
	GENERATED_BODY()

public:
	//~Begin UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	//~End UObject interface

	UPROPERTY(EditAnywhere, Category = Calibration)
	TArray<FExtendedLensFile> CameraCalibrations;

	UPROPERTY(EditAnywhere, Category = Calibration)
	TArray<FStereoPair> StereoPairs;

#if WITH_EDITORONLY_DATA
	/** Importing data and options used for importing mhaical files */
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Import Settings")
	TObjectPtr<UAssetImportData> AssetImportData;
#endif // WITH_EDITORONLY_DATA

	bool ConvertToTrackerNodeCameraModels(TArray<FCameraCalibration>& OutCalibrations,
										  TArray<TPair<FString, FString>>& OutStereoReconstructionPairs) const;

	bool ConvertFromTrackerNodeCameraModels(TArray<FCameraCalibration>& InCalibrations);

	int32 GetIndexByCameraName(const FString& InName) const;
};