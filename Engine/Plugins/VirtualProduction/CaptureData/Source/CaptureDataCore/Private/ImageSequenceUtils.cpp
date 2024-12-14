// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageSequenceUtils.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImgMediaSource.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"


bool FImageSequenceUtils::GetImageSequencePathAndFiles(const UImgMediaSource* InImgSequence, FString& OutFullSequencePath, TArray<FString>& OutImageFiles)
{
	if (InImgSequence == nullptr)
	{
		return false;
	}

	OutFullSequencePath = InImgSequence->GetFullPath();

	return GetImageSequencePathAndFiles(OutFullSequencePath, OutImageFiles);
}

bool FImageSequenceUtils::GetImageSequencePathAndFiles(const FString& InFullSequencePath, TArray<FString>& OutImageFiles)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	IFileManager& FileManager = IFileManager::Get();

	bool bIterateResult = FileManager.IterateDirectory(*InFullSequencePath, [&OutImageFiles, &ImageWrapperModule](const TCHAR* InFilenameOrDirectory, bool bInIsDirectory)
	{
		if (!bInIsDirectory)
		{
			EImageFormat Format = ImageWrapperModule.GetImageFormatFromExtension(*FPaths::GetExtension(InFilenameOrDirectory));
			if (Format != EImageFormat::Invalid)
			{
				OutImageFiles.Add(FPaths::GetCleanFilename(InFilenameOrDirectory));
			}
		}

		return true;
	});

	return !OutImageFiles.IsEmpty() && bIterateResult;
}

bool FImageSequenceUtils::GetImageSequenceInfo(const class UImgMediaSource* InImgSequence, FIntVector2& OutDimensions, int32& OutNumImages)
{
	if (InImgSequence == nullptr)
	{
		return false;
	}

	return GetImageSequenceInfo(InImgSequence->GetFullPath(), OutDimensions, OutNumImages);
}

bool FImageSequenceUtils::GetImageSequenceInfo(const FString& InFullSequencePath, FIntVector2& OutDimensions, int32& OutNumImages)
{
	TArray<FString> ImageFiles;
	bool bFoundImages = GetImageSequencePathAndFiles(InFullSequencePath, ImageFiles);

	if (!bFoundImages)
	{
		return false;
	}

	OutNumImages = ImageFiles.Num();

	const FString SampleImagePath = InFullSequencePath / ImageFiles[0];

	TArray<uint8> RawFileData;
	if (FFileHelper::LoadFileToArray(RawFileData, *SampleImagePath))
	{
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(RawFileData.GetData(), RawFileData.Num());
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

		if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
		{
			OutDimensions.X = ImageWrapper->GetWidth();
			OutDimensions.Y = ImageWrapper->GetHeight();
			return true;
		}
	}

	return false;
}
