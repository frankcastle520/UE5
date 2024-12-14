// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoDecoderInputBitstreamProcessorH264.h"

#include "Utilities/Utilities.h"
#include "Utils/MPEG/ElectraUtilsMPEGVideo_H264.h"
#include "StreamAccessUnitBuffer.h"
#include "Decoder/VideoDecoderHelpers.h"

#include "ElectraDecodersUtils.h"
#include "IElectraDecoderFeaturesAndOptions.h"
#include "IElectraDecoderOutputVideo.h"

namespace Electra
{


class FVideoDecoderInputBitstreamProcessorH264 : public IVideoDecoderInputBitstreamProcessor
{
public:
	FVideoDecoderInputBitstreamProcessorH264(const TMap<FString, FVariant>& InDecoderConfigOptions);
	virtual ~FVideoDecoderInputBitstreamProcessorH264() = default;
	void Clear() override;
	EProcessResult ProcessAccessUnitForDecoding(FBitstreamInfo& OutBSI, FAccessUnit* InAccessUnit) override;
	void SetPropertiesOnOutput(TSharedPtr<IElectraDecoderVideoOutput, ESPMode::ThreadSafe> InDecoderOutput, FParamDict* InOutProperties, const FBitstreamInfo& InFromBSI) override;

private:
	class FCodecSpecificMessages : public ICodecSpecificMessages
	{
	public:
		virtual ~FCodecSpecificMessages() = default;
		TMap<uint32, ElectraDecodersUtil::MPEG::H264::FSequenceParameterSet> SPSs;
		TArray<ElectraDecodersUtil::MPEG::FSEIMessage> SEIMessages;
	};

	bool bReplaceLengthWithStartcode = true;

	TSharedPtr<const FAccessUnit::CodecData, ESPMode::ThreadSafe> PreviousCodecData;
	TSharedPtr<const FAccessUnit::CodecData, ESPMode::ThreadSafe> CurrentCodecData;
	TMap<uint32, ElectraDecodersUtil::MPEG::H264::FSequenceParameterSet> SPSs;

	MPEG::FColorimetryHelper Colorimetry;

	bool HandlePicTimingSEI(FParamDict* InOutProperties, const ElectraDecodersUtil::MPEG::FSEIMessage& InSEI, const TMap<uint32, ElectraDecodersUtil::MPEG::H264::FSequenceParameterSet>& InSPSs);
};





TSharedPtr<IVideoDecoderInputBitstreamProcessor, ESPMode::ThreadSafe> IVideoDecoderInputBitstreamProcessorH264::Create(const FString& InCodec, const TMap<FString, FVariant>& InDecoderConfigOptions)
{
	check(InCodec.StartsWith(TEXT("avc")));
	return MakeShared<FVideoDecoderInputBitstreamProcessorH264, ESPMode::ThreadSafe>(InDecoderConfigOptions);
}


FVideoDecoderInputBitstreamProcessorH264::FVideoDecoderInputBitstreamProcessorH264(const TMap<FString, FVariant>& InDecoderConfigOptions)
{
	int32 S2L = (int32)ElectraDecodersUtil::GetVariantValueSafeI64(InDecoderConfigOptions, IElectraDecoderFeature::StartcodeToLength, -1);
	check(S2L == -1 || S2L == 0);

	bReplaceLengthWithStartcode = S2L == -1;
}

void FVideoDecoderInputBitstreamProcessorH264::Clear()
{
	PreviousCodecData.Reset();
	CurrentCodecData.Reset();
	SPSs.Empty();
	Colorimetry.Reset();
}

IVideoDecoderInputBitstreamProcessor::EProcessResult FVideoDecoderInputBitstreamProcessorH264::ProcessAccessUnitForDecoding(FBitstreamInfo& OutBSI, FAccessUnit* InOutAccessUnit)
{
	IVideoDecoderInputBitstreamProcessor::EProcessResult Result = IVideoDecoderInputBitstreamProcessor::EProcessResult::None;
	if (!InOutAccessUnit)
	{
		return Result;
	}
	check(InOutAccessUnit->ESType == EStreamType::Video);

	TSharedPtr<FCodecSpecificMessages> Msgs = StaticCastSharedPtr<FCodecSpecificMessages>(OutBSI.CodecSpecificMessages);
	if (!Msgs.IsValid())
	{
		Msgs = MakeShared<FCodecSpecificMessages>();
		OutBSI.CodecSpecificMessages = Msgs;
	}

	//
	// Extract sequence parameter sets from the codec specific data.
	//
	if (InOutAccessUnit->AUCodecData.IsValid() && InOutAccessUnit->AUCodecData.Get() != CurrentCodecData.Get())
	{
		// Pointers are different. Is the content too?
		bool bDifferent = !CurrentCodecData.IsValid() || (CurrentCodecData.IsValid() && InOutAccessUnit->AUCodecData->CodecSpecificData != CurrentCodecData->CodecSpecificData);
		if (bDifferent)
		{
			SPSs.Empty();
			TArray<ElectraDecodersUtil::MPEG::FNaluInfo> NALUs;
			const uint8* pD = InOutAccessUnit->AUCodecData->CodecSpecificData.GetData();
			ElectraDecodersUtil::MPEG::ParseBitstreamForNALUs(NALUs, pD, InOutAccessUnit->AUCodecData->CodecSpecificData.Num());
			for(int32 i=0; i<NALUs.Num(); ++i)
			{
				const uint8* NALU = (const uint8*)Electra::AdvancePointer(pD, NALUs[i].Offset + NALUs[i].UnitLength);
				uint8 nal_unit_type = *NALU & 0x1f;
				// SPS?
				if (nal_unit_type == 7)
				{
					bool bOk = ElectraDecodersUtil::MPEG::H264::ParseSequenceParameterSet(SPSs, NALU, NALUs[i].Size);
					check(bOk); (void)bOk;
				}
			}
			Result = IVideoDecoderInputBitstreamProcessor::EProcessResult::CSDChanged;
		}
		PreviousCodecData = CurrentCodecData.IsValid() ? CurrentCodecData : InOutAccessUnit->AUCodecData;
		CurrentCodecData = InOutAccessUnit->AUCodecData;
	}

	// Set the messages from the CSD on the access unit.
	Msgs->SPSs = SPSs;

	// NOTE: In a second phase we should probably scan the input access unit for inband SPS if the codec is avc3.

	// Now go over the NALUs in the access unit and see what is there.
	OutBSI.bIsDiscardable = true;
	OutBSI.bIsSyncFrame = InOutAccessUnit->bIsSyncSample;
	uint32* NALU = (uint32*)InOutAccessUnit->AUData;
	uint32* LastNALU = (uint32*)Electra::AdvancePointer(NALU, InOutAccessUnit->AUSize);
	while(NALU < LastNALU)
	{
		uint32 naluLen = MEDIA_FROM_BIG_ENDIAN(*NALU);

		// Check the nal_ref_idc in the NAL unit for dependencies.
		uint8 nal = *(const uint8*)(NALU + 1);
		check((nal & 0x80) == 0);
		if ((nal >> 5) != 0)
		{
			OutBSI.bIsDiscardable = false;
		}
		// IDR frame?
		if ((nal & 0x1f) == 5)
		{
			OutBSI.bIsSyncFrame = true;
		}
		// SEI message(s)?
		if ((nal & 0x1f) == 6)
		{
			ElectraDecodersUtil::MPEG::ExtractSEIMessages(Msgs->SEIMessages, Electra::AdvancePointer(NALU, 5), naluLen-1, ElectraDecodersUtil::MPEG::ESEIStreamType::H264, false);
		}

		if (bReplaceLengthWithStartcode)
		{
			*NALU = MEDIA_TO_BIG_ENDIAN(0x00000001U);
		}
		NALU = Electra::AdvancePointer(NALU, naluLen + 4);
	}

	return Result;
}


void FVideoDecoderInputBitstreamProcessorH264::SetPropertiesOnOutput(TSharedPtr<IElectraDecoderVideoOutput, ESPMode::ThreadSafe> InDecoderOutput, FParamDict* InOutProperties, const FBitstreamInfo& InFromBSI)
{
	if (!InOutProperties)
	{
		return;
	}

	TSharedPtr<FCodecSpecificMessages> Msg = StaticCastSharedPtr<FCodecSpecificMessages>(InFromBSI.CodecSpecificMessages);
	const TMap<uint32, ElectraDecodersUtil::MPEG::H264::FSequenceParameterSet>& SPSMap = Msg.IsValid() ? Msg->SPSs : SPSs;

	// We only interact with the first SPS.
	if (SPSMap.Num() > 0)
	{
		auto It = SPSMap.CreateConstIterator();
		const ElectraDecodersUtil::MPEG::H264::FSequenceParameterSet& sps = It->Value;
		// Set the bit depth and the colorimetry.
		uint8 colour_primaries=2, transfer_characteristics=2, matrix_coeffs=2;
		uint8 video_full_range_flag=0, video_format=5;
		if (sps.colour_description_present_flag)
		{
			colour_primaries = sps.colour_primaries;
			transfer_characteristics = sps.transfer_characteristics;
			matrix_coeffs = sps.matrix_coefficients;
		}
		if (sps.video_signal_type_present_flag)
		{
			video_full_range_flag = sps.video_full_range_flag;
			video_format = sps.video_format;
		}

		Colorimetry.Update(colour_primaries, transfer_characteristics, matrix_coeffs, video_full_range_flag, video_format);
		Colorimetry.UpdateParamDict(*InOutProperties);
	}

	// Handle SEI messages we are interested in
	for(int32 i=0, iMax=Msg.IsValid()?Msg->SEIMessages.Num():0; i<iMax; ++i)
	{
		const ElectraDecodersUtil::MPEG::FSEIMessage& sei = Msg->SEIMessages[i];
		switch(sei.PayloadType)
		{
			case 1:	// pic_timing()
			{
				HandlePicTimingSEI(InOutProperties, sei, SPSMap);
				break;
			}
			default:
			{
				break;
			}
		}
	}
}

bool FVideoDecoderInputBitstreamProcessorH264::HandlePicTimingSEI(FParamDict* InOutProperties, const ElectraDecodersUtil::MPEG::FSEIMessage& InSEI, const TMap<uint32, ElectraDecodersUtil::MPEG::H264::FSequenceParameterSet>& InSPSs)
{
	// Parsing the pic_timing() SEI message requires the active SPS.
	// NOTE: We do NOT know which SPS is active, if there are several as the activation is determined by the slice being decoded.
	if (InSPSs.Num() != 1)
	{
		return false;
	}

	auto It = InSPSs.CreateConstIterator();
	const ElectraDecodersUtil::MPEG::H264::FSequenceParameterSet& sps = It->Value;

	ElectraDecodersUtil::MPEG::H264::FBitstreamReader br(InSEI.Message.GetData(), InSEI.Message.Num());

	IVideoDecoderTimecode::FMPEGDefinition ClockTimestamp[3];

	const bool CpbDpbDelaysPresentFlag = sps.nal_hrd_parameters_present_flag || sps.vcl_hrd_parameters_present_flag;
	if (CpbDpbDelaysPresentFlag)
	{
		int32 NumBits1 = 1 + (sps.nal_hrd_parameters_present_flag ? sps.nal_hrd_parameters.cpb_removal_delay_length_minus1 : sps.vcl_hrd_parameters.cpb_removal_delay_length_minus1);
		int32 NumBits2 = 1 + (sps.nal_hrd_parameters_present_flag ? sps.nal_hrd_parameters.dpb_output_delay_length_minus1 : sps.vcl_hrd_parameters.dpb_output_delay_length_minus1);
		uint32 cpb_removal_delay = br.GetBits(NumBits1);
		uint32 dpb_output_delay = br.GetBits(NumBits2);
		(void)cpb_removal_delay;
		(void)dpb_output_delay;
	}
	if (sps.pic_struct_present_flag)
	{
		static const uint8 kNumClockTS[9] = { 1, 1, 1, 2, 2, 3, 3, 2, 3 };
		uint32 pic_struct = br.GetBits(4);
		if (pic_struct > 8)
		{
			return false;
		}
		for(int32 nc=0,ncMax=kNumClockTS[pic_struct]; nc<ncMax; ++nc)
		{
			IVideoDecoderTimecode::FMPEGDefinition& ct(ClockTimestamp[nc]);
			ct.FromH26x = 4;
			ct.clock_timestamp_flag = (uint8) br.GetBits(1);
			if (ct.clock_timestamp_flag)
			{
				// Set the timing values from the SPS.
				ct.timing_info_present_flag = sps.timing_info_present_flag;
				ct.num_units_in_tick = sps.num_units_in_tick;
				ct.time_scale = sps.time_scale;

				// Read the values from the message.
				ct.ct_type = (uint8) br.GetBits(2);
				ct.nuit_field_based_flag = (uint8) br.GetBits(1);
				ct.counting_type = (uint8) br.GetBits(5);
				ct.full_timestamp_flag = (uint8) br.GetBits(1);
				ct.discontinuity_flag = (uint8) br.GetBits(1);
				ct.cnt_dropped_flag = (uint8) br.GetBits(1);
				ct.n_frames = (uint16) br.GetBits(8);
				if (ct.full_timestamp_flag)
				{
					ct.seconds_value = (uint8) br.GetBits(6);
					ct.minutes_value = (uint8) br.GetBits(6);
					ct.hours_value = (uint8) br.GetBits(5);
				}
				else
				{
					// seconds_flag
					if (br.GetBits(1))
					{
						ct.seconds_value = (uint8) br.GetBits(6);
						// minutes_flag
						if (br.GetBits(1))
						{
							ct.minutes_value = (uint8) br.GetBits(6);
							// hours_flag
							if (br.GetBits(1))
							{
								ct.hours_value = (uint8) br.GetBits(5);
							}
						}
					}
				}
				uint32 time_offset_length = sps.nal_hrd_parameters_present_flag ? sps.nal_hrd_parameters.time_offset_length : sps.vcl_hrd_parameters_present_flag ? sps.vcl_hrd_parameters.time_offset_length : 24;
				uint32 time_offset = br.GetBits(time_offset_length);
				ct.time_offset = (int32)(time_offset << (32 - time_offset_length)) >> (32 - time_offset_length);

				if (ct.timing_info_present_flag)
				{
					ct.clockTimestamp = (((int64)ct.hours_value * 60 + ct.minutes_value) * 60 + ct.seconds_value) * ct.time_scale + ct.n_frames * (ct.num_units_in_tick * (ct.nuit_field_based_flag + 1)) + ct.time_offset;
				}
			}
		}

		TSharedPtr<MPEG::FVideoDecoderTimecode, ESPMode::ThreadSafe> NewTimecode = MakeShared<MPEG::FVideoDecoderTimecode, ESPMode::ThreadSafe>();
		// Set from the first clock since we are only dealing with progressive frames, not interlaced fields.
		NewTimecode->Update(ClockTimestamp[0]);
		InOutProperties->Set(IDecoderOutputOptionNames::Timecode, FVariantValue(NewTimecode));
	}
	return true;
}

} // namespace Electra
