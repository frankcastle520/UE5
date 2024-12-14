// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMAlphaOneMinusPS.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "DynamicMaterialShadersEditorModule.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"
#include "UObject/Package.h"

const FString FDMAlphaOneMinusPS::ShaderPath = FString(UE::DynamicMaterialShadersEditor::Internal::VirtualShaderMountPoint) + TEXT("/DMAlphaOneMinusPS.usf");

bool FDMAlphaOneMinusPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
{
	return IsFeatureLevelSupported(InParameters.Platform, ERHIFeatureLevel::ES3_1);
}

FDMAlphaOneMinusPS::FParameters* FDMAlphaOneMinusPS::AllocateAndSetParameters(FRDGBuilder& InGraphBuilder, FRDGTextureRef InRGBATexture, FRDGTextureRef InOutputTexture)
{
	FParameters* Parameters = InGraphBuilder.AllocParameters<FParameters>();
	Parameters->InputTexture = InRGBATexture;
	Parameters->InputSampler = TStaticSamplerState<SF_Point>::GetRHI();
	Parameters->RenderTargets[0] = FRenderTargetBinding{InOutputTexture, ERenderTargetLoadAction::ENoAction};
	return Parameters;
}

IMPLEMENT_SHADER_TYPE(, FDMAlphaOneMinusPS, *FDMAlphaOneMinusPS::ShaderPath, TEXT("MainPS"), SF_Pixel);
