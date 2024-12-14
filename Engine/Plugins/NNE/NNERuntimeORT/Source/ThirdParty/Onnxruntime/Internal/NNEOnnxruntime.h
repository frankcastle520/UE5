// Copyright Epic Games, Inc. All Rights Reserved.

// Summary: Wrapper around the ONNX Runtime C/C++ API.
// - Only include THIS header, DO NOT include any ORT header directly
// - Forward declaration of any Ort:: types odes not work because of the injected inline namespace
// - Manually load the DLL and use the optained exports to initialize the C++ API, i.e.:
//
//		DllHandle = FPlatformProcess::GetDllHandle(*DllPath);
//
// 		TUniquePtr<UE::NNEOnnxruntime::OrtApiFunctions> OrtApiFunctions = UE::NNEOnnxruntime::LoadApiFunctions(DllHandle);
// 		if (OrtApiFunctions.IsValid())
// 		{
// 			Ort::InitApi(OrtApiFunctions->OrtGetApiBase()->GetApi(ORT_API_VERSION));
// 		}
//
// - To avoid conflicts among multiple API's, set CPP definitions in Build.cs accordingly, i.e.:
// 		PublicDefinitions.Add("UE_ORT_USE_INLINE_NAMESPACE = 1");
// 		PublicDefinitions.Add("UE_ORT_INLINE_NAMESPACE_NAME = Ort011401");

// Warp around another version of ONNX Runtime:
// - Add this as Internal header and adapt Build.cs
// - Add macro to inject inline namespace to namespace Ort (in files onnxruntime_cxx_api.h and onnxruntime_cxx_inline.h)
// - Check for changes in C API and adapt wrapper struct and loading code if necessary
// - Client code should not require any modification unless ORT changed its C or C++ API's

#pragma once

#include "HAL/Platform.h"
#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"
#include "Templates/UniquePtr.h"

// Log category declaration
DECLARE_LOG_CATEGORY_EXTERN(LogNNEOnnxruntime, Log, All);

// Add log catecory definition to client cpp:
// DEFINE_LOG_CATEGORY(LogNNEOnnxruntime);

// Helper macro to convert a CPP variable to a string literal.
#define UE_ORT_INTERNAL_DO_TOKEN_STR(x) #x
#define UE_ORT_INTERNAL_TOKEN_STR(x) UE_ORT_INTERNAL_DO_TOKEN_STR(x)

#if !defined(UE_ORT_USE_INLINE_NAMESPACE) || \
	!defined(UE_ORT_INLINE_NAMESPACE_NAME)
#error Onnxruntime.Build.cs is misconfigured.
#endif

// Check that UE_ORT_INLINE_NAMESPACE_NAME is not empty
#if defined(__cplusplus) && UE_ORT_USE_INLINE_NAMESPACE == 1
#define UE_ORT_INTERNAL_INLINE_NAMESPACE_STR \
  UE_ORT_INTERNAL_TOKEN_STR(UE_ORT_INLINE_NAMESPACE_NAME)

static_assert(UE_ORT_INTERNAL_INLINE_NAMESPACE_STR[0] != '\0',
			  "Onnxruntime.Build.cs is misconfigured: UE_ORT_INLINE_NAMESPACE_NAME must "
			  "not be empty.");

#endif

#if UE_ORT_USE_INLINE_NAMESPACE == 0
#define UE_ORT_NAMESPACE_BEGIN
#define UE_ORT_NAMESPACE_END
#elif UE_ORT_USE_INLINE_NAMESPACE == 1
#define UE_ORT_NAMESPACE_BEGIN \
	inline namespace UE_ORT_INLINE_NAMESPACE_NAME {
#define UE_ORT_NAMESPACE_END }
#else
#error Onnxruntime.Build.cs is misconfigured.
#endif

// We register our own error handler for the case when exceptions are diabled
#ifdef ORT_NO_EXCEPTIONS
#define ORT_CXX_API_THROW(string, code) \
	UE_LOG(LogNNEOnnxruntime, Fatal, TEXT("%hs"), Ort::Exception(string, code).what());
#endif

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif
THIRD_PARTY_INCLUDES_START
#include "onnxruntime_cxx_api.h"
#include "cpu_provider_factory.h"
#if PLATFORM_WINDOWS
#include "dml_provider_factory.h"
#endif
THIRD_PARTY_INCLUDES_END
#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace UE::NNEOnnxruntime
{

typedef const OrtApiBase* (*OrtGetApiBaseFunction)(void);
typedef OrtStatusPtr (*OrtSessionOptionsAppendExecutionProvider_CPUFunction)(OrtSessionOptions*, int);
#if PLATFORM_WINDOWS
typedef OrtStatusPtr (*OrtSessionOptionsAppendExecutionProvider_DMLFunction)(OrtSessionOptions*, int);
typedef OrtStatusPtr (*OrtSessionOptionsAppendExecutionProviderEx_DMLFunction)(OrtSessionOptions*, IDMLDevice*, ID3D12CommandQueue*);
#endif

struct OrtApiFunctions
{
	OrtGetApiBaseFunction OrtGetApiBase;
	OrtSessionOptionsAppendExecutionProvider_CPUFunction OrtSessionOptionsAppendExecutionProvider_CPU;
#if PLATFORM_WINDOWS
	OrtSessionOptionsAppendExecutionProvider_DMLFunction OrtSessionOptionsAppendExecutionProvider_DML;
	OrtSessionOptionsAppendExecutionProviderEx_DMLFunction OrtSessionOptionsAppendExecutionProviderEx_DML;
#endif
};

inline TUniquePtr<OrtApiFunctions> LoadApiFunctions(void* DllHandle)
{
	TUniquePtr<OrtApiFunctions> Result = MakeUnique<OrtApiFunctions>();

	bool bHasLoadedFunctions = true;
	Result->OrtGetApiBase = reinterpret_cast<OrtGetApiBaseFunction>(FPlatformProcess::GetDllExport(DllHandle, TEXT("OrtGetApiBase")));
	Result->OrtSessionOptionsAppendExecutionProvider_CPU = reinterpret_cast<OrtSessionOptionsAppendExecutionProvider_CPUFunction>(FPlatformProcess::GetDllExport(DllHandle, TEXT("OrtSessionOptionsAppendExecutionProvider_CPU")));
#if PLATFORM_WINDOWS
	Result->OrtSessionOptionsAppendExecutionProvider_DML = reinterpret_cast<OrtSessionOptionsAppendExecutionProvider_DMLFunction>(FPlatformProcess::GetDllExport(DllHandle, TEXT("OrtSessionOptionsAppendExecutionProvider_DML")));
	Result->OrtSessionOptionsAppendExecutionProviderEx_DML = reinterpret_cast<OrtSessionOptionsAppendExecutionProviderEx_DMLFunction>(FPlatformProcess::GetDllExport(DllHandle, TEXT("OrtSessionOptionsAppendExecutionProviderEx_DML")));
#endif

	bHasLoadedFunctions = bHasLoadedFunctions && Result->OrtGetApiBase;
	bHasLoadedFunctions = bHasLoadedFunctions && Result->OrtSessionOptionsAppendExecutionProvider_CPU;
#if PLATFORM_WINDOWS
	bHasLoadedFunctions = bHasLoadedFunctions && Result->OrtSessionOptionsAppendExecutionProvider_DML;
	bHasLoadedFunctions = bHasLoadedFunctions && Result->OrtSessionOptionsAppendExecutionProviderEx_DML;
#endif

	if (!bHasLoadedFunctions)
	{
		return {};
	}
	
	return Result;
}

} // namespace UE::NNEOnnxruntime