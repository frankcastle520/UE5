// Copyright Epic Games, Inc. All Rights Reserved.

namespace RayTracing
{
	struct FSceneOptions;
};

namespace RayTracingShadows
{
	void SetRayTracingSceneOptions(bool bSceneHasLightsWithRayTracedShadows, RayTracing::FSceneOptions& SceneOptions);
};