// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfigurationTypes_Viewport.h"
#include "DisplayClusterConfigurationTypes_ICVFX.h"
#include "DisplayClusterConfigurationUtils.h"

///////////////////////////////////////////////////////////////////////////////////////
// UDisplayClusterConfigurationViewport
///////////////////////////////////////////////////////////////////////////////////////
EDisplayClusterViewportICVFXFlags UDisplayClusterConfigurationViewport::GetViewportICVFXFlags(const FDisplayClusterConfigurationICVFX_StageSettings& InStageSettings) const
{
	EDisplayClusterViewportICVFXFlags OutFlags = EDisplayClusterViewportICVFXFlags::None;
	if (ICVFX.bAllowICVFX)
	{
		EnumAddFlags(OutFlags, EDisplayClusterViewportICVFXFlags::Enable);
	}

	// Override camera render mode
	EDisplayClusterConfigurationICVFX_OverrideCameraRenderMode UsedCameraRenderMode = ICVFX.CameraRenderMode;
	if (!ICVFX.bAllowInnerFrustum || !InStageSettings.bEnableInnerFrustums)
	{
		UsedCameraRenderMode = EDisplayClusterConfigurationICVFX_OverrideCameraRenderMode::Disabled;
	}

	switch (UsedCameraRenderMode)
	{
	// Disable camera frame render for this viewport
	case EDisplayClusterConfigurationICVFX_OverrideCameraRenderMode::Disabled:
		EnumAddFlags(OutFlags, EDisplayClusterViewportICVFXFlags::DisableCamera | EDisplayClusterViewportICVFXFlags::DisableChromakey | EDisplayClusterViewportICVFXFlags::DisableChromakeyMarkers);
		break;

	// Disable chromakey render for this viewport
	case EDisplayClusterConfigurationICVFX_OverrideCameraRenderMode::DisableChromakey:
		EnumAddFlags(OutFlags, EDisplayClusterViewportICVFXFlags::DisableChromakey | EDisplayClusterViewportICVFXFlags::DisableChromakeyMarkers);
		break;

	// Disable chromakey markers render for this viewport
	case EDisplayClusterConfigurationICVFX_OverrideCameraRenderMode::DisableChromakeyMarkers:
		EnumAddFlags(OutFlags, EDisplayClusterViewportICVFXFlags::DisableChromakeyMarkers);
		break;

	default:
		break;
	}

	// Disable lightcards rendering
	const EDisplayClusterShaderParametersICVFX_LightCardRenderMode LightCardRenderMode = InStageSettings.Lightcard.GetLightCardRenderMode(EDisplayClusterConfigurationICVFX_PerLightcardRenderMode::Default, this);
	if (LightCardRenderMode == EDisplayClusterShaderParametersICVFX_LightCardRenderMode::None)
	{
		EnumAddFlags(OutFlags, EDisplayClusterViewportICVFXFlags::DisableLightcard);
	}

	// Per-viewport lightcard
	const EDisplayClusterShaderParametersICVFX_LightCardRenderMode LightCardRenderModeOverride = InStageSettings.Lightcard.GetLightCardRenderModeOverride(this);
	switch (LightCardRenderModeOverride)
	{
	case EDisplayClusterShaderParametersICVFX_LightCardRenderMode::Over:
		EnumAddFlags(OutFlags, EDisplayClusterViewportICVFXFlags::LightcardAlwaysOver);
		break;

	case EDisplayClusterShaderParametersICVFX_LightCardRenderMode::Under:
		EnumAddFlags(OutFlags, EDisplayClusterViewportICVFXFlags::LightcardAlwaysUnder);
		break;

	default:
		EnumAddFlags(OutFlags, EDisplayClusterViewportICVFXFlags::LightcardUseStageSettings);
		break;
	}


	return OutFlags;
}
