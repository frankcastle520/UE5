// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/DataProcessors/ChaosVDDataProcessorBase.h"

/**
 * Data processor implementation that is able to deserialize traced scene queries data
 */
class FChaosVDSceneQueryDataProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FChaosVDSceneQueryDataProcessor();

	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};

