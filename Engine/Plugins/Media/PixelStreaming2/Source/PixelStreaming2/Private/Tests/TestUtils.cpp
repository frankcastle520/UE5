// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestUtils.h"

#include "EpicRtcVideoBufferI420.h"
#include "GenericPlatform/GenericPlatformTime.h"
#include "IPixelStreaming2Module.h"
#include "Logging.h"
#include "PixelCaptureInputFrameI420.h"
#include "PixelStreaming2PluginSettings.h"
#include "Streamer.h"
#include "VideoProducer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::PixelStreaming2
{
	int32 TestUtils::NextStreamerPort()
	{
		// Start of IANA un-registerable ports (49152 - 65535)
		static int NextStreamerPort = 49152;
		return NextStreamerPort++;
	}

	int32 TestUtils::NextPlayerPort()
	{
		// Half of IANA un-registerable ports (49152 - 65535)
		static int NextPlayerPort = 57344;
		return NextPlayerPort++;
	}

	/* ---------- Latent Automation Commands ----------- */

	bool FWaitSeconds::Update()
	{
		double DeltaTime = FPlatformTime::Seconds() - StartTime;
		if (DeltaTime > WaitSeconds)
		{
			return true;
		}
		return false;
	}

	bool FSendSolidColorFrame::Update()
	{
		TSharedPtr<FPixelCaptureBufferI420> Buffer = MakeShared<FPixelCaptureBufferI420>(FrameConfig.Width, FrameConfig.Height);

		uint8_t* yData = Buffer->GetMutableDataY();
		uint8_t* uData = Buffer->GetMutableDataU();
		uint8_t* vData = Buffer->GetMutableDataV();
		for (int y = 0; y < Buffer->GetHeight(); ++y)
		{
			for (int x = 0; x < Buffer->GetWidth(); ++x)
			{
				const int x2 = x / 2;
				const int y2 = y / 2;

				yData[x + (y * Buffer->GetStrideY())] = FrameConfig.Y;
				uData[x2 + (y2 * Buffer->GetStrideUV())] = FrameConfig.U;
				vData[x2 + (y2 * Buffer->GetStrideUV())] = FrameConfig.V;
			}
		}

		VideoProducer->PushFrame(FPixelCaptureInputFrameI420(Buffer));
		return true;
	}

	bool FSendCustomMessageToStreamer::Update()
	{
		UE_LOG(LogPixelStreaming2, Log, TEXT("FSendCustomMessageToStreamer: %s"), *MessageType);
		if (Player->DataChannelAvailable())
		{
			if (!Player->SendMessage(MessageType, Body))
			{
				UE_LOG(LogPixelStreaming2, Error, TEXT("Data channel send message failed."));
			}
		}
		else
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("No DataChannel on player."));
		}

		return true;
	}

	bool FSendDataChannelMessageToStreamer::Update()
	{
		UE_LOG(LogPixelStreaming2, Log, TEXT("SendDataChannelMessageToStreamer: %s, %s"), *MessageType, *Body);
		if (Player->DataChannelAvailable())
		{
			if (!Player->SendMessage(MessageType, Body))
			{
				UE_LOG(LogPixelStreaming2, Error, TEXT("Data channel send message failed."));
			}
		}
		else
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("No DataChannel on player."));
		}

		return true;
	}

	bool FSendDataChannelMessageFromStreamer::Update()
	{
		UE_LOG(LogPixelStreaming2, Log, TEXT("SendDataChannelMessageFromStreamer: %s, %s"), *MessageType, *Body);
		if (Streamer)
		{
			Streamer->SendAllPlayersMessage(MessageType, Body);
		}
		else
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("No DataChannel on player."));
		}

		return true;
	}

	bool FWaitForFrameReceived::Update()
	{
		if (VideoSink && VideoSink->HasReceivedFrame())
		{
			UE_LOG(LogPixelStreaming2, Log, TEXT("Successfully received streamed frame."));

			TRefCountPtr<EpicRtcVideoBufferInterface> Buffer = VideoSink->GetReceivedBuffer();

			FString WidthTestString = FString::Printf(TEXT("Expected frame res=%dx%d, actual res=%dx%d"),
				FrameConfig.Width,
				FrameConfig.Height,
				Buffer->GetWidth(),
				Buffer->GetHeight());

			if (FrameConfig.Width != Buffer->GetWidth() || FrameConfig.Height != Buffer->GetHeight())
			{
				UE_LOG(LogPixelStreaming2, Error, TEXT("%s"), *WidthTestString);
			}
			else
			{
				UE_LOG(LogPixelStreaming2, Log, TEXT("%s"), *WidthTestString);
			}

			EpicRtcPixelFormat Format = Buffer->GetFormat();
			if (Format != EpicRtcPixelFormat::I420)
			{
				UE_LOG(LogPixelStreaming2, Error, TEXT("Invalid Pixel Format"));
			}

			uint8_t* DataY = reinterpret_cast<uint8_t*>(Buffer->GetData());
			uint8_t* DataU = DataY + Buffer->GetWidth() * Buffer->GetHeight();
			uint8_t* DataV = DataU + ((Buffer->GetWidth() + 1) / 2) * ((Buffer->GetHeight() + 1) / 2);

			/* ----- Test the pixels of the received frame ---- */

			// Due this frame being a single solid color we "should" only need to look at a single element.
			FString PixelTestString = FString::Printf(TEXT("Expected solid color frame.| Expect: Y=%d, Actual: Y=%d | Expected: U=%d, Actual: U=%d | Expected: V=%d, Actual: V=%d"),
				FrameConfig.Y,
				DataY[0],
				FrameConfig.U,
				DataU[0],
				FrameConfig.V,
				DataV[0]);

			const int YDelta = FMath::Max(FrameConfig.Y, DataY[0]) - FMath::Min(FrameConfig.Y, DataY[0]);
			const int UDelta = FMath::Max(FrameConfig.U, DataU[0]) - FMath::Min(FrameConfig.U, DataU[0]);
			const int VDelta = FMath::Max(FrameConfig.V, DataV[0]) - FMath::Min(FrameConfig.V, DataV[0]);
			const int Tolerance = 10;

			// Match pixel values within a tolerance as compression can result in color variations, but not much as this is a solid color.
			if (YDelta > Tolerance || UDelta > Tolerance || VDelta > Tolerance)
			{
				UE_LOG(LogPixelStreaming2, Error, TEXT("%s"), *PixelTestString);
			}
			else
			{
				UE_LOG(LogPixelStreaming2, Log, TEXT("%s"), *PixelTestString);
			}

			// So we can use this sink for this test again if we want.
			VideoSink->ResetReceivedFrame();

			return true;
		}

		double DeltaTime = FPlatformTime::Seconds() - StartTime;
		if (DeltaTime > TimeoutSeconds)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Timed out waiting to receive a frame of video through the video sink."));
			return true;
		}
		return false;
	}

	bool FWaitForDataChannelOrTimeout::Update()
	{
		if (OutPlayer->DataChannelAvailable())
		{
			return true;
		}

		double DeltaTime = FPlatformTime::Seconds() - StartTime;
		if (DeltaTime > TimeoutSeconds)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Timed out waiting to subscribe player."));
			return true;
		}
		return false; // Not connected or timed out so run this latent test again next frame
	}

	bool FWaitForDataChannelMessageOrTimeout::Update()
	{
		double DeltaTime = FPlatformTime::Seconds() - StartTime;
		if (DeltaTime > TimeoutSeconds)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Player timed out waiting for a datachannel message."));
			return true;
		}
		return *bComplete.Get(); // Not connected or timed out so run this latent test again next frame
	}

	bool FWaitForStreamerDataChannelMessageOrTimeout::Update()
	{
		double DeltaTime = FPlatformTime::Seconds() - StartTime;
		if (DeltaTime > TimeoutSeconds)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Streamer timed out waiting for a datachannel message."));
			return true;
		}
		return *bComplete.Get(); // Not connected or timed out so run this latent test again next frame
	}

	bool FSubscribePlayerAfterStreamerConnectedOrTimeout::Update()
	{
		if (OutPlayer->Subscribe(StreamerName))
		{
			return true;
		}

		double DeltaTime = FPlatformTime::Seconds() - StartTime;
		if (DeltaTime > TimeoutSeconds)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Timed out waiting to subscribe player."));
			return true;
		}
		return false; // Not connected or timed out so run this latent test again next frame
	}

	bool FCleanupAll::Update()
	{
		if (OutPlayer)
		{
			OutPlayer.Reset();
		}

		if (OutStreamer)
		{
			OutStreamer->StopStreaming();
			OutStreamer.Reset();
		}

		if (OutSignallingServer)
		{
			OutSignallingServer->Stop();
			OutSignallingServer.Reset();
		}
		return true;
	}

	/* ---------- Utility functions ----------- */

	void SetCodec(EVideoCodec Codec)
	{
		// Set codec
		UE::PixelStreaming2::DoOnGameThreadAndWait(MAX_uint32, [Codec]() {
			UPixelStreaming2PluginSettings::CVarEncoderCodec.AsVariable()->Set(*UE::PixelStreaming2::GetCVarStringFromEnum(Codec));
		});
	}

	void SetDisableTransmitVideo(bool bDisableTransmitVideo)
	{
		// Set whether to transmit video
		UE::PixelStreaming2::DoOnGameThreadAndWait(MAX_uint32, [bDisableTransmitVideo]() {
			UPixelStreaming2PluginSettings::CVarWebRTCDisableTransmitVideo->Set(bDisableTransmitVideo, ECVF_SetByCode);
		});
	}

	TSharedPtr<IPixelStreaming2Streamer> CreateStreamer(const FString& StreamerName, int StreamerPort)
	{
		TSharedPtr<IPixelStreaming2Streamer> OutStreamer = IPixelStreaming2Module::Get().CreateStreamer(StreamerName);
		OutStreamer->SetVideoProducer(FVideoProducer::Create());
		OutStreamer->SetSignallingServerURL(FString::Printf(TEXT("ws://127.0.0.1:%d"), StreamerPort));

		OutStreamer->OnStreamingStarted().AddLambda([](IPixelStreaming2Streamer*) {
			UE_LOG(LogPixelStreaming2, Verbose, TEXT("CreateStreamer: Streamer Connected"));
		});

		return OutStreamer;
	}

	TSharedPtr<FMockPlayer> CreatePlayer()
	{
		TSharedPtr<FMockPlayer> OutPlayer = MakeShared<FMockPlayer>();

		return OutPlayer;
	}

	TSharedPtr<UE::PixelStreaming2Servers::IServer> CreateSignallingServer(int StreamerPort, int PlayerPort)
	{
		// Make signalling server
		TSharedPtr<UE::PixelStreaming2Servers::IServer> OutSignallingServer = UE::PixelStreaming2Servers::MakeSignallingServer();

		UE::PixelStreaming2Servers::FLaunchArgs LaunchArgs;
		LaunchArgs.ProcessArgs = FString::Printf(TEXT("--StreamerPort=%d --HttpPort=%d"), StreamerPort, PlayerPort);
		bool bLaunchedSignallingServer = OutSignallingServer->Launch(LaunchArgs);
		if (!bLaunchedSignallingServer)
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Failed to launch signalling server."));
		}
		UE_LOG(LogPixelStreaming2, Log, TEXT("Signalling server launched=%s"), bLaunchedSignallingServer ? TEXT("true") : TEXT("false"));
		return OutSignallingServer;
	}
} // namespace UE::PixelStreaming2

#endif // WITH_DEV_AUTOMATION_TESTS
