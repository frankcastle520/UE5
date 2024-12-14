// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHITestsCommon.h"

/**
 * Set of tests for binding UAVs to non-compute shaders
 */

class FRHIGraphicsUAVTests
{
public:

	static bool Test_GraphicsUAV_PixelShader(FRHICommandListImmediate& RHICmdList);
	static bool Test_GraphicsUAV_VertexShader(FRHICommandListImmediate& RHICmdList);
};

