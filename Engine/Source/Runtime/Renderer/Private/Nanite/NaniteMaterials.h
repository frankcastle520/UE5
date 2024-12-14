// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"
#include "NaniteCullRaster.h"
#include "MeshPassProcessor.h"
#include "GBufferInfo.h"

struct FNaniteShadingCommands;
class  FLumenCardPassUniformParameters;
class  FCardPageRenderData;
class  FSceneRenderer;

struct FNaniteMaterialSlot
{
	struct FPacked
	{
		uint32 Data[2];
	};

	FNaniteMaterialSlot()
	: ShadingBin(0xFFFF)
	, RasterBin(0xFFFF)
	, FallbackRasterBin(0xFFFF)
	, Unused(0xFFFF)
	{
	}

	inline FPacked Pack() const
	{
		FPacked Ret;
		Ret.Data[0] = (ShadingBin << 16u | RasterBin);
		Ret.Data[1] = (Unused << 16u | FallbackRasterBin);
		return Ret;
	}

	uint16 ShadingBin;
	uint16 RasterBin;
	uint16 FallbackRasterBin;
	uint16 Unused;
};

struct FNaniteMaterialDebugViewInfo
{
#if WITH_DEBUG_VIEW_MODES
	struct FPacked
	{
		uint32 Data[3];
	};

	FNaniteMaterialDebugViewInfo()
	: InstructionCountVS(0)
	, InstructionCountPS(0)
	, InstructionCountCS(0)
	, LWCComplexityVS(0)
	, LWCComplexityPS(0)
	, LWCComplexityCS(0)
	{
	}

	uint16 InstructionCountVS;
	uint16 InstructionCountPS;
	uint16 InstructionCountCS;

	uint16 LWCComplexityVS;
	uint16 LWCComplexityPS;
	uint16 LWCComplexityCS;

	FPacked Pack() const
	{
		FPacked Result;
		Result.Data[0] = static_cast<uint32>(InstructionCountPS)	<< 16u | static_cast<uint32>(InstructionCountVS);
		Result.Data[1] = static_cast<uint32>(LWCComplexityVS)		<< 16u | static_cast<uint32>(InstructionCountCS);
		Result.Data[2] = static_cast<uint32>(LWCComplexityPS)		<< 16u | static_cast<uint32>(LWCComplexityCS);
		return Result;
	}
#endif
};
