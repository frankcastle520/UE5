// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Set.h"
#include "EpicRtcVideoTrack.h"
#include "VideoSink.h"

namespace UE::PixelStreaming2
{
	/**
	 * Video sink class that receives a frame from EpicRtc and passes the frame to all added consumers
	 */
	class FEpicRtcVideoSink : public FVideoSink, public FEpicRtcVideoTrack
	{
	public:
		static TSharedPtr<FEpicRtcVideoSink> Create(TRefCountPtr<EpicRtcVideoTrackInterface> InTrack);
		// Note: destructor will call destroy on any attached video consumers
		virtual ~FEpicRtcVideoSink() = default;

	private:
		FEpicRtcVideoSink(TRefCountPtr<EpicRtcVideoTrackInterface> InTrack);
	};
} // namespace UE::PixelStreaming2