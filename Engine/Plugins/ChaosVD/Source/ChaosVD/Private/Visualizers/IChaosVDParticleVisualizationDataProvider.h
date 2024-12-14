// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Math/Transform.h"
#include "HAL/Platform.h"
#include "Templates/SharedPointer.h"

class UChaosVDSettingsObjectBase;
class UChaosVDCollisionDataVisualizationSettings;
struct FChaosVDParticleDataWrapper;
class FChaosVDScene;

/** Interface to be used by any object that contains Particle Data that needs to be visualized.
 * @note As we are still not settled on if we will stick with having Actors to represent each particle, this interface allows us to abstract the Particle data access
 * instead of using directly AChaosVDParticleActor
 */
class IChaosVDParticleVisualizationDataProvider
{
public:
	virtual ~IChaosVDParticleVisualizationDataProvider() = default;

	virtual TSharedPtr<const FChaosVDParticleDataWrapper> GetParticleData() { return nullptr; }
};
