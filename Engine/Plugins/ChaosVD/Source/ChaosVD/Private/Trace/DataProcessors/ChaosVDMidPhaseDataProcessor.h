// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ChaosVDDataProcessorBase.h"
#include "Templates/SharedPointer.h"

struct FChaosVDSolverFrameData;
struct FChaosVDStepData;
struct FChaosVDParticlePairMidPhase;

/**
 * Data processor implementation that is able to deserialize traced MidPhases 
 */
class FChaosVDMidPhaseDataProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FChaosVDMidPhaseDataProcessor();

	virtual bool ProcessRawData(const TArray<uint8>& InData) override;

	void AddMidPhaseToParticleIDMap(const TSharedPtr<FChaosVDParticlePairMidPhase>& MidPhaseData, int32 ParticleID, FChaosVDStepData& InSolverStageData);
};
