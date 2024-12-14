// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDDataProcessorBase.h"

/**
 * Data processor implementation that is able to deserialize traced Implicit objects
 */
class FChaosVDTraceImplicitObjectProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FChaosVDTraceImplicitObjectProcessor();

	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};
