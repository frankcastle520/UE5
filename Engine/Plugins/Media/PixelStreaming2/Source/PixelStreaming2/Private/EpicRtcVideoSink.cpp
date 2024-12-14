// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcVideoSink.h"

namespace UE::PixelStreaming2 
{
	TSharedPtr<FEpicRtcVideoSink> FEpicRtcVideoSink::Create(TRefCountPtr<EpicRtcVideoTrackInterface> InTrack)
	{
		return TSharedPtr<FEpicRtcVideoSink>(new FEpicRtcVideoSink(InTrack));
	}

	FEpicRtcVideoSink::FEpicRtcVideoSink(TRefCountPtr<EpicRtcVideoTrackInterface> InTrack)
	{
		Track = InTrack;
	}
} // namespace UE::PixelStreaming2