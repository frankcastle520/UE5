// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"

class FVisualizeTexturePresent
{
public:
	/** Present the visualize texture tool on screen. */
	static void PresentContent(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassRenderTarget Output);
};