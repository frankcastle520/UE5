// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ChaosVDDataProcessorBase.h"

/**
 * Data processor implementation that is able to deserialize traced Debug Draw Box shapes
 */
class FChaosVDDebugDrawBoxDataProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FChaosVDDebugDrawBoxDataProcessor();
	
	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};
