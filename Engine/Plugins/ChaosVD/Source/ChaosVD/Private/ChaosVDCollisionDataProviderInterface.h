// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "ChaosVDCollisionDataProviderInterface.generated.h"

struct FChaosVDParticlePairMidPhase;
struct FChaosVDSolverDataSelectionHandle;
struct FChaosVDCollisionDataFinder;

// This class does not need to be modified.
UINTERFACE()
class UChaosVDCollisionDataProviderInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface for any object that is able to provide CVD Collision Data
 */
class IChaosVDCollisionDataProviderInterface
{
	GENERATED_BODY()

public:
	
	/**
	 * Gathers and populates the provided array with any existing collision data for this object (if any) 
	 */
	virtual TConstArrayView<TSharedPtr<FChaosVDParticlePairMidPhase>> GetCollisionData() PURE_VIRTUAL(IChaosVDCollisionDataProviderInterface::GetCollisionData, return TConstArrayView<TSharedPtr<FChaosVDParticlePairMidPhase>>(););

	/**
	 * Checks if the object implementing the interface has any collision data available
	 * @return True if any collision data is found 
	 */
	virtual bool HasCollisionData() PURE_VIRTUAL(IChaosVDCollisionDataProviderInterface::HasCollisionData, return false;)

	/**
	 * Returns the name of the object providing the collision data
	 */
	virtual FName GetProviderName() PURE_VIRTUAL(IChaosVDCollisionDataProviderInterface::GetProviderName, return FName();)
};
