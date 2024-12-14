// Copyright Epic Games, Inc. All Rights Reserved.

#include "CaptureData.h"

#include "CaptureDataLog.h"
#include "CameraCalibration.h"

#include "ImageSequenceUtils.h"

#include "SoundWaveTimecodeUtils.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "StaticMeshAttributes.h"
#include "MeshAttributes.h"
#include "Engine/StaticMesh.h"
#include "Misc/TransactionObjectEvent.h"
#include "Sound/SoundWave.h"
#include "ImgMediaSource.h"
#include "MediaTexture.h"
#include "Algo/AllOf.h"
#include "Engine/SkeletalMesh.h"
#include "ImageSequenceTimecodeUtils.h"

#if WITH_EDITOR
#include "IDirectoryWatcher.h"
#include "DirectoryWatcherModule.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(CaptureData)

/////////////////////////////////////////////////////
// UCaptureData

#if WITH_EDITOR

void UCaptureData::PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);
	NotifyInternalsChanged();
}

void UCaptureData::PostTransacted(const FTransactionObjectEvent& InTransactionEvent)
{
	Super::PostTransacted(InTransactionEvent);
	NotifyInternalsChanged();
}

#endif

void UCaptureData::NotifyInternalsChanged()
{
	OnCaptureDataInternalsChangedDelegate.Broadcast();
}

/////////////////////////////////////////////////////
// UMeshCaptureData

bool UMeshCaptureData::IsInitialized() const
{
	// We need access to the source data when fitting and that is not available during runtime
#if WITH_EDITOR
	return TargetMesh != nullptr && (TargetMesh->IsA<UStaticMesh>() || TargetMesh->IsA<USkeletalMesh>());
#else
	return false;
#endif
}

void UMeshCaptureData::GetDataForConforming(const FTransform& InTransform, TArray<float>& OutVertices, TArray<int32>& OutTriangles) const
{
#if WITH_EDITOR
	if (USkeletalMesh* TargetSkeletalMesh = Cast<USkeletalMesh>(TargetMesh))
	{

		FSkeletalMeshModel* ImportedModel = TargetSkeletalMesh->GetImportedModel();
		FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[0];

		OutVertices.Reset(LODModel.NumVertices * 3);

		for (FSkelMeshSection& Section : LODModel.Sections)
		{
			const int32 NumVertices = Section.GetNumVertices();
			for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
			{
				FSoftSkinVertex& OriginalVertex = Section.SoftVertices[VertexIndex];
				FVector TransformedVertex = InTransform.TransformPosition(FVector{ OriginalVertex.Position });
				OutVertices.Add(TransformedVertex.Y);
				OutVertices.Add(-TransformedVertex.Z);
				OutVertices.Add(TransformedVertex.X);
			}
		}

		OutTriangles.Reset(LODModel.IndexBuffer.Num());
		for (uint32 Index : LODModel.IndexBuffer)
		{
			OutTriangles.Add(Index);
		}
	}
	else if (UStaticMesh* TargetStaticMesh = Cast<UStaticMesh>(TargetMesh))
	{
		FMeshDescription* MeshDescription = TargetStaticMesh->GetMeshDescription(0);
		check(MeshDescription);
		FStaticMeshAttributes Attributes(*MeshDescription);

		TVertexAttributesRef<FVector3f> OriginalMeshVerts = Attributes.GetVertexPositions();
		TTriangleAttributesRef<TArrayView<FVertexID>>  OriginalMeshIndices = Attributes.GetTriangleVertexIndices();

		OutVertices.Reset(OriginalMeshVerts.GetNumElements());

		for (int32 RenderCtr = 0; RenderCtr < OriginalMeshVerts.GetNumElements(); ++RenderCtr)
		{
			// map the mesh vertices (in UE coordinate system) to OpenCV coordinate system
			FVector3f OriginalVertex = OriginalMeshVerts.Get(RenderCtr);
			FVector TransformedVertex = InTransform.TransformPosition(FVector{ OriginalVertex });
			OutVertices.Add(TransformedVertex.Y);
			OutVertices.Add(-TransformedVertex.Z);
			OutVertices.Add(TransformedVertex.X);
		}

		OutTriangles.Reset(OriginalMeshIndices.GetNumElements());
		const TArrayView<FVertexID>& RawIndArray = OriginalMeshIndices.GetRawArray();
		for (const auto& Index : RawIndArray)
		{
			OutTriangles.Add(Index.GetValue());
		}
	}
	else
	{
		// This is an error state so log it accordingly
		if (TargetMesh != nullptr)
		{
			UE_LOG(LogCaptureDataCore, Error, TEXT("Failed to get data for conforming as TargetMesh is a '%s' but should be a UStaticMesh or USkeletalMesh"), *TargetMesh->GetClass()->GetName());
		}
		else
		{
			UE_LOG(LogCaptureDataCore, Error, TEXT("Failed to get data for conforming as TargetMesh is invalid"));
		}
	}
#endif
}

/////////////////////////////////////////////////////
// UFootageCaptureData

UFootageCaptureData::FPathAssociation::FPathAssociation(const FString& InPathOnDisk, const FString& InAssetPath) :
	PathOnDisk(InPathOnDisk),
	AssetPath(InAssetPath)
{
}

TOptional<FFootageCaptureMetadata::FIosDeviceVersion> FFootageCaptureMetadata::ParseIosDeviceVersion(const FString& Prefix, const FString& ModelName)
{
	FString CombinedModelNumbers;
	if (ModelName.Split(Prefix, nullptr, &CombinedModelNumbers)) // Combined model numbers, e.g. "1,2"
	{
		TArray<FString> SeparatedModelNumbers;
		if (CombinedModelNumbers.ParseIntoArray(SeparatedModelNumbers, TEXT(",")) == 2) // Separated model numbers, e.g. ["1", "2"]
		{
			return FIosDeviceVersion(FCString::Atoi(*SeparatedModelNumbers[0]), FCString::Atoi(*SeparatedModelNumbers[1]));
		}
	}

	return TOptional<FIosDeviceVersion>();
}

EFootageDeviceClass FFootageCaptureMetadata::IPhoneDeviceClass(TOptional<FIosDeviceVersion> IosDeviceVersion)
{
	if (IosDeviceVersion.IsSet())
	{
		const FIosDeviceVersion Version = IosDeviceVersion.GetValue();
		const uint16 Major = Version.Major;
		const uint16 Minor = Version.Minor;
		if (Major < 12) // Before iPhone 11
		{
			return EFootageDeviceClass::iPhone11OrEarlier;
		}
		else if (Major == 12) // iPhone 11
		{
			const TArray<uint16> IPhone11MinorVersions = { 1, 3, 5 };
			if (IPhone11MinorVersions.Contains(Minor))
			{
				return EFootageDeviceClass::iPhone11OrEarlier;
			}
		}
		else if (Major == 13) // iPhone 12
		{
			return EFootageDeviceClass::iPhone12;
		}
		else if (Major == 14) // iPhone 13 or non pro iPhone 14 models
		{
			const TArray<uint16> IPhone13MinorVersions = { 2, 3, 4, 5 };
			const TArray<uint16> IPhone14MinorVersions = { 7, 8 };
			if (IPhone13MinorVersions.Contains(Minor))
			{
				return EFootageDeviceClass::iPhone13;
			}
			else if (IPhone14MinorVersions.Contains(Minor))
			{
				return EFootageDeviceClass::iPhone14OrLater;
			}
		}
		else if (Major >= 15) // iPhone 14 Pro Models or later
		{
			return  EFootageDeviceClass::iPhone14OrLater;
		}
	}

	return EFootageDeviceClass::OtheriOSDevice;
}

void FFootageCaptureMetadata::SetDeviceClass(const FString& InDeviceModel)
{
	const FString IPhone = TEXT("iPhone");
	const FString IPad = TEXT("iPad");
	const FString StereoHMC = TEXT("StereoHMC");

	if (InDeviceModel.StartsWith(IPhone))
	{
		DeviceClass = IPhoneDeviceClass(ParseIosDeviceVersion(IPhone, InDeviceModel));
	}
	else if (InDeviceModel.StartsWith(IPad))
	{
		DeviceClass = EFootageDeviceClass::OtheriOSDevice;
	}
	else if (InDeviceModel == StereoHMC)
	{
		DeviceClass = EFootageDeviceClass::StereoHMC;
	}
	else
	{
		DeviceClass = EFootageDeviceClass::Unspecified;
	}
}

#define CHECK_AND_PRINT_RET(InCondition, InFormat, ...) if (!(InCondition)) { return MakeError(FString::Printf(InFormat, ##__VA_ARGS__)); }
#define CHECK_AND_RET_ERROR(InFunction) if (UFootageCaptureData::FVerifyResult Result = InFunction; Result.HasError()) { return MoveTemp(Result); }

bool UFootageCaptureData::IsInitialized() const
{
	return !VerifyData().HasError();
}

UFootageCaptureData::FVerifyResult UFootageCaptureData::VerifyData() const
{
	CHECK_AND_RET_ERROR(ViewsContainsValidData());
	CHECK_AND_RET_ERROR(MetadataContainsValidData());
	CHECK_AND_RET_ERROR(CalibrationContainsValidData());

	return MakeValue();
}

TArray<UFootageCaptureData::FPathAssociation> UFootageCaptureData::CheckImageSequencePaths() const
{
	TArray<FPathAssociation> InvalidImageSequences;

	for (const TObjectPtr<class UImgMediaSource>& ImageSequence : ImageSequences)
	{
		if (ImageSequence && !FPaths::DirectoryExists(ImageSequence->GetFullPath()))
		{
			InvalidImageSequences.Emplace(ImageSequence->GetFullPath(), ImageSequence->GetPathName());
		}
	}

	return InvalidImageSequences;
}

UFootageCaptureData::FVerifyResult UFootageCaptureData::ViewsContainsValidData() const
{
	CHECK_AND_PRINT_RET(!ImageSequences.IsEmpty(), TEXT("Capture Data doesn't contain image sequences"));
	CHECK_AND_PRINT_RET(!DepthSequences.IsEmpty(), TEXT("Capture Data doesn't contain depth sequences"));

	for (int32 Index = 0; Index < ImageSequences.Num(); Index++)
	{
		int32 NumImageFrames = 0;
		FIntVector2 ImageDimensions;

		UImgMediaSource* ImageSequence = ImageSequences[Index];
		UImgMediaSource* DepthSequence = DepthSequences[Index];

		CHECK_AND_PRINT_RET(FImageSequenceUtils::GetImageSequenceInfo(ImageSequence, ImageDimensions, NumImageFrames), TEXT("Image Sequence asset is invalid"));

		int32 NumDepthFrames = 0;
		FIntVector2 DepthDimensions;

		CHECK_AND_PRINT_RET(FImageSequenceUtils::GetImageSequenceInfo(DepthSequence, DepthDimensions, NumImageFrames), TEXT("Depth Sequence asset is invalid"));

		CHECK_AND_PRINT_RET(ImageSequence->FrameRateOverride.IsValid(), TEXT("Image Sequence asset contains invalid frame rate"));
		CHECK_AND_PRINT_RET(DepthSequence->FrameRateOverride.IsValid(), TEXT("Depth Sequence asset contains invalid frame rate"));
		CHECK_AND_PRINT_RET(ImageSequence->FrameRateOverride == DepthSequence->FrameRateOverride, TEXT("Image Sequence asset and Depth Sequence asset contain different frame rates"));
	}

	return MakeValue();
}

UFootageCaptureData::FVerifyResult UFootageCaptureData::MetadataContainsValidData() const
{
	CHECK_AND_PRINT_RET(Metadata.Height > 0 && Metadata.Width > 0, TEXT("Invalid values for the image resolution"));
	CHECK_AND_PRINT_RET(!FMath::IsNearlyZero(Metadata.FrameRate), TEXT("Frame rate can't be set to 0"));

	return MakeValue();
}

UFootageCaptureData::FVerifyResult UFootageCaptureData::CalibrationContainsValidData() const
{
	CHECK_AND_PRINT_RET(!CameraCalibrations.IsEmpty(), TEXT("Calibration assets are empty"));
	for (UCameraCalibration* CameraCalibration : CameraCalibrations)
	{
		CHECK_AND_PRINT_RET(!CameraCalibration->CameraCalibrations.IsEmpty(), TEXT("Calibration asset not configured"));
	}
	return MakeValue();
}

#undef CHECK_AND_PRINT_RET
#undef CHECK_AND_RET

static TRange<FFrameNumber> GetFrameRange(const FFrameRate& InTargetRate, const FTimecode& InMediaTimecode, const FFrameRate& InMediaTimecodeRate, bool bInMediaStartFrameIsZero, const FFrameRate& InMediaRate, const FFrameNumber& InMediaDuration)
{
	const FFrameNumber MediaStartFrame = bInMediaStartFrameIsZero ? 0 : InMediaTimecode.ToFrameNumber(InMediaTimecodeRate);
	const FFrameTime TargetStartFrameTime = FFrameRate::TransformTime(MediaStartFrame, InMediaTimecodeRate, InTargetRate);
	const FFrameTime TargetDurationTime = FFrameRate::TransformTime(InMediaDuration, InMediaRate, InTargetRate);
	const FFrameTime TargetEndFrameTime = TargetStartFrameTime + TargetDurationTime;
	// When converting from frame time to frame number, deal with sub frame times by taking minimum frame range.
	TRange<FFrameNumber> TargetFrameRange(TargetStartFrameTime.CeilToFrame(), TargetEndFrameTime.FloorToFrame());
	return TargetFrameRange;
}

static TRange<FFrameNumber> GetFrameRange(const FFrameRate& InTargetRate, UImgMediaSource* InMedia, const FTimecode& InMediaTimecode, const FFrameRate& InMediaTimecodeRate, bool bInMediaStartFrameIsZero)
{
	int32 Duration = 0;
	FIntVector2 ImageDimensions;
	const bool bImageOK = FImageSequenceUtils::GetImageSequenceInfo(InMedia, ImageDimensions, Duration);
	ensure(bImageOK);

	return GetFrameRange(InTargetRate, InMediaTimecode, InMediaTimecodeRate, bInMediaStartFrameIsZero, InMedia->FrameRateOverride, Duration);
}

static TRange<FFrameNumber> GetFrameRange(const FFrameRate& InTargetRate, USoundWave* InMedia, const FTimecode& InMediaTimecode, const FFrameRate& InMediaTimecodeRate, bool bInMediaStartFrameIsZero)
{
	const int32 Duration = InMedia->GetDuration() * InMediaTimecodeRate.AsDecimal();

	return GetFrameRange(InTargetRate, InMediaTimecode, InMediaTimecodeRate, bInMediaStartFrameIsZero, InMediaTimecodeRate, Duration);
}

FIntPoint UFootageCaptureData::GetFootageColorResolution() const
{
	FIntPoint Resolution(ForceInit);

	if (!CameraCalibrations.IsEmpty())
	{
		TArray<FCameraCalibration> Calibrations;
		TArray<TPair<FString, FString>> StereoPairs;
		CameraCalibrations[0]->ConvertToTrackerNodeCameraModels(Calibrations, StereoPairs);

		Resolution = FIntPoint(Calibrations[0].ImageSizeX, Calibrations[0].ImageSizeY);
	}

	return Resolution;
}

void UFootageCaptureData::GetFrameRanges(const FFrameRate& InTargetRate, ETimecodeAlignment InTimecodeAlignment, bool bInIncludeAudio, TMap<TWeakObjectPtr<UObject>, TRange<FFrameNumber>>& OutMediaFrameRanges, TRange<FFrameNumber>& OutProcessingFrameRange, TRange<FFrameNumber>& OutMaximumFrameRange) const
{
	OutMediaFrameRanges.Reset();
	OutProcessingFrameRange = TRange<FFrameNumber>(0, 0);
	OutMaximumFrameRange = TRange<FFrameNumber>(0, 0);

	const bool bMediaStartFrameIsZero = InTimecodeAlignment == ETimecodeAlignment::None;

	for (int32 Index = 0; Index < ImageSequences.Num(); ++Index)
	{
		const TObjectPtr<UImgMediaSource>& ImageSequence = ImageSequences[Index];

		if (ImageSequence)
		{
			const FTimecode EffectiveImageTimecode = GetEffectiveImageTimecode(Index);
			const FFrameRate EffectiveImageTimecodeRate = GetEffectiveImageTimecodeRate(Index);
			const TRange<FFrameNumber> ImageFrameFrange = GetFrameRange(InTargetRate, ImageSequence, EffectiveImageTimecode, EffectiveImageTimecodeRate, bMediaStartFrameIsZero);

			OutMediaFrameRanges.Add(ImageSequence, ImageFrameFrange);
		}
	}

	for (int32 Index = 0; Index < DepthSequences.Num(); ++Index)
	{
		const TObjectPtr<UImgMediaSource>& DepthSequence = DepthSequences[Index];

		if (DepthSequence)
		{
			const FTimecode EffectiveDepthTimecode = GetEffectiveDepthTimecode(Index);
			const FFrameRate EffectiveDepthTimecodeRate = GetEffectiveDepthTimecodeRate(Index);
			const TRange<FFrameNumber> DepthFrameRange = GetFrameRange(InTargetRate, DepthSequence, EffectiveDepthTimecode, EffectiveDepthTimecodeRate, bMediaStartFrameIsZero);

			OutMediaFrameRanges.Add(DepthSequence, DepthFrameRange);
		}
	}

	if (bInIncludeAudio)
	{
		for (TObjectPtr<class USoundWave> Audio : Audios)
		{
			if (Audio)
			{
				const FTimecode EffectiveAudioTimecode = GetEffectiveAudioTimecode();
				const FFrameRate EffectiveAudioTimecodeRate = GetEffectiveAudioTimecodeRate();
				const TRange<FFrameNumber> AudioFrameRange = GetFrameRange(InTargetRate, Audio, EffectiveAudioTimecode, EffectiveAudioTimecodeRate, bMediaStartFrameIsZero);

				OutMediaFrameRanges.Add(Audio, AudioFrameRange);
			}
		}
	}

	if (InTimecodeAlignment == ETimecodeAlignment::Relative)
	{
		FFrameNumber LowestStartFrame;

		bool bFirstPass = true;
		for (const TPair<TWeakObjectPtr<UObject>, TRange<FFrameNumber>>& MediaFrameRangePair : OutMediaFrameRanges)
		{
			if (bFirstPass || MediaFrameRangePair.Value.GetLowerBoundValue() < LowestStartFrame)
			{
				LowestStartFrame = MediaFrameRangePair.Value.GetLowerBoundValue();
			}

			bFirstPass = false;
		}

		for (TPair<TWeakObjectPtr<UObject>, TRange<FFrameNumber>>& MediaFrameRangePair : OutMediaFrameRanges)
		{
			MediaFrameRangePair.Value.SetLowerBoundValue(MediaFrameRangePair.Value.GetLowerBoundValue() - LowestStartFrame);
			MediaFrameRangePair.Value.SetUpperBoundValue(MediaFrameRangePair.Value.GetUpperBoundValue() - LowestStartFrame);
		}
	}

	bool bFirstPass = true;
	for (const TPair<TWeakObjectPtr<UObject>, TRange<FFrameNumber>>& MediaFrameRangePair : OutMediaFrameRanges)
	{
		if (MediaFrameRangePair.Key.IsValid() &&
			(MediaFrameRangePair.Key->IsA(UImgMediaSource::StaticClass()) || MediaFrameRangePair.Key->IsA(USoundWave::StaticClass())))
		{
			if (bFirstPass || MediaFrameRangePair.Value.GetLowerBoundValue() > OutProcessingFrameRange.GetLowerBoundValue())
			{
				OutProcessingFrameRange.SetLowerBoundValue(MediaFrameRangePair.Value.GetLowerBoundValue());
			}

			if (bFirstPass || MediaFrameRangePair.Value.GetUpperBoundValue() < OutProcessingFrameRange.GetUpperBoundValue())
			{
				OutProcessingFrameRange.SetUpperBoundValue(MediaFrameRangePair.Value.GetUpperBoundValue());
			}

			if (bFirstPass || MediaFrameRangePair.Value.GetLowerBoundValue() < OutMaximumFrameRange.GetLowerBoundValue())
			{
				OutMaximumFrameRange.SetLowerBoundValue(MediaFrameRangePair.Value.GetLowerBoundValue());
			}

			if (bFirstPass || MediaFrameRangePair.Value.GetUpperBoundValue() > OutMaximumFrameRange.GetUpperBoundValue())
			{
				OutMaximumFrameRange.SetUpperBoundValue(MediaFrameRangePair.Value.GetUpperBoundValue());
			}

			bFirstPass = false;
		}
	}

	if (OutProcessingFrameRange.GetUpperBoundValue() <= OutProcessingFrameRange.GetLowerBoundValue())
	{
		OutProcessingFrameRange = TRange<FFrameNumber>(0, 0);
	}
}

TRange<FFrameNumber> UFootageCaptureData::GetAudioFrameRange(const FFrameRate& InTargetRate, ETimecodeAlignment InTimecodeAlignment, USoundWave* InMedia, const FTimecode& InMediaTimecode, const FFrameRate& InMediaTimecodeRate)
{
	bool bInMediaStartFrameIsZero = InTimecodeAlignment == ETimecodeAlignment::None;
	TRange<FFrameNumber> AudioFrameRange = GetFrameRange(InTargetRate, InMedia, InMediaTimecode, InMediaTimecodeRate, bInMediaStartFrameIsZero);

	if (InTimecodeAlignment == ETimecodeAlignment::Relative)
	{
		FFrameNumber StartFrame = AudioFrameRange.GetLowerBoundValue();
		AudioFrameRange.SetLowerBoundValue(AudioFrameRange.GetLowerBoundValue() - StartFrame);
		AudioFrameRange.SetUpperBoundValue(AudioFrameRange.GetUpperBoundValue() - StartFrame);
	}

	return AudioFrameRange;
}

FTimecode UFootageCaptureData::GetEffectiveImageTimecode(int32 InView) const
{
	check(InView < ImageSequences.Num());
	check(InView < DepthSequences.Num());

	FTimecode ImgSequenceTimecode = UImageSequenceTimecodeUtils::GetTimecode(ImageSequences[InView]);
	if (UImageSequenceTimecodeUtils::IsValidTimecode(ImgSequenceTimecode))
	{
		return ImgSequenceTimecode;
	}

	FTimecode Timecode;
	FFrameRate FrameRate;
	GetDefaultTimecodeInfo(Timecode, FrameRate);

	return Timecode;
}

FFrameRate UFootageCaptureData::GetEffectiveImageTimecodeRate(int32 InView) const
{
	check(InView < ImageSequences.Num());
	check(InView < DepthSequences.Num());

	FFrameRate ImgSequenceFrameRate = UImageSequenceTimecodeUtils::GetFrameRate(ImageSequences[InView]);
	if (UImageSequenceTimecodeUtils::IsValidFrameRate(ImgSequenceFrameRate))
	{
		return ImgSequenceFrameRate;
	}

	FTimecode Timecode;
	FFrameRate FrameRate;
	GetDefaultTimecodeInfo(Timecode, FrameRate);

	return FrameRate;
}

FTimecode UFootageCaptureData::GetEffectiveDepthTimecode(int32 InView) const
{
	check(InView < ImageSequences.Num());
	check(InView < DepthSequences.Num());

	FTimecode DepthSequenceTimecode = UImageSequenceTimecodeUtils::GetTimecode(DepthSequences[InView]);
	if (UImageSequenceTimecodeUtils::IsValidTimecode(DepthSequenceTimecode))
	{
		return DepthSequenceTimecode;
	}

	FTimecode Timecode;
	FFrameRate FrameRate;
	GetDefaultTimecodeInfo(Timecode, FrameRate);

	return Timecode;
}

FFrameRate UFootageCaptureData::GetEffectiveDepthTimecodeRate(int32 InView) const
{
	check(InView < ImageSequences.Num());
	check(InView < DepthSequences.Num());

	FFrameRate DepthSequenceFrameRate = UImageSequenceTimecodeUtils::GetFrameRate(DepthSequences[InView]);
	if (UImageSequenceTimecodeUtils::IsValidFrameRate(DepthSequenceFrameRate))
	{
		return DepthSequenceFrameRate;
	}

	FTimecode Timecode;
	FFrameRate FrameRate;
	GetDefaultTimecodeInfo(Timecode, FrameRate);

	return FrameRate;
}

FTimecode UFootageCaptureData::GetEffectiveAudioTimecode() const
{
	FTimecode Timecode;
	bool TimecodeFromAsset = false;

	if (Audios.Num() > 0)
	{
		const USoundWave* SoundWave = Audios[0].Get();
		check(IsValid(SoundWave));

		TOptional<FTimecode> TimecodeOpt = USoundWaveTimecodeUtils::GetTimecode(SoundWave);
		if (TimecodeOpt.IsSet())
		{
			Timecode = TimecodeOpt.GetValue();
			TimecodeFromAsset = true;
		}
	}

	if (!TimecodeFromAsset)
	{
		FFrameRate FrameRate;
		GetDefaultTimecodeInfo(Timecode, FrameRate);
	}

	return Timecode;
}

FFrameRate UFootageCaptureData::GetEffectiveAudioTimecodeRate() const
{
	FFrameRate FrameRate;
	bool TimecodeRateFromAsset = false;

	if (Audios.Num() > 0)
	{
		const USoundWave* SoundWave = Audios[0].Get();
		check(IsValid(SoundWave));

		TOptional<FFrameRate> FrameRateOpt = USoundWaveTimecodeUtils::GetFrameRate(SoundWave);
		if (FrameRateOpt.IsSet())
		{
			FrameRate = FrameRateOpt.GetValue();
			TimecodeRateFromAsset = true;
		}
	}

	if (!TimecodeRateFromAsset)
	{
		FTimecode Timecode;
		GetDefaultTimecodeInfo(Timecode, FrameRate);
	}

	return FrameRate;
}

void UFootageCaptureData::GetDefaultTimecodeInfo(FTimecode& OutTimecode, FFrameRate& OutFrameRate) const
{
	for (const TObjectPtr<class UImgMediaSource>& ImageSequence : ImageSequences)
	{
		FTimecode Timecode = UImageSequenceTimecodeUtils::GetTimecode(ImageSequence);
		FFrameRate FrameRate = UImageSequenceTimecodeUtils::GetFrameRate(ImageSequence);

		if (UImageSequenceTimecodeUtils::IsValidTimecodeInfo(Timecode, FrameRate))
		{
			OutTimecode = MoveTemp(Timecode);
			OutFrameRate = MoveTemp(FrameRate);
			return;
		}
	}

	for (const TObjectPtr<class UImgMediaSource>& DepthSequence : DepthSequences)
	{
		FTimecode Timecode = UImageSequenceTimecodeUtils::GetTimecode(DepthSequence);
		FFrameRate FrameRate = UImageSequenceTimecodeUtils::GetFrameRate(DepthSequence);

		if (UImageSequenceTimecodeUtils::IsValidTimecodeInfo(Timecode, FrameRate))
		{
			OutTimecode = MoveTemp(Timecode);
			OutFrameRate = MoveTemp(FrameRate);
			return;
		}
	}

	if (Audios.Num() > 0)
	{
		const USoundWave* SoundWave = Audios[0].Get();
		check(IsValid(SoundWave));

		TOptional<FTimecode> TimecodeOpt = USoundWaveTimecodeUtils::GetTimecode(SoundWave);
		TOptional<FFrameRate> FrameRateOpt = USoundWaveTimecodeUtils::GetFrameRate(SoundWave);

		if (TimecodeOpt.IsSet() && FrameRateOpt.IsSet())
		{
			OutTimecode = TimecodeOpt.GetValue();
			OutFrameRate = FrameRateOpt.GetValue();
			return;
		}
	}

	OutTimecode = FTimecode(0, 0, 0, 0, false);
	OutFrameRate = FFrameRate(30, 1);
}

void UFootageCaptureData::PostLoad()
{
	Super::PostLoad();

	if (Metadata.DeviceModel_DEPRECATED != EFootageDeviceClass::Unspecified)
	{
		Metadata.DeviceClass = Metadata.DeviceModel_DEPRECATED;
		Metadata.DeviceModel_DEPRECATED = EFootageDeviceClass::Unspecified;
	}

	if (CameraCalibration_DEPRECATED != nullptr)
	{
		CameraCalibrations.Add(MoveTemp(CameraCalibration_DEPRECATED));
		CameraCalibration_DEPRECATED = nullptr;
	}

	if (Audio_DEPRECATED != nullptr)
	{
		Audios.Add(MoveTemp(Audio_DEPRECATED));
		Audio_DEPRECATED = nullptr;
	}

#if WITH_EDITOR
	for (FFootageCaptureView& View : Views_DEPRECATED)
	{
		if (View.bImageTimecodePresent)
		{
			UImageSequenceTimecodeUtils::SetTimecodeInfo(View.ImageTimecode, View.ImageTimecodeRate, View.ImageSequence.Get());
		}
		ImageSequences.Add(MoveTemp(View.ImageSequence));

		if (View.bDepthTimecodePresent)
		{
			UImageSequenceTimecodeUtils::SetTimecodeInfo(View.DepthTimecode, View.DepthTimecodeRate, View.DepthSequence.Get());
		}
		DepthSequences.Add(MoveTemp(View.DepthSequence));
	}
#endif
	if (Audios.Num() > 0 && bAudioTimecodePresent_DEPRECATED == true) 
	{
		USoundWave* SoundWave = Audios[0].Get();
		check(IsValid(SoundWave));

		USoundWaveTimecodeUtils::SetTimecodeInfo(AudioTimecode_DEPRECATED, AudioTimecodeRate_DEPRECATED, SoundWave);
		bAudioTimecodePresent_DEPRECATED = false;
	}
}

void UFootageCaptureData::PopulateCameraNames(UFootageCaptureData* InFootageCaptureData, FString& InOutCamera, TArray<TSharedPtr<FString>>& OutCameraNames)
{
	OutCameraNames.Reset();

	if (InFootageCaptureData && !InFootageCaptureData->CameraCalibrations.IsEmpty())
	{
		const UCameraCalibration* CameraCalibration = InFootageCaptureData->CameraCalibrations[0];
		if (CameraCalibration)
		{
			for (const FExtendedLensFile& LensFile : CameraCalibration->CameraCalibrations)
			{
				if (!LensFile.IsDepthCamera)
				{
					OutCameraNames.Add(MakeShared<FString>(LensFile.Name));
				}
			}
		}
	}

	if (OutCameraNames.IsEmpty())
	{
		InOutCamera = TEXT("");
	}
	else
	{
		bool bNameFound = false;

		for (TSharedPtr<FString> Name : OutCameraNames)
		{
			if (*Name == InOutCamera)
			{
				bNameFound = true;
				break;
			}
		}

		if (!bNameFound)
		{
			InOutCamera = *OutCameraNames[0];
		}
	}
}