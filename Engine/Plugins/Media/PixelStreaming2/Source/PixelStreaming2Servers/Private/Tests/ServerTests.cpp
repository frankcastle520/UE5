// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "PixelStreaming2Servers.h"
#include "Misc/Paths.h"
#include "Logging.h"
#include "ServerUtils.h"
#include "WebSocketProbe.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::PixelStreaming2Servers
{
	static const int	 HttpPort = 85;
	static const FString ExpectedWebserverAddress = FString::Printf(TEXT("http://127.0.0.1:%d"), HttpPort);
	static const FString ExpectedPlayerWSAddress = FString::Printf(TEXT("ws://127.0.0.1:%d"), HttpPort);
	static const int	 SFUPort = 8889;
	static const FString ExpectedSFUAddress = FString::Printf(TEXT("ws://127.0.0.1:%d"), SFUPort);

	static const int	 StreamerPort = 8989;
	static const FString ExpectedStreamerAddress = FString::Printf(TEXT("ws://127.0.0.1:%d"), StreamerPort);

	static const bool bTestServerBinary = false;

	DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FWaitForServerOrTimeout, TSharedPtr<IServer>, Server);
	bool FWaitForServerOrTimeout::Update()
	{
		if (Server)
		{
			return Server->IsTimedOut() || Server->IsReady();
		}
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FCleanupServer, TSharedPtr<IServer>, Server);
	bool FCleanupServer::Update()
	{
		if (Server)
		{
			Server->Stop();
		}
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCheckNumStreamers, TSharedPtr<IServer>, Server, uint16, ExpectedNumStreamers);
	bool FCheckNumStreamers::Update()
	{
		if (Server && Server->IsReady())
		{
			uint16 ActualNumberOfStreamers = 0;

			Server->GetNumStreamers([&ActualNumberOfStreamers](uint16 NumStreamers) {
				ActualNumberOfStreamers = NumStreamers;
			});

			bool	bTestPassed = ActualNumberOfStreamers == ExpectedNumStreamers;
			FString LogString = FString::Printf(TEXT("Testing num ws connections. Actual=%d | Expected=%d"), ActualNumberOfStreamers, ExpectedNumStreamers);
			if (bTestPassed)
			{
				UE_LOG(LogPixelStreaming2Servers, Log, TEXT("Success: %s"), *LogString);
				return true;
			}
			else if (GetCurrentRunTime() > 2.0f)
			{
				UE_LOG(LogPixelStreaming2Servers, Error, TEXT("Failed (timed out after 2s): %s"), *LogString);
				return true;
			}
		}
		return false;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FStartWebsocketClient, TSharedPtr<IServer>, Server, TSharedPtr<FWebSocketProbe>, Probe, FURL, WSStreamerURL);
	bool FStartWebsocketClient::Update()
	{
		if (GetCurrentRunTime() > 2.0f)
		{
			UE_LOG(LogPixelStreaming2Servers, Error, TEXT("Timed out after 2s of waiting for websocket to connect"));
			return true;
		}

		// Do not proceed further into the test if server is not ready
		if (!Server || !Server->IsReady())
		{
			return false;
		}

		if (!Probe)
		{
			return false;
		}

		if (Probe->Probe())
		{
			UE_LOG(LogPixelStreaming2Servers, Log, TEXT("Websocket client probe connected."));
			return true;
		}
		else
		{
			return false;
		}
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCloseWebsocketClient, TSharedPtr<IServer>, Server, TSharedPtr<FWebSocketProbe>, Probe);
	bool FCloseWebsocketClient::Update()
	{
		if (GetCurrentRunTime() > 2.0f)
		{
			UE_LOG(LogPixelStreaming2Servers, Error, TEXT("Timed out after 2s of waiting for websocket to close"));
			return true;
		}

		// Do not proceed further into the test if server is not ready
		if (!Server || !Server->IsReady())
		{
			return false;
		}

		// Do not proceed further into the test if probe is not setup
		if (!Probe)
		{
			return false;
		}

		if (Probe->IsConnected())
		{
			Probe->Close();
			UE_LOG(LogPixelStreaming2Servers, Log, TEXT("Asked websocket client probe to close."));
			return false;
		}
		else
		{
			UE_LOG(LogPixelStreaming2Servers, Log, TEXT("Websocket client probe closed."));
			return true;
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPS2LaunchEmbeddedCirrus, "System.Plugins.PixelStreaming2.FPS2LaunchEmbeddedCirrus", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
	bool FPS2LaunchEmbeddedCirrus::RunTest(const FString& Parameters)
	{
		UE_LOG(LogPixelStreaming2Servers, Log, TEXT("----------- LaunchEmbeddedCirrusTest -----------"));

		TSharedPtr<IServer> SignallingServer = MakeSignallingServer();
		FLaunchArgs			LaunchArgs;
		LaunchArgs.bPollUntilReady = true;
		LaunchArgs.ReconnectionTimeoutSeconds = 30.0f;
		LaunchArgs.ReconnectionIntervalSeconds = 2.0f;
		LaunchArgs.ProcessArgs = FString::Printf(TEXT("--HttpPort=%d --StreamerPort=%d"), HttpPort, StreamerPort);

		bool bLaunched = SignallingServer->Launch(LaunchArgs);
		UE_LOG(LogPixelStreaming2Servers, Log, TEXT("Embedded cirrus launched: %s"), bLaunched ? TEXT("true") : TEXT("false"));
		TestTrue("Embedded cirrus launched.", bLaunched);

		if (!bLaunched)
		{
			return false;
		}

		SignallingServer->OnReady.AddLambda([this](TMap<EEndpoint, FURL> Endpoints) {
			TestTrue("Got server OnReady.", true);

			FString ActualWebserverUrl = Utils::ToString(Endpoints[EEndpoint::Signalling_Webserver]);
			UE_LOG(LogPixelStreaming2Servers, Log, TEXT("Http address for webserver. Actual=%s | Expected=%s"), *ActualWebserverUrl, *ExpectedWebserverAddress);
			TestTrue(FString::Printf(TEXT("Http address for webserver. Actual=%s | Expected=%s"), *ActualWebserverUrl, *ExpectedWebserverAddress),
				ActualWebserverUrl == ExpectedWebserverAddress);

			FString ActualStreamerUrl = Utils::ToString(Endpoints[EEndpoint::Signalling_Streamer]);
			UE_LOG(LogPixelStreaming2Servers, Log, TEXT("Websocket address for streamer messages. Actual=%s | Expected=%s"), *ActualStreamerUrl, *ExpectedStreamerAddress);
			TestTrue(FString::Printf(TEXT("Websocket address for streamer messages. Actual=%s | Expected=%s"), *ActualStreamerUrl, *ExpectedStreamerAddress),
				ActualStreamerUrl == ExpectedStreamerAddress);

			FString ActualPlayersUrl = Utils::ToString(Endpoints[EEndpoint::Signalling_Players]);
			UE_LOG(LogPixelStreaming2Servers, Log, TEXT("Websocket address for player messages. Actual=%s | Expected=%s"), *ActualPlayersUrl, *ExpectedPlayerWSAddress);
			TestTrue(FString::Printf(TEXT("Websocket address for player messages. Actual=%s | Expected=%s"), *ActualPlayersUrl, *ExpectedPlayerWSAddress),
				ActualPlayersUrl == ExpectedPlayerWSAddress);
		});

		SignallingServer->OnFailedToReady.AddLambda([this]() {
			TestTrue("Server was not ready.", false);
		});

		ADD_LATENT_AUTOMATION_COMMAND(FWaitForServerOrTimeout(SignallingServer));
		ADD_LATENT_AUTOMATION_COMMAND(FCleanupServer(SignallingServer));

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPS2TwoWebsocketToEmbeddedCirrusTest, "System.Plugins.PixelStreaming2.FPS2TwoWebsocketToEmbeddedCirrusTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
	bool FPS2TwoWebsocketToEmbeddedCirrusTest::RunTest(const FString& Parameters)
	{
		UE_LOG(LogPixelStreaming2Servers, Log, TEXT("----------- TwoWebsocketToEmbeddedCirrus -----------"));

		TSharedPtr<IServer> SignallingServer = MakeSignallingServer();
		FLaunchArgs			LaunchArgs;
		LaunchArgs.bPollUntilReady = true;
		LaunchArgs.ReconnectionTimeoutSeconds = 30.0f;
		LaunchArgs.ReconnectionIntervalSeconds = 2.0f;
		LaunchArgs.ProcessArgs = FString::Printf(TEXT("--HttpPort=%d --StreamerPort=%d"), HttpPort, StreamerPort);

		bool bLaunched = SignallingServer->Launch(LaunchArgs);
		UE_LOG(LogPixelStreaming2Servers, Log, TEXT("Embedded cirrus launched: %s"), bLaunched ? TEXT("true") : TEXT("false"));
		TestTrue("Embedded cirrus launched.", bLaunched);

		if (!bLaunched)
		{
			return false;
		}

		FURL WSStreamerURL;
		WSStreamerURL.Protocol = TEXT("ws");
		WSStreamerURL.Host = TEXT("127.0.0.1");
		WSStreamerURL.Port = StreamerPort;
		WSStreamerURL.Map = FString();

		SignallingServer->OnReady.AddLambda([this](TMap<EEndpoint, FURL> Endpoints) {
			TestTrue("Got server OnReady.", true);
		});

		SignallingServer->OnFailedToReady.AddLambda([this]() {
			TestTrue("Server was not ready.", false);
		});

		// These websocket clients will be used to test number of connections
		TArray<FString> Protocols;
		Protocols.Add(FString(TEXT("binary")));
		TSharedPtr<FWebSocketProbe> Client1 = TSharedPtr<FWebSocketProbe>(new FWebSocketProbe(WSStreamerURL, Protocols));
		TSharedPtr<FWebSocketProbe> Client2 = TSharedPtr<FWebSocketProbe>(new FWebSocketProbe(WSStreamerURL, Protocols));

		ADD_LATENT_AUTOMATION_COMMAND(FWaitForServerOrTimeout(SignallingServer));
		// Test that there should be zero streamers connected after the server is initially up
		ADD_LATENT_AUTOMATION_COMMAND(FCheckNumStreamers(SignallingServer, 0));
		// Start ws client 1
		ADD_LATENT_AUTOMATION_COMMAND(FStartWebsocketClient(SignallingServer, Client1, WSStreamerURL));
		// Check num streamers is 1
		ADD_LATENT_AUTOMATION_COMMAND(FCheckNumStreamers(SignallingServer, 1));
		// Start ws client 2
		ADD_LATENT_AUTOMATION_COMMAND(FStartWebsocketClient(SignallingServer, Client2, WSStreamerURL));
		// Check num streamers = 2
		ADD_LATENT_AUTOMATION_COMMAND(FCheckNumStreamers(SignallingServer, 2));
		// Close client 1
		ADD_LATENT_AUTOMATION_COMMAND(FCloseWebsocketClient(SignallingServer, Client1));
		// Check num streamers is 1
		ADD_LATENT_AUTOMATION_COMMAND(FCheckNumStreamers(SignallingServer, 1));
		// Close client 2
		ADD_LATENT_AUTOMATION_COMMAND(FCloseWebsocketClient(SignallingServer, Client2));
		// Check num streamers is 0
		ADD_LATENT_AUTOMATION_COMMAND(FCheckNumStreamers(SignallingServer, 0));
		// Shut down server
		ADD_LATENT_AUTOMATION_COMMAND(FCleanupServer(SignallingServer));
		return true;
	}

} // namespace UE::PixelStreaming2Servers

#endif // WITH_DEV_AUTOMATION_TESTS