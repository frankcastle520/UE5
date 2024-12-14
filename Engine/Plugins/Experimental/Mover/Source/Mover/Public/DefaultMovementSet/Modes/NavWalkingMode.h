// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovementMode.h"
#include "MoveLibrary/BasedMovementUtils.h"
#include "AI/Navigation/NavigationTypes.h"
#include "NavWalkingMode.generated.h"

class UNavMoverComponent;
class INavigationDataInterface;
class UCommonLegacyMovementSettings;

namespace NavWalkingBlackBoard
{
	const FName ProjectedNavMeshHitResult = TEXT("ProjectedNavMeshHitResult");
}

/**
 * NavWalkingMode: a default movement mode for traversing surfaces and movement bases by using an active navmesh when moving the actor rather than collision checks.
 * Note: This movement mode requires a NavMoverComponent be on the actor to function properly. This mode also contains some randomization to avoid navmesh look ups
 *	happening at the same time (which is fine for AI characters running on the server) but may cause issues if used on autonomous proxies.
 */
UCLASS(Blueprintable, BlueprintType)
class MOVER_API UNavWalkingMode : public UBaseMovementMode
{
	GENERATED_BODY()

public:
	UNavWalkingMode();
	
	UFUNCTION(BlueprintCallable, Category=Mover)
	virtual void OnGenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const override;

	UFUNCTION(BlueprintCallable, Category=Mover)
	virtual void OnSimulationTick(const FSimulationTickParams& Params, FMoverTickEndData& OutputState) override;

	/**
	 * Whether or not the actor should sweep for collision geometry while walking.
	 */
	UPROPERTY(Category="NavMesh Movement", EditAnywhere, BlueprintReadWrite)
	bool bSweepWhileNavWalking;
	
	/** Whether to raycast to underlying geometry to better conform navmesh-walking actors */
	UPROPERTY(Category="NavMesh Movement", EditAnywhere, BlueprintReadOnly)
	bool bProjectNavMeshWalking;

	/**
	 * Scale of the total capsule height to use for projection from navmesh to underlying geometry in the upward direction.
	 * In other words, start the trace at [CapsuleHeight * NavMeshProjectionHeightScaleUp] above nav mesh.
	 */
	UPROPERTY(Category="NavMesh Movement", EditAnywhere, BlueprintReadWrite, meta=(editcondition = "bProjectNavMeshWalking", ClampMin="0", UIMin="0"))
	float NavMeshProjectionHeightScaleUp;

	/**
	 * Scale of the total capsule height to use for projection from navmesh to underlying geometry in the downward direction.
	 * In other words, trace down to [CapsuleHeight * NavMeshProjectionHeightScaleDown] below nav mesh.
	 */
	UPROPERTY(Category="NavMesh Movement", EditAnywhere, BlueprintReadWrite, meta=(editcondition = "bProjectNavMeshWalking", ClampMin="0", UIMin="0"))
	float NavMeshProjectionHeightScaleDown;

	/** How often we should raycast to project from navmesh to underlying geometry */
	UPROPERTY(Category="NavMesh Movement", EditAnywhere, BlueprintReadWrite, meta=(editcondition = "bProjectNavMeshWalking", ForceUnits="s"))
	float NavMeshProjectionInterval;

	/** Speed at which to interpolate agent navmesh offset between traces. 0: Instant (no interp) > 0: Interp speed") */
	UPROPERTY(Category="NavMesh Movement", EditAnywhere, BlueprintReadWrite, meta=(editcondition = "bProjectNavMeshWalking", ClampMin="0", UIMin="0"))
	float NavMeshProjectionInterpSpeed;
	
	UPROPERTY(Transient)
	float NavMeshProjectionTimer;
	
	/** last known location projected on navmesh */
	FNavLocation CachedNavLocation;
	
	/**
	 * Project a location to navmesh to find adjusted height.
	 * @param TestLocation		Location to project
	 * @param NavFloorLocation	Location on navmesh
	 * @return True if projection was performed (successfully or not)
	 */
	virtual bool FindNavFloor(const FVector& TestLocation, FNavLocation& NavFloorLocation) const;

	// Returns the active turn generator. Note: you will need to cast the return value to the generator you expect to get, it can also be none
	UFUNCTION(BlueprintPure, Category=Mover)
	UObject* GetTurnGenerator();

	// Sets the active turn generator to use the class provided. Note: To set it back to the default implementation pass in none
	UFUNCTION(BlueprintCallable, Category=Mover)
	void SetTurnGeneratorClass(UPARAM(meta=(MustImplement="/Script/Mover.TurnGeneratorInterface", AllowAbstract="false")) TSubclassOf<UObject> TurnGeneratorClass);
	
protected:
	/** Associated Movement component that will actually move the actor */ 
	UPROPERTY(BlueprintReadOnly, Category="Nav Movement")
	TObjectPtr<UNavMoverComponent> NavMoverComponent;
	
	/** Use both WorldStatic and WorldDynamic channels for NavWalking geometry conforming */
	UPROPERTY(Category = "NavMesh Movement", EditAnywhere, BlueprintReadOnly, AdvancedDisplay)
	uint8 bProjectNavMeshOnBothWorldChannels : 1;

	/** Optional modular object for generating rotation towards desired orientation. If not specified, linear interpolation will be used. */
	UPROPERTY(EditAnywhere, Instanced, Category=Mover, meta=(MustImplement="/Script/Mover.TurnGeneratorInterface"))
	TObjectPtr<UObject> TurnGenerator;

	/** Switch collision settings for NavWalking mode (ignore world collisions) */
	virtual void SetCollisionForNavWalking(bool bEnable);
	
	virtual void OnActivate() override;

	virtual void OnDeactivate() override;
	
	/** Get Navigation data for the actor. Returns null if there is no associated nav data. */
	const INavigationDataInterface* GetNavData() const;

	/** Performs trace for ProjectLocationFromNavMesh */
	virtual void FindBestNavMeshLocation(const FVector& TraceStart, const FVector& TraceEnd, const FVector& CurrentFeetLocation, const FVector& TargetNavLocation, FHitResult& OutHitResult) const;

	/** 
	 * Attempts to better align navmesh walking actors with underlying geometry (sometimes 
	 * navmesh can differ quite significantly from geometry).
	 * Updates CachedProjectedNavMeshHitResult, access this for more info about hits.
	 */
	virtual FVector ProjectLocationFromNavMesh(float DeltaSeconds, const FVector& CurrentFeetLocation, const FVector& TargetNavLocation, float UpOffset, float DownOffset);

	
	virtual void OnRegistered(const FName ModeName) override;
	virtual void OnUnregistered() override;
	
	void CaptureFinalState(USceneComponent* UpdatedComponent, bool bDidAttemptMovement, const FFloorCheckResult& FloorResult, const FMovementRecord& Record, FMoverDefaultSyncState& OutputSyncState) const;

	FRelativeBaseInfo UpdateFloorAndBaseInfo(const FFloorCheckResult& FloorResult) const;
	
	TObjectPtr<const UCommonLegacyMovementSettings> CommonLegacySettings;
};
