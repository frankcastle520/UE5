// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/DataProcessors/ChaosVDDataProcessorBase.h"

/**
 * Data processor implementation that is able to deserialize traced Name Entries
 */
class FChaosVDSerializedNameEntryDataProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FChaosVDSerializedNameEntryDataProcessor();

	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};
