// Copyright Epic Games, Inc. All Rights Reserved.

#include "DXGIUtilities.h"

#include "RHIStats.h"
#include "CoreMinimal.h"

#if PLATFORM_MICROSOFT

const TCHAR* UE::DXGIUtilities::GetFormatString(DXGI_FORMAT Format)
{
	const TCHAR* Result = TEXT("");

#define DXGI_FORMAT_CASE(x) case x: Result = TEXT(#x); break;
	switch (Format)
	{
		DXGI_FORMAT_CASE(DXGI_FORMAT_R8G8B8A8_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_B8G8R8A8_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_B8G8R8X8_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_BC1_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_BC2_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_BC3_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_BC4_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R16G16B16A16_FLOAT)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R32G32B32A32_FLOAT)
		DXGI_FORMAT_CASE(DXGI_FORMAT_UNKNOWN)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R8_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R32G8X24_TYPELESS)
		DXGI_FORMAT_CASE(DXGI_FORMAT_D24_UNORM_S8_UINT)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R32_FLOAT)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R16G16_UINT)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R16G16_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R16G16_SNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R16G16_FLOAT)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R32G32_FLOAT)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R10G10B10A2_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R16G16B16A16_UINT)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R8G8_SNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_BC5_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R1_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_R8G8B8A8_TYPELESS)
		DXGI_FORMAT_CASE(DXGI_FORMAT_B8G8R8A8_TYPELESS)
		DXGI_FORMAT_CASE(DXGI_FORMAT_BC7_UNORM)
		DXGI_FORMAT_CASE(DXGI_FORMAT_BC6H_UF16)
		default:
			Result = TEXT("");
	}
#undef DXGI_FORMAT_CASE
	return Result;
}

HRESULT UE::DXGIUtilities::GetD3DMemoryStats(IDXGIAdapter* Adapter, FD3DMemoryStats& OutStats)
{
#if PLATFORM_WINDOWS
	SCOPE_CYCLE_COUNTER(STAT_D3DUpdateVideoMemoryStats);

	TRefCountPtr<IDXGIAdapter3> Adapter3;
	HRESULT Res = Adapter->QueryInterface(IID_PPV_ARGS(Adapter3.GetInitReference()));
	if (FAILED(Res))
	{
		return Res;
	}

	DXGI_QUERY_VIDEO_MEMORY_INFO LocalMemoryInfo;
	Res = Adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &LocalMemoryInfo);
	if (FAILED(Res))
	{
		return Res;
	}

	DXGI_QUERY_VIDEO_MEMORY_INFO NonLocalMemoryInfo;
	Res = Adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &NonLocalMemoryInfo);
	if (FAILED(Res))
	{
		return Res;
	}

	// In case of multiple GPUs, use the memory info from the one with the highest local budget.
	if (!GVirtualMGPU)
	{
		for (uint32 Index = 1; Index < GNumExplicitGPUsForRendering; ++Index)
		{
			DXGI_QUERY_VIDEO_MEMORY_INFO TempLocalMemoryInfo;
			Res = Adapter3->QueryVideoMemoryInfo(Index, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &TempLocalMemoryInfo);
			if (FAILED(Res))
			{
				return Res;
			}

			DXGI_QUERY_VIDEO_MEMORY_INFO TempNonLocalMemoryInfo;
			Res = Adapter3->QueryVideoMemoryInfo(Index, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &TempNonLocalMemoryInfo);
			if (FAILED(Res))
			{
				return Res;
			}

			if (TempLocalMemoryInfo.Budget > LocalMemoryInfo.Budget)
			{
				LocalMemoryInfo = TempLocalMemoryInfo;
				NonLocalMemoryInfo = TempNonLocalMemoryInfo;
			}
		}
	}

	OutStats.BudgetLocal = LocalMemoryInfo.Budget;
	OutStats.BudgetSystem = NonLocalMemoryInfo.Budget;
	OutStats.UsedLocal = LocalMemoryInfo.CurrentUsage;
	OutStats.UsedSystem = NonLocalMemoryInfo.CurrentUsage;

	// Check if we're over budget.
	if (OutStats.UsedLocal > OutStats.BudgetLocal)
	{
		OutStats.AvailableLocal = 0;
		OutStats.DemotedLocal = OutStats.UsedLocal - OutStats.BudgetLocal;
	}
	else
	{
		OutStats.AvailableLocal = OutStats.BudgetLocal - OutStats.UsedLocal;
		OutStats.DemotedLocal= 0;
	}

	if (OutStats.UsedSystem > OutStats.BudgetSystem)
	{
		OutStats.AvailableSystem = 0;
		OutStats.DemotedSystem = OutStats.UsedSystem - OutStats.BudgetSystem;
	}
	else
	{
		OutStats.AvailableSystem = OutStats.BudgetSystem - OutStats.UsedSystem;
		OutStats.DemotedSystem = 0;
	}

	return S_OK;
#else
	return E_NOINTERFACE;
#endif
}

#endif // PLATFORM_MICROSOFT
