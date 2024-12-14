// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxMediaTextureSample.h"

#include "RenderGraphUtils.h"
#include "RivermaxMediaPlayer.h"
#include "RivermaxMediaTextureSampleConverter.h"
#include "RivermaxMediaUtils.h"
#include "Templates/RefCounting.h"

namespace UE::RivermaxMedia
{

FRivermaxMediaTextureSample::FRivermaxMediaTextureSample()
	: FMediaIOCoreTextureSampleBase()
{
	SampleReceivedEvent = FGenericPlatformProcess::GetSynchEventFromPool(false);
}

FRivermaxMediaTextureSample::~FRivermaxMediaTextureSample()
{
	FGenericPlatformProcess::ReturnSynchEventToPool(SampleReceivedEvent);
}

const FMatrix& FRivermaxMediaTextureSample::GetYUVToRGBMatrix() const
{
	return MediaShaders::YuvToRgbRec709Scaled;
}

IMediaTextureSampleConverter* FRivermaxMediaTextureSample::GetMediaTextureSampleConverter()
{
	return Converter.Get();
}

bool FRivermaxMediaTextureSample::InitializeJITR(const FMediaIOCoreSampleJITRConfigurationArgs& Args)
{
	if (!FMediaIOCoreTextureSampleBase::InitializeJITR(Args))
	{
		return false;
	}

	ERivermaxMediaSourcePixelFormat DesiredPixelFormat = StaticCastSharedPtr<FRivermaxMediaPlayer>(Args.Player)->GetDesiredPixelFormat();
	SetInputFormat(DesiredPixelFormat);

	return true;
}

void FRivermaxMediaTextureSample::CopyConfiguration(const TSharedPtr<FMediaIOCoreTextureSampleBase>& SourceSample)
{
	FMediaIOCoreTextureSampleBase::CopyConfiguration(SourceSample);
	TSharedPtr<FRivermaxMediaTextureSample> RivermaxSample = StaticCastSharedPtr<FRivermaxMediaTextureSample>(SourceSample);
	SetInputFormat(RivermaxSample->InputFormat);
}

void FRivermaxMediaTextureSample::InitializeGPUBuffer(const FIntPoint& InResolution, ERivermaxMediaSourcePixelFormat InSampleFormat, bool bSupportsGPUDirect)
{
	using namespace UE::RivermaxMediaUtils::Private;
	const FSourceBufferDesc BufferDescription = GetBufferDescription(InResolution, InSampleFormat);

	FRDGBufferDesc RDGBufferDesc = FRDGBufferDesc::CreateStructuredDesc(BufferDescription.BytesPerElement, BufferDescription.NumberOfElements);
	
	{
		// Required to share resource across different graphics API (DX, Cuda)
		RDGBufferDesc.Usage |= EBufferUsageFlags::Shared;
	}

	InputFormat = InSampleFormat;
	ENQUEUE_RENDER_COMMAND(FRivermaxMediaTextureSample)(
	[SharedThis = AsShared(), RDGBufferDesc](FRHICommandListImmediate& CommandList)
	{
		SharedThis->GPUBuffer = AllocatePooledBuffer(RDGBufferDesc, TEXT("RmaxInput Buffer"));

	});
}

bool FRivermaxMediaTextureSample::IsCacheable() const
{
	return false;
}

uint8* FRivermaxMediaTextureSample::GetVideoBufferRawPtr(uint32 VideoBufferSize)
{
	return reinterpret_cast<uint8*>(GetOrRequestBuffer(VideoBufferSize));
}

ERivermaxMediaSourcePixelFormat FRivermaxMediaTextureSample::GetInputFormat() const
{
	return InputFormat;
}

void FRivermaxMediaTextureSample::SetInputFormat(ERivermaxMediaSourcePixelFormat InFormat)
{
	InputFormat = InFormat;

	switch (InputFormat)
	{
		case ERivermaxMediaSourcePixelFormat::RGB_12bit:
			// Falls through
		case ERivermaxMediaSourcePixelFormat::RGB_16bit_Float:
		{
			SampleFormat = EMediaTextureSampleFormat::FloatRGBA;
			break;
		}
		case ERivermaxMediaSourcePixelFormat::RGB_10bit:
			// Falls through
		case ERivermaxMediaSourcePixelFormat::YUV422_10bit:
		{
			SampleFormat = EMediaTextureSampleFormat::CharBGR10A2;
			break;
		}
		case ERivermaxMediaSourcePixelFormat::YUV422_8bit:
			// Falls through
		case ERivermaxMediaSourcePixelFormat::RGB_8bit:
			// Falls through
		default:
		{
			SampleFormat = EMediaTextureSampleFormat::CharBGRA;
			break;
		}
	}
}

TRefCountPtr<FRDGPooledBuffer> FRivermaxMediaTextureSample::GetGPUBuffer() const
{
	return GPUBuffer;
}


}

