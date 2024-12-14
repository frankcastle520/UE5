#pragma once

class FMP3Decoder
{
public:
	FMP3Decoder();
	~FMP3Decoder();

	struct FFrameInfo
	{
		int NumFrameBytes = 0;
		int NumChannels = 0;
		int SampleRate = 0;
		int Layer = 0;
		int BitrateKbps = 0;
	};

	int Decode(FFrameInfo* OutFrameInfo, float* OutDecodedPCM, int InOutDecodedPCMSize, const unsigned char* InCompressedData, int InCompressedDataSize);
	void Reset();
private:
	class FImpl;
	FImpl* Impl = nullptr;
};
