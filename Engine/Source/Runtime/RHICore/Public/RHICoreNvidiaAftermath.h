// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef NV_AFTERMATH
#define NV_AFTERMATH 0
#endif

#if NV_AFTERMATH

	#include "RHIBreadcrumbs.h"

	//
	// Disabled for now until we can solve RHI breadcrumb lifetime issues with Aftermath.
	// (Aftermath holds pointers to FRHIBreadcrumbNode instances which we may have already deleted by the time the crash callback happens).
	//
	#define NV_AFTERMATH_USE_BREADCRUMB_PTRS 0

	namespace UE::RHICore::Nvidia::Aftermath
	{
		typedef TUniqueFunction<void(const void* MarkerData, const uint32_t MarkerDataSize, void* UserData, void** ResolvedMarkerData, uint32_t* ResolvedMarkerDataSize)> FResolveMarkerFunc;

		RHICORE_API bool IsEnabled();
		RHICORE_API bool AreMarkersEnabled();

		//
		// Called by platform RHIs to activate Aftermath.
		// The optional ResolveMarkerFunc is provided for platform RHIs to replace the default marker resolution with something custom.
		//
		RHICORE_API void InitializeBeforeDeviceCreation(FResolveMarkerFunc ResolveMarkerFunc = {});

		//
		// Called by platform RHIs after device creation.
		// InitCallback should return an Aftermath result value.
		//
		RHICORE_API bool InitializeDevice(TFunctionRef<uint32(uint32 AftermathFeatureFlags)> InitCallback);

		struct FCrashResult
		{
			FString OutputLog;
			TOptional<FString> DumpPath;
			TOptional<uint64> GPUFaultAddress;
		};

		//
		// Called by platform RHIs when a GPU crash is detected.
		// Waits for Aftermath to finish crash dump processing, then results the result.
		//
		RHICORE_API FCrashResult OnGPUCrash();

		static inline constexpr TCHAR RootNodeName[] = TEXT("<root>");

		//
		// Platform RHI helper for implementing RHIBeginBreadcrumbGPU / RHIEndBreadcrumbGPU
		//
	#if WITH_RHI_BREADCRUMBS
		class FMarker
		{
		#if !NV_AFTERMATH_USE_BREADCRUMB_PTRS
			FRHIBreadcrumb::FBuffer Buffer;
		#endif

			void const* Ptr = nullptr;
			uint32 Size = 0;

		public:
			FMarker(FRHIBreadcrumbNode* Breadcrumb)
			{
				if (!AreMarkersEnabled())
					return;

			#if NV_AFTERMATH_USE_BREADCRUMB_PTRS
				//
				// Have Aftermath store the breadcrumb node pointer directly.
				// Aftermath marker API fails if passed nullptr, so replace the tree root with the sentinel node.
				//
				Ptr = Breadcrumb ? Breadcrumb : FRHIBreadcrumbNode::Sentinel;
			#else
				// Generate the breadcrumb node name and have Aftermath copy the string.
				TCHAR const* NameStr = Breadcrumb ? Breadcrumb->Name.GetTCHAR(Buffer) : RootNodeName;
				Ptr = NameStr;
				Size = (FCString::Strlen(NameStr) + 1) * sizeof(TCHAR);// Include null terminator
			#endif
			}

			operator bool() const { return Ptr != nullptr; }

			void*  GetPtr () const { return const_cast<void*>(Ptr); }
			uint32 GetSize() const { return Size; }
		};
	#endif // WITH_RHI_BREADCRUMBS
	};

#endif // NV_AFTERMATH
