// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"

/**
 * Stat Names used by Pixel Streaming.
 */
namespace PixelStreaming2StatNames
{
	PIXELSTREAMING2_API extern const FName Bitrate;
	PIXELSTREAMING2_API extern const FName BitrateMegabits;
	PIXELSTREAMING2_API extern const FName TargetBitrateMegabits;
	PIXELSTREAMING2_API extern const FName MeanSendDelay;
	PIXELSTREAMING2_API extern const FName SourceFps;
	PIXELSTREAMING2_API extern const FName Fps;
	PIXELSTREAMING2_API extern const FName MeanEncodeTime;
	PIXELSTREAMING2_API extern const FName EncodedFramesPerSecond;
	PIXELSTREAMING2_API extern const FName DecodedFramesPerSecond;
	PIXELSTREAMING2_API extern const FName MeanQPPerSecond;
	PIXELSTREAMING2_API extern const FName FramesSentPerSecond;
	PIXELSTREAMING2_API extern const FName FramesReceivedPerSecond;
	PIXELSTREAMING2_API extern const FName JitterBufferDelay;
	PIXELSTREAMING2_API extern const FName FramesSent;
	PIXELSTREAMING2_API extern const FName FramesReceived;
	PIXELSTREAMING2_API extern const FName FramesPerSecond;
	PIXELSTREAMING2_API extern const FName FramesDecoded;
	PIXELSTREAMING2_API extern const FName FramesDropped;
	PIXELSTREAMING2_API extern const FName FramesCorrupted;
	PIXELSTREAMING2_API extern const FName PartialFramesLost;
	PIXELSTREAMING2_API extern const FName FullFramesLost;
	PIXELSTREAMING2_API extern const FName HugeFramesSent;
	PIXELSTREAMING2_API extern const FName JitterBufferTargetDelay;
	PIXELSTREAMING2_API extern const FName InterruptionCount;
	PIXELSTREAMING2_API extern const FName TotalInterruptionDuration;
	PIXELSTREAMING2_API extern const FName FreezeCount;
	PIXELSTREAMING2_API extern const FName PauseCount;
	PIXELSTREAMING2_API extern const FName TotalFreezesDuration;
	PIXELSTREAMING2_API extern const FName TotalPausesDuration;
	PIXELSTREAMING2_API extern const FName FirCount;
	PIXELSTREAMING2_API extern const FName PliCount;
	PIXELSTREAMING2_API extern const FName NackCount;
	PIXELSTREAMING2_API extern const FName RetransmittedBytesSent;
	PIXELSTREAMING2_API extern const FName TargetBitrate;
	PIXELSTREAMING2_API extern const FName TotalEncodeBytesTarget;
	PIXELSTREAMING2_API extern const FName KeyFramesEncoded;
	PIXELSTREAMING2_API extern const FName FrameWidth;
	PIXELSTREAMING2_API extern const FName FrameHeight;
	PIXELSTREAMING2_API extern const FName BytesSent;
	PIXELSTREAMING2_API extern const FName BytesReceived;
	PIXELSTREAMING2_API extern const FName QPSum;
	PIXELSTREAMING2_API extern const FName TotalEncodeTime;
	PIXELSTREAMING2_API extern const FName TotalPacketSendDelay;
	PIXELSTREAMING2_API extern const FName FramesEncoded;
	PIXELSTREAMING2_API extern const FName AvgSendDelay;
	PIXELSTREAMING2_API extern const FName MessagesSent;
	PIXELSTREAMING2_API extern const FName MessagesReceived;
	PIXELSTREAMING2_API extern const FName PacketsLost;
	PIXELSTREAMING2_API extern const FName Jitter;
	PIXELSTREAMING2_API extern const FName RoundTripTime;
	PIXELSTREAMING2_API extern const FName KeyFramesDecoded;
	PIXELSTREAMING2_API extern const FName AudioLevel;
	PIXELSTREAMING2_API extern const FName TotalSamplesDuration;
	PIXELSTREAMING2_API extern const FName AvailableOutgoingBitrate;
	PIXELSTREAMING2_API extern const FName AvailableIncomingBitrate;
	PIXELSTREAMING2_API extern const FName RetransmittedBytesReceived;
	PIXELSTREAMING2_API extern const FName RetransmittedPacketsReceived;
	PIXELSTREAMING2_API extern const FName DataChannelBytesSent;
	PIXELSTREAMING2_API extern const FName DataChannelBytesReceived;
	PIXELSTREAMING2_API extern const FName DataChannelMessagesSent;
	PIXELSTREAMING2_API extern const FName DataChannelMessagesReceived;
	PIXELSTREAMING2_API extern const FName InputController;

} // namespace PixelStreaming2StatNames
