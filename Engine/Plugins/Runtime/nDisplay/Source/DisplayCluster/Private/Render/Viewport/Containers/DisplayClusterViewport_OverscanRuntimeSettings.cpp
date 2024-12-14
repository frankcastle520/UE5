// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/Containers/DisplayClusterViewport_OverscanRuntimeSettings.h"
#include "Render/Viewport/DisplayClusterViewportHelpers.h"

#include "HAL/IConsoleManager.h"

int32 GDisplayClusterRenderOverscanEnable = 1;
static FAutoConsoleVariableRef CVarDisplayClusterRenderOverscanEnable(
	TEXT("nDisplay.render.overscan.enable"),
	GDisplayClusterRenderOverscanEnable,
	TEXT("Enable overscan feature.\n")
	TEXT(" 0 - to disable.\n"),
	ECVF_Default
);

int32 GDisplayClusterRenderOverscanMaxValue = 50;
static FAutoConsoleVariableRef CVarDisplayClusterRenderOverscanMaxValue(
	TEXT("nDisplay.render.overscan.max_percent"),
	GDisplayClusterRenderOverscanMaxValue,
	TEXT("Max percent for overscan (default 50).\n"),
	ECVF_Default
);

namespace UE::DisplayCluster::Viewport::OverscanHelpers
{
	/** Clamp percent for overscan settings. */
	static inline double ClampPercent(double InValue)
	{
		const double MaxCustomFrustumValue = double(GDisplayClusterRenderOverscanMaxValue) / 100;

		// We can't use negative overscan values.
		// The idea behind the overscan is to add extra space on the sides of the RTT.
		// Note: this only applies to regular viewports (Outers, etc.).
		// 
		// The inner frustum viewport has its own implementation for the overscan feature called "CustomFrustum".
		// (see FDisplayClusterViewport_CustomFrustumRuntimeSettings)

		return FMath::Clamp(InValue, 0, MaxCustomFrustumValue);
	}
};

///////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterViewport_OverscanRuntimeSettings
///////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterViewport_OverscanRuntimeSettings::UpdateProjectionAngles(
	const FDisplayClusterViewport_OverscanRuntimeSettings& InOverscanRuntimeSettings,
	const FIntPoint& InRenderTargetSize,
	double& InOutLeft,
	double& InOutRight,
	double& InOutTop,
	double& InOutBottom)
{
	if (InOverscanRuntimeSettings.bIsEnabled)
	{
		const FVector2D SizeFOV(InOutRight - InOutLeft, InOutTop - InOutBottom);

		InOutLeft   -= SizeFOV.X * InOverscanRuntimeSettings.OverscanPercent.Left;
		InOutRight  += SizeFOV.X * InOverscanRuntimeSettings.OverscanPercent.Right;
		InOutBottom -= SizeFOV.Y * InOverscanRuntimeSettings.OverscanPercent.Bottom;
		InOutTop    += SizeFOV.Y * InOverscanRuntimeSettings.OverscanPercent.Top;

		return true;
	}

	return false;
}

void FDisplayClusterViewport_OverscanRuntimeSettings::UpdateOverscanSettings(
	const FString& InViewportId,
	const FDisplayClusterViewport_OverscanSettings& InOverscanSettings,
	FDisplayClusterViewport_OverscanRuntimeSettings& InOutOverscanRuntimeSettings,
	FIntRect& InOutRenderTargetRect)
{
	using namespace UE::DisplayCluster::Viewport;

	const FIntPoint Size = InOutRenderTargetRect.Size();

	// Disable viewport overscan feature
	if (!GDisplayClusterRenderOverscanEnable || !InOverscanSettings.bEnabled)
	{
		return;
	}

	switch (InOverscanSettings.Unit)
	{
	case EDisplayClusterViewport_FrustumUnit::Percent:
	{
		InOutOverscanRuntimeSettings.bIsEnabled = true;

		InOutOverscanRuntimeSettings.OverscanPercent.Left   = OverscanHelpers::ClampPercent(InOverscanSettings.Left);
		InOutOverscanRuntimeSettings.OverscanPercent.Right  = OverscanHelpers::ClampPercent(InOverscanSettings.Right);
		InOutOverscanRuntimeSettings.OverscanPercent.Top    = OverscanHelpers::ClampPercent(InOverscanSettings.Top);
		InOutOverscanRuntimeSettings.OverscanPercent.Bottom = OverscanHelpers::ClampPercent(InOverscanSettings.Bottom);
		break;
	}

	case EDisplayClusterViewport_FrustumUnit::Pixels:
	{
		InOutOverscanRuntimeSettings.bIsEnabled = true;

		InOutOverscanRuntimeSettings.OverscanPercent.Left   = OverscanHelpers::ClampPercent(InOverscanSettings.Left   / Size.X);
		InOutOverscanRuntimeSettings.OverscanPercent.Right  = OverscanHelpers::ClampPercent(InOverscanSettings.Right  / Size.X);
		InOutOverscanRuntimeSettings.OverscanPercent.Top    = OverscanHelpers::ClampPercent(InOverscanSettings.Top    / Size.Y);
		InOutOverscanRuntimeSettings.OverscanPercent.Bottom = OverscanHelpers::ClampPercent(InOverscanSettings.Bottom / Size.Y);

		break;
	}

	default:
		break;
	}

	// Update RTT size for overscan
	if (InOutOverscanRuntimeSettings.bIsEnabled)
	{
		// Calc pixels from percent
		InOutOverscanRuntimeSettings.OverscanPixels.Left   = FMath::RoundToInt(Size.X * InOutOverscanRuntimeSettings.OverscanPercent.Left);
		InOutOverscanRuntimeSettings.OverscanPixels.Right  = FMath::RoundToInt(Size.X * InOutOverscanRuntimeSettings.OverscanPercent.Right);
		InOutOverscanRuntimeSettings.OverscanPixels.Top    = FMath::RoundToInt(Size.Y * InOutOverscanRuntimeSettings.OverscanPercent.Top);
		InOutOverscanRuntimeSettings.OverscanPixels.Bottom = FMath::RoundToInt(Size.Y * InOutOverscanRuntimeSettings.OverscanPercent.Bottom);

		// Quantize the overscan percentage to exactly fit the number of pixels.
		// This will avoid a mismatch between the overscanned frustum calculated in UpdateProjectionAngles and the pixel crop in GetFinalContextRect.
		InOutOverscanRuntimeSettings.OverscanPercent.Left   = double(InOutOverscanRuntimeSettings.OverscanPixels.Left)   / double(Size.X);
		InOutOverscanRuntimeSettings.OverscanPercent.Right  = double(InOutOverscanRuntimeSettings.OverscanPixels.Right)  / double(Size.X);
		InOutOverscanRuntimeSettings.OverscanPercent.Top    = double(InOutOverscanRuntimeSettings.OverscanPixels.Top)    / double(Size.Y);
		InOutOverscanRuntimeSettings.OverscanPercent.Bottom = double(InOutOverscanRuntimeSettings.OverscanPixels.Bottom) / double(Size.Y);

		const FIntPoint OverscanSize = Size + InOutOverscanRuntimeSettings.OverscanPixels.Size();
		const FIntPoint ValidOverscanSize = FDisplayClusterViewportHelpers::GetValidViewportRect(FIntRect(FIntPoint(0, 0), OverscanSize), InViewportId, TEXT("Overscan")).Size();
		
		bool bOversize = InOverscanSettings.bOversize;

		if (OverscanSize != ValidOverscanSize)
		{
			// can't use overscan with extra size, disable oversize
			bOversize = false;
		}

		if (bOversize)
		{
			InOutRenderTargetRect.Max = OverscanSize;
		}
		else
		{
			const double ScaleX = double(Size.X) / double(OverscanSize.X);
			const double ScaleY = double(Size.Y) / double(OverscanSize.Y);

			InOutOverscanRuntimeSettings.OverscanPixels.Left   = FMath::RoundToInt(InOutOverscanRuntimeSettings.OverscanPixels.Left   * ScaleX);
			InOutOverscanRuntimeSettings.OverscanPixels.Right  = FMath::RoundToInt(InOutOverscanRuntimeSettings.OverscanPixels.Right  * ScaleX);
			InOutOverscanRuntimeSettings.OverscanPixels.Top    = FMath::RoundToInt(InOutOverscanRuntimeSettings.OverscanPixels.Top    * ScaleY);
			InOutOverscanRuntimeSettings.OverscanPixels.Bottom = FMath::RoundToInt(InOutOverscanRuntimeSettings.OverscanPixels.Bottom * ScaleY);

			const FIntPoint ScaledSize(
				Size.X - (InOutOverscanRuntimeSettings.OverscanPixels.Left + InOutOverscanRuntimeSettings.OverscanPixels.Right),
				Size.Y - (InOutOverscanRuntimeSettings.OverscanPixels.Top  + InOutOverscanRuntimeSettings.OverscanPixels.Bottom)
			);

			// Quantize the overscan percentage to exactly fit the number of pixels.
			InOutOverscanRuntimeSettings.OverscanPercent.Left   = double(InOutOverscanRuntimeSettings.OverscanPixels.Left)   / double(ScaledSize.X);
			InOutOverscanRuntimeSettings.OverscanPercent.Right  = double(InOutOverscanRuntimeSettings.OverscanPixels.Right)  / double(ScaledSize.X);
			InOutOverscanRuntimeSettings.OverscanPercent.Top    = double(InOutOverscanRuntimeSettings.OverscanPixels.Top)    / double(ScaledSize.Y);
			InOutOverscanRuntimeSettings.OverscanPercent.Bottom = double(InOutOverscanRuntimeSettings.OverscanPixels.Bottom) / double(ScaledSize.Y);
		}
	}
}
