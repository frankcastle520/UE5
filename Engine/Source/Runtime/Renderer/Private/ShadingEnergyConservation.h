// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeferredShadingRenderer.h: Scene rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"

class FViewInfo;

struct FShadingEnergyConservationData
{
	bool bEnergyConservation = false;
	bool bEnergyPreservation = false;

	TRefCountPtr<IPooledRenderTarget> GGXSpecEnergyTexture = nullptr;
	TRefCountPtr<IPooledRenderTarget> GGXGlassEnergyTexture = nullptr;
	TRefCountPtr<IPooledRenderTarget> ClothEnergyTexture = nullptr;
	TRefCountPtr<IPooledRenderTarget> DiffuseEnergyTexture = nullptr;
};

namespace ShadingEnergyConservation
{
	void Init(FRDGBuilder& GraphBuilder, const FViewInfo& View);
	void Debug(FRDGBuilder& GraphBuilder, const FViewInfo& View, FSceneTextures& SceneTextures);
	FShadingEnergyConservationData GetData(const FViewInfo& View);

	RENDERER_API bool IsEnable();
}
