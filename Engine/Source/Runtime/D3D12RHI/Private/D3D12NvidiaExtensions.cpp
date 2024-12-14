// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12NvidiaExtensions.h"
#include "D3D12RHICommon.h"

#if NV_AFTERMATH

#if PLATFORM_WINDOWS
	#include "Windows/AllowWindowsPlatformTypes.h"
#endif

THIRD_PARTY_INCLUDES_START
	#include "GFSDK_Aftermath.h"
	#include "GFSDK_Aftermath_GpuCrashdump.h"
	#include "GFSDK_Aftermath_GpuCrashDumpDecoding.h"
THIRD_PARTY_INCLUDES_END

#if PLATFORM_WINDOWS
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace UE::RHICore::Nvidia::Aftermath::D3D12
{
	void InitializeDevice(ID3D12Device* RootDevice)
	{
		UE::RHICore::Nvidia::Aftermath::InitializeDevice([&](uint32 Flags)
		{
			return GFSDK_Aftermath_DX12_Initialize(GFSDK_Aftermath_Version_API, Flags, RootDevice);
		});
	}

	FCommandList RegisterCommandList(ID3D12CommandList* D3DCommandList)
	{
		FCommandList Handle{};
		if (IsEnabled())
		{
			GFSDK_Aftermath_Result Result = GFSDK_Aftermath_DX12_CreateContextHandle(D3DCommandList, &Handle);
			if (Result != GFSDK_Aftermath_Result_Success)
			{
				UE_LOG(LogD3D12RHI, VeryVerbose, TEXT("GFSDK_Aftermath_DX12_CreateContextHandle failed: 0x%08x"), Result);
				Handle = {};
			}
		}

		return Handle;
	}

	void UnregisterCommandList(FCommandList CommandList)
	{
		if (IsEnabled() && CommandList)
		{
			GFSDK_Aftermath_Result Result = GFSDK_Aftermath_ReleaseContextHandle(CommandList);
			UE_CLOG(Result != GFSDK_Aftermath_Result_Success, LogD3D12RHI, VeryVerbose, TEXT("GFSDK_Aftermath_ReleaseContextHandle failed: 0x%08x"), Result);
		}
	}

	FResource RegisterResource(ID3D12Resource* D3DResource)
	{
		FResource Handle{};
		if (IsEnabled())
		{
			GFSDK_Aftermath_Result Result = GFSDK_Aftermath_DX12_RegisterResource(D3DResource, &Handle);
			if (Result != GFSDK_Aftermath_Result_Success)
			{
				UE_LOG(LogD3D12RHI, VeryVerbose, TEXT("GFSDK_Aftermath_DX12_RegisterResource failed: 0x%08x"), Result);
				Handle = {};
			}
		}
		return Handle;
	}

	void UnregisterResource(FResource Resource)
	{
		if (IsEnabled() && Resource)
		{
			GFSDK_Aftermath_Result Result = GFSDK_Aftermath_DX12_UnregisterResource(Resource);
			UE_CLOG(Result != GFSDK_Aftermath_Result_Success, LogD3D12RHI, VeryVerbose, TEXT("GFSDK_Aftermath_DX12_UnregisterResource failed: 0x%08x"), Result);
		}
	}

#if WITH_RHI_BREADCRUMBS
	void BeginBreadcrumb(FCommandList CommandList, FRHIBreadcrumbNode* Breadcrumb)
	{
		FMarker Marker(Breadcrumb);
		if (Marker)
		{
			GFSDK_Aftermath_Result Result = GFSDK_Aftermath_SetEventMarker(CommandList, Marker.GetPtr(), Marker.GetSize());
			UE_CLOG(Result != GFSDK_Aftermath_Result_Success, LogD3D12RHI, VeryVerbose, TEXT("GFSDK_Aftermath_SetEventMarker failed in BeginBreadcrumb: 0x%08x"), Result);
		}
	}

	void EndBreadcrumb(FCommandList CommandList, FRHIBreadcrumbNode* Breadcrumb)
	{
		FMarker Marker(Breadcrumb->GetParent());
		if (Marker)
		{
			GFSDK_Aftermath_Result Result = GFSDK_Aftermath_SetEventMarker(CommandList, Marker.GetPtr(), Marker.GetSize());
			UE_CLOG(Result != GFSDK_Aftermath_Result_Success, LogD3D12RHI, VeryVerbose, TEXT("GFSDK_Aftermath_SetEventMarker failed in EndBreadcrumb: 0x%08x"), Result);
		}
	}
#endif
}

#endif // NV_AFTERMATH
