// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class FSceneViewFamily;
struct FGlobalShaderPermutationParameters;
enum EShaderPlatform : uint16;
class FRDGTexture;
using FRDGTextureRef = FRDGTexture*;

struct FScreenMessageWriter;

namespace ECastRayTracedShadow
{
	enum Type : int;
};

class FMegaLightsVolume
{
public:
	FRDGTextureRef Texture = nullptr;
};

enum class EMegaLightsMode
{
	Disabled,
	EnabledRT,
	EnabledVSM
};

// Public MegaLights interface
namespace MegaLights
{
	bool IsEnabled(const FSceneViewFamily& ViewFamily);

	bool IsUsingClosestHZB(const FSceneViewFamily& ViewFamily);
	bool IsUsingGlobalSDF(const FSceneViewFamily& ViewFamily);
	bool IsUsingLightFunctions(const FSceneViewFamily& ViewFamily);

	bool IsSoftwareRayTracingSupported(const FSceneViewFamily& ViewFamily);
	bool IsHardwareRayTracingSupported(const FSceneViewFamily& ViewFamily);

	EMegaLightsMode GetMegaLightsMode(const FSceneViewFamily& ViewFamily, uint8 LightType, bool bLightAllowsMegaLights, TEnumAsByte<EMegaLightsShadowMethod::Type> ShadowMethod);
	bool UseHardwareRayTracing(const FSceneViewFamily& ViewFamily);
	bool UseInlineHardwareRayTracing(const FSceneViewFamily& ViewFamily);
	bool ShouldCompileShaders(EShaderPlatform ShaderPlatform);

	bool UseVolume();

	uint32 GetSampleMargin();

	bool HasWarning(const FSceneViewFamily& ViewFamily);
	void WriteWarnings(const FSceneViewFamily& ViewFamily, FScreenMessageWriter& Writer);
};