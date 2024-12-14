// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ChaosVDDataProcessorBase.h"

/**
 * Data processor implementation that is able to deserialize traced Debug Draw Implicit Objects
 */
class FChaosVDDebugDrawImplicitObjectDataProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FChaosVDDebugDrawImplicitObjectDataProcessor();
	
	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};
