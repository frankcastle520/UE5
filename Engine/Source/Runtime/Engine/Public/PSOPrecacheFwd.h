// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PSOPrecacheFwd.h
=============================================================================*/

#pragma once

#include "Containers/Array.h"
#include "HAL/Platform.h"
#include "PipelineStateCache.h"

// General switch that decides whether to compile out some PSO precaching code (most importantly, reduce sizeofs of common classes)
#ifndef UE_WITH_PSO_PRECACHING
	#define UE_WITH_PSO_PRECACHING	(PLATFORM_SUPPORTS_PSO_PRECACHING || PLATFORM_SUPPORTS_DYNAMIC_SHADER_PRELOADING)
#endif // UE_WITH_PSO_PRECACHING

// Only enable pso validation when the platform support its. Note: UE_WITH_PSO_PRECACHING can be define when dynamic shader preloading is supported. 
#define PSO_PRECACHING_VALIDATE !WITH_EDITOR && (UE_WITH_PSO_PRECACHING && PLATFORM_SUPPORTS_PSO_PRECACHING)

struct FMaterialInterfacePSOPrecacheParams;
struct FPSOPrecacheParams;

typedef TArray<FMaterialInterfacePSOPrecacheParams, TInlineAllocator<4> > FMaterialInterfacePSOPrecacheParamsList;

// Unique request ID of MaterialPSOPrecache which can be used to boost the priority of a PSO precache requests if it's needed for rendering
using FMaterialPSOPrecacheRequestID = uint32;
