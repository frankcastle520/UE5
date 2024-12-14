// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AVConstants.h"
#include "EpicRtcVideoCommon.h"
#include "HAL/PlatformFileManager.h"
#include "IPixelCaptureOutputFrame.h"
#include "Video/Encoders/Configs/VideoEncoderConfigAV1.h"
#include "Video/Encoders/Configs/VideoEncoderConfigH264.h"
#include "Video/Encoders/Configs/VideoEncoderConfigVP8.h"
#include "Video/Encoders/Configs/VideoEncoderConfigVP9.h"
#include "Video/Resources/VideoResourceCPU.h"
#include "Video/Resources/VideoResourceRHI.h"
#include "Video/VideoEncoder.h"

#include "epic_rtc/core/video/video_encoder.h"

namespace UE::PixelStreaming2
{
	template <std::derived_from<FVideoResource> TVideoResource>
	class TEpicRtcVideoEncoder : public EpicRtcVideoEncoderInterface, public TRefCountingMixin<TEpicRtcVideoEncoder<TVideoResource>>
	{
	public:
		TEpicRtcVideoEncoder(EpicRtcVideoCodecInfoInterface* InCodecInfo);
		virtual ~TEpicRtcVideoEncoder() override;

		// Begin EpicRtcVideoEncoderInterface
		virtual EpicRtcStringView		  GetName() const override;
		virtual EpicRtcVideoEncoderConfig GetConfig() const override;
		virtual EpicRtcMediaResult		  SetConfig(const EpicRtcVideoEncoderConfig& VideoEncoderConfig) override;
		virtual EpicRtcVideoEncoderInfo	  GetInfo() override;
		virtual EpicRtcMediaResult		  Encode(const EpicRtcVideoFrame& VideoFrame, EpicRtcVideoFrameTypeArrayInterface* FrameTypes) override;
		virtual void					  RegisterCallback(EpicRtcVideoEncoderCallbackInterface* InCallback) override;
		virtual void					  Reset() override;
		// End EpicRtcVideoEncoderInterface

	private:
		EpicRtcVideoEncoderConfig								EncoderConfig;
		TUniquePtr<FVideoEncoderConfig>							InitialVideoConfig;
		TSharedPtr<TVideoEncoder<TVideoResource>>				Encoder;
		TRefCountPtr<EpicRtcVideoEncoderCallbackInterface>		VideoEncoderCallback;
		TRefCountPtr<EpicRtcVideoCodecInfoInterface>			CodecInfo;
		TRefCountPtr<FEpicRtcPixelFormatArray>					PreferredPixelFormats;
		TRefCountPtr<FEpicRtcVideoResolutionBitrateLimitsArray> ResolutionBitrateLimits;

		TOptional<uint32_t> EpicRtcTargetBitrates[Video::MaxSpatialLayers][Video::MaxTemporalStreams];
		TOptional<double>	EpicRtcTargetFramerate;

		// We store this so we can restore back to it if the user decides to use then stop using the PixelStreaming2.Encoder.TargetBitrate CVar.
		uint32_t EpicRtcProposedTargetBitrate = 5000000;

		// File handle used to save the encoded output to
		IFileHandle* FileHandle = nullptr;

	private:
		bool	 LateInitEncoder();
		void	 SetInitialSettings(const EpicRtcVideoEncoderConfig& CodecSettings, FVideoEncoderConfig& VideoConfig);
		void	 UpdateConfig(uint32 Width, uint32 Height);
		void	 MaybeDumpFrame(EpicRtcEncodedVideoFrame const& EncodedImage);
		uint32_t SumAndResetBitrates();

		void UpdateFrameMetadataPreEncode(IPixelCaptureOutputFrame& Frame);
		void UpdateFrameMetadataPostEncode(IPixelCaptureOutputFrame& Frame);
		void UpdateFrameMetadataPrePacketization(IPixelCaptureOutputFrame& Frame);
		void UpdateFrameMetadataPostPacketization(IPixelCaptureOutputFrame& Frame);

		void OnEncoderDebugDumpFrameChanged(IConsoleVariable* Var);
		void CreateDumpFile();

		FDelegateHandle DelegateHandle;

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<TEpicRtcVideoEncoder>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<TEpicRtcVideoEncoder>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<TEpicRtcVideoEncoder>::GetRefCount(); }
		// End EpicRtcRefCountInterface
	};
} // namespace UE::PixelStreaming2
