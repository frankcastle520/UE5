// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibration.h"
#include "CaptureDataLog.h"
#include "Models/SphericalLensModel.h"
#include "OpenCVHelperLocal.h"
#include "LensFile.h"
#include "EditorFramework/AssetImportData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Misc/Paths.h"

void UCameraCalibration::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
}

void UCameraCalibration::PostLoad()
{
	Super::PostLoad();

	// Back-compatability with older import where camera name was not recorded.
	// These always have 2 cameras, the first being rgb the seconds being depth.
	// Distinguish between iphone and HMC import by looking at relative size of RGB and depth images.
	// The RGB camera for the iphone case we'll call "iPhone"
	// The RGB camera for the HMC case will be "bot"

	if (CameraCalibrations.Num() == 2)
	{
		if (CameraCalibrations[0].Name.IsEmpty() && CameraCalibrations[1].Name.IsEmpty())
		{
			if (CameraCalibrations[0].LensFile)
			{
				if (CameraCalibrations[0].LensFile->LensInfo.ImageDimensions.X == CameraCalibrations[1].LensFile->LensInfo.ImageDimensions.X * 2)
				{
					CameraCalibrations[0].Name = TEXT("iPhone");
				}
				else
				{
					CameraCalibrations[0].Name = TEXT("bot");
				}
			}
			else
			{
				CameraCalibrations[0].Name = TEXT("Unknown");
			}

			CameraCalibrations[1].Name = TEXT("Depth");
		}
	}
}

void UCameraCalibration::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
#if WITH_EDITORONLY_DATA
	if (AssetImportData != nullptr)
	{
		OutTags.Emplace(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden);
	}
#endif
}

bool UCameraCalibration::ConvertToTrackerNodeCameraModels(TArray<FCameraCalibration>& OutCalibrations,
																   TArray<TPair<FString, FString>>& OutStereoReconstructionPairs) const
{
	OutCalibrations.SetNum(CameraCalibrations.Num());
	OutStereoReconstructionPairs.SetNum(StereoPairs.Num());

	for (int32 Pair = 0; Pair < OutStereoReconstructionPairs.Num(); Pair++)
	{
		OutStereoReconstructionPairs[Pair] = TPair<FString, FString>(FString::FromInt(StereoPairs[Pair].CameraIndex1), FString::FromInt(StereoPairs[Pair].CameraIndex2));
	}


	for (int32 Cam = 0; Cam < CameraCalibrations.Num(); Cam++)
	{
		FCameraCalibration CurCalib;
		CurCalib.Name = CameraCalibrations[Cam].Name;
		CurCalib.ImageSizeX = CameraCalibrations[Cam].LensFile->LensInfo.ImageDimensions.X;
		CurCalib.ImageSizeY = CameraCalibrations[Cam].LensFile->LensInfo.ImageDimensions.Y;

		if (CameraCalibrations[Cam].LensFile->LensInfo.LensModel != USphericalLensModel::StaticClass())
		{
			UE_LOG(LogCaptureDataCore, Warning, TEXT("Camera calibration does not contain a SphericalLensModel lens distortion."));
			return false;
		}

		const FDistortionTable& DistortionTable = CameraCalibrations[Cam].LensFile->DistortionTable;
		FDistortionInfo DistortionData;
		bool bGotPoint = DistortionTable.GetPoint(0.0f, 0.0f, DistortionData);

		if (!bGotPoint)
		{
			UE_LOG(LogCaptureDataCore, Warning, TEXT("Camera calibration does not contain a valid lens distortion."));
			return false;
		}

		check(DistortionData.Parameters.Num() == 5);
		CurCalib.K1 = DistortionData.Parameters[0];
		CurCalib.K2 = DistortionData.Parameters[1];
		CurCalib.K3 = DistortionData.Parameters[2]; // parameters are stored K1 K2 K3 P1 P2 rather than OpenCV order of K1 K2 P1 P2 K3
		CurCalib.P1 = DistortionData.Parameters[3];
		CurCalib.P2 = DistortionData.Parameters[4];
		CurCalib.K4 = 0.0;
		CurCalib.K5 = 0.0;
		CurCalib.K6 = 0.0;

		const FFocalLengthTable& FocalLengthTable = CameraCalibrations[Cam].LensFile->FocalLengthTable;
		FFocalLengthInfo FocalLengthData;
		bGotPoint = FocalLengthTable.GetPoint(0.0f, 0.0f, FocalLengthData);

		if (!bGotPoint)
		{
			UE_LOG(LogCaptureDataCore, Warning, TEXT("Camera calibration does not contain a valid focal length."));
			return false;
		}
		const FVector2D& FxFy = FocalLengthData.FxFy;
		CurCalib.FX = FxFy.X * CurCalib.ImageSizeX;
		CurCalib.FY = FxFy.Y * CurCalib.ImageSizeY;

		const FImageCenterTable& ImageCenterTable = CameraCalibrations[Cam].LensFile->ImageCenterTable;
		FImageCenterInfo ImageCenterData;
		bGotPoint = ImageCenterTable.GetPoint(0.0f, 0.0f, ImageCenterData);

		if (!bGotPoint)
		{
			UE_LOG(LogCaptureDataCore, Warning, TEXT("Camera calibration does not contain a valid image center."));
			return false;
		}
		const FVector2D& PrincipalPoint = ImageCenterData.PrincipalPoint;
		CurCalib.CX = PrincipalPoint.X * CurCalib.ImageSizeX;
		CurCalib.CY = PrincipalPoint.Y * CurCalib.ImageSizeY;

		const FNodalOffsetTable& NodalOffsetTable = CameraCalibrations[Cam].LensFile->NodalOffsetTable;
		FNodalPointOffset NodalOffsetData;
		bGotPoint = NodalOffsetTable.GetPoint(0.0f, 0.0f, NodalOffsetData);

		if (!bGotPoint)
		{
			UE_LOG(LogCaptureDataCore, Warning, TEXT("Camera calibration does not contain a valid nodal offset."));
			return false;
		}

		FTransform Transform;
		Transform.SetLocation(NodalOffsetData.LocationOffset);
		Transform.SetRotation(NodalOffsetData.RotationOffset);
		FOpenCVHelperLocal::ConvertUnrealToOpenCV(Transform);
		CurCalib.Transform = Transform.ToMatrixWithScale();

		OutCalibrations[Cam] = CurCalib;
	}

	return true;
}

bool UCameraCalibration::ConvertFromTrackerNodeCameraModels(TArray<FCameraCalibration>& InCalibrations)
{
	check(InCalibrations.Num() == 2 || InCalibrations.Num() == 3); // only works for stereo hmc, or for the case of one RGB stream and one depth stream

	for (int32 Index = 0; Index < InCalibrations.Num(); ++Index)
	{
		const FCameraCalibration& Calibration = InCalibrations[Index];

		bool bIsDepthCamera = (Index == InCalibrations.Num() - 1);

		const FString ObjectName = bIsDepthCamera ?
			FString::Printf(TEXT("%s_Depth_LensFile"), *GetName()) :
			FString::Printf(TEXT("%s_%s_RGB_LensFile"), *GetName(), *Calibration.Name);
		FString ParentPath = FString::Printf(TEXT("%s/../%s"), *GetPackage()->GetPathName(), *ObjectName);
		FPaths::CollapseRelativeDirectories(ParentPath);
		UObject* Parent = CreatePackage(*ParentPath);

		FExtendedLensFile CameraCalibration;
		CameraCalibration.Name = Calibration.Name;
		CameraCalibration.IsDepthCamera = bIsDepthCamera;
		CameraCalibration.LensFile = NewObject<ULensFile>(Parent, ULensFile::StaticClass(), *ObjectName, GetFlags());

		// These a for a non-FIZ camera.
		const float Focus = 0.0f;
		const float Zoom = 0.0f;

		// LensInfo
		CameraCalibration.LensFile->LensInfo.LensModel = USphericalLensModel::StaticClass();
		CameraCalibration.LensFile->LensInfo.LensModelName = FString::Printf(TEXT("Lens"));
		// lens serial number is not needed
		
		// leave sensor dimensions with default values and de-normalize using VideoDimensions or DepthDimensions
		CameraCalibration.LensFile->LensInfo.ImageDimensions = FIntPoint(Calibration.ImageSizeX, Calibration.ImageSizeY);

		// FocalLengthInfo
		FFocalLengthInfo FocalLengthInfo;
		FocalLengthInfo.FxFy = FVector2D(Calibration.FX / Calibration.ImageSizeX, Calibration.FY / Calibration.ImageSizeY);

		// DistortionInfo
		FDistortionInfo DistortionInfo;
		FSphericalDistortionParameters SphericalParameters;

		SphericalParameters.K1 = Calibration.K1;
		SphericalParameters.K2 = Calibration.K2;
		SphericalParameters.P1 = Calibration.P1;
		SphericalParameters.P2 = Calibration.P2;
		SphericalParameters.K3 = Calibration.K3;

		USphericalLensModel::StaticClass()->GetDefaultObject<ULensModel>()->ToArray(
			SphericalParameters,
			DistortionInfo.Parameters
		);

		// ImageCenterInfo
		FImageCenterInfo ImageCenterInfo;
		ImageCenterInfo.PrincipalPoint = FVector2D(Calibration.CX / Calibration.ImageSizeX, Calibration.CY / Calibration.ImageSizeY);

		// NodalOffset
		FNodalPointOffset NodalPointOffset;
		FTransform Transform;
		Transform.SetFromMatrix(Calibration.Transform);
		FOpenCVHelperLocal::ConvertOpenCVToUnreal(Transform);
		NodalPointOffset.LocationOffset = Transform.GetLocation();
		NodalPointOffset.RotationOffset = Transform.GetRotation();

		CameraCalibration.LensFile->AddDistortionPoint(Focus, Zoom, DistortionInfo, FocalLengthInfo);
		CameraCalibration.LensFile->AddImageCenterPoint(Focus, Zoom, ImageCenterInfo);
		CameraCalibration.LensFile->AddNodalOffsetPoint(Focus, Zoom, NodalPointOffset);

		CameraCalibration.LensFile->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(CameraCalibration.LensFile);

		CameraCalibrations.Add(CameraCalibration);
	}

	if (InCalibrations.Num() == 3)
	{
		// stereo HMC so make the stereo pair
		FStereoPair CameraPair;
		CameraPair.CameraIndex1 = 0;
		CameraPair.CameraIndex2 = 1;
		StereoPairs.Add(CameraPair);
	}

	return true;
}

int32 UCameraCalibration::GetIndexByCameraName(const FString& InName) const
{
	for (int32 Calibration = 0; Calibration < CameraCalibrations.Num(); ++Calibration)
	{
		if (CameraCalibrations[Calibration].Name == InName)
		{
			return Calibration;
		}
	}

	UE_LOG(LogCaptureDataCore, Warning, TEXT("Specified camera name not valid"));

	return -1;
}

