// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#pragma once
#include "ChaosVDDataProcessorBase.h"

struct FChaosVDSolverFrameData;
struct FChaosVDConstraint;

/**
 * Data processor implementation that is able to deserialize traced character ground constraints
 */
class FChaosVDCharacterGroundConstraintDataProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FChaosVDCharacterGroundConstraintDataProcessor();
	
	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};


