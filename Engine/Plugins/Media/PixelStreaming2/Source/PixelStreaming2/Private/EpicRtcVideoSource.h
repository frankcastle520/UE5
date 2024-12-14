// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcVideoTrack.h"
#include "VideoCapturer.h"

namespace UE::PixelStreaming2
{
	class FVideoSourceGroup;
	/**
	 * A source of video frames for an EpicRtc peer. Has a video input which will provide
	 * frame data to this source. The source will then pass that data to an adapter
	 * which will have one or many adapt processes that are provided by the input object
	 * and are responsible for converting the frame data to the format required for
	 * the selected video encoder.
	 * This video source should be contained within a FVideoSourceGroup which is responsible
	 * for telling each source to push a frame to EpicRtc at the expected rate. This source
	 * will make sure that the adapter has valid output and if so will create a frame
	 * for EpicRtc. Otherwise it will continue to wait until the next frame.
	 */
	class FEpicRtcVideoSource : public FEpicRtcVideoTrack
	{
	public:
		static TSharedPtr<FEpicRtcVideoSource> Create(TRefCountPtr<EpicRtcVideoTrackInterface> InVideoTrack, TSharedPtr<FVideoCapturer> InVideoCapturer, TSharedPtr<FVideoSourceGroup> InVideoSourceGroup);
		virtual ~FEpicRtcVideoSource() = default;

		void PushFrame();
		void ForceKeyFrame();
		void SetMuted(bool bIsMuted);

		TRefCountPtr<EpicRtcVideoTrackInterface> GetVideoTrack();

	private:
		FEpicRtcVideoSource(TRefCountPtr<EpicRtcVideoTrackInterface> InVideoTrack, TSharedPtr<FVideoCapturer> InVideoCapturer);

	private:
		TSharedPtr<FVideoCapturer> VideoCapturer;

		bool bIsMuted;
	};
} // namespace UE::PixelStreaming2
