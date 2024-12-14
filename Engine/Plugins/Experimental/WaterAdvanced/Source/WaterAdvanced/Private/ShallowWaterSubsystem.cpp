// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShallowWaterSubsystem.h"

#include "ShallowWaterSettings.h"
#include "NiagaraComponent.h"
#include "NiagaraDataChannel.h"
#include "NiagaraDataChannelAccessor.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "WaterBodyActor.h"
#include "WaterSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTarget2DArray.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpectatorPawn.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/AssetManager.h"
#include "Engine/Texture2D.h"

//UE_DISABLE_OPTIMIZATION_SHIP

int32 GSWEnabled = 1;
static FAutoConsoleVariableRef CVarSWEnabled(
	TEXT("r.ShallowWater.Enabled"),
	GSWEnabled,
	TEXT("Should create ShallowWaterSubsystem at all"),
	ECVF_Scalability
);

float GSWRemainActiveForSeconds = 15.f;
static FAutoConsoleVariableRef CVarSWRemainActiveForSeconds(
	TEXT("r.ShallowWater.FadeOutWait"),
	GSWRemainActiveForSeconds,
	TEXT("If not any kind of collision or collision tracker is active, how long the simulation continues to be active to wait out the ripples")
);

float GSWImpactTrackerActiveForSeconds = 5.f;
static FAutoConsoleVariableRef CVarImpactTrackerActiveForSeconds(
	TEXT("r.ShallowWater.CollisionTracker.ImpactTrackerActiveForSeconds"),
	GSWImpactTrackerActiveForSeconds,
	TEXT("How long impacts (e.g. bullets hit water) are tracked, keeping the sim active") 
);

int32 GSWDrawWaterSurfaceProjection = 0;
static FAutoConsoleVariableRef CVarSWDrawWaterSurfaceProjection(
	TEXT("r.ShallowWater.DrawSurfaceProjection"),
	GSWDrawWaterSurfaceProjection,
	TEXT("")
);

int32 GSWDebugRender = 0;
static FAutoConsoleVariableRef CVarSWDebugRender(
	TEXT("r.ShallowWater.DebugRender"),
	GSWDebugRender,
	TEXT("")
);

int32 GSWUseWaterInfoTexture = 1;
static FAutoConsoleVariableRef CVarSWUseWaterInfoTexture(
	TEXT("r.ShallowWater.UseWaterInfoTexture"),
	GSWUseWaterInfoTexture,
	TEXT("")
);

int32 GSWUseFullVehiclePhysicsAssets = 1;
static FAutoConsoleVariableRef CVarSWUUseFullVehiclePhysicsAssetse(
	TEXT("r.ShallowWater.UseFullVehiclePhysicsAssets"),
	GSWUseFullVehiclePhysicsAssets,
	TEXT("")
);

FName UShallowWaterSubsystem::ColliderComponentTag = FName("RigidMesh_ShallowWaterCollider");

TArray<AWaterBody*> FShallowWaterCollisionTracker_Actor::GetOverlappingWaterBodies() const
{
	TArray<AWaterBody*> Result;
	if(const AActor* ActorPointer = CollisionActor.Get())
	{
    	TArray<AActor*> OverlappingActors;
		ActorPointer->GetOverlappingActors(OverlappingActors, AWaterBody::StaticClass());
		for (AActor* WaterBody : OverlappingActors)
		{
			if (AWaterBody* Actor = Cast<AWaterBody>(WaterBody))
			{
				Result.AddUnique(Actor);
			}
		}
	}
    return Result;
}

UShallowWaterSubsystem::UShallowWaterSubsystem()
{
}

void UShallowWaterSubsystem::PostInitialize()
{
	Super::PostInitialize();

	// Register default PA Proxies before all other GFP chimes in
	Settings = GetMutableDefault<UShallowWaterSettings>();
	if (!Settings)
	{
		ensureMsgf(false, TEXT("UShallowWaterSubsystem::PostInitialize() - UShallowWaterSettings is not valid"));
	}
}

void UShallowWaterSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	TArray<ULocalPlayer*> LocalPlayers = InWorld.GetGameInstance() != nullptr ? InWorld.GetGameInstance()->GetLocalPlayers() : TArray<ULocalPlayer*>();
	
	// we don't support split screen
	if (LocalPlayers.Num() == 1)
	{
		if (ULocalPlayer* const LocalPlayer = LocalPlayers[0])
		{
			APlayerController* Controller = LocalPlayer->GetPlayerController(&InWorld);
			if (Controller)
			{
				OnLocalPlayerControllerBecomesValid(Controller);
			}
			LocalPlayer->OnPlayerControllerChanged().AddUObject(this, &UShallowWaterSubsystem::OnLocalPlayerControllerBecomesValid);
		}
	}
	else if (LocalPlayers.Num() > 1)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("Shallow Water Simulation is disabled during splitscreen"));
	}
}

bool UShallowWaterSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!GSWEnabled)
	{
		return false;
	}
	
	if (!FApp::CanEverRender() || IsRunningDedicatedServer())
	{
		return false;
	}

	// IsRunningDedicatedServer() is a static check and doesn't work in PIE "As Client' mode where both a server and a client are run
	if (UWorld* World = Cast<UWorld>(Outer))
	{
		if (World->IsNetMode(NM_DedicatedServer))
		{
			return false;
		}
	}
	
	return Super::ShouldCreateSubsystem(Outer);
}

void UShallowWaterSubsystem::Tick(float DeltaTime)
{	
	Super::Tick(DeltaTime);
	
	if (!IsShallowWaterInitialized())
	{
		return;
	}

	UpdateActivePawns();
	GatherContextsFromPawns(ActivePawns);
	UpdateCollisionForPendingContexts();

	/*
	 **** Ticking order ****
	 *
	 * Niagara CPU Script and Pawns (order can be mixed)
	 *
	 * Subsystem
	 *     - ThisClass::UpdateGridMovement()
	 *     - Set SimCenter to water materials
	 *
	 * Single Layer Water shader
	 */
	// Leave the sim center at previous water body Z level untouched, if owner character is not in water
	TSet<AWaterBody*> WaterBodies = GetAllOverlappingWaterBodiesAndUpdateCollisionTrackers();
	if (!WaterBodies.IsEmpty())
	{
		UpdateOverlappingWaterBodiesHistory(WaterBodies.Array());
	
		if (WaterInfoTexture == nullptr)
		{
			TryGetOrWaitForWaterInfoTextureFromWaterBodies(WaterBodies);
		}
	
		for (AWaterBody* Water : WaterBodies)
		{
			if (Water == nullptr)
			{
				continue;
			}
			TryUpdateWaterBodyMIDParameters(Water->GetWaterBodyComponent());
		}
	}

	// We might miss some impacts because the Niagara System hasn't been activated in time.
	// Note that we do this one frame after Niagara System activation otherwise it won't work
	// due to reset tick logic in Niagara skipping the main simulation loop
	if (bFlushPendingImpactsNextTick)
	{		
		FlushPendingImpacts();
	}
	bFlushPendingImpactsNextTick = false;

	if (ShouldSimulateThisFrame())
	{
		UpdateGridMovement();
		if (!ShallowWaterNiagaraSimulation->IsActive())
		{
			ShallowWaterNiagaraSimulation->Activate(false);

			bFlushPendingImpactsNextTick = true;
		}
	}
	else
	{
		if (ShallowWaterNiagaraSimulation->IsActive())
		{
			ShallowWaterNiagaraSimulation->Deactivate();
		}
	}
}

TStatId UShallowWaterSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UShallowWaterSubsystem, STATGROUP_Tickables);
}

void UShallowWaterSubsystem::InitializeShallowWater()
{
	if (!ensure(WeakPlayerController.IsValid()))
	{
		UE_LOG(LogShallowWater, Warning, TEXT("PlayerController is invalid during initialization"));
		return;
	}

	// another check to make sure we don't simulate when we have split screen active
	if (WeakPlayerController->GetSplitscreenPlayerCount() > 1)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("Shallow Water Simulation is disabled during splitscreen"));
		return;
	}

	if(!IsShallowWaterAllowedToInitialize())
	{
		return;
	}

	// async load the ShallowWater MPC and NS
	if (!bInitializationAsyncLoadsAttempted)
	{
		bInitializationAsyncLoadsAttempted = true;

		Settings = GetMutableDefault<UShallowWaterSettings>();

		TArray< FSoftObjectPath> ObjectsToLoad;
		ObjectsToLoad.Add(Settings->WaterMPC.ToSoftObjectPath());
		ObjectsToLoad.Add(Settings->DefaultShallowWaterNiagaraSimulation.ToSoftObjectPath());
		ObjectsToLoad.Add(Settings->DefaultShallowWaterCollisionNDC.ToSoftObjectPath());
		ObjectsToLoad.Add(Settings->PhysicsAssetProxiesDataAsset.ToSoftObjectPath());

		UAssetManager::GetStreamableManager().RequestAsyncLoad(ObjectsToLoad,
			FStreamableDelegate::CreateWeakLambda(this, [this]()
				{
					// continue with initialization after MPC and NS are loaded
					InitializeShallowWater();
				})
		);

		return;
	}
	
	MPC = Settings->WaterMPC.Get();
	if (MPC == nullptr)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterSubsystem::InitializeShallowWater() - MPC cannot be loaded. Make sure it's set in ShallowWater Settings."));
		return;
	}

	/*
	 * From here, non-spectator PlayerPawn might not be available if the game is a replay
	 * So we relies on  GetTheMostRelevantPlayerPawn();
	 */
	const APawn* CursorPawn = GetTheMostRelevantPlayerPawn();
	if (CursorPawn == nullptr)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("Could not find CursorPawn during initialization"));
		return;
	}

	// async load the NS, then create the actor
	UNiagaraSystem* ShallowWaterTemplate = Settings->DefaultShallowWaterNiagaraSimulation.Get();
	if (ShallowWaterTemplate == nullptr)
	{		
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterSubsystem::InitializeShallowWater() - Couldn't find ShallowWater template in settings"));
		return;
	}

	// async load the NS, then create the actor
	UNiagaraDataChannelAsset* ShallowWaterCollisionNDC = Settings->DefaultShallowWaterCollisionNDC.Get();
	if (ShallowWaterCollisionNDC == nullptr)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterSubsystem::InitializeShallowWater() - Couldn't find ShallowWater collision NDC in settings"));
		return;
	}

	const FVector SpawnLocation =  CursorPawn->GetActorLocation();
	ShallowWaterNiagaraSimulation = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), ShallowWaterTemplate,
		SpawnLocation, FRotator::ZeroRotator, FVector::OneVector, false,
		false,
		ENCPoolMethod::None, false);

	if (ShallowWaterNiagaraSimulation == nullptr)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterSubsystem::InitializeShallowWater() - ShallowWaterNiagaraSystem spawn failed"));
		return;
	}
	/*
	 * Initialize succeeds
	 */

	bIsShallowWaterInitialized = true;
	
	CreateRTs();
	
	ShallowWaterNiagaraSimulation->SetUsingAbsoluteRotation(true);
	ShallowWaterNiagaraSimulation->Activate();
		
	InitializeParameters();

	for (TWeakObjectPtr<AWaterBody> WeakWaterBody : PendingWaterBodiesToSetMIDOnInitialize)
	{
		if (AWaterBody* WaterBody = WeakWaterBody.Get())
		{
			TryUpdateWaterBodyMIDParameters(WaterBody->GetWaterBodyComponent());
		}
	}
	PendingWaterBodiesToSetMIDOnInitialize.Empty();

	PendingImpacts.Reset();
	
	// Register default PA Proxies before all other GFP chimes in
	if (Settings->PhysicsAssetProxiesDataAsset.IsValid())
	{
		RegisterPhysicsAssetProxiesDataAsset(Settings->PhysicsAssetProxiesDataAsset.Get());
	}
	else
	{
		UE_LOG(LogShallowWater, Log, TEXT("UShallowWaterSubsystem::InitializeShallowWater() - UShallowWaterSettings::PhyicsAssetProxiesDataAsset is not valid"));	
	}

	UE_LOG(LogShallowWater, Log, TEXT("UShallowWaterSubsystem::InitializeShallowWater() finished successfully"));
}

bool UShallowWaterSubsystem::IsShallowWaterAllowedToInitialize() const
{
	return false;
}

bool UShallowWaterSubsystem::IsShallowWaterInitialized() const
{
	return bIsShallowWaterInitialized;
}

APawn* UShallowWaterSubsystem::GetNonSpectatorPawnFromWeakController() const
{
	const APlayerController* Controller = WeakPlayerController.Get();
	if (Controller)
	{
		APawn* Pawn = Controller->GetPawn();
		if (Pawn && !Pawn->GetClass()->IsChildOf(ASpectatorPawn::StaticClass()))
		{
			return Pawn; 
		}
	}
	return nullptr;
}

TOptional<FVector> UShallowWaterSubsystem::GetCameraLocationFromWeakController() const
{
	TOptional<FVector> Result;
	const APlayerController* Controller = WeakPlayerController.Get();
	if (Controller)
	{
		const TObjectPtr<APlayerCameraManager> CamMan = Controller->PlayerCameraManager;
		if (CamMan)
		{
			Result = CamMan->GetCameraLocation();
			return Result;
		}
	}
	return Result;
}

APawn* UShallowWaterSubsystem::GetTheMostRelevantPlayerPawn() const
{
	const APlayerController* Controller = WeakPlayerController.Get();
	if (Controller)
	{
		APawn* Pawn = GetNonSpectatorPawnFromWeakController();
		if (Pawn)
		{
			return Pawn;
		}
		
		const TOptional<FVector> CamLoc = GetCameraLocationFromWeakController();
		if (CamLoc.IsSet())
		{
			APawn* BestPawn = nullptr;
			float BestDistance = FLT_MAX;
			for (APawn* TestPawn : GetPawnsInRange())
			{
			    const float Distance = FVector::Distance(CamLoc.GetValue(), TestPawn->GetActorLocation()); 
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					BestPawn = TestPawn;
				}
			}
			if (BestPawn)
			{
				return BestPawn;
			}
		}
	}
	return nullptr;
}

void UShallowWaterSubsystem::CreateRTs()
{
	int32 Resolution = Settings->ShallowWaterSimParameters.ResolutionMaxAxis;

	if (Resolution <= 0)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("ShallowWaterComponent: Invalid Resolution Max Axis.  Defauling to 1x1 simulation."))
		Resolution = 1;
	}

	NormalRT = NewObject<UTextureRenderTarget2D>(this);
	check(NormalRT);
	NormalRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGB10A2;
	NormalRT->ClearColor = FLinearColor(0.f,0.f,0.f,0.f);
	NormalRT->bAutoGenerateMips = false;
	NormalRT->bCanCreateUAV = true;    // Niagara RT DI requires UAV. Do we need to create it here?
	NormalRT->InitAutoFormat(Resolution, Resolution);	
	NormalRT->UpdateResourceImmediate(true);	
}

void UShallowWaterSubsystem::InitializeParameters()
{
	if (ShallowWaterNiagaraSimulation)
	{
		ShallowWaterNiagaraSimulation->SetVariableVec2(FName("WorldGridSize"), FVector2D(GetGridSize()));
		ShallowWaterNiagaraSimulation->SetVariableInt(FName("ResolutionMaxAxis"), GetGridResolution());
		ShallowWaterNiagaraSimulation->SetVariableTextureRenderTarget(FName("NormalRT"), NormalRT);		
	}
	else
	{
		UE_LOG(LogShallowWater, Error, TEXT("ShallowWaterComponent: No simulation found on component."))
	}
	
	if (MPC)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, MPC,
			Settings->WorldGridSizeMPCName,
			GetGridSize());
		UKismetMaterialLibrary::SetScalarParameterValue(this, MPC,
			Settings->ResolutionMaxAxisMPCName,
			GetGridResolution());
	}
	else
	{
		UE_LOG(LogShallowWater, Warning, TEXT("ShallowWaterComponent: No valid MPC found in Project Settings - Water Advanced. The simulation will work but would show preview from the Niagara renderer only."))
	}
}

void UShallowWaterSubsystem::UpdateGridMovement()
{
	if (ShallowWaterNiagaraSimulation == nullptr)
	{
		return;
	}
	
	APawn* CursorPawn = GetTheMostRelevantPlayerPawn();
	if (CursorPawn == nullptr)
	{
		return;
	}
	const FVector CursorPawnLocation = CursorPawn->GetActorLocation();
	
	/*
	 * Snap the sim center to player character
	 */
	// #note this query gets the closest water surface in 3D space, not a 2D topdown projection as we instinctively assumed
	// Could be better and could be worse

	float BestDistanceSqr = FLT_MAX;
	TOptional<FVector> BestWaterLocation;
	
	TWeakObjectPtr<AWaterBody> BestWaterBody;
	for (TWeakObjectPtr<AWaterBody> WeakWaterBody : LastOverlappingWaterBodies_Internal)
	{
		if (const AWaterBody* WaterBody = WeakWaterBody.Get())
		{
			const FWaterBodyQueryResult WaterInfo = WaterBody->GetWaterBodyComponent()->QueryWaterInfoClosestToWorldLocation(
				CursorPawnLocation, EWaterBodyQueryFlags::ComputeLocation);
			const FVector WaterLocation = WaterInfo.GetWaterSurfaceLocation();
			const float ds = FVector::DistSquared(WaterLocation, CursorPawnLocation); 
			if (ds < BestDistanceSqr)
			{
				BestDistanceSqr = ds;
				BestWaterLocation = WaterLocation;
				BestWaterBody = WeakWaterBody;
			}
		}
	}

	if (!BestWaterLocation.IsSet())
	{
		return;
	}

	const FVector ProjectedLocation = FVector(CursorPawnLocation.X, CursorPawnLocation.Y, 0);
	ShallowWaterNiagaraSimulation->SetWorldLocation(ProjectedLocation);
	ShallowWaterNiagaraSimulation->SetVariableFloat(FName("WaterHeightAtParent"), BestWaterLocation.GetValue().Z);

	// time left before sim is destroyed- used to attenuate waves
	const float SecondsSinceCollision = GetWorld()->GetTimeSeconds() - LastTimeOverlappingAnyWaterBody;
	const float SecondsUntilDestroyed = FMath::Max(0, GSWRemainActiveForSeconds - SecondsSinceCollision);
	ShallowWaterNiagaraSimulation->SetVariableFloat(FName("SecondsUntilDestroyed"), SecondsUntilDestroyed);

	ShallowWaterNiagaraSimulation->SetVariableBool(FName("UseDebugRender"), GSWDebugRender == 1);
	ShallowWaterNiagaraSimulation->SetVariableBool(FName("UseWaterInfoTexture"), GSWUseWaterInfoTexture == 1);

	bool UseBakedSim = false;
	if (BestWaterBody != nullptr && BestWaterBody->GetWaterBodyComponent())
	{
		if (const UWaterBodyComponent* WaterBodyComp = BestWaterBody->GetWaterBodyComponent())
		{
			if (WaterBodyComp->UseBakedSimulationForQueriesAndPhysics())
			{
				UBakedShallowWaterSimulationComponent* BakedSim = WaterBodyComp->GetBakedShallowWaterSimulation();				
				UTexture* BakedSimTex = Cast<UTexture>(BakedSim->SimulationData.BakedTexture.Get());

				if (BakedSimTex != nullptr)
				{
					ShallowWaterNiagaraSimulation->SetVariableVec3(FName("BakedWaterSimLocation"), BakedSim->SimulationData.Position);
					ShallowWaterNiagaraSimulation->SetVariableVec2(FName("BakedWaterSimSize"), BakedSim->SimulationData.Size);
					ShallowWaterNiagaraSimulation->SetVariableTexture(FName("BakedWaterSimTexture"), BakedSimTex);

					UseBakedSim = true;				
				}
			}
		}
	}
	ShallowWaterNiagaraSimulation->SetVariableBool(FName("UseBakedSim"), UseBakedSim);

#if ENABLE_DRAW_DEBUG
	if (GSWDrawWaterSurfaceProjection)
	{
		DrawDebugLine(GetWorld(), CursorPawnLocation, BestWaterLocation.GetValue(), FColor::Yellow, false, 0.5f);
		DrawDebugPoint(GetWorld(), BestWaterLocation.GetValue(), 5.f, FColor::Yellow, false, 0.5f);
	}
#endif

	/*
	 * Feed sim center to MPC
	 */
	if (MPC != nullptr)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(this, MPC, Settings->GridCenterMPCName,
			FLinearColor(PreviousProjectedLocation.X, PreviousProjectedLocation.Y, 0.f, 0.f));
	}
	PreviousProjectedLocation = ProjectedLocation;
}

void UShallowWaterSubsystem::RegisterImpact(FVector ImpactPosition, FVector ImpactVelocity, float ImpactRadius)
{
	if (ShallowWaterNiagaraSimulation == nullptr)
	{
		return;
	}
	
	FHitResult OutHit;
	GetWorld()->LineTraceSingleByChannel(OutHit, ImpactPosition + FVector(0,0,10.f), ImpactPosition + FVector(0,0,-10.f),
		GetImpactCollisionChannel());
	AWaterBody* WaterBody = Cast<AWaterBody>(OutHit.GetActor());
	if (WaterBody == nullptr)
	{
		return;
	}
	Tracker_Directs.Add(FShallowWaterCollisionTracker_Direct(GetWorld()->GetTimeSeconds(), GSWImpactTrackerActiveForSeconds, WaterBody));
	
	if (!ShallowWaterNiagaraSimulation->IsActive())
	{
		// queue up impacts we missed
		PendingImpact TmpImpact;
		TmpImpact.ImpactPosition = ImpactPosition;
		TmpImpact.ImpactVelocity = ImpactVelocity;
		TmpImpact.ImpactRadius = ImpactRadius;
		PendingImpacts.Add(TmpImpact);

		return;
	}

	WriteImpactToNDC(ImpactPosition, ImpactVelocity, ImpactRadius);
}

// these impacts are a frame or two late, and we only want to update the Niagara system and not 
// perform an additional overlap test
void UShallowWaterSubsystem::FlushPendingImpacts()
{
	for (PendingImpact CurrPendingImpact : PendingImpacts)
	{
		WriteImpactToNDC(CurrPendingImpact.ImpactPosition, CurrPendingImpact.ImpactVelocity, CurrPendingImpact.ImpactRadius);
	}
	PendingImpacts.Reset();
}

void UShallowWaterSubsystem::WriteImpactToNDC(FVector ImpactPosition, FVector ImpactVelocity, float ImpactRadius)
{
	FNiagaraDataChannelSearchParameters SearchParams(ImpactPosition);
	const UNiagaraDataChannelAsset* NDC = Settings->DefaultShallowWaterCollisionNDC.Get();
	if (UNiagaraDataChannelWriter* DCWriter = UNiagaraDataChannelLibrary::WriteToNiagaraDataChannel(ShallowWaterNiagaraSimulation, NDC, SearchParams, 1, false, true, true, TEXT("ShallowWaterWriteImpact")))
	{
		int32 Index = 0;
		DCWriter->WritePosition(TEXT("Position"), Index, ImpactPosition);
		DCWriter->WriteVector(TEXT("Velocity"), Index, ImpactVelocity);
		DCWriter->WriteFloat(TEXT("Radius"), Index, ImpactRadius);
	}
}

void UShallowWaterSubsystem::SetWaterBodyMIDParameters(AWaterBody* WaterBody)
{
	if (WaterBody)
	{
		if (IsShallowWaterInitialized())
		{
			TryUpdateWaterBodyMIDParameters(WaterBody->GetWaterBodyComponent());
		}
		else
		{
			// On BeginPlay this might not be ready yet
			PendingWaterBodiesToSetMIDOnInitialize.AddUnique(WaterBody);
		}
	}
}

void UShallowWaterSubsystem::TryUpdateWaterBodyMIDParameters(UWaterBodyComponent* WaterBodyComponent)
{
	if (WaterBodyComponent == nullptr || WaterBodyComponentsWithProperMIDParameters.Contains(WaterBodyComponent))
	{
		// UE_LOG(LogShallowWater, Warning, TEXT("TryUpdateWaterBodyMIDParameters failed"));
		return;
	}
	WaterBodyComponentsWithProperMIDParameters.Add(WaterBodyComponent);
	
	if (UMaterialInstanceDynamic* WaterMID = WaterBodyComponent->GetWaterMaterialInstance(); WaterMID)
	{
		WaterMID->SetTextureParameterValue(Settings->NormalRTMaterialName, NormalRT);
		WaterMID->SetScalarParameterValue(FName(TEXT("DEV_UseNewShallowWaterSubsystem")), 1.f);   // Temp before replacing OG system
	}
	if (UMaterialInstanceDynamic* WaterMID = WaterBodyComponent->GetRiverToOceanTransitionMaterialInstance(); WaterMID)
	{
		WaterMID->SetTextureParameterValue(Settings->NormalRTMaterialName, NormalRT);
		WaterMID->SetScalarParameterValue(FName(TEXT("DEV_UseNewShallowWaterSubsystem")), 1.f);   // Temp before replacing OG system
	}
	if (UMaterialInstanceDynamic* WaterMID = WaterBodyComponent->GetRiverToLakeTransitionMaterialInstance(); WaterMID)
	{
		WaterMID->SetTextureParameterValue(Settings->NormalRTMaterialName, NormalRT);
		WaterMID->SetScalarParameterValue(FName(TEXT("DEV_UseNewShallowWaterSubsystem")), 1.f);   // Temp before replacing OG system
	}
}

void UShallowWaterSubsystem::RegisterPhysicsAssetProxiesDataAsset(const UShallowWaterPhysicsAssetOverridesDataAsset* Proxies)
{
	if (Proxies == nullptr)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("ShallowWaterComponent: UShallowWaterPhysicsAssetOverridesDataAsset is NULL.  No vehicle interaction will be possible."))
		return;
	}
	else if (Proxies->Overrides.Num() <= 0)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("ShallowWaterComponent: Input UShallowWaterPhysicsAssetOverridesDataAsset: %s has 0 entries.  No additional vehicles will be supported."), *Proxies->GetName())
		return;
	}

	for (const TTuple<FGameplayTag, FShallowWaterPhysicsAssetOverride>& Override : Proxies->Overrides)
	{
		if (RegisteredPhysicsAssetProxies.Contains(Override.Key))
		{
			UE_LOG(LogShallowWater, Log, TEXT("Physics Asset Override in %s is overwriting an existing Override. GameplayTag = %s. This could be intended."),
				*Proxies->GetName(), *Override.Key.ToString())
		}
	}
	RegisteredPhysicsAssetProxies.Append(Proxies->Overrides);

	if (RegisteredPhysicsAssetProxies.Num() <= 0)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("ShallowWaterComponent: RegisteredPhysicsAssetProxies has 0 entries.  No vehicle interaction will be possible."))
		return;
	}
}

TSet<AWaterBody*> UShallowWaterSubsystem::GetAllOverlappingWaterBodiesAndUpdateCollisionTrackers() 
{
	TSet<AWaterBody*> Result = GetOverlappingWaterBodiesFromPawns();
	Result.Append(GetOverlappingWaterBodiesFromActorTrackersAndUpdate());
	Result.Append(GetOverlappingWaterBodiesFromDirectTrackersAndUpdate());
	return Result;
}

void UShallowWaterSubsystem::AddCollisionTrackerForActor(AActor* CollisionTrackerActor, float MaxLifespan)
{
	if(CollisionTrackerActor == nullptr)
	{
		return;
	}
		
	bool bFound = false;
	for (FShallowWaterCollisionTracker_Actor& Tracker : Tracker_Actors)
	{
		if (Tracker.CollisionActor == CollisionTrackerActor)
		{
			Tracker.TimeSpawned = GetWorld()->GetTimeSeconds();
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		Tracker_Actors.Add(FShallowWaterCollisionTracker_Actor(GetWorld()->GetTimeSeconds(), MaxLifespan, CollisionTrackerActor));
	}
}

void UShallowWaterSubsystem::RemoveCollisionTrackerForActor(AActor* CollisionTrackerActor)
{
	if(CollisionTrackerActor == nullptr)
	{
		return;
	}

	for (int32 Index = Tracker_Actors.Num()-1; Index >= 0; Index--)
	{
		if (Tracker_Actors[Index].CollisionActor == CollisionTrackerActor)
		{
			Tracker_Actors.RemoveAt(Index);
		}
	}
}

TSet<AWaterBody*> UShallowWaterSubsystem::GetOverlappingWaterBodiesFromPawns() const
{
	TSet<AWaterBody*> Result;
	TArray<APawn*> Pawns = GetPawnsInRange();
	for (const APawn* Pawn : Pawns)
	{
		TArray<AActor*> OverlappingActors;
		Pawn->GetOverlappingActors(OverlappingActors, AWaterBody::StaticClass());
		for (AActor* WaterBody : OverlappingActors)
		{
			if (AWaterBody* Actor = Cast<AWaterBody>(WaterBody))
			{
				Result.Add(Actor);
			}
		}
	}
	return Result;
}

TSet<AWaterBody*> UShallowWaterSubsystem::GetOverlappingWaterBodiesFromActorTrackersAndUpdate()
{
	TSet<AWaterBody*> Result;
	for (int32 Index = Tracker_Actors.Num() - 1; Index >= 0; Index--)
	{
		if (!Tracker_Actors[Index].IsValid(GetWorld()->GetTimeSeconds()))
		{
			Tracker_Actors.RemoveAt(Index);
			continue;
		}
		Result.Append(Tracker_Actors[Index].GetOverlappingWaterBodies());
	}
	return Result;
}

TSet<AWaterBody*> UShallowWaterSubsystem::GetOverlappingWaterBodiesFromDirectTrackersAndUpdate()
{
	TSet<AWaterBody*> Result;
	for (int32 Index = Tracker_Directs.Num() - 1; Index >= 0; Index--)
	{
		if (!Tracker_Directs[Index].IsValid(GetWorld()->GetTimeSeconds()))
		{
			Tracker_Directs.RemoveAt(Index);
			continue;
		}
		if(AWaterBody* WaterBody = Tracker_Directs[Index].GetOverlappingWaterBody())
		{
			Result.Add(WaterBody);
		}
	}
	return Result;
}

void UShallowWaterSubsystem::UpdateOverlappingWaterBodiesHistory(TArray<AWaterBody*> OverlappingWaterBodies)
{
	if (!OverlappingWaterBodies.IsEmpty())
	{
		LastTimeOverlappingAnyWaterBody = GetWorld()->GetTimeSeconds();
		LastOverlappingWaterBodies_Internal.Reset();
		for (AWaterBody* WaterBody : OverlappingWaterBodies)
		{
			LastOverlappingWaterBodies_Internal.Add(WaterBody);  			
		}
	}
}

bool UShallowWaterSubsystem::ShouldSimulateThisFrame() const
{
	return (GetWorld()->GetTimeSeconds() - LastTimeOverlappingAnyWaterBody <= GSWRemainActiveForSeconds);
}

int32 UShallowWaterSubsystem::UpdateActivePawns()
{
	const APawn* CursorPawn = GetTheMostRelevantPlayerPawn();
	if (CursorPawn == nullptr)
	{
		ActivePawns.Empty();
		return 0;
	}
	FVector CursorPawnLocation = CursorPawn->GetActorLocation();
	
	// Remove irrelevant pawns from ActivePawns
	ActivePawns.RemoveAll([&](TWeakObjectPtr<APawn> Pawn)
	{
		if(Pawn == nullptr)
		{
			return true;
		}
		if ((Pawn->GetActorLocation() - CursorPawnLocation).SquaredLength() > GetColliderMaxRange() * GetColliderMaxRange())
		{
			return true;
		}
		return false;
	});

	const int32 SlotsLeft = Settings->MaxActivePawnNum - ActivePawns.Num();
	int32 NewPawnsAdded = 0;
	if (SlotsLeft > 0)
	{
		// Get pawns nearby who become relevant this frame
		TArray<APawn*> RelevantPawns = GetPawnsInRange();
		RelevantPawns.RemoveAll([&](APawn* Pawn)
		{
			for (const TWeakObjectPtr<APawn> Other : ActivePawns)
			{
				if (Pawn == nullptr || Pawn == Other.Get())
				{
					return true;
				}
			}
			return false;
		});

		// Sort pawn candidates
		RelevantPawns.Sort([CursorPawnLocation] (const APawn& Left, const APawn& Right)
		{
			// Compare 3D distance since the other player right above you, although might be pretty far away, are going to drop on your face fast
			// Thus prioritized over the player in water but a little further away
			return (Left.GetActorLocation() - CursorPawnLocation).SquaredLength()
			< (Right.GetActorLocation() - CursorPawnLocation).SquaredLength();
		});

		NewPawnsAdded = FMath::Min(SlotsLeft, RelevantPawns.Num());
		for (int32 Index = 0; Index < NewPawnsAdded; Index++)
		{
			ActivePawns.Add(RelevantPawns[Index]);
		}

#if ENABLE_DRAW_DEBUG
		if (Settings->bVisualizeActivePawn)
		{
			// Use Niagara to draw debug should be better. If yes delete this
		}
#endif
	}
	
	return NewPawnsAdded;
}

void UShallowWaterSubsystem::GatherContextsFromPawns(TArray<TWeakObjectPtr<APawn>> InActivePawns)
{
	for (const TWeakObjectPtr<APawn>& Pawn : InActivePawns)
	{
		if (APawn* PawnPtr = Pawn.Get())
		{
			TOptional<FShallowWaterCollisionContext> Context = GetCollisionContextFromPawn(PawnPtr);
			if (Context.IsSet())
			{
				// AddUnique() since multiple pawns can return the same Context. E.g. a multi-seat vehicle
				PendingContexts.AddUnique(Context.GetValue());
			}
		}
	}
}

TOptional<FShallowWaterCollisionContext> UShallowWaterSubsystem::GetCollisionContextFromPawn(APawn* InPawn) const
{
	TOptional<FShallowWaterCollisionContext> Result;
	USkeletalMeshComponent* Component = nullptr;
	const ACharacter* Character = Cast<ACharacter>(InPawn);
	if (Character)
	{
		Component = Character->GetMesh();
	}
	else
	{
		TArray<USkeletalMeshComponent*> SKMs;
		InPawn->GetComponents<USkeletalMeshComponent>(SKMs);
		if (!SKMs.IsEmpty())
		{
			Component = SKMs[0];
		}
	}
	if (Component)
	{
		Result = FShallowWaterCollisionContext(EShallowWaterCollisionContextType::Pawn, Component); 
	}
	return Result;
}

void UShallowWaterSubsystem::CleanUpVehicleCollisionProxies()
{
	TArray<TTuple<const FShallowWaterCollisionContext, USkeletalMeshComponent*>> EntriesToRemove;
	for (TTuple<const FShallowWaterCollisionContext, USkeletalMeshComponent*> Entry : VehicleCollisionProxies)
	{
		// Shotgun approach to cover all weird possibilities
		// E.g. if the Vehicle component is destroyed but not the owning actor somehow
		if ( Entry.Key.Component == nullptr || Entry.Key.Component->IsBeingDestroyed() ||
			Entry.Value == nullptr || Entry.Value->IsBeingDestroyed())
		{
			EntriesToRemove.Add(Entry);
		}
	}
	for (TTuple<const FShallowWaterCollisionContext, USkeletalMeshComponent*> Entry : EntriesToRemove)
	{
		DisableCollisionForVehicle(Entry.Key);
	}
}

void UShallowWaterSubsystem::UpdateCollisionForPendingContexts()
{
	CleanUpVehicleCollisionProxies();
	
	// Find components that need their collision disabled
	TArray<FShallowWaterCollisionContext> ContextsToDisable = PreviousContexts;
	ContextsToDisable.RemoveAll([&] (FShallowWaterCollisionContext Context)
	{
		return PendingContexts.Find(Context) != INDEX_NONE;
	});

	// Find components that don't already exist
	TArray<FShallowWaterCollisionContext> ContextsToEnable = PendingContexts;
	ContextsToEnable.RemoveAll([&] (FShallowWaterCollisionContext Context)
	{
		return PreviousContexts.Find(Context) != INDEX_NONE;
	});
	

	
	for (const FShallowWaterCollisionContext& Context : ContextsToDisable)
	{
		DisableCollisionForContext(Context);
	}
	for (const FShallowWaterCollisionContext& Context : ContextsToEnable)
	{
		EnableCollisionForContext(Context);
	}

	PreviousContexts = PendingContexts;
	PendingContexts.Empty();
}

void UShallowWaterSubsystem::EnableCollisionForContext(const FShallowWaterCollisionContext& Context)
{
	if (Context.Component == nullptr)
	{		
		UE_LOG(LogShallowWater, Warning, TEXT("EnableCollisionForContext() - Context Component is nullptr"));
		return;
	}
	switch (Context.Type)
	{
	case EShallowWaterCollisionContextType::Pawn:
		
		Context.Component->ComponentTags.AddUnique(ColliderComponentTag);
		break;
		
	case EShallowWaterCollisionContextType::Vehicle:
		{
		// Spawn proxy SKM, attach to vehicle
		const FString BaseName = TEXT("FluidsimCollisionProxy");
		const FName CompName = MakeUniqueObjectName(this, USkeletalMeshComponent::StaticClass(), FName(*BaseName));
		USkeletalMeshComponent* ProxyComp = NewObject<USkeletalMeshComponent>(Context.Component->GetOwner(), USkeletalMeshComponent::StaticClass(), CompName);
		ProxyComp->SetSkeletalMeshAsset(Context.Component->GetSkeletalMeshAsset());

		// Spawn an empty dummy SKM component to be used as Collision 
		// Apply PhysicsAsset override if defined in the data asset
		// #todo confirm if SetLeaderPoseComponent actually works if Mesh is set to empty #PLAY-29387
		UPhysicsAsset* PhysicsAssetOverride = nullptr;
		bool IsSet = false;
		if (!RegisteredPhysicsAssetProxies.IsEmpty())
		{
			const FGameplayTagContainer VehicleTags = GetVehicleTags(Context);

			const FShallowWaterPhysicsAssetOverride* FoundOverride = nullptr;
			for (FGameplayTag Tag : VehicleTags)
			{
				FoundOverride = RegisteredPhysicsAssetProxies.Find(Tag);
				if (FoundOverride)
				{
					break;
				}
			}
			if (FoundOverride)
			{
				IsSet = true;

				TSoftObjectPtr<UPhysicsAsset> TmpPhysicsAsset = FoundOverride->PhysicsAsset;

				UAssetManager::GetStreamableManager().RequestAsyncLoad(TmpPhysicsAsset.ToSoftObjectPath(),
					FStreamableDelegate::CreateWeakLambda(this, [this, TmpPhysicsAsset, ProxyComp, Context]()
						{								
							if (TmpPhysicsAsset.IsValid())
							{								
								UPhysicsAsset* PhysicsAssetOverride = TmpPhysicsAsset.Get();

								ProxyComp->SetPhysicsAsset(PhysicsAssetOverride);

								ProxyComp->ComponentTags.AddUnique(ColliderComponentTag);

								ProxyComp->SetupAttachment(Context.Component->GetOwner()->GetRootComponent());
								ProxyComp->SetWorldTransform(Context.Component->GetComponentTransform());
								ProxyComp->SetLeaderPoseComponent(Context.Component);
								ProxyComp->SetVisibility(false);
								ProxyComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
								ProxyComp->RegisterComponent();

								VehicleCollisionProxies.Add(Context, ProxyComp);
							}
						})
				);
			}			
		}
		if (!IsSet)
		{
			if (GSWUseFullVehiclePhysicsAssets)
			{
				Context.Component->ComponentTags.AddUnique(ColliderComponentTag);
			}
			else
			{
			 	UE_LOG(LogShallowWater, Warning, TEXT("EnableCollisionForContext() - Vehicle will not have collisions because no physics asset override was found"));
			}
		}
		break;
		}
		
	default:
		;
	}
}

void UShallowWaterSubsystem::DisableCollisionForContext(const FShallowWaterCollisionContext& Context)
{
	if (!Context.IsValidAndAlive())
	{
		switch (Context.Type)
		{
		case EShallowWaterCollisionContextType::Pawn:
			break;
		case EShallowWaterCollisionContextType::Vehicle:
			DisableCollisionForVehicle(Context);
			break;
		default:
			;
		}
		// Otherwise no care needed because the component is dead
		return;
	}

	switch (Context.Type)
	{
	case EShallowWaterCollisionContextType::Pawn:
		Context.Component->ComponentTags.Remove(ColliderComponentTag);
		break;
	case EShallowWaterCollisionContextType::Vehicle:
		DisableCollisionForVehicle(Context);
		break;
	default:
		;
	}
}

void UShallowWaterSubsystem::DisableCollisionForVehicle(const FShallowWaterCollisionContext& Context)
{
	if (Context.Type != EShallowWaterCollisionContextType::Vehicle)
	{
		return;
	}

	TObjectPtr<USkeletalMeshComponent>* ProxyPointer = VehicleCollisionProxies.Find(Context);
	if (ProxyPointer == nullptr)
	{		
		return;
	}
	
	USkeletalMeshComponent* Proxy = ToRawPtr(*ProxyPointer);
	if (Proxy && !Proxy->IsBeingDestroyed())
	{
		Proxy->DestroyComponent();
	}
	VehicleCollisionProxies.Remove(Context);
}

float UShallowWaterSubsystem::GetColliderMaxRange() const
{
	return GetGridSize() * 0.5f;
}

TArray<APawn*> UShallowWaterSubsystem::GetPawnsInRange(const bool bShouldSortBySignificance) const
{
	const APlayerController* Controller = WeakPlayerController.Get();
	APawn* CursorPawn = nullptr;
	if (Controller)
	{
		CursorPawn = GetNonSpectatorPawnFromWeakController();
	}
	
	FVector ObservingLocation;
	if (CursorPawn)
	{
		ObservingLocation = CursorPawn->GetActorLocation();
	}
	else
	{
		TOptional<FVector> CamLoc = GetCameraLocationFromWeakController();
		if (CamLoc.IsSet())
		{
			ObservingLocation = CamLoc.GetValue();
		}
		else
		{
			// Not even camera location is available. Game state not valid.
			return TArray<APawn*>();
		}
	}

	return GetPawnsInRange(ObservingLocation, bShouldSortBySignificance);
}

TArray<APawn*> UShallowWaterSubsystem::GetPawnsInRange(const FVector ObservingLocation, const bool bShouldSortBySignificance) const
{
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(Cast<UObject>(GetWorld()), APawn::StaticClass(), FoundActors);
	TArray<APawn*> Results;
	for (AActor* Actor : FoundActors)
	{
		const float Distance = FVector::Distance(ObservingLocation, Actor->GetActorLocation());
		if (Distance <= GetColliderMaxRange())
		{
			Results.Add(Cast<APawn>(Actor));
		}
	}

	if (bShouldSortBySignificance)
	{
		Results.Sort([ObservingLocation](APawn& PawnA, APawn& PawnB)
		{
			return FVector::DistSquared(ObservingLocation, PawnA.GetActorLocation()) < FVector::DistSquared(ObservingLocation, PawnB.GetActorLocation());
		});
	}
	
	return Results;
}

void UShallowWaterSubsystem::TryGetOrWaitForWaterInfoTextureFromWaterBodies(TSet<AWaterBody*> CurrentWaterBodies)
{
    for (const AWaterBody* CurrentWaterBody : CurrentWaterBodies)
    {
	    if (const UWaterBodyComponent* WaterBodyComp = CurrentWaterBody->GetWaterBodyComponent())
	    {
	    	if (AWaterZone* WaterZone = WaterBodyComp->GetWaterZone())
	    	{
	    		const TObjectPtr<UTextureRenderTarget2DArray> NewWaterInfoTexture = WaterZone->WaterInfoTextureArray;

	    		if (NewWaterInfoTexture == nullptr)
	    		{
	    			WaterZone->GetOnWaterInfoTextureArrayCreated().RemoveDynamic(this, &UShallowWaterSubsystem::OnWaterInfoTextureArrayCreated);
	    			WaterZone->GetOnWaterInfoTextureArrayCreated().AddDynamic(this, &UShallowWaterSubsystem::OnWaterInfoTextureArrayCreated);
	    		}
	    		else
	    		{
	    			OnWaterInfoTextureArrayCreated(NewWaterInfoTexture);
	    		}

				if (ShallowWaterNiagaraSimulation)
				{
					// The following index assume that there is no split screen support and will request the position of the first player's water view.
					const int32 PlayerIndex = 0;
					const FVector2D ZoneLocation = FVector2D(WaterZone->GetDynamicWaterInfoCenter(PlayerIndex));

					const FVector2D ZoneExtent = FVector2D(WaterZone->GetDynamicWaterInfoExtent());
					const FVector2D WaterHeightExtents = FVector2D(WaterZone->GetWaterHeightExtents());
					const float GroundZMin = WaterZone->GetGroundZMin();

					ShallowWaterNiagaraSimulation->SetVariableVec2(FName("WaterZoneLocation"), ZoneLocation);
					ShallowWaterNiagaraSimulation->SetVariableVec2(FName("WaterZoneExtent"), ZoneExtent);
					ShallowWaterNiagaraSimulation->SetVariableInt(FName("WaterZoneIdx"), WaterZone->GetWaterZoneIndex());
				}
				else
				{
					ensureMsgf(false, TEXT("UShallowWaterSubsystem::TryGetOrWaitForWaterInfoTextureFromWaterBodies was called with NULL ShallowWaterNiagaraSimulation"));
					return;
				}
	    		// Currently there can only be one unique WaterInfoTexture
	    		break;
	    	}
	    }
    }
}

void UShallowWaterSubsystem::OnWaterInfoTextureArrayCreated(const UTextureRenderTarget2DArray* InWaterInfoTexture)
{
	if(InWaterInfoTexture == nullptr)
	{
		ensureMsgf(false, TEXT("UShallowWaterSubsystem::OnWaterInfoTextureCreated was called with NULL WaterInfoTexture"));
		return;
	}
	WaterInfoTexture = InWaterInfoTexture;
	if (ShallowWaterNiagaraSimulation)
	{
		UTexture* WITTextureArray = Cast<UTexture>(const_cast<UTextureRenderTarget2DArray*>(WaterInfoTexture.Get()));
		if (WITTextureArray == nullptr)
		{
			ensureMsgf(false, TEXT("UShallowWaterSubsystem::OnWaterInfoTextureCreated was called with Water Info Texture that isn't valid"));
			return;
		}

		ShallowWaterNiagaraSimulation->SetVariableTexture(FName("WaterInfoTexture"), WITTextureArray);
	}
	else
	{
		ensureMsgf(false, TEXT("UShallowWaterSubsystem::OnWaterInfoTextureCreated was called with NULL ShallowWaterNiagaraSimulation"));
		return;
	}
}

void UShallowWaterSubsystem::OnLocalPlayerControllerBecomesValid(APlayerController* InPlayerController)
{
	if (InPlayerController == nullptr)
	{
		UE_LOG(LogShallowWater, Log, TEXT("OnLocalPlayerControllerBecomesValid() returned nullptr"))
		return;
	}
	
	UE_LOG(LogShallowWater, Log, TEXT("OnLocalPlayerControllerBecomesValid() returned PC: %s"), *InPlayerController->GetFullName())
	WeakPlayerController = InPlayerController;
	if (APawn* const Pawn = InPlayerController->GetPawn())
	{
		OnLocalPlayerPawnBecomesValid(nullptr, Pawn);
	}
	InPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnLocalPlayerPawnBecomesValid);
	InPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::OnLocalPlayerPawnBecomesValid);
}

void UShallowWaterSubsystem::OnLocalPlayerPawnBecomesValid(APawn* OldPawn, APawn* NewPawn)
{
	if (NewPawn == nullptr)
	{
		UE_LOG(LogShallowWater, Log, TEXT("OnLocalPlayerPawnBecomesValid() returned nullptr"))
		return;
	}
	
	UE_LOG(LogShallowWater, Log, TEXT("OnLocalPlayerPawnBecomesValid() returned Pawn: %s"), *NewPawn->GetFullName())
	// #todo Should re-initialize if pawn changed, or uninitialize if pawn lost 
	if (!IsShallowWaterInitialized())
	{
		InitializeShallowWater();
	}
	else
	{
		UE_LOG(LogShallowWater, Log, TEXT("OnLocalPlayerPawnBecomesValid called but subsystem is already initialized."));
	}
}

//UE_ENABLE_OPTIMIZATION_SHIP
