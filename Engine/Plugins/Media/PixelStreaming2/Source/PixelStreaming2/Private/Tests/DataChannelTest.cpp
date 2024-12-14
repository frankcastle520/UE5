// Copyright Epic Games, Inc. All Rights Reserved.

#include "IPixelStreaming2InputModule.h"
#include "IPixelStreaming2Module.h"
#include "IPixelStreaming2Streamer.h"
#include "Logging.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::PixelStreaming2
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPS2DataChannelEchoTest, "System.Plugins.PixelStreaming2.FPS2DataChannelEchoTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
	bool FPS2DataChannelEchoTest::RunTest(const FString& Parameters)
	{
		// need to be able to accept codec to handshake otherwise setting local description fails when generating an answer
		SetCodec(EVideoCodec::VP8);

		int32 StreamerPort = TestUtils::NextStreamerPort();
		int32 PlayerPort = TestUtils::NextPlayerPort();

		TSharedPtr<UE::PixelStreaming2Servers::IServer> SignallingServer = CreateSignallingServer(StreamerPort, PlayerPort);

		FString									   StreamerName(FString::Printf(TEXT("MockStreamer%d"), StreamerPort));
		TSharedPtr<IPixelStreaming2Streamer> Streamer = CreateStreamer(StreamerName, StreamerPort);

		TSharedPtr<FMockPlayer>	   Player = CreatePlayer();
		TSharedPtr<FMockVideoSink> VideoSink = Player->GetVideoSink();

		TSharedPtr<IPixelStreaming2InputHandler> InputHandler = Streamer->GetInputHandler().Pin();

		uint8	FromStreamerEchoId = InputHandler->GetFromStreamerProtocol()->Find(EPixelStreaming2FromStreamerMessage::TestEcho)->GetID();
		FString EchoFromStreamerContent = TEXT("StreamWillEchoThis");

		Streamer->StartStreaming();

		if (!Streamer->IsStreaming())
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("Streamer is not connected."));
			return false;
		}

		// Setup listener on the Player side for "FromStreamer" messages
		TSharedPtr<bool>					  bGotMessageFromStreamer = MakeShared<bool>(false);
		TFunction<void(const TArray<uint8>&)> Callback = [this, bGotMessageFromStreamer, FromStreamerEchoId, EchoFromStreamerContent](const TArray<uint8>& RawBuffer) {
			const uint8 Type = RawBuffer[0];

			if (Type == FromStreamerEchoId && RawBuffer.Num() > 1)
			{
				*bGotMessageFromStreamer.Get() = true;
				const size_t  DescriptorSize = (RawBuffer.Num() - 1) / sizeof(TCHAR);
				const TCHAR*  DescPtr = reinterpret_cast<const TCHAR*>(RawBuffer.GetData() + 1);
				const FString Message(DescriptorSize, DescPtr);
				TestTrue(FString::Printf(TEXT("Got message from streamer (%s), expected (%s)."), *Message, *EchoFromStreamerContent), Message == EchoFromStreamerContent);
			}
		};
		Player->OnMessageReceived.AddLambda(Callback);
		Player->Connect(PlayerPort);

		// The streamer send an "echo" message to the player. The player then sends this message back to the streamer and we check that we receive this echo.
		ADD_LATENT_AUTOMATION_COMMAND(FSubscribePlayerAfterStreamerConnectedOrTimeout(5.0, Streamer, Player, StreamerName));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForDataChannelOrTimeout(5.0, Player));
		ADD_LATENT_AUTOMATION_COMMAND(FSendDataChannelMessageToStreamer(Player, EPixelStreaming2ToStreamerMessage::TestEcho, EchoFromStreamerContent));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForDataChannelMessageOrTimeout(15.0, Player, Callback, bGotMessageFromStreamer));
		ADD_LATENT_AUTOMATION_COMMAND(FCleanupAll(SignallingServer, Streamer, Player));
		return true;
	}
} // namespace UE::PixelStreaming2

#endif // WITH_DEV_AUTOMATION_TESTS
