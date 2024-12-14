// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RendererInterface.h"

class FStochasticLightingViewState
{
public:
	TRefCountPtr<IPooledRenderTarget> SceneDepthHistory;
	TRefCountPtr<IPooledRenderTarget> SceneNormalHistory;

	void SafeRelease()
	{
		SceneDepthHistory.SafeRelease();
		SceneNormalHistory.SafeRelease();
	}

	uint64 GetGPUSizeBytes(bool bLogSizes) const;
};