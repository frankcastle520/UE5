// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/MPEG/ElectraUtilsMPEGAudio.h"
#include "Utils/ElectraBitstreamReader.h"

namespace ElectraDecodersUtil
{
	namespace MPEG
	{
		namespace AACUtils
		{
			int32 GetNumberOfChannelsFromChannelConfiguration(uint32 InChannelConfiguration)
			{
				static const uint8 NumChannelsForConfig[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 7, 8, 0, 8, 0 };
				return InChannelConfiguration < 16 ? NumChannelsForConfig[InChannelConfiguration] : 0;
			}
		}


		namespace AACParseHelper
		{
			static uint32 GIndexToSampleRate[16] =
			{
				96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 0, 0, 0, 0
			};

			static uint32 GetAudioObjectType(FElectraBitstreamReader& Bitstream)
			{
				uint32 ObjectType = Bitstream.GetBits(5);
				if (ObjectType == 31)
				{
					ObjectType = 32 + Bitstream.GetBits(6);
				}
				return ObjectType;
			}
			static uint32 GetSamplingRateIndex(FElectraBitstreamReader& Bitstream, uint32& Rate)
			{
				uint32 SamplingFrequencyIndex = Bitstream.GetBits(4);
				if (SamplingFrequencyIndex == 15)
				{
					Rate = Bitstream.GetBits(24);
				}
				else
				{
					Rate = GIndexToSampleRate[SamplingFrequencyIndex];
				}
				return SamplingFrequencyIndex;
			}
			static void GetGASpecificConfig(FElectraBitstreamReader& Bitstream, uint32 SamplingFrequencyIndex, uint32 ChannelConfiguration, uint32 AudioObjectType)
			{
				int32 FrameLengthFlag = Bitstream.GetBits(1);
				int32 DependsOnCoreDecoder = Bitstream.GetBits(1);
				if (DependsOnCoreDecoder)
				{
					Bitstream.SkipBits(14);		// coreCoderDelay
				}
				int32 ExtensionFlag = Bitstream.GetBits(1);
				if (ChannelConfiguration == 0)
				{
					// TODO:
				}
				if (AudioObjectType == 6 || AudioObjectType == 20)
				{
					Bitstream.SkipBits(3);	// layerNr
				}
				if (ExtensionFlag)
				{
					if (AudioObjectType == 22)
					{
						Bitstream.SkipBits(5);	// numOfSubFrame
						Bitstream.SkipBits(11);	// layer_length
					}
					if (AudioObjectType == 17 || AudioObjectType == 19 || AudioObjectType == 20 || AudioObjectType == 23)
					{
						Bitstream.SkipBits(1);	// aacSectionDataResilienceFlag
						Bitstream.SkipBits(1);	// aacScalefactorDataResilienceFlag;
						Bitstream.SkipBits(1);	// aacSpectralDataResilienceFlag;
					}
					int32 ExtensionFlag3 = Bitstream.GetBits(1);
					if (ExtensionFlag)
					{
						// TODO:
					}
				}
			}

		} // namespace AACParseHelper


		FAACDecoderConfigurationRecord::FAACDecoderConfigurationRecord()
		{
			Reset();
		}

		void FAACDecoderConfigurationRecord::Reset()
		{
			SBRSignal = -1;
			PSSignal = -1;
			ChannelConfiguration = 0;
			SamplingFrequencyIndex = 0;
			SamplingRate = 0;
			ExtSamplingFrequencyIndex = 0;
			ExtSamplingFrequency = 0;
			AOT = 0;
			ExtAOT = 0;
		}


		const TArray<uint8>& FAACDecoderConfigurationRecord::GetCodecSpecificData() const
		{
			return CodecSpecificData;
		}

		bool FAACDecoderConfigurationRecord::ParseFrom(const void* Data, int64 Size)
		{
			CodecSpecificData.Empty();
			CodecSpecificData.SetNumUninitialized(Size);
			FMemory::Memcpy(CodecSpecificData.GetData(), Data, Size);

			FElectraBitstreamReader bsp(Data, Size);
			SBRSignal = -1;
			PSSignal = -1;
			SamplingRate = 0;
			ExtSamplingFrequency = 0;
			ExtSamplingFrequencyIndex = 0;
			ExtAOT = 0;
			AOT = AACParseHelper::GetAudioObjectType(bsp);
			SamplingFrequencyIndex = AACParseHelper::GetSamplingRateIndex(bsp, SamplingRate);
			ChannelConfiguration = bsp.GetBits(4);

			if (AOT == 5 /*SBR*/ || AOT == 29 /*PS*/)
			{
				ExtAOT = AOT;
				SBRSignal = 1;
				if (AOT == 29)
				{
					PSSignal = 1;
				}

				ExtSamplingFrequencyIndex = AACParseHelper::GetSamplingRateIndex(bsp, ExtSamplingFrequency);
				AOT = AACParseHelper::GetAudioObjectType(bsp);
			}
			// Handle supported AOT configs
			if (AOT == 2 /*LC*/)		// Only LC for now
			{
				AACParseHelper::GetGASpecificConfig(bsp, SamplingFrequencyIndex, ChannelConfiguration, AOT);
			}
			// Would need to handle epConfig here now for a couple of AOT's we are NOT supporting
			// ...

			// Check for backward compatible SBR signaling.
			if (ExtAOT != 5)
			{
				while (bsp.GetRemainingBits() > 15)
				{
					int32 syncExtensionType = bsp.PeekBits(11);
					if (syncExtensionType == 0x2b7)
					{
						bsp.SkipBits(11);
						ExtAOT = AACParseHelper::GetAudioObjectType(bsp);
						if (ExtAOT == 5)
						{
							SBRSignal = bsp.GetBits(1);
							if (SBRSignal)
							{
								ExtSamplingFrequencyIndex = AACParseHelper::GetSamplingRateIndex(bsp, ExtSamplingFrequency);
							}
						}
						if (bsp.GetRemainingBits() > 11 && bsp.GetBits(11) == 0x548)
						{
							PSSignal = bsp.GetBits(1);
						}
						break;
					}
					else
					{
						bsp.SkipBits(1);
					}
				}
			}

			//int64 remainingBits = bsp.GetRemainingBits();
			return true;
		}




		namespace UtilsMPEG123
		{
			bool HasValidSync(uint32 InFrameHeader)
			{
				return (InFrameHeader & 0xffe00000U) == 0xffe00000U;
			}

			int32 GetVersionId(uint32 InFrameHeader)
			{
				check(HasValidSync(InFrameHeader));
				return (int32) ((InFrameHeader >> 19) & 3U);
			}

			int32 GetLayerIndex(uint32 InFrameHeader)
			{
				check(HasValidSync(InFrameHeader));
				return (int32) ((InFrameHeader >> 17) & 3U);
			}

			int32 GetBitrateIndex(uint32 InFrameHeader)
			{
				check(HasValidSync(InFrameHeader));
				return (int32) ((InFrameHeader >> 12) & 15U);
			}

			int32 GetSamplingRateIndex(uint32 InFrameHeader)
			{
				check(HasValidSync(InFrameHeader));
				return (int32) ((InFrameHeader >> 10) & 3U);
			}

			int32 GetChannelMode(uint32 InFrameHeader)
			{
				check(HasValidSync(InFrameHeader));
				return (int32) ((InFrameHeader >> 6) & 3U);
			}

			int32 GetNumPaddingBytes(uint32 InFrameHeader)
			{
				check(HasValidSync(InFrameHeader));
				return (int32) ((InFrameHeader >> 9) & 1U);
			}

			int32 GetVersion(uint32 InFrameHeader)
			{
				switch(GetVersionId(InFrameHeader))
				{
					default:
						return 0;
					case 0:
						return 3;
					case 2:
						return 2;
					case 3:
						return 1;
				}
			}


			int32 GetLayer(uint32 InFrameHeader)
			{
				switch(GetLayerIndex(InFrameHeader))
				{
					default:
						return 0;
					case 1:
						return 3;
					case 2:
						return 2;
					case 3:
						return 1;
				}
			}

			int32 GetBitrate(uint32 InFrameHeader)
			{
				static const int16 kBitrateTableMPEG1[3][16] =
				{
					{ 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, -1 },
					{ 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, -1 },
					{ 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, -1 }
				};
				static const int16 kBitrateTableMPEG2[3][16] =
				{
					{ 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, -1 },
					{ 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 , -1 },
					{ 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 , -1 }
				};
				int32 Version = GetVersion(InFrameHeader);
				int32 Layer = GetLayer(InFrameHeader);
				int32 BitrateIndex = GetBitrateIndex(InFrameHeader);
				if (Version == 0 || Layer == 0 || BitrateIndex == 15)
				{
					return -1;
				}
				int32 bps = Version == 1 ? kBitrateTableMPEG1[Layer - 1][BitrateIndex] : kBitrateTableMPEG2[Layer - 1][BitrateIndex];
				return bps * 1000;
			}

			int32 GetSamplingRate(uint32 InFrameHeader)
			{
				static const int32 kSamplingRates[4][4] =
				{
					{ -1, -1, -1, -1 },				// invalid
					{ 44100, 48000, 32000, -1 },	// MPEG 1
					{ 22050, 24000, 16000, -1 },	// MPEG 2
					{ 11025, 12000,  8000, -1 }		// MPEG 2.5
				};
				int32 Version = GetVersion(InFrameHeader);
				int32 SampleRateIndex = GetSamplingRateIndex(InFrameHeader);
				return kSamplingRates[Version][SampleRateIndex];
			}
			int32 GetChannelCount(uint32 InFrameHeader)
			{
				return GetChannelMode(InFrameHeader) == 3 ? 1 : 2;
			}

			int32 GetFrameSize(uint32 InFrameHeader, int32 InForcedPadding)
			{
				static const int32 kNumCoeffs[2][3] =
				{
					{ 12, 144, 144 },		// MPEG 1 (layer 1, 2, 3)
					{ 12, 144, 72 }			// MPEG 2 / 2.5 (layer 1, 2, 3)
				};
				static const int32 kSlotSize[3] =
				{
					4, 1, 1					// Layer 1, 2, 3
				};

				int32 NumPadding = InForcedPadding < 0 ? GetNumPaddingBytes(InFrameHeader) : InForcedPadding;
				int32 Version = GetVersion(InFrameHeader);
				int32 Layer = GetLayer(InFrameHeader);
				int32 Bitrate = GetBitrate(InFrameHeader);
				int32 SampleRate = GetSamplingRate(InFrameHeader);
				if (Version == 0 || Layer == 0 || Bitrate <= 0 || SampleRate <= 0)
				{
					return 0;
				}
				int32 FrameSize = (kNumCoeffs[Version==1?0:1][Layer-1] * Bitrate / SampleRate + NumPadding) * kSlotSize[Layer-1];
				return FrameSize;
			}

			int32 GetSamplesPerFrame(uint32 InFrameHeader)
			{
				static const int32 kSamplesPerFrame[2][4] =
				{
					{ 0, 384, 1152, 1152 },	// MPEG 1 (layer 1, 2, 3)
					{ 0, 384, 1152, 576 }	// MPEG 2 / 2.5 (layer 1, 2, 3)
				};
				int32 Version = GetVersion(InFrameHeader);
				int32 Layer = GetLayer(InFrameHeader);
				return kSamplesPerFrame[Version==1?0:1][Layer];
			}

		} // namespace UtilsMPEG123


	} // namespace MPEG
} // namespace ElectraDecodersUtil

#if 0

struct ADTSheader
{
	// Refer to http://wiki.multimedia.cx/index.php?title=MPEG-4_Audio
	unsigned mSyncWord : 12;		//!< All set
	unsigned mVersion : 1;			//!< 0=MPEG-4, 1=MPEG-2
	unsigned mLayer : 2;			//!< 0
	unsigned mProtAbsent : 1;		//!< 0 if CRC present, 1 if CRC absent
	unsigned mProfile : 2;			//!< MPEG-4 audio object type -1
	unsigned mSmpFrqIndex : 4;		//!< MPEG-4 sampling frequency index (15 is forbidden)
	unsigned mPrivateStream : 1;
	unsigned mChannelCfg : 3;		//!< MPEG-4 channel configuration
	unsigned mOriginality : 1;
	unsigned mHome : 1;
	unsigned mCopyrighted : 1;
	unsigned mCopyrightStart : 1;
	unsigned mFrameLength : 13;
	unsigned mBufferFullness : 11;
	unsigned mNumFrames : 2;
	unsigned mCRCifPresent : 16;
};

static bool GetADTSHeader(ADTSheader& header, const void* pData, int64 nDataSize)
{
	if (nDataSize < 7)
	{
		return false;
	}

	FElectraBitstreamReader bs(pData, nDataSize);
	header.mSyncWord = bs.GetBits(12);
	header.mVersion = bs.GetBits(1);
	header.mLayer = bs.GetBits(2);
	header.mProtAbsent = bs.GetBits(1);
	header.mProfile = bs.GetBits(2);
	header.mSmpFrqIndex = bs.GetBits(4);
	header.mPrivateStream = bs.GetBits(1);
	header.mChannelCfg = bs.GetBits(3);
	header.mOriginality = bs.GetBits(1);
	header.mHome = bs.GetBits(1);
	header.mCopyrighted = bs.GetBits(1);
	header.mCopyrightStart = bs.GetBits(1);
	header.mFrameLength = bs.GetBits(13);
	header.mBufferFullness = bs.GetBits(11);
	header.mNumFrames = bs.GetBits(2);
	if (nDataSize >= 9 && !header.mProtAbsent)
	{
		header.mCRCifPresent = bs.GetBits(16);
	}
	return header.mSyncWord == 0xfff && header.mLayer == 0;
}

/*
According to the Audio Coding Wiki, which is no longer available as of June, 2010, the ADIF format actually is just one header at the beginning of the AAC file.
And the rest of the data are consecutive raw data blocks.
This file format is meant for simple local storing purposes unlike ADTS or LATM which are meant for streaming AAC.
The ADIF header is made up of the following Field name/Field size in bits/Comment (separated by semicolons):
	adif_id/32/Always: "ADIF";
	copyright_id_present/1/[none];
	copyright_id/72/only if copyright_id_present == 1;
	original_copy/1/[none];
	home/1/[none];
	bitstream_type/1/0: CBR, 1: VBR; bitrate/23/for CBR: bitrate, for VBR: peak bitrate, 0 means unknown;
	num_program_config_elements/4/[none].
	The next 2 fields come (num_program_config_elements+1) times: buffer_fullness/20/only if bitstream_type == 0; program_config_element/VAR/[none].
*/


#endif
