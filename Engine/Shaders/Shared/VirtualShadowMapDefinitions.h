// Copyright Epic Games, Inc. All Rights Reserved.

/*================================================================================================
VirtualShadowMapDefinitions.h: used in virtual shadow map shaders and C++ code to define common constants
!!! Changing this file requires recompilation of the engine !!!
=================================================================================================*/

#pragma once

// Page size is 128x128
#define VSM_LOG2_PAGE_SIZE 7u
#define VSM_PAGE_SIZE (1u << VSM_LOG2_PAGE_SIZE)
#define VSM_PAGE_SIZE_MASK (VSM_PAGE_SIZE - 1u)
// Page table size is 128x128 (total 16k)
#define VSM_LOG2_LEVEL0_DIM_PAGES_XY 7u
#define VSM_LEVEL0_DIM_PAGES_XY (1u << VSM_LOG2_LEVEL0_DIM_PAGES_XY)
#define VSM_MAX_MIP_LEVELS (VSM_LOG2_LEVEL0_DIM_PAGES_XY + 1u)
#define VSM_VIRTUAL_MAX_RESOLUTION_XY (VSM_LEVEL0_DIM_PAGES_XY * VSM_PAGE_SIZE)
#define VSM_RASTER_WINDOW_PAGES (4u)
#define VSM_PAGE_TABLE_SIZE 21845u // (0x55555555u & ((1u << (VSM_MAX_MIP_LEVELS << 1u)) - 1u))


#define VIRTUAL_SHADOW_MAP_VISUALIZE_NONE						0
#define VIRTUAL_SHADOW_MAP_VISUALIZE_SHADOW_FACTOR				(1 << 0)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_CLIPMAP_OR_MIP				(1 << 1)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_VIRTUAL_PAGE				(1 << 2)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_CACHED_PAGE				(1 << 3)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_SMRT_RAY_COUNT				(1 << 4)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_CLIPMAP_VIRTUAL_SPACE		(1 << 5)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_GENERAL_DEBUG				(1 << 6)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_DIRTY_PAGE					(1 << 7)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_GPU_INVALIDATED_PAGE		(1 << 8)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_MERGED_PAGE				(1 << 9)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_NANITE_OVERDRAW			(1 << 10)


#define VSM_PROJ_FLAG_CURRENT_DISTANT_LIGHT (1U << 0)
#define VSM_PROJ_FLAG_UNCACHED (1U << 1) // Used to indicate that the light is uncached and should only render to dynamic pages
#define VSM_PROJ_FLAG_UNREFERENCED (1U << 2) // Used to indicate that the light is not referenced/rendered to this render

// Hard limit for max distant lights supported 8k for now - we may revise later. We need to keep them in a fixed range for now to make allocation easy and minimize overhead for indexing.
#define VSM_MAX_SINGLE_PAGE_SHADOW_MAPS (1024U * 8U)

#define VSM_INVALIDATION_PAYLOAD_FLAG_NONE                      0
#define VSM_INVALIDATION_PAYLOAD_FLAG_FORCE_STATIC              (1 << 0)
// 8 bit flags, 24 bit VSM ID
#define VSM_INVALIDATION_PAYLOAD_FLAG_BITS                      8


#define VSM_STAT_REQUESTED_THIS_FRAME_PAGES				 0
#define VSM_STAT_STATIC_CACHED_PAGES					 1
#define VSM_STAT_STATIC_INVALIDATED_PAGES				 2
#define VSM_STAT_DYNAMIC_CACHED_PAGES					 3
#define VSM_STAT_DYNAMIC_INVALIDATED_PAGES				 4
#define VSM_STAT_EMPTY_PAGES							 5
#define VSM_STAT_NON_NANITE_INSTANCES_TOTAL				 6
#define VSM_STAT_NON_NANITE_INSTANCES_DRAWN				 7
#define VSM_STAT_NON_NANITE_INSTANCES_HZB_CULLED		 8
#define VSM_STAT_NON_NANITE_INSTANCES_PAGE_MASK_CULLED	 9
#define VSM_STAT_NON_NANITE_INSTANCES_EMPTY_RECT_CULLED	10
#define VSM_STAT_NON_NANITE_INSTANCES_FRUSTUM_CULLED	11
#define VSM_STAT_NUM_PAGES_TO_MERGE						12
#define VSM_STAT_NUM_PAGES_TO_CLEAR						13
#define VSM_STAT_NUM_HZB_PAGES_BUILT					14
#define VSM_STAT_ALLOCATED_NEW							15
#define VSM_STAT_NANITE_TRIANGLES						16
#define VSM_STAT_NANITE_INSTANCES_MAIN					17
#define VSM_STAT_NANITE_INSTANCES_POST					18
#define VSM_STAT_WPO_CONSIDERED_PAGES					19
#define VSM_STAT_OVERFLOW_FLAGS							20
#define VSM_STAT_TMP_1									21
#define VSM_STAT_TMP_2									22
#define VSM_STAT_TMP_3									23
#define VSM_STAT_NUM									24

#define VSM_STAT_OVERFLOW_FLAG_MARKING_JOB_QUEUE 		(1<<0)
#define VSM_STAT_OVERFLOW_FLAG_OPP_MAX_LIGHTS	 		(1<<1)
#define VSM_STAT_OVERFLOW_FLAG_PAGE_POOL		 		(1<<2)
#define VSM_STAT_OVERFLOW_FLAG_VISIBLE_INSTANCES 		(1<<3)
#define VSM_STAT_OVERFLOW_FLAG_NUM						4

#define VSM_STATUS_MSG_PAGE_MANAGEMENT					0
#define VSM_STATUS_MSG_OVERFLOW							1

#ifdef __cplusplus
#include "HLSLTypeAliases.h"
#include "HLSLMathAliases.h"

namespace UE::HLSL
{
#endif

struct FVSMVisibleInstanceCmd
{
	uint PackedPageInfo;
	uint InstanceIdAndFlags;
	uint IndirectArgIndex;
};

struct FVSMCullingBatchInfo
{
	uint FirstPrimaryView;
	uint NumPrimaryViews;
};

struct FNextVirtualShadowMapData
{
	int NextVirtualShadowMapId;
	int2 PageAddressOffset;
	int _Padding;
};


inline uint GetMipLevelLocal(float Footprint, uint MipMode, float ShadowMapResolutionLodBias, float GlobalResolutionLodBias)
{
	float MipLevelFloat = log2(Footprint) + ShadowMapResolutionLodBias + GlobalResolutionLodBias;
	uint MipLevel = uint(max(floor(MipLevelFloat), 0.0f));
	MipLevel = min(MipLevel, (VSM_MAX_MIP_LEVELS - 1U));

	if (MipMode == 1)
	{
		// Even mips
		MipLevel &= ~(1U);
	}
	else if (MipMode == 2)
	{
		// Odd mips
		MipLevel |= 1U;
	}

	return MipLevel;
}

#ifdef __cplusplus
} // namespace UE::HLSL

using FVSMVisibleInstanceCmd = UE::HLSL::FVSMVisibleInstanceCmd;
using FVSMCullingBatchInfo = UE::HLSL::FVSMCullingBatchInfo;
using FNextVirtualShadowMapData = UE::HLSL::FNextVirtualShadowMapData;

#endif
