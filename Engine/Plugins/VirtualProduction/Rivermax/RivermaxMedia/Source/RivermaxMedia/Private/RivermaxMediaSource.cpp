// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxMediaSource.h"

#include "MediaIOCorePlayerBase.h"
#include "RivermaxMediaSourceOptions.h"
#include "RivermaxMediaUtils.h"


/*
 * IMediaOptions interface
 */

URivermaxMediaSource::URivermaxMediaSource() : UCaptureCardMediaSource()
{
	Deinterlacer = nullptr;
	bOverrideSourceEncoding = false;
	bOverrideSourceColorSpace = false;
	bRenderJIT = true;
	EvaluationType = EMediaIOSampleEvaluationType::Latest;
}

bool URivermaxMediaSource::GetMediaOption(const FName& Key, bool DefaultValue) const
{
	if (Key == RivermaxMediaOption::UseGPUDirect)
	{
		return bUseGPUDirect;
	}
	if (Key == RivermaxMediaOption::OverrideResolution)
	{
		return bOverrideResolution;
	}

	return Super::GetMediaOption(Key, DefaultValue);
}

int64 URivermaxMediaSource::GetMediaOption(const FName& Key, int64 DefaultValue) const
{
	if (Key == RivermaxMediaOption::Port)
	{
		return Port;
	}
	else if (Key == RivermaxMediaOption::PixelFormat)
	{
		return (int64)PixelFormat;
	}
	else if (Key == FMediaIOCoreMediaOption::FrameRateNumerator)
	{
		return FrameRate.Numerator;
	}
	else if (Key == FMediaIOCoreMediaOption::FrameRateDenominator)
	{
		return FrameRate.Denominator;
	}
	else if (Key == FMediaIOCoreMediaOption::ResolutionWidth)
	{
		return Resolution.X;
	}
	else if (Key == FMediaIOCoreMediaOption::ResolutionHeight)
	{
		return Resolution.Y;
	}
	return Super::GetMediaOption(Key, DefaultValue);
}

FString URivermaxMediaSource::GetMediaOption(const FName& Key, const FString& DefaultValue) const
{
	if (Key == FMediaIOCoreMediaOption::VideoModeName)
	{
		return FString::Printf(TEXT("FormatDescriptorTodo"));
	}
	else if (Key == RivermaxMediaOption::InterfaceAddress)
	{
		return InterfaceAddress;
	}
	else if (Key == RivermaxMediaOption::StreamAddress)
	{
		return StreamAddress;
	}
	return Super::GetMediaOption(Key, DefaultValue);
}

bool URivermaxMediaSource::HasMediaOption(const FName& Key) const
{
	if ((Key == RivermaxMediaOption::InterfaceAddress) ||
		(Key == RivermaxMediaOption::StreamAddress) ||
		(Key == RivermaxMediaOption::Port) ||
		(Key == RivermaxMediaOption::PixelFormat) ||
		(Key == RivermaxMediaOption::UseGPUDirect) ||
		(Key == FMediaIOCoreMediaOption::FrameRateNumerator) ||
		(Key == FMediaIOCoreMediaOption::FrameRateDenominator) ||
		(Key == FMediaIOCoreMediaOption::ResolutionWidth) ||
		(Key == FMediaIOCoreMediaOption::ResolutionHeight) ||
		(Key == FMediaIOCoreMediaOption::VideoModeName))
	{
		return true;
	}

	return Super::HasMediaOption(Key);
}

void URivermaxMediaSource::Serialize(FArchive& Ar_Asset)
{
	Super::Serialize(Ar_Asset);

	Ar_Asset.UsingCustomVersion(FRivermaxMediaVersion::GUID);
}

void URivermaxMediaSource::PostLoad()
{
	Super::PostLoad();
	// We can only recover data during editor. Proprties will be fixed during cook.
#if WITH_EDITORONLY_DATA
	const int32 RivermaxMediaVersion = GetLinkerCustomVersion(FRivermaxMediaVersion::GUID);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;

	// Rivermax was merged with other capture card media sources and these properties 
	// are either duplicates or provide limited functionality compared to the inherited UI.
	if (RivermaxMediaVersion < FRivermaxMediaVersion::BeforeCustomVersionAdded)
	{
		if (bUseZeroLatency_DEPRECATED == false)
		{
			bUseTimeSynchronization = true;
			FrameDelay = 1;
		}

		if (bIsSRGBInput_DEPRECATED == true)
		{
			bOverrideSourceEncoding = true;
			OverrideSourceEncoding = EMediaIOCoreSourceEncoding::sRGB;
		}

		if (PlayerMode_DEPRECATED == ERivermaxPlayerMode_DEPRECATED::Framelock)
		{
			EvaluationType = EMediaIOSampleEvaluationType::Timecode;
			bUseTimeSynchronization = true;
			bFramelock = true;
		}
		else
		{
			EvaluationType = EMediaIOSampleEvaluationType::Latest;
			bFramelock = false;
		}

		Modify();
	}

	PRAGMA_ENABLE_DEPRECATION_WARNINGS;
#endif

}

/*
 * UMediaSource interface
 */

FString URivermaxMediaSource::GetUrl() const
{
	return TEXT("rmax://");//todo support proper url
}

bool URivermaxMediaSource::Validate() const
{
	return true;
}

