// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12ShaderResources.h: Internal D3D12 RHI RayTracing definitions.
=============================================================================*/

#pragma once

#include "HAL/Platform.h"
#include "RayTracingBuiltInResources.h"

#include "D3D12RHI.h"

#if !defined(D3D12_MAJOR_VERSION)
	#include "D3D12ThirdParty.h"
#endif

// Built-in local root parameters that are always bound to all hit shaders
// Contains union for bindless and non-bindless index/vertex buffer data to make the code handling the hit group parameters easier to use
// (otherwise all cached hit parameter code has to be done twice and stored twice making everything more complicated)
// Ideally the non bindless code path should be removed 'soon' - this constant buffer size for FD3D12HitGroupSystemParameters in Bindless is 8 bytes bigger than needed
struct FD3D12HitGroupSystemParameters
{	
	FHitGroupSystemRootConstants RootConstants;
	union
	{
		struct
		{
			uint32 BindlessHitGroupSystemIndexBuffer;
			uint32 BindlessHitGroupSystemVertexBuffer;
		};
		struct
		{
			D3D12_GPU_VIRTUAL_ADDRESS IndexBuffer;
			D3D12_GPU_VIRTUAL_ADDRESS VertexBuffer;
		};
	};
};
