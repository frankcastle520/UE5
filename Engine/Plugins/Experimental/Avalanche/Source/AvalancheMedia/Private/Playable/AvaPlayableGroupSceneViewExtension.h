// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Set.h"
#include "SceneTypes.h"
#include "SceneViewExtension.h"

class FAvaPlayableGroupSceneViewExtension final : public FSceneViewExtensionBase
{
public:
	FAvaPlayableGroupSceneViewExtension(const FAutoRegister& InAutoReg);
	virtual ~FAvaPlayableGroupSceneViewExtension() override = default;

	void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
};
