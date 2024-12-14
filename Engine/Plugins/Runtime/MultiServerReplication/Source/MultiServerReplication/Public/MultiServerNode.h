// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "MultiServerNode.generated.h"

class AActor;
class APlayerController;
class AGameSession;

class AMultiServerBeaconHost;
class AMultiServerBeaconHostObject;
class AMultiServerBeaconClient;
class UMultiServerPeerConnection;

DECLARE_DELEGATE_ThreeParams(FOnMultiServerConnected, const FString&, const FString&, AMultiServerBeaconClient*);

/* Parameters for initializing a UMultiServerNode */
struct FMultiServerNodeCreateParams
{
	/** World in which to create the node */
	UWorld* World = nullptr;

	/** String identifier of this node. Must be unique among all nodes that will connect to each other. */
	FString LocalPeerId;

	/** The port on which this node will listen for new connections. */ 
	uint16 ListenPort = 0;

	/** List of addresses of other nodes to attempt to connect to. */
	TArray<FString> PeerAddresses;

	/** Beacon client class that will be instantiated for each connection. Can implement its own RPCs. */
	TSubclassOf<AMultiServerBeaconClient> UserBeaconClass;

	/** Callback invoked when a connection to a remote node is established. */
	FOnMultiServerConnected OnMultiServerConnected;
};

/**
 * The MultiServer node is a system / control scheme for connecting multiple dedicated server
 * processes to each other and allowing them to communicate via online beacons.
 * 
 * The basic usage pattern is to create a UMultiServerNode in project code via UMultiServerNode::Create.
 * For a typical UE game, a good place might be in an AGameSession subclass in the RegisterServer override.
 * The node manages all the connections to other servers, and will attempt to establish them upon creation
 * based on the PeerAddresses in the FMultiServerNodeCreateParams.
 * 
 * The main user-extension point is to subclass AMultiServerBeaconClient. This subclass can implement its own
 * RPCs to send custom messages to other servers connected to the node. The OnMultiServerConnected callback in the
 * FMultiServerNodeCreateParams will be called when a new connection is established, with the instance of the user
 * beacon as an argument. The user code can call its RPCs on the instance.
 */
UCLASS(Config=Engine, Transient, DisplayName = "MultiServer Node")
class MULTISERVERREPLICATION_API UMultiServerNode : public UObject
{
	GENERATED_BODY()

public:
	UMultiServerNode();

	static UMultiServerNode* Create(const FMultiServerNodeCreateParams& Params);

	static void ParseCommandLineIntoCreateParams(FMultiServerNodeCreateParams& InOutParams);

	bool RegisterServer(const FMultiServerNodeCreateParams& Params);

	AMultiServerBeaconClient* GetBeaconClientForRemotePeer(FStringView RemotePeerId);

	template<class T>
	T* GetBeaconClientForRemotePeer(FStringView RemotePeerId);

	AMultiServerBeaconClient* GetBeaconClientForURL(const FString& InURL);

	template<class T>
	T* GetBeaconClientForURL(const FString& InURL);

	FString GetLocalPeerId() const { return LocalPeerId; }

	float GetRetryConnectDelay() const { return RetryConnectDelay; }
	float GetRetryConnectMaxDelay() const { return RetryConnectMaxDelay; }

	TSubclassOf<AMultiServerBeaconClient> GetUserBeaconClass() const { return UserBeaconClass; }

private:
	friend AMultiServerBeaconClient;

	FOnMultiServerConnected OnMultiServerConnected;

	FString LocalPeerId;

	UPROPERTY()
	TObjectPtr<AMultiServerBeaconHost> BeaconHost;

	UPROPERTY()
	TObjectPtr<AMultiServerBeaconHostObject> BeaconHostObject;

	UPROPERTY()
	TArray<TObjectPtr<UMultiServerPeerConnection>> PeerConnections;

	UPROPERTY(Config)
	float RetryConnectDelay;

	UPROPERTY(Config)
	float RetryConnectMaxDelay;

	TSubclassOf<AMultiServerBeaconClient> UserBeaconClass;
};

template<class T>
T* UMultiServerNode::GetBeaconClientForRemotePeer(FStringView RemotePeerId)
{
	return Cast<T>(GetBeaconClientForRemotePeer(RemotePeerId));
}

template<class T>
T* UMultiServerNode::GetBeaconClientForURL(const FString& InURL)

{
	return Cast<T>(GetBeaconClientForURL(InURL));
}