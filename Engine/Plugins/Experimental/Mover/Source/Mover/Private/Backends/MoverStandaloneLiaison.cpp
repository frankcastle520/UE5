// Copyright Epic Games, Inc. All Rights Reserved.

#include "Backends/MoverStandaloneLiaison.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MoverComponent.h"


UMoverStandaloneLiaisonComponent::UMoverStandaloneLiaisonComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bCanEverTick = true;

	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(false);

	CurrentSimTimeMs = 0.0;
	CurrentSimFrame = 0;

}

float UMoverStandaloneLiaisonComponent::GetCurrentSimTimeMs()
{
	return CurrentSimTimeMs;
}

int32 UMoverStandaloneLiaisonComponent::GetCurrentSimFrame()
{
	return CurrentSimFrame;
}


bool UMoverStandaloneLiaisonComponent::ReadPendingSyncState(OUT FMoverSyncState& OutSyncState)
{
	OutSyncState = CachedLastSyncState;
	return true;
}

bool UMoverStandaloneLiaisonComponent::WritePendingSyncState(const FMoverSyncState& SyncStateToWrite)
{
	CachedLastSyncState = SyncStateToWrite;
	return true;
}

void UMoverStandaloneLiaisonComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentSimTimeMs = GetWorld()->GetTimeSeconds() * 1000.0;
	CurrentSimFrame = 0;

	if (const AActor* OwnerActor = GetOwner())
	{
		ensureMsgf(OwnerActor->GetNetMode() == NM_Standalone, TEXT("UMoverStandaloneLiaisonComponent is only valid for use in Standalone projects. Movement will not work properly in networked play."));

		if (UMoverComponent* FoundMoverComp = OwnerActor->FindComponentByClass<UMoverComponent>())
		{
			MoverComp = FoundMoverComp;

			MoverComp->InitMoverSimulation();

			MoverComp->InitializeSimulationState(OUT &CachedLastSyncState, OUT &CachedLastAuxState);
		}
		else
		{
			ensureMsgf(MoverComp, TEXT("Owning actor %s does not have a MoverComponent."), *GetNameSafe(GetOwner()));
		}
	}	
}


void UMoverStandaloneLiaisonComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (MoverComp)
	{
		// TODO Here is where we might accumulate time and perform fixed tick updates

		const int32 DeltaTimeMs = DeltaTime * 1000.f;

		CurrentSimTimeMs = GetWorld()->GetTimeSeconds() * 1000.0;
		CurrentSimFrame = GFrameCounter;

		FMoverInputCmdContext InputCmd;
		MoverComp->ProduceInput(DeltaTimeMs, OUT &InputCmd);

		FMoverTimeStep TimeStep;
		TimeStep.ServerFrame = CurrentSimFrame;
		TimeStep.BaseSimTimeMs = CurrentSimTimeMs;
		TimeStep.StepMs = FMath::Floor(DeltaTime * 1000.f);

		FMoverTickStartData StartData;
		FMoverTickEndData EndData;

		StartData.InputCmd = InputCmd;
		StartData.SyncState = CachedLastSyncState;
		StartData.AuxState = CachedLastAuxState;

		MoverComp->SimulationTick(TimeStep, StartData, OUT EndData);

		CachedLastSyncState = EndData.SyncState;
		CachedLastAuxState = EndData.AuxState;

		MoverComp->FinalizeFrame(&EndData.SyncState, &EndData.AuxState);
	}

}


