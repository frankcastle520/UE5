// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ChaosVDDataProcessorBase.h"

/**
 * Data processor implementation that is able to deserialize traced Debug Draw sphere shapes
 */
class FChaosVDDebugDrawSphereDataProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FChaosVDDebugDrawSphereDataProcessor();
	
	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};

