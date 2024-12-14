// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreaming2Streamer.h"
#include "Misc/AutomationTest.h"
#include "MockPlayer.h"
#include "PixelStreaming2Servers.h"
#include "VideoProducer.h"
#include "Video/VideoConfig.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::PixelStreaming2
{
	namespace TestUtils
	{
		int32 NextStreamerPort();
		int32 NextPlayerPort();
	} // namespace TestUtils

	// Equivalent to DEFINE_LATENT_AUTOMATION_COMMAND_FOUR_PARAMETER, but instead we define a custom constructor
	class FWaitForDataChannelMessageOrTimeout : public IAutomationLatentCommand
	{
	public:
		FWaitForDataChannelMessageOrTimeout(double InTimeoutSeconds, TSharedPtr<FMockPlayer> InPlayer, TFunction<void(const TArray<uint8>&)>& InCallback, TSharedPtr<bool> InbComplete)
			: TimeoutSeconds(InTimeoutSeconds)
			, Player(InPlayer)
			, Callback(InCallback)
			, bComplete(InbComplete)
		{
			MessageReceivedHandle = Player->OnMessageReceived.AddLambda([this](const TArray<uint8>& RawBuffer) {
				(Callback)(RawBuffer);
			});
		}

		virtual ~FWaitForDataChannelMessageOrTimeout()
		{
			Player->OnMessageReceived.Remove(MessageReceivedHandle);
		}
		virtual bool Update() override;

	private:
		double								  TimeoutSeconds;
		TSharedPtr<FMockPlayer>				  Player;
		TFunction<void(const TArray<uint8>&)> Callback;
		TSharedPtr<bool>					  bComplete;
		FDelegateHandle						  MessageReceivedHandle;
	};

	class FWaitForStreamerDataChannelMessageOrTimeout : public IAutomationLatentCommand
	{
	public:
		FWaitForStreamerDataChannelMessageOrTimeout(double InTimeoutSeconds, TSharedPtr<IPixelStreaming2Streamer> InStreamer, TSharedPtr<bool> InbComplete)
			: TimeoutSeconds(InTimeoutSeconds)
			, Streamer(InStreamer)
			, bComplete(InbComplete)
		{
		}

		virtual ~FWaitForStreamerDataChannelMessageOrTimeout()
		{
		}
		virtual bool Update() override;

	private:
		double									   TimeoutSeconds;
		TSharedPtr<IPixelStreaming2Streamer> Streamer;
		TSharedPtr<bool>						   bComplete;
	};

	DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FWaitSeconds, double, WaitSeconds);
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FSendSolidColorFrame, TSharedPtr<FVideoProducer>, VideoProducer, FMockVideoFrameConfig, FrameConfig);
	DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FWaitForFrameReceived, double, TimeoutSeconds, TSharedPtr<FMockVideoSink>, VideoSink, FMockVideoFrameConfig, FrameConfig);
	DEFINE_LATENT_AUTOMATION_COMMAND_FOUR_PARAMETER(FSubscribePlayerAfterStreamerConnectedOrTimeout, double, TimeoutSeconds, TSharedPtr<IPixelStreaming2Streamer>, OutStreamer, TSharedPtr<FMockPlayer>, OutPlayer, FString, StreamerName);
	DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FCleanupAll, TSharedPtr<UE::PixelStreaming2Servers::IServer>, OutSignallingServer, TSharedPtr<IPixelStreaming2Streamer>, OutStreamer, TSharedPtr<FMockPlayer>, OutPlayer);
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FWaitForDataChannelOrTimeout, double, TimeoutSeconds, TSharedPtr<FMockPlayer>, OutPlayer);
	DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FSendDataChannelMessageToStreamer, TSharedPtr<FMockPlayer>, Player, FString, MessageType, const FString, Body);
	DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FSendDataChannelMessageFromStreamer, TSharedPtr<IPixelStreaming2Streamer>, Streamer, FString, MessageType, const FString, Body);
	DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FSendCustomMessageToStreamer, TSharedPtr<FMockPlayer>, Player, FString, MessageType, uint16, Body);

	TSharedPtr<IPixelStreaming2Streamer>			  CreateStreamer(const FString& StreamerName, int StreamerPort);
	TSharedPtr<FMockPlayer>								  CreatePlayer();
	TSharedPtr<UE::PixelStreaming2Servers::IServer> CreateSignallingServer(int StreamerPort, int PlayerPort);
	void												  SetCodec(EVideoCodec Codec);
	void												  SetDisableTransmitVideo(bool bDisableTransmitVideo);
} // namespace UE::PixelStreaming2

#endif // WITH_DEV_AUTOMATION_TESTS