#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
//#define DR_MP3_NO_SIMD

#define DRMP3_ASSERT(expression)

#define DR_MP3_FLOAT_OUTPUT
//#define DR_MP3_ONLY_MP3

#define DRMP3_API static

#include "dr_mp3.h"


#include "dr_libs_mp3decoder.h"

class FMP3Decoder::FImpl
{
public:
	FImpl()
	{
		Reset();
	}
	int Decode(FFrameInfo* OutFrameInfo, float* OutDecodedPCM, int InOutDecodedPCMSize, const unsigned char* InCompressedData, int InCompressedDataSize)
	{
		drmp3dec_frame_info info;
		int Result = drmp3dec_decode_frame(&mp3, (const drmp3_uint8*)InCompressedData, InCompressedDataSize, OutDecodedPCM, &info);
		if (OutFrameInfo)
		{
			OutFrameInfo->NumFrameBytes = info.frame_bytes;
			OutFrameInfo->NumChannels = info.channels;
			OutFrameInfo->SampleRate = info.hz;
			OutFrameInfo->Layer = info.layer;
			OutFrameInfo->BitrateKbps = info.bitrate_kbps;
		}
		return Result;
	}
	void Reset()
	{
		memset(&mp3, 0, sizeof(drmp3dec));
		drmp3dec_init(&mp3);
	}
private:
	drmp3dec mp3;
};


FMP3Decoder::FMP3Decoder()
{
	Impl = new FImpl;
}
FMP3Decoder::~FMP3Decoder()
{
	delete Impl;
}
int FMP3Decoder::Decode(FFrameInfo* OutFrameInfo, float* OutDecodedPCM, int InOutDecodedPCMSize, const unsigned char* InCompressedData, int InCompressedDataSize)
{
	return Impl->Decode(OutFrameInfo, OutDecodedPCM, InOutDecodedPCMSize, InCompressedData, InCompressedDataSize);
}
void FMP3Decoder::Reset()
{
	Impl->Reset();
}
