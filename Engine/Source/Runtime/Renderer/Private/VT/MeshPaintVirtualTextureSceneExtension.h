// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneExtensions.h"

class FMeshPaintVirtualTextureSceneExtension : public ISceneExtension
{
	DECLARE_SCENE_EXTENSION(RENDERER_API, FMeshPaintVirtualTextureSceneExtension);

public:
	static bool ShouldCreateExtension(FScene& Scene);

	//~ Begin ISceneExtension Interface.
	virtual void InitExtension(FScene& InScene) override;
	virtual ISceneExtensionRenderer* CreateRenderer() override;
	//~ End ISceneExtension Interface.

protected:
	class FRenderer : public ISceneExtensionRenderer
	{
		DECLARE_SCENE_EXTENSION_RENDERER(FRenderer, FMeshPaintVirtualTextureSceneExtension);

	public:
		//~ Begin ISceneExtensionRenderer Interface.
		virtual void UpdateSceneUniformBuffer(FRDGBuilder& GraphBuilder, FSceneUniformBuffer& SceneUniformBuffer) override;
		//~ End ISceneExtensionRenderer Interface.
	};
};
