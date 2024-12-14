// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "MoverSimulationTypes.h"
#include "MoverBackendLiaison.generated.h"

class FDataValidationContext;
class UMoverComponent;

/**
 * MoverBackendLiaisonInterface: any object or system wanting to be the driver of Mover actors must implement this. The intent is to act as a
 * middleman between the Mover actor and the system that drives it, such as the Network Prediction plugin.
 * In practice, objects implementing this interface should be some kind of UActorComponent. The Mover actor instantiates its backend liaison
 * when initialized, then relies on the liaison to call various functions as the simulation progresses. See @MoverComponent.
 */
UINTERFACE(MinimalAPI)
class UMoverBackendLiaisonInterface : public UInterface
{
	GENERATED_BODY()
};

class IMoverBackendLiaisonInterface
{
	GENERATED_BODY()

public:
	virtual float GetCurrentSimTimeMs() = 0;
	virtual int32 GetCurrentSimFrame() = 0;

	// Pending State: the simulation state currently being authored
	virtual bool ReadPendingSyncState(OUT FMoverSyncState& OutSyncState) { return false; }
	virtual bool WritePendingSyncState(const FMoverSyncState& SyncStateToWrite) { return false; }
	
	// Presentation State: the most recent presentation state, possibly the result of interpolation or smoothing. Writing to it does not affect the official simulation record.
	virtual bool ReadPresentationSyncState(OUT FMoverSyncState& OutSyncState) { return false; }
	virtual bool WritePresentationSyncState(const FMoverSyncState& SyncStateToWrite) { return false; }

	// Previous Presentation State: the state that our optional smoothing process is moving away from, towards a more recent state. Writing to it does not affect the official simulation record.
	virtual bool ReadPrevPresentationSyncState(OUT FMoverSyncState& OutSyncState) { return false; }
	virtual bool WritePrevPresentationSyncState(const FMoverSyncState& SyncStateToWrite) { return false; }

#if WITH_EDITOR
	virtual EDataValidationResult ValidateData(FDataValidationContext& Context, const UMoverComponent& ValidationMoverComp) const { return EDataValidationResult::Valid; }
#endif // WITH_EDITOR

};