// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Templates/Function.h"

class FSceneView;
class FSceneViewFamily;

struct FMeshEdgesViewSettings
{
	// Opacity of the wireframe blended with the shaded view.
	float Opacity = 1.0;
};

RENDERER_API const FMeshEdgesViewSettings& GetMeshEdgesViewSettings(const FSceneView& View);
RENDERER_API FMeshEdgesViewSettings& GetMeshEdgesViewSettings(FSceneView& View);

struct FMeshEdgesViewFamilySettings
{
	TFunction<void(FSceneViewFamily& WireframeViewFamily)> OnBeforeWireframeRender = [](auto&){};
};

RENDERER_API const FMeshEdgesViewFamilySettings& GetMeshEdgesViewFamilySettings(const FSceneViewFamily& ViewFamily);
RENDERER_API FMeshEdgesViewFamilySettings& GetMeshEdgesViewFamilySettings(FSceneViewFamily& ViewFamily);
