// Copyright Epic Games, Inc. All Rights Reserved.

#include "MultiServerNode.h"
#include "MultiServerBeaconHost.h"
#include "MultiServerBeaconHostObject.h"
#include "MultiServerBeaconClient.h"
#include "MultiServerPeerConnection.h"
#include "MultiServerReplicationTypes.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MultiServerNode)

void UMultiServerNode::ParseCommandLineIntoCreateParams(FMultiServerNodeCreateParams& InOutParams)
{
	FParse::Value(FCommandLine::Get(), TEXT("MultiServerLocalId="), InOutParams.LocalPeerId);
	FParse::Value(FCommandLine::Get(), TEXT("MultiServerListenPort="), InOutParams.ListenPort);

	FString PeerAddressesString;
	FParse::Value(FCommandLine::Get(), TEXT("MultiServerPeers="), PeerAddressesString, false);

	PeerAddressesString.ParseIntoArray(InOutParams.PeerAddresses, TEXT(","), true);
}

UMultiServerNode::UMultiServerNode()
	: UObject()
{
	RetryConnectDelay = 0.5f;
	RetryConnectMaxDelay = 30.0f;
}

UMultiServerNode* UMultiServerNode::Create(const FMultiServerNodeCreateParams& Params)
{
	UMultiServerNode* NewNode = NewObject<UMultiServerNode>(Params.World);
	const bool bResult = NewNode->RegisterServer(Params);

	if (bResult)
	{
		return NewNode;
	}
	else
	{
		// Is this the best way to clean up on failed registration?
		NewNode->MarkAsGarbage();
	}
	
	return nullptr;
}

bool UMultiServerNode::RegisterServer(const FMultiServerNodeCreateParams& Params)
{
	if (Params.World == nullptr)
	{
		UE_LOG(LogMultiServerReplication, Warning, TEXT("UMultiServerNode::RegisterServer: null world - failed to register."));
		return false;
	}

	if (Params.LocalPeerId.IsEmpty())
	{
		UE_LOG(LogMultiServerReplication, Warning, TEXT("UMultiServerNode::RegisterServer: no MultiServerLocalId specified - required for multiserver to work properly."));
		return false;
	}

	LocalPeerId = Params.LocalPeerId;
	UserBeaconClass = Params.UserBeaconClass;

	OnMultiServerConnected = Params.OnMultiServerConnected;

	if (Params.ListenPort == 0)
	{
		UE_LOG(LogMultiServerReplication, Log, TEXT("UMultiServerNode::RegisterServer: no listen port specified, not listening."));
	}
	else
	{
		UE_LOG(LogMultiServerReplication, Log, TEXT("UMultiServerNode::RegisterServer: setting up host beacon for %s."), ToCStr(LocalPeerId));

		// Set up host beacon
		if (ensureMsgf(BeaconHost == nullptr, TEXT("UMultiServerNode::RegisterServer: BeaconHost already created.")))
		{
			// Always create a new beacon host, state will be determined in a moment
			BeaconHost = Params.World->SpawnActor<AMultiServerBeaconHost>(AMultiServerBeaconHost::StaticClass());
			check(BeaconHost);

			BeaconHost->ListenPort = Params.ListenPort;

			bool bBeaconInit = false;
			if (BeaconHost->InitHost())
			{ 
				BeaconHostObject = Params.World->SpawnActor<AMultiServerBeaconHostObject>(AMultiServerBeaconHostObject::StaticClass());
				check(BeaconHostObject);

				BeaconHostObject->SetClientBeaconActorClass(Params.UserBeaconClass);
				BeaconHostObject->SetOwningNode(this);

				BeaconHost->RegisterHost(BeaconHostObject);
				BeaconHost->PauseBeaconRequests(false);
			}
			else
			{
				UE_LOG(LogMultiServerReplication, Warning, TEXT("Failed to init multiserver host beacon %s"), *BeaconHost->GetName());
				return false;
			}
		}
	}

	// If the -MultiServerPeers command-line option is set, start a client beacon for each one and connect to them.
	// (they are expected to be listening already)
	if (Params.PeerAddresses.Num() == 0)
	{
		UE_LOG(LogMultiServerReplication, Log, TEXT("UMultiServerNode::RegisterServer: no peers specified, not connecting to any. LocalPeerId %s"), ToCStr(LocalPeerId));
	}
	else
	{
		for (const FString& PeerAddress : Params.PeerAddresses)
		{
			FURL PeerURL(nullptr, ToCStr(PeerAddress), ETravelType::TRAVEL_Absolute);
			
			if (!PeerURL.Valid)
			{
				UE_LOG(LogMultiServerReplication, Verbose, TEXT("Failed to parse peer address %s, not connecting."), ToCStr(PeerAddress));
				continue;
			}

			// Only connect if the peer port is "lower" than the listening port, to prevent redundant connections.
			// This only works when connecting to instances on the same machine, so limit it to loopback addresses.
			// TODO: Handle this for remote addresses, or require the filtering to be done at a higher level (command line, etc).
			if (PeerURL.Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase) ||
				PeerURL.Host.StartsWith(TEXT("127."), ESearchCase::IgnoreCase))
			{
				if (Params.ListenPort != 0 && PeerURL.Port >= Params.ListenPort)
				{
					continue;
				}
			}

			if (!PeerAddress.IsEmpty())
			{
				UMultiServerPeerConnection* Peer = NewObject<UMultiServerPeerConnection>(this);
				Peer->SetOwningNode(this);
				Peer->SetRemoteAddress(PeerAddress);
				Peer->SetLocalPeerId(LocalPeerId);
				Peer->InitClientBeacon();
				PeerConnections.Add(Peer);
			}
		}
	}

	return true;
}

AMultiServerBeaconClient* UMultiServerNode::GetBeaconClientForRemotePeer(FStringView RemotePeerId)
{
	// See if we are the host of the target server
	UNetDriver* HostNetDriver = BeaconHost ? BeaconHost->GetNetDriver() : nullptr;
	if (HostNetDriver)
	{
		for (UNetConnection* ClientConnection : HostNetDriver->ClientConnections)
		{
			AMultiServerBeaconClient* BeaconClient = Cast<AMultiServerBeaconClient>(BeaconHost->GetClientActor(ClientConnection));
			if (BeaconClient)
			{
				if (RemotePeerId.Equals(BeaconClient->GetRemotePeerId(), ESearchCase::IgnoreCase))
				{
					return BeaconClient;
				}
			}
		}
	}

	// See if we are a client of the target server
	for (UMultiServerPeerConnection* Peer : PeerConnections)
	{
		AMultiServerBeaconClient* Beacon = Peer->BeaconClient;
		if (Beacon)
		{
			if (RemotePeerId.Equals(Beacon->GetRemotePeerId(), ESearchCase::IgnoreCase))
			{
				return Beacon;
			}
		}
	}

	return nullptr;
}

AMultiServerBeaconClient* UMultiServerNode::GetBeaconClientForURL(const FString& InURL)
{
	FURL URL(nullptr, ToCStr(InURL), TRAVEL_Absolute);

	// See if we are the host of the target server
	UNetDriver* HostNetDriver = BeaconHost ? BeaconHost->GetNetDriver() : nullptr;
	if (HostNetDriver)
	{
		for (UNetConnection* ClientConnection : HostNetDriver->ClientConnections)
		{
			if (ClientConnection &&
				ClientConnection->URL.Host.Equals(URL.Host, ESearchCase::IgnoreCase) &&
				ClientConnection->URL.Port == URL.Port)
			{
				return Cast<AMultiServerBeaconClient>(BeaconHost->GetClientActor(ClientConnection));
			}
		}
	}

	// See if we are a client of the target server
	for (UMultiServerPeerConnection* Peer : PeerConnections)
	{
		AMultiServerBeaconClient* Beacon = Peer->BeaconClient;
		if (Beacon)
		{
			UNetConnection* ClientConnection = Beacon->GetNetConnection();
			if (ClientConnection &&
				ClientConnection->URL.Host.Equals(URL.Host, ESearchCase::IgnoreCase) &&
				ClientConnection->URL.Port == URL.Port)
			{
				return Beacon;
			}
		}
	}

	return nullptr;
}
