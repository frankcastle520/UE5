// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include <Containers/Array.h>

namespace ElectraDecodersUtil
{
	namespace MPEG
	{

		class ELECTRADECODERS_API FAACDecoderConfigurationRecord
		{
		public:
			FAACDecoderConfigurationRecord();

			bool ParseFrom(const void* Data, int64 Size);
			void Reset();
			const TArray<uint8>& GetCodecSpecificData() const;

			void SetRawData(const TArray<uint8>& InRawData)
			{
				RawData = InRawData;
			}
			const TArray<uint8>& GetRawData() const
			{
				return RawData;
			}

			int32		SBRSignal;
			int32		PSSignal;
			uint32		ChannelConfiguration;
			uint32		SamplingFrequencyIndex;
			uint32		SamplingRate;
			uint32		ExtSamplingFrequencyIndex;
			uint32		ExtSamplingFrequency;
			uint32		AOT;
			uint32		ExtAOT;
		private:
			TArray<uint8>	CodecSpecificData;
			TArray<uint8>	RawData;
		};


		namespace AACUtils
		{
			int32 ELECTRADECODERS_API GetNumberOfChannelsFromChannelConfiguration(uint32 InChannelConfiguration);
		}


		namespace UtilsMPEG123
		{
			// Raw bit methods
			bool ELECTRADECODERS_API HasValidSync(uint32 InFrameHeader);
			int32 ELECTRADECODERS_API GetVersionId(uint32 InFrameHeader);			// 0=MPEG2.5, 1=reserved, 2=MPEG2 (ISO/IEC 13818-3), 3=MPEG1 (ISO/IEC 11172-3)
			int32 ELECTRADECODERS_API GetLayerIndex(uint32 InFrameHeader);			// 0=reserved, 1=Layer III, 2=Layer II, 3=Layer I
			int32 ELECTRADECODERS_API GetBitrateIndex(uint32 InFrameHeader);		// 0-15
			int32 ELECTRADECODERS_API GetSamplingRateIndex(uint32 InFrameHeader);	// 0-3
			int32 ELECTRADECODERS_API GetChannelMode(uint32 InFrameHeader);			// 0-3
			int32 ELECTRADECODERS_API GetNumPaddingBytes(uint32 InFrameHeader);		// 0 or 1


			// Convenience methods
			int32 ELECTRADECODERS_API GetVersion(uint32 InFrameHeader);				// 1=MPEG 1, 2=MPEG2, 3=MPEG2.5, 0=reserved
			int32 ELECTRADECODERS_API GetLayer(uint32 InFrameHeader);				// 0=reserved, 1=Layer I, 2=Layer II, 3=Layer III
			int32 ELECTRADECODERS_API GetBitrate(uint32 InFrameHeader);				// Bitrate in kbps, -1=invalid
			int32 ELECTRADECODERS_API GetSamplingRate(uint32 InFrameHeader);		// Sampling rate, -1=invalid
			int32 ELECTRADECODERS_API GetChannelCount(uint32 InFrameHeader);		// 1=mono, 2=stereo

			int32 ELECTRADECODERS_API GetSamplesPerFrame(uint32 InFrameHeader);		// number of samples encoded in the frame
			int32 ELECTRADECODERS_API GetFrameSize(uint32 InFrameHeader, int32 InForcedPadding=-1);		// number of bytes in the packet if CBR encoded, 0=could not calculate
		}

	} // namespace MPEG

} // namespace ElectraDecodersUtil
