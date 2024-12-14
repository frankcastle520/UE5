// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/NetTestHelpers.h"

#include "CoreGlobals.h"
#include "Engine/NetworkObjectList.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PackageMapClient.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMath.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

#if WITH_EDITOR
#include "Settings/LevelEditorPlaySettings.h"
#endif

#if UE_WITH_IRIS
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#endif

#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

#if WITH_EDITOR

namespace UE::Net
{

FTestWorldInstance FTestWorldInstance::CreateServer(const TCHAR* InURL)
{
	FGameInstancePIEParameters ServerParams;
	ServerParams.bSimulateInEditor = false;
	ServerParams.bAnyBlueprintErrors = false;
	ServerParams.bStartInSpectatorMode = false;
	ServerParams.bRunAsDedicated = true;
	ServerParams.bIsPrimaryPIEClient = false;
	ServerParams.WorldFeatureLevel = ERHIFeatureLevel::SM5;
	ServerParams.EditorPlaySettings = nullptr;
	ServerParams.NetMode = PIE_ListenServer;

	FTestWorldInstance NewInstance(ServerParams);

	FURL LocalURL(nullptr, InURL, TRAVEL_Absolute);
	FString BrowseError;
	GEngine->Browse(*NewInstance.GameInstance->GetWorldContext(), LocalURL, BrowseError);

	return NewInstance;
}

FTestWorldInstance FTestWorldInstance::CreateClient(int32 ServerPort)
{
	FGameInstancePIEParameters ClientParams;
	ClientParams.bSimulateInEditor = false;
	ClientParams.bAnyBlueprintErrors = false;
	ClientParams.bStartInSpectatorMode = false;
	ClientParams.bRunAsDedicated = false;
	ClientParams.bIsPrimaryPIEClient = false;
	ClientParams.WorldFeatureLevel = ERHIFeatureLevel::SM5;
	ClientParams.EditorPlaySettings = nullptr;
	ClientParams.NetMode = PIE_Client;

	FTestWorldInstance NewInstance(ClientParams);

	FWorldContext* ClientWorldContext = NewInstance.GameInstance->GetWorldContext();

	UGameViewportClient* ViewportClient = NewObject<UGameViewportClient>(GEngine, GEngine->GameViewportClientClass);
	ViewportClient->Init(*ClientWorldContext, NewInstance.GameInstance);
	ClientWorldContext->GameViewport = ViewportClient;

	FString OutCreatePlayerError;
	ViewportClient->SetupInitialLocalPlayer(OutCreatePlayerError);
	GEngine->BrowseToDefaultMap(*ClientWorldContext);

	FString ClientURLString = FString::Format(TEXT("127.0.0.1:{0}"), {ServerPort});

	FURL ClientURL(nullptr, *ClientURLString, TRAVEL_Absolute);
	FString ClientBrowseError;
	GEngine->Browse(*ClientWorldContext, ClientURL, ClientBrowseError);

	return NewInstance;
}

FTestWorldInstance::FTestWorldInstance(const FGameInstancePIEParameters& InstanceParams)
{
	GameInstance = NewObject<UGameInstance>(GEngine, UGameInstance::StaticClass());
	GameInstance->AddToRoot();

	GameInstance->InitializeForPlayInEditor(FindUnusedPIEInstance(), InstanceParams);

	FWorldContext* WorldContext = GameInstance->GetWorldContext();
}

FTestWorldInstance::FTestWorldInstance(FTestWorldInstance&& Other)
	: GameInstance(Other.GameInstance)
{
	Other.GameInstance = nullptr;
}

FTestWorldInstance& FTestWorldInstance::operator=(FTestWorldInstance&& Other)
{
	if (this != &Other)
	{
		Shutdown();

		GameInstance = Other.GameInstance;
		Other.GameInstance = nullptr;
	}

	return *this;
}

FTestWorldInstance::~FTestWorldInstance()
{
	Shutdown();
}

void FTestWorldInstance::Shutdown()
{
	UWorld* World = GetWorld();

	if (World)
	{
		for (FActorIterator ActorIt(World); ActorIt; ++ActorIt)
		{
			ActorIt->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
		}
	}

	if (GameInstance)
	{
		GameInstance->Shutdown();
		GameInstance->RemoveFromRoot();
	}
	
	if (World)
	{
		World->BeginTearingDown();
		GEngine->ShutdownWorldNetDriver(World);
		GEngine->DestroyWorldContext(World);
		World->CleanupWorld();
	}
}

int32 FTestWorldInstance::FindUnusedPIEInstance()
{
	if (!GEngine)
	{
		return INDEX_NONE;
	}

	int32 MaxUsedPIEInstance = INDEX_NONE;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		MaxUsedPIEInstance = FMath::Max(MaxUsedPIEInstance, Context.PIEInstance);
	}

	return MaxUsedPIEInstance + 1;
}

FTestWorldInstance::FContext FTestWorldInstance::GetTestContext() const
{
#if UE_WITH_IRIS
	UReplicationSystem* RepSystem = GetNetDriver() ? GetNetDriver()->GetReplicationSystem() : nullptr;
	UObjectReplicationBridge* RepBridge  = RepSystem ? RepSystem->GetReplicationBridgeAs<UObjectReplicationBridge>() : nullptr;
#endif

	return FContext
	{ 
		.World = GetWorld(),
		.NetDriver = GetNetDriver(),
		.IrisRepSystem = RepSystem,
		.IrisRepBridge = RepBridge,
	};
}

UWorld* FTestWorldInstance::GetWorld() const
{
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

FWorldContext* FTestWorldInstance::GetWorldContext() const
{
	return GameInstance->GetWorldContext();
}

UNetDriver* FTestWorldInstance::GetNetDriver() const
{
	UWorld* World = GetWorld();
	return World ? World->GetNetDriver() : nullptr;
}

void FTestWorldInstance::Tick(float DeltaSeconds)
{
	GEngine->TickWorldTravel(*GetWorldContext(), DeltaSeconds);
	if (UWorld* World = GetWorld())
	{
		World->Tick(LEVELTICK_All, DeltaSeconds);
		World->UpdateLevelStreaming();
	}
}

int32 FTestWorldInstance::GetPort()
{
	if (const UWorld* const World = GetWorld())
	{
		if (UNetDriver* const NetDriver = World->GetNetDriver())
		{
			if (NetDriver->GetLocalAddr().IsValid())
			{
				return NetDriver->GetLocalAddr()->GetPort();
			}
		}
	}

	return 0;
}

void FTestWorldInstance::LoadStreamingLevel(FName LevelName)
{
	FLatentActionInfo LoadLatentInfo;
	LoadLatentInfo.UUID = LevelStreamRequestUUID++;
	constexpr bool bMakeVisibleAfterLoad = true;
	constexpr bool bShouldBlockOnLoad = true;
	UGameplayStatics::LoadStreamLevel(GetWorld(), LevelName, bMakeVisibleAfterLoad, bShouldBlockOnLoad, LoadLatentInfo);
}

void FTestWorldInstance::UnloadStreamingLevel(FName LevelName)
{
	FLatentActionInfo UnloadLatentInfo;
	UnloadLatentInfo.UUID = LevelStreamRequestUUID++;
	constexpr bool bShouldBlockOnUnload = true;
	UGameplayStatics::UnloadStreamLevel(GetWorld(), LevelName, UnloadLatentInfo, bShouldBlockOnUnload);
}

//============================================================================
// FTestWorlds
//============================================================================

FTestWorlds::FTestWorlds(const TCHAR* ServerURL, float DeltaSeconds)
	: Server(true)
	, TickDeltaSeconds(DeltaSeconds)
{
	NetDriverCreatedHandle = FWorldDelegates::OnNetDriverCreated.AddRaw(this, &FTestWorlds::OnNetDriverCreated);

	Server = FTestWorldInstance::CreateServer(ServerURL);
}

FTestWorlds::~FTestWorlds()
{
	FWorldDelegates::OnNetDriverCreated.Remove(NetDriverCreatedHandle);
}

void FTestWorlds::OnNetDriverCreated(UWorld* InWorld, UNetDriver* InNetDriver)
{
	// Make sure NetDriver will tick every engine frame
	InNetDriver->MaxNetTickRate = 0;
}

bool FTestWorlds::CreateAndConnectClient()
{
	Clients.Emplace(FTestWorldInstance::CreateClient(Server.GetPort()));
	const bool bConnected = WaitForClientConnect(Clients.Last());
	return bConnected;
}

bool FTestWorlds::WaitForClientConnect(FTestWorldInstance& Client)
{
	return TickAllUntil([&Client]()
	{
		if (UWorld* World = Client.GetWorld())
		{
			APlayerController* PC = World->GetFirstPlayerController();
			return IsValid(PC) && (PC->GetLocalRole() == ROLE_AutonomousProxy);
		}
		return false;
	});
}

void FTestWorlds::TickAll(int32 NumTicks)
{
	for (int32 i = 0; i < NumTicks; ++i)
	{
		TickServer();
		TickClients();
		GFrameCounter++;
	}
}

void FTestWorlds::TickServer()
{
	Server.Tick(TickDeltaSeconds);
}

void FTestWorlds::TickClients()
{
	for (FTestWorldInstance& Client : Clients)
	{
		Client.Tick(TickDeltaSeconds);
	}
}

void FTestWorlds::TickServerAndDrop()
{
#if DO_ENABLE_NET_TEST
	UNetDriver* NetDriver = Server.GetNetDriver();
	NetDriver->PacketSimulationSettings.PktLoss = 100;
	NetDriver->OnPacketSimulationSettingsChanged();
	
	Server.Tick(TickDeltaSeconds);

	NetDriver->PacketSimulationSettings.PktLoss = 0;
	NetDriver->OnPacketSimulationSettingsChanged();
#else
	UE_LOG(LogNet, Error, TEXT("FTestWorlds::TickServerAndDrop does not work called without NetDriver Simulation Settings"));
#endif
}

void FTestWorlds::TickClientsAndDrop()
{
#if DO_ENABLE_NET_TEST
	for (FTestWorldInstance& Client : Clients)
	{
		UNetDriver* NetDriver = Client.GetNetDriver();
		NetDriver->PacketSimulationSettings.PktLoss = 100;
		NetDriver->OnPacketSimulationSettingsChanged();

		Client.Tick(TickDeltaSeconds);
		
		NetDriver->PacketSimulationSettings.PktLoss = 0;
		NetDriver->OnPacketSimulationSettingsChanged();
	}
#else
	UE_LOG(LogNet, Error, TEXT("FTestWorlds::TickClientsAndDrop does not work without NetDriver Simulation Settings"));
#endif
}

void FTestWorlds::TickServerAndDelay(uint32 NumFramesToDelay)
{
#if DO_ENABLE_NET_TEST
	UNetDriver* NetDriver = Server.GetNetDriver();
	NetDriver->PacketSimulationSettings.PktFrameDelay = NumFramesToDelay;
	NetDriver->OnPacketSimulationSettingsChanged();

	Server.Tick(TickDeltaSeconds);

	NetDriver->PacketSimulationSettings.PktFrameDelay = 0;
	NetDriver->OnPacketSimulationSettingsChanged();
#else
	UE_LOG(LogNet, Error, TEXT("FTestWorlds::TickServerAndDelay does not work without NetDriver Simulation Settings"));
#endif
}

void FTestWorlds::TickClientsAndDelay(uint32 NumFramesToDelay)
{
#if DO_ENABLE_NET_TEST
	for (FTestWorldInstance& Client : Clients)
	{
		UNetDriver* NetDriver = Client.GetNetDriver();
		NetDriver->PacketSimulationSettings.PktFrameDelay = NumFramesToDelay;
		NetDriver->OnPacketSimulationSettingsChanged();

		Client.Tick(TickDeltaSeconds);

		NetDriver->PacketSimulationSettings.PktFrameDelay = 0;
		NetDriver->OnPacketSimulationSettingsChanged();
	}
#else
	UE_LOG(LogNet, Error, TEXT("FTestWorlds::TickClientsAndDelay does not work without NetDriver Simulation Settings"));
#endif
}

 APlayerController* FTestWorlds::GetServerPlayerControllerOfClient(uint32 ClientIndex)
{
	if (Clients.IsValidIndex(ClientIndex) == false)
	{
		// No client exists for that index
		return nullptr;
	}

	int32 PlayerId = -1;
	// Get the unique info from the PlayerController on the client world
	{
		APlayerController* ClientPC = Clients[ClientIndex].GetWorld()->GetFirstPlayerController();
		if (!ClientPC || !ClientPC->PlayerState)
		{
			return nullptr;
		}

		PlayerId = ClientPC->PlayerState->GetPlayerId();
	}

	// Find the PlayerController on the server related to to this client
	UWorld* ServerWorld = Server.GetWorld();
	for (FConstPlayerControllerIterator PCIter = ServerWorld->GetPlayerControllerIterator(); PCIter; ++PCIter)
	{
		if (APlayerController* PC = PCIter->Get())
		{
			if (PC->PlayerState && PC->PlayerState->GetPlayerId() == PlayerId)
			{
				return PC;
			}
		}
	}

	return nullptr;
}

UObject* FTestWorlds::FindReplicatedObjectOnClient(UObject* ServerObject, uint32 ClientIndex) const
{
	using namespace UE::Net;

	if (!Clients.IsValidIndex(ClientIndex))
	{
		ensureMsgf(false, TEXT("FTestWorlds::FindReplicatedObjectOnClient received invalid ClientIndex: %u"), ClientIndex);
		return nullptr;
	}

	if (ServerObject->GetWorld() != Server.GetWorld())
	{
		ensureMsgf(false, TEXT("FTestWorlds::FindReplicatedObjectOnClient received object %s not part of the Server world"), *GetFullNameSafe(ServerObject));
		return nullptr;
	}

	const FTestWorldInstance::FContext ServerContext = Server.GetTestContext();
	const FTestWorldInstance::FContext ClientContext = Clients[ClientIndex].GetTestContext();

	if (ServerContext.NetDriver->IsUsingIrisReplication())
	{
#if UE_WITH_IRIS
	const FNetRefHandle NetHandle = ServerContext.IrisRepBridge->GetReplicatedRefHandle(ServerObject);

		if (!NetHandle.IsValid())
		{
			ensureMsgf(false, TEXT("FTestWorlds::FindReplicatedObjectOnClient ServerObject: %s is not replicated."), *GetFullNameSafe(ServerObject));
			return nullptr;
		}

		return ClientContext.IrisRepBridge->GetReplicatedObject(NetHandle);

#endif // UE_WITH_IRIS
	}
	else
	{
		const FNetworkGUID NetGUID = ServerContext.NetDriver->GuidCache->GetNetGUID(ServerObject);

		if (!NetGUID.IsValid())
		{
			ensureMsgf(false, TEXT("FTestWorlds::FindReplicatedObjectOnClient ServerObject: %s is not replicated."), *GetFullNameSafe(ServerObject));
		}

		return ClientContext.NetDriver->GuidCache->GetObjectFromNetGUID(NetGUID, false);
	}
}

//------------------------------------------------------------------------

FScopedCVarOverrideInt::FScopedCVarOverrideInt(const TCHAR* VariableName, int32 Value)
	: Variable(IConsoleManager::Get().FindConsoleVariable(VariableName))
{
	if (Variable)
	{
		SavedValue = Variable->GetInt();
		Variable->Set(Value);
	}
}

FScopedCVarOverrideInt::~FScopedCVarOverrideInt()
{
	if (Variable)
	{
		Variable->Set(SavedValue);
	}
}

//------------------------------------------------------------------------

FScopedTestSettings::FScopedTestSettings()
	: AddressResolutionDisabled(TEXT("net.IpConnectionDisableResolution"), 1)
	, BandwidthThrottlingDisabled(TEXT("net.DisableBandwithThrottling"), 1)
	, RepGraphBandwidthThrottlingDisabled(TEXT("Net.RepGraph.DisableBandwithLimit"), 1)
	, RandomNetUpdateDelayDisabled(TEXT("net.DisableRandomNetUpdateDelay"), 1)
	, GameplayDebuggerDisabled(TEXT("GameplayDebugger.AutoCreateGameplayDebuggerManager"), 0)
{
}

FScopedTestSettings::~FScopedTestSettings()
{
}

//------------------------------------------------------------------------

FScopedNetTestPIERestoration::FScopedNetTestPIERestoration()
	: OldGWorld(GWorld)
	, OldPIEID(UE::GetPlayInEditorID())
	, OldGIsPlayInEditorWorld(GIsPlayInEditorWorld)
{

}

FScopedNetTestPIERestoration::~FScopedNetTestPIERestoration()
{
	GWorld = OldGWorld;
	UE::SetPlayInEditorID(OldPIEID);
	GIsPlayInEditorWorld = OldGIsPlayInEditorWorld;
}

} // end namespace UE::Net

#endif // WITH_EDITOR