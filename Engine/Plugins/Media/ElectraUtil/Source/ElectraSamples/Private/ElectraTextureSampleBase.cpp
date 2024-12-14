// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/Build.h"

#if !UE_SERVER

#include "HAL/IConsoleManager.h"
#include "HAL/Platform.h"
#include "ElectraTextureSample.h"
#include "ElectraTextureSampleUtils.h"
#include "ElectraSamplesModule.h"

// -------------------------------------------------------------------------------------------------------------------------------------------------------

namespace
{

static TOptional<FTimecode> CreateTimecodeFromMPEGDefinition(TOptional<FFrameRate>& OutFramerate, const IVideoDecoderTimecode::FMPEGDefinition* InMPEGTimecode)
{
	if (InMPEGTimecode->timing_info_present_flag)
	{
		const FTimespan ts(Electra::FTimeValue(InMPEGTimecode->clockTimestamp, InMPEGTimecode->time_scale).GetAsTimespan());
		OutFramerate = FFrameRate(InMPEGTimecode->time_scale, InMPEGTimecode->num_units_in_tick);
		return FTimecode::FromTimespan(ts, OutFramerate.GetValue(), InMPEGTimecode->ct_type > 1, false);
	}
	return TOptional<FTimecode>();
}

}

static TAutoConsoleVariable<float> CVarElectraHdrWhiteLevel(
	TEXT("Electra.HDR.WhiteLevel"),
	MediaTextureSample::kLinearToNitsScale_BT2408,
	TEXT("White level as a linear to nits scale factor.\n")
	TEXT("(default: 203.0)"),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarElectraHdrToneMapMethod(
	TEXT("Electra.HDR.ToneMapMethod"),
	static_cast<int32>(MediaShaders::EToneMapMethod::Hable),
	TEXT("Tone mapping method applied on source HDR media:\n")
	TEXT(" 0: None\n")
	TEXT(" 1: Hable (default)\n")
	TEXT(" 2: SimpleReinhard\n"),
	ECVF_Default);

// -------------------------------------------------------------------------------------------------------------------------------------------------------

void IElectraTextureSampleBase::Initialize(FVideoDecoderOutput* InVideoDecoderOutput)
{
	VideoDecoderOutput = StaticCastSharedPtr<FVideoDecoderOutput, IDecoderOutputPoolable, ESPMode::ThreadSafe>(InVideoDecoderOutput->AsShared());
	Colorimetry = VideoDecoderOutput->GetColorimetry();
	HDRInfo = VideoDecoderOutput->GetHDRInformation();
	TSharedPtr<const IVideoDecoderTimecode, ESPMode::ThreadSafe> TimecodePtr = VideoDecoderOutput->GetTimecode();

	if (TimecodePtr.IsValid())
	{
		// Store this in case this is needed again.
		DecoderTimecode = TimecodePtr;
		Timecode = CreateTimecodeFromMPEGDefinition(Framerate, TimecodePtr->GetMPEGDefinition());
	}

	// Get various basic MP4-style colorimetry values (we default to video range Rec709 SDR)
	bool bFullRange = false;
	uint8 ColorPrimaries = ElectraColorimetryUtils::DefaultMPEGColorPrimaries;
	uint8 TransferCharacteristics = ElectraColorimetryUtils::DefaultMPEGMatrixCoefficients;
	uint8 MatrixCoefficients = ElectraColorimetryUtils::DefaultMPEGTransferCharacteristics;
	if (auto PinnedColorimetry = Colorimetry.Pin())
	{
		bFullRange = (PinnedColorimetry->GetMPEGDefinition()->VideoFullRangeFlag != 0);
		ColorPrimaries = PinnedColorimetry->GetMPEGDefinition()->ColourPrimaries;
		TransferCharacteristics = PinnedColorimetry->GetMPEGDefinition()->TransferCharacteristics;
		MatrixCoefficients = PinnedColorimetry->GetMPEGDefinition()->MatrixCoefficients;
	}

	// Compute the bits per component in the data we get passed in
	EPixelFormat PixFmt = VideoDecoderOutput->GetFormat();
	uint8 NumBits = 8;
	if (!IsDXTCBlockCompressedTextureFormat(PixFmt))
	{
		if (PixFmt == PF_NV12)
		{
			NumBits = 8;
		}
		else if (PixFmt == PF_A2B10G10R10)
		{
			NumBits = 10;
		}
		else if (PixFmt == PF_P010)
		{
			NumBits = 16;
		}
		else
		{
			NumBits = (8 * GPixelFormats[PixFmt].BlockBytes) / GPixelFormats[PixFmt].NumComponents;
		}
	}

	FVector Off = FVector::Zero();
	const FMatrix* Mtx = nullptr;

	// Defaults in case no HDR info is present
	DisplayMasteringColorSpace.Reset();
	DisplayMasteringLuminanceMin = -1.0f;
	DisplayMasteringLuminanceMax = -1.0f;
	MaxCLL = 0;
	MaxFALL = 0;

	// Do we have specific HDR information, so we can assume a standard?
	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		//
		// HDR information present
		//

		// Mastering display info...
		if (auto ColorVolume = PinnedHDRInfo->GetMasteringDisplayColourVolume())
		{
			// A few sanity checks on the primaries coordinates (by no means exhaustive, but it should catch a fair share of oddities)
			if (ColorVolume->display_primaries_x[0] > FMath::Max(ColorVolume->display_primaries_x[1], ColorVolume->display_primaries_x[2]) &&	// Red has largest X
				ColorVolume->display_primaries_y[1] > FMath::Max(ColorVolume->display_primaries_y[0], ColorVolume->display_primaries_y[2]) &&	// Green has largest Y
				ColorVolume->display_primaries_x[2] <= ColorVolume->display_primaries_x[0] &&													// Blue's X is smaller than Red's
				ColorVolume->display_primaries_y[2] <= ColorVolume->display_primaries_y[0] &&													// Blue's Y is smaller than Red's
				ColorVolume->display_primaries_x[2] <= ColorVolume->display_primaries_x[1] && 													// Blue's X is smaller or same than Green's
				ColorVolume->display_primaries_x[1] <= ColorVolume->display_primaries_x[0]) 													// Red's X is greater or same than Green's
			{
				DisplayMasteringColorSpace = UE::Color::FColorSpace(FVector2d(ColorVolume->display_primaries_x[0], ColorVolume->display_primaries_y[0]),
														   FVector2d(ColorVolume->display_primaries_x[1], ColorVolume->display_primaries_y[1]),
														   FVector2d(ColorVolume->display_primaries_x[2], ColorVolume->display_primaries_y[2]),
														   FVector2d(ColorVolume->white_point_x, ColorVolume->white_point_y));
			}

			DisplayMasteringLuminanceMin = ColorVolume->min_display_mastering_luminance;
			DisplayMasteringLuminanceMax = ColorVolume->max_display_mastering_luminance;
		}

		// Content light level info...
		if (auto ContentLightLevelInfo = PinnedHDRInfo->GetContentLightLevelInfo())
		{
			MaxCLL = ContentLightLevelInfo->max_content_light_level;
			MaxFALL =  ContentLightLevelInfo->max_pic_average_light_level;
		}
	}

	// The sample source color space is always defined by the color primaries value
	SourceColorSpace = UE::Color::FColorSpace(ElectraColorimetryUtils::TranslateMPEGColorPrimaries(ColorPrimaries));

	// Select the YUV-RGB conversion matrix to use
	switch (ElectraColorimetryUtils::TranslateMPEGMatrixCoefficients(MatrixCoefficients))
	{
		case UE::Color::EColorSpace::None:	// ID (RGB)
			// no conversion, data is RGB
			break;
		case UE::Color::EColorSpace::sRGB:
			Mtx = bFullRange ? &MediaShaders::YuvToRgbRec709Unscaled : &MediaShaders::YuvToRgbRec709Scaled;
			break;
		case UE::Color::EColorSpace::Rec2020:
			Mtx = bFullRange ? &MediaShaders::YuvToRgbRec2020Unscaled : &MediaShaders::YuvToRgbRec2020Scaled;
			break;
		default:
			check(!"*** Unexpected matrix coefficients!");
			Mtx = bFullRange ? &MediaShaders::YuvToRgbRec709Unscaled : &MediaShaders::YuvToRgbRec709Scaled;
	}

	// Get color encoding (sRGB, linear, PQ, HLG...)
	ColorEncoding = ElectraColorimetryUtils::TranslateMPEGTransferCharacteristics(TransferCharacteristics);

	if (Mtx)
	{
		// Select the offsets prior to YUV conversion needed per the incoming data
		switch (NumBits)
		{
		case 8:		Off = bFullRange ? MediaShaders::YUVOffsetNoScale8bits : MediaShaders::YUVOffset8bits; break;
		case 10:	Off = bFullRange ? MediaShaders::YUVOffsetNoScale10bits : MediaShaders::YUVOffset10bits; break;
		case 16:	Off = bFullRange ? MediaShaders::YUVOffsetNoScale16bits : MediaShaders::YUVOffset16bits; break;
		case 32:	Off = bFullRange ? MediaShaders::YUVOffsetNoScaleFloat : MediaShaders::YUVOffsetFloat; break;
		default:	check(!"Unexpected number of bits per channel!");
		}
	}

	// Correctional scale for input data
	// (data should be placed in the upper 10-bits of the 16-bit texture channels, but some platforms do not do this - they provide a correctional factor here)
	float DataScale = GetSampleDataScale(NumBits == 10);

	// Compute scale to make correct towards the max value (P010 will max out at 0xffc0 not 0xffff - so if it is present we need to adjust the scale a bit)
	float NormScale = (VideoDecoderOutput->GetFormat() == PF_P010) ? (65535.0f / 65472.0f) : 1.0f;

	// Matrix to transform sample data to standard YUV values
	FMatrix PreMtx = FMatrix::Identity;
	PreMtx.M[0][0] = DataScale * NormScale;
	PreMtx.M[1][1] = DataScale * NormScale;
	PreMtx.M[2][2] = DataScale * NormScale;
	PreMtx.M[0][3] = -Off.X;
	PreMtx.M[1][3] = -Off.Y;
	PreMtx.M[2][3] = -Off.Z;

	// Combine this with the actual YUV-RGB conversion
	SampleToRgbMtx = FMatrix44f(Mtx ? (*Mtx * PreMtx) : PreMtx);

	// Also store the plain YUV->RGB matrix (pointer) for later reference
	YuvToRgbMtx = Mtx;
}

void IElectraTextureSampleBase::InitializePoolable()
{
}

void IElectraTextureSampleBase::ShutdownPoolable()
{
	VideoDecoderOutput.Reset();
}

FIntPoint IElectraTextureSampleBase::GetDim() const
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetDim();
	}
	return FIntPoint::ZeroValue;
}


FIntPoint IElectraTextureSampleBase::GetOutputDim() const
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetOutputDim();
	}
	return FIntPoint::ZeroValue;
}


FMediaTimeStamp IElectraTextureSampleBase::GetTime() const
{
	if (VideoDecoderOutput)
	{
		const FDecoderTimeStamp TimeStamp = VideoDecoderOutput->GetTime();
		return FMediaTimeStamp(TimeStamp.Time, TimeStamp.SequenceIndex);
	}
	return FMediaTimeStamp();
}

void IElectraTextureSampleBase::SetTime(const FMediaTimeStamp& InTime)
{
	if (VideoDecoderOutput)
	{
		VideoDecoderOutput->SetTime(FDecoderTimeStamp(InTime.Time, InTime.SequenceIndex));
	}
}


FTimespan IElectraTextureSampleBase::GetDuration() const
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetDuration();
	}
	return FTimespan::Zero();
}

bool IElectraTextureSampleBase::IsOutputSrgb() const
{
	return ColorEncoding == UE::Color::EEncoding::sRGB;
}

const FMatrix& IElectraTextureSampleBase::GetYUVToRGBMatrix() const
{
	return YuvToRgbMtx ? *YuvToRgbMtx : FMatrix::Identity;
}

bool IElectraTextureSampleBase::GetFullRange() const
{
	if (auto PinnedColorimetry = Colorimetry.Pin())
	{
		return (PinnedColorimetry->GetMPEGDefinition()->VideoFullRangeFlag != 0);
	}
	return false;
}

FMatrix44f IElectraTextureSampleBase::GetSampleToRGBMatrix() const
{
	return SampleToRgbMtx;
}

const UE::Color::FColorSpace& IElectraTextureSampleBase::GetSourceColorSpace() const
{
	return SourceColorSpace;
}

UE::Color::EEncoding IElectraTextureSampleBase::GetEncodingType() const
{
	return ColorEncoding;
}

float IElectraTextureSampleBase::GetHDRNitsNormalizationFactor() const
{
	if (GetEncodingType() == UE::Color::EEncoding::sRGB || GetEncodingType() == UE::Color::EEncoding::Linear)
	{
		return 1.0f;
	}
	else
	{
		return 1.0f / CVarElectraHdrWhiteLevel->GetFloat();
	}
}

bool IElectraTextureSampleBase::GetDisplayMasteringLuminance(float& OutMin, float& OutMax) const
{
	if (DisplayMasteringLuminanceMin < 0.0f && DisplayMasteringLuminanceMax < 0.0f)
	{
		return false;
	}

	OutMin = DisplayMasteringLuminanceMin;
	OutMax = DisplayMasteringLuminanceMax;
	return true;
}

TOptional<UE::Color::FColorSpace> IElectraTextureSampleBase::GetDisplayMasteringColorSpace() const
{
	return DisplayMasteringColorSpace;
}

bool IElectraTextureSampleBase::GetMaxLuminanceLevels(uint16& OutCLL, uint16& OutFALL) const
{
	if (MaxCLL == 0 && MaxFALL == 0)
	{
		return false;
	}

	OutCLL = MaxCLL;
	OutFALL = MaxFALL;
	return true;
}

MediaShaders::EToneMapMethod IElectraTextureSampleBase::GetToneMapMethod() const
{
	if (GetEncodingType() == UE::Color::EEncoding::sRGB || GetEncodingType() == UE::Color::EEncoding::Linear)
	{
		return MediaShaders::EToneMapMethod::None;
	}
	else
	{
		const int32 ToneMapMethod = FMath::Clamp(CVarElectraHdrToneMapMethod->GetInt(), 0, static_cast<int32>(MediaShaders::EToneMapMethod::MAX) - 1);

		return static_cast<MediaShaders::EToneMapMethod>(ToneMapMethod);
	}
}

#endif
