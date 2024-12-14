// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/MathFwd.h" // FTransform

class FRHICommandListImmediate;
class FTextureResource;

namespace UE:: Landscape::PatchUtil 
{
	void CopyTextureOnRenderThread(FRHICommandListImmediate& RHICmdList, const FTextureResource& Source, FTextureResource& Destination);

	/**
	 * Given a landscape transform, gives a transform from heightmap coordinates (where the Z value is the
	 * two byte integer value stored as the height) to world coordinates.
	 */
	FTransform GetHeightmapToWorld(const FTransform& InLandscapeTransform);

}//end UE::Landscape::PatchUtil
