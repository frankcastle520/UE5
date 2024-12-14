// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneViewExtension.h"

class FAvaTransitionSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FAvaTransitionSceneViewExtension(const FAutoRegister& InAutoRegister)
		: FSceneViewExtensionBase(InAutoRegister)
	{
	}

	//~ Begin FSceneViewExtensionBase
	void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	//~ End FSceneViewExtensionBase
};
