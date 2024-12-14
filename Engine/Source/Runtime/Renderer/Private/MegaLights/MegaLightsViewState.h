// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector4.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

class FMegaLightsViewState
{
public:
	TRefCountPtr<IPooledRenderTarget> DiffuseLightingAndSecondMomentHistory;
	TRefCountPtr<IPooledRenderTarget> SpecularLightingAndSecondMomentHistory;
	TRefCountPtr<IPooledRenderTarget> NumFramesAccumulatedHistory;
	TRefCountPtr<FRDGPooledBuffer> VisibleLightHashHistory;
	TRefCountPtr<FRDGPooledBuffer> VisibleLightMaskHashHistory;

	FVector4f HistoryScreenPositionScaleBias = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f HistoryUVMinMax = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f HistoryGatherUVMinMax = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FIntPoint HistoryVisibleLightHashViewMinInTiles = 0;
	FIntPoint HistoryVisibleLightHashViewSizeInTiles = 0;

	void SafeRelease()
	{
		DiffuseLightingAndSecondMomentHistory.SafeRelease();
		SpecularLightingAndSecondMomentHistory.SafeRelease();
		NumFramesAccumulatedHistory.SafeRelease();
		VisibleLightHashHistory.SafeRelease();
		VisibleLightMaskHashHistory.SafeRelease();
	}

	uint64 GetGPUSizeBytes(bool bLogSizes) const;
};