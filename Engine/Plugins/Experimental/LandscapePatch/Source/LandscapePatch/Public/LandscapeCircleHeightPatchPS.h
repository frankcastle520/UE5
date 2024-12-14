// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

/**
 * Shader that applies a circle height patch to a landscape heightmap.
 */
class FLandscapeCircleHeightPatchPS : public FGlobalShader
{
	DECLARE_EXPORTED_GLOBAL_SHADER(FLandscapeCircleHeightPatchPS, LANDSCAPEPATCH_API);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeCircleHeightPatchPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InSourceTexture) // Our input texture
		// Offset of the source heightmap relative to the 0,0 location in the destination heightmap, because
		// the source is likely to be a copied region from some inner part of the destination. This is basically
		// a SourceHeightmapToDestinationHeightmap coordinate transformation, except that it is always a simple 
		// integer translation.
		SHADER_PARAMETER(FIntPoint, InSourceTextureOffset)
		SHADER_PARAMETER(FVector3f, InCenter)
		SHADER_PARAMETER(float, InRadius)
		SHADER_PARAMETER(float, InFalloff)
		RENDER_TARGET_BINDING_SLOTS() // Holds our output
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	LANDSCAPEPATCH_API static void AddToRenderGraph(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FIntRect& DestinationBounds);
};

/**
 * Shader that applies a circle patch to a landscape visibility layer.
 */
class FLandscapeCircleVisibilityPatchPS : public FLandscapeCircleHeightPatchPS
{
	DECLARE_EXPORTED_GLOBAL_SHADER(FLandscapeCircleVisibilityPatchPS, LANDSCAPEPATCH_API);

public:
	FLandscapeCircleVisibilityPatchPS() = default;
	FLandscapeCircleVisibilityPatchPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLandscapeCircleHeightPatchPS(Initializer)
	{}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
	LANDSCAPEPATCH_API static void AddToRenderGraph(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FIntRect& DestinationBounds);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
