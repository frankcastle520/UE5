// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/DMMaterialUtils.h"
#include "MaterialShared.h"
#include "RenderUtils.h"

bool FDMMaterialUtils::IsMaterialPropertyActive(const FParams& InParams)
{
	const bool bSubstrateEnabled = Substrate::IsSubstrateEnabled();
	const bool bSubstrateOpacityOverrideAllowed = InParams.BlendMode == BLEND_AlphaComposite; // Should we always have it enabled to be able to be plugged in an fed when blend mode is toggled later on a material instance?

	switch (InParams.Domain)
	{
		case EMaterialDomain::MD_PostProcess:
			return IsMaterialPropertyActive_PostProcess(InParams, bSubstrateEnabled, bSubstrateOpacityOverrideAllowed);

		case EMaterialDomain::MD_LightFunction:
			return IsMaterialPropertyActive_LightFunction(InParams, bSubstrateEnabled, bSubstrateOpacityOverrideAllowed);

		case EMaterialDomain::MD_DeferredDecal:
			return IsMaterialPropertyActive_DeferredDecal(InParams, bSubstrateEnabled, bSubstrateOpacityOverrideAllowed);

		case EMaterialDomain::MD_Volume:
			return IsMaterialPropertyActive_Volume(InParams, bSubstrateEnabled, bSubstrateOpacityOverrideAllowed);

		case EMaterialDomain::MD_UI:
			return IsMaterialPropertyActive_UI(InParams, bSubstrateEnabled, bSubstrateOpacityOverrideAllowed);

		case EMaterialDomain::MD_Surface:
			return IsMaterialPropertyActive_Surface(InParams, bSubstrateEnabled, bSubstrateOpacityOverrideAllowed);

		default:
			return false;
	}
}

bool FDMMaterialUtils::IsMaterialPropertyActive_PostProcess(const FParams& InParams, bool bInSubstrateEnabled, bool bInSubstrateOpacityOverrideAllowed)
{
	if (bInSubstrateEnabled)
	{
		return InParams.Property == MP_FrontMaterial || (InParams.Property == MP_Opacity && bInSubstrateOpacityOverrideAllowed);
	}

	return InParams.Property == MP_EmissiveColor || (InParams.bBlendableOutputAlpha && InParams.Property == MP_Opacity);
}

bool FDMMaterialUtils::IsMaterialPropertyActive_LightFunction(const FParams& InParams, bool bInSubstrateEnabled, bool bInSubstrateOpacityOverrideAllowed)
{
	// light functions should already use MSM_Unlit but we also we don't want WorldPosOffset
	if (bInSubstrateEnabled)
	{
		return InParams.Property == MP_FrontMaterial;
	}

	return InParams.Property == MP_EmissiveColor;
}

bool FDMMaterialUtils::IsMaterialPropertyActive_DeferredDecal(const FParams& InParams, bool bInSubstrateEnabled, bool bInSubstrateOpacityOverrideAllowed)
{
	if (bInSubstrateEnabled)
	{
		return InParams.Property == MP_FrontMaterial
			|| InParams.Property == MP_AmbientOcclusion
			|| (InParams.Property == MP_Opacity && bInSubstrateOpacityOverrideAllowed);
	}

	if (InParams.Property >= MP_CustomizedUVs0 && InParams.Property <= MP_CustomizedUVs7)
	{
		return true;
	}
	
	if (InParams.Property == MP_MaterialAttributes)
	{
		// todo: MaterialAttruibutes would not return true, should it? Why we don't check for the checkbox in the material
		return true;
	}

	if (InParams.Property == MP_WorldPositionOffset)
	{
		// Note: DeferredDecals don't support this but MeshDecals do
		return true;
	}

	switch (InParams.BlendMode)
	{
		case BLEND_Translucent:
			return InParams.Property == MP_EmissiveColor
				|| InParams.Property == MP_Normal
				|| InParams.Property == MP_Metallic
				|| InParams.Property == MP_Specular
				|| InParams.Property == MP_BaseColor
				|| InParams.Property == MP_Roughness
				|| InParams.Property == MP_Opacity
				|| InParams.Property == MP_AmbientOcclusion;

		case BLEND_AlphaComposite:
			// AlphaComposite decals never write normal.
			return InParams.Property == MP_EmissiveColor
				|| InParams.Property == MP_Metallic
				|| InParams.Property == MP_Specular
				|| InParams.Property == MP_BaseColor
				|| InParams.Property == MP_Roughness
				|| InParams.Property == MP_Opacity;

		case BLEND_Modulate:
			return InParams.Property == MP_EmissiveColor
				|| InParams.Property == MP_Normal
				|| InParams.Property == MP_Metallic
				|| InParams.Property == MP_Specular
				|| InParams.Property == MP_BaseColor
				|| InParams.Property == MP_Roughness
				|| InParams.Property == MP_Opacity;

		default:
			break;
	}

	return false;
}

bool FDMMaterialUtils::IsMaterialPropertyActive_Volume(const FParams& InParams, bool bInSubstrateEnabled, bool bInSubstrateOpacityOverrideAllowed)
{
	if (bInSubstrateEnabled)
	{
		return InParams.Property == MP_FrontMaterial;
	}

	return InParams.Property == MP_EmissiveColor
		|| InParams.Property == MP_SubsurfaceColor
		|| InParams.Property == MP_BaseColor
		|| InParams.Property == MP_AmbientOcclusion;
}

bool FDMMaterialUtils::IsMaterialPropertyActive_UI(const FParams& InParams, bool bInSubstrateEnabled, bool bInSubstrateOpacityOverrideAllowed)
{
	if (bInSubstrateEnabled)
	{
		return InParams.Property == MP_FrontMaterial
			|| (InParams.Property == MP_WorldPositionOffset)
			|| (InParams.Property == MP_OpacityMask && IsMaskedBlendMode(InParams.BlendMode))
			|| (InParams.Property == MP_Opacity && bInSubstrateOpacityOverrideAllowed)
			|| (InParams.Property >= MP_CustomizedUVs0 && InParams.Property <= MP_CustomizedUVs7);
	}

	return InParams.Property == MP_EmissiveColor
		|| (InParams.Property == MP_WorldPositionOffset)
		|| (InParams.Property == MP_OpacityMask && IsMaskedBlendMode(InParams.BlendMode))
		|| (InParams.Property == MP_Opacity && IsTranslucentBlendMode(InParams.BlendMode) && InParams.BlendMode != BLEND_Modulate)
		|| (InParams.Property >= MP_CustomizedUVs0 && InParams.Property <= MP_CustomizedUVs7);
}

bool FDMMaterialUtils::IsMaterialPropertyActive_Surface(const FParams& InParams, bool bInSubstrateEnabled, bool bInSubstrateOpacityOverrideAllowed)
{
	const bool bIsTranslucentBlendMode = IsTranslucentBlendMode(InParams.BlendMode);
	const bool bIsNonDirectionalTranslucencyLightingMode = InParams.TranslucencyLightingMode == TLM_VolumetricNonDirectional || InParams.TranslucencyLightingMode == TLM_VolumetricPerVertexNonDirectional;
	const bool bIsVolumetricTranslucencyLightingMode = InParams.TranslucencyLightingMode == TLM_VolumetricNonDirectional
		|| InParams.TranslucencyLightingMode == TLM_VolumetricDirectional
		|| InParams.TranslucencyLightingMode == TLM_VolumetricPerVertexNonDirectional
		|| InParams.TranslucencyLightingMode == TLM_VolumetricPerVertexDirectional;

	bool Active = true;

	if (bInSubstrateEnabled)
	{
		Active = false;

		if (InParams.bIsSupported)
		{
			switch (InParams.Property)
			{
				case MP_Refraction:
					Active = (bIsTranslucentBlendMode && !IsAlphaHoldoutBlendMode(InParams.BlendMode) && !IsModulateBlendMode(InParams.BlendMode) && InParams.bUsesDistortion) || InParams.ShadingModels.HasShadingModel(MSM_SingleLayerWater);
					break;

				case MP_Opacity:
					// Opacity is used as alpha override for alpha composite blending. 
					Active = bInSubstrateOpacityOverrideAllowed;
					break;

				case MP_OpacityMask:
					Active = IsMaskedBlendMode(InParams.BlendMode);
					break;

				case MP_AmbientOcclusion:
					Active = InParams.ShadingModels.IsLit();
					break;

				case MP_Displacement:
					Active = InParams.bIsTessellationEnabled;
					break;

				case MP_PixelDepthOffset:
					Active = (!bIsTranslucentBlendMode) || (InParams.bIsTranslucencyWritingVelocity);
					break;

				case MP_SurfaceThickness:
					Active = InParams.bIsThinSurface;
					break;

				case MP_WorldPositionOffset:
				case MP_FrontMaterial:
				case MP_MaterialAttributes:
					Active = true;
					break;

				default:
					if (InParams.Property >= MP_CustomizedUVs0 && InParams.Property <= MP_CustomizedUVs7)
					{
						Active = true;
					}
			}
		}
	}
	else
	{
		switch (InParams.Property)
		{
			case MP_Refraction:
				Active = (bIsTranslucentBlendMode && !IsAlphaHoldoutBlendMode(InParams.BlendMode) && !IsModulateBlendMode(InParams.BlendMode) && InParams.bUsesDistortion) || InParams.ShadingModels.HasShadingModel(MSM_SingleLayerWater);
				break;

			case MP_Opacity:
				Active = (bIsTranslucentBlendMode && !IsModulateBlendMode(InParams.BlendMode)) || InParams.ShadingModels.HasShadingModel(MSM_SingleLayerWater);
				if (IsSubsurfaceShadingModel(InParams.ShadingModels))
				{
					Active = true;
				}
				break;

			case MP_OpacityMask:
				Active = IsMaskedBlendMode(InParams.BlendMode);
				break;

			case MP_BaseColor:
			case MP_AmbientOcclusion:
				Active = InParams.ShadingModels.IsLit();
				break;

			case MP_Specular:
			case MP_Roughness:
				Active = InParams.ShadingModels.IsLit() && (!bIsTranslucentBlendMode || !bIsVolumetricTranslucencyLightingMode);
				break;

			case MP_Anisotropy:
				Active = InParams.ShadingModels.HasAnyShadingModel({MSM_DefaultLit, MSM_ClearCoat}) && (!bIsTranslucentBlendMode || !bIsVolumetricTranslucencyLightingMode);
				break;

			case MP_Metallic:
				// Subsurface models store opacity in place of Metallic in the GBuffer
				Active = InParams.ShadingModels.IsLit() && (!bIsTranslucentBlendMode || !bIsVolumetricTranslucencyLightingMode);
				break;

			case MP_Normal:
				Active = (InParams.ShadingModels.IsLit() && (!bIsTranslucentBlendMode || !bIsNonDirectionalTranslucencyLightingMode)) || InParams.bUsesDistortion;
				break;

			case MP_Tangent:
				Active = InParams.ShadingModels.HasAnyShadingModel({MSM_DefaultLit, MSM_ClearCoat}) && (!bIsTranslucentBlendMode || !bIsVolumetricTranslucencyLightingMode);
				break;

			case MP_SubsurfaceColor:
				Active = InParams.ShadingModels.HasAnyShadingModel({MSM_Subsurface, MSM_PreintegratedSkin, MSM_TwoSidedFoliage, MSM_Cloth});
				break;

			case MP_CustomData0:
				Active = InParams.ShadingModels.HasAnyShadingModel({MSM_ClearCoat, MSM_Hair, MSM_Cloth, MSM_Eye, MSM_SubsurfaceProfile});
				break;

			case MP_CustomData1:
				Active = InParams.ShadingModels.HasAnyShadingModel({MSM_ClearCoat, MSM_Eye});
				break;

			case MP_EmissiveColor:
				// Emissive is always active, even for light functions and post process materials, 
				// but not for AlphaHoldout
				Active = !IsAlphaHoldoutBlendMode(InParams.BlendMode);
				break;

			case MP_Displacement:
				Active = InParams.bIsTessellationEnabled;
				break;

			case MP_PixelDepthOffset:
				Active = (!bIsTranslucentBlendMode) || (InParams.bIsTranslucencyWritingVelocity);
				break;

			case MP_ShadingModel:
				Active = InParams.bUsesShadingModelFromMaterialExpression;
				break;

			case MP_DiffuseColor:
			case MP_SpecularColor:
			case MP_SurfaceThickness:
			case MP_FrontMaterial:
				Active = false;
				break;

			case MP_WorldPositionOffset:
			case MP_MaterialAttributes:
			default:
				Active = true;
				break;
		}
	}

	return Active;
}
