// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/DataProcessors/ChaosVDDataProcessorBase.h"

/**
 * Data processor implementation that is able to deserialize traced Particles data
 */
class FChaosVDTraceParticleDataProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FChaosVDTraceParticleDataProcessor();

	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};
