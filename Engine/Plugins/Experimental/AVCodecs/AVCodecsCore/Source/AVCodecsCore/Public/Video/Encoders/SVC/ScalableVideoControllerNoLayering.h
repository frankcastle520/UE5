// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Video/Encoders/SVC/ScalableVideoController.h"

class AVCODECSCORE_API FScalableVideoControllerNoLayering : public FScalableVideoController
{
public:
	~FScalableVideoControllerNoLayering() override = default;

	virtual FStreamLayersConfig		  StreamConfig() const override;
	virtual FFrameDependencyStructure DependencyStructure() const override;

	virtual TArray<FScalableVideoController::FLayerFrameConfig> NextFrameConfig(bool bRestart) override;
	virtual FGenericFrameInfo									OnEncodeDone(const FScalableVideoController::FLayerFrameConfig& Config) override;
	virtual void												OnRatesUpdated(const FVideoBitrateAllocation& Bitrates) override;

private:
	bool bStart = true;
	bool bEnabled = true;
};