// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Backends/MoverBackendLiaison.h"
#include "MoverTypes.h"

#include "MoverStandaloneLiaison.generated.h"

class UMoverComponent;

/**
 * MoverStandaloneLiaison: this component acts as a backend driver for an actor's Mover component, for use in Standalone (non-networked) games.
 * This class is set on a Mover component as the "back end".
 * TODO: Support options for fixed ticking rates and state smoothing
 */
UCLASS()
class MOVER_API UMoverStandaloneLiaisonComponent : public UActorComponent, public IMoverBackendLiaisonInterface
{
	GENERATED_BODY()

public:
	UMoverStandaloneLiaisonComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// IMoverBackendLiaisonInterface
	virtual float GetCurrentSimTimeMs() override;
	virtual int32 GetCurrentSimFrame() override;
	virtual bool ReadPendingSyncState(OUT FMoverSyncState& OutSyncState) override;
	virtual bool WritePendingSyncState(const FMoverSyncState& SyncStateToWrite) override;
	// End IMoverBackendLiaisonInterface

	// Begin UActorComponent interface
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// End UActorComponent interface


protected:
	TObjectPtr<UMoverComponent> MoverComp;	// the component that we're in charge of driving

	double CurrentSimTimeMs;
	int32 CurrentSimFrame;

	FMoverSyncState CachedLastSyncState;
	FMoverAuxStateContext CachedLastAuxState;
};