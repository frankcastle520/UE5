// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGModule.h"

#include "HAL/IConsoleManager.h"
#include "NNE.h"
#include "NNEHlslShadersLog.h"
#include "NNERuntimeRDGHlsl.h"
#include "UObject/WeakInterfacePtr.h"
#include "DataDrivenShaderPlatformInfo.h"

static TAutoConsoleVariable<int32> CVarHlslModelOptimization(
	TEXT("nne.hlsl.ModelOptimization"),
	1,
	TEXT("Allows model optimizations when model are cooked for the HLSL runtime.\n")
	TEXT(" 0: disabled\n")
	TEXT(" 1: enabled (default)")
#if !WITH_EDITOR
	,ECVF_ReadOnly
#endif
	);

void FNNERuntimeRDGModule::RegisterRuntime()
{
	NNERuntimeRDGHlsl = NewObject<UNNERuntimeRDGHlslImpl>();
	if (NNERuntimeRDGHlsl.IsValid())
	{
		TWeakInterfacePtr<INNERuntime> RuntimeInterface(NNERuntimeRDGHlsl.Get());

		NNERuntimeRDGHlsl->Init();
		NNERuntimeRDGHlsl->AddToRoot();
		UE::NNE::RegisterRuntime(RuntimeInterface);
	}
}

void FNNERuntimeRDGModule::StartupModule()
{
#if WITH_EDITOR
	#ifdef WITH_NNE_RUNTIME_HLSL 
		RegisterRuntime();
	#endif
#else
	#ifdef WITH_NNE_RUNTIME_HLSL 
		if(FDataDrivenShaderPlatformInfo::GetSupportsNNEShaders(GMaxRHIShaderPlatform))
		{
			if(UNNERuntimeRDGHlslImpl::IsCurrentPlatformSupported())
			{
				RegisterRuntime();
			}
			else
			{
				UE_LOG(LogNNERuntimeRDGHlsl, Display, TEXT("Not registering runtime because current hardware is incompatible, consider bypassing by setting the define NNE_FORCE_HARDWARE_SUPPORTS_HLSL."));
			}
		}
		else
		{
			UE_LOG(LogNNERuntimeRDGHlsl, Display, TEXT("Not registering runtime because current RHI shader platform is not enabled, consider setting the flag bSupportsNNEShaders in DataDrivenPlatformInfo."));
		}
	#else
		UE_LOG(LogNNERuntimeRDGHlsl, Display, TEXT("Not registering runtime as platform is not enabled, if needed set define WITH_NNE_RUNTIME_HLSL."));
	#endif
#endif
}

void FNNERuntimeRDGModule::ShutdownModule()
{

	// NNE runtime RDG Hlsl shutdown
	if (NNERuntimeRDGHlsl.IsValid())
	{
		TWeakInterfacePtr<INNERuntime> RuntimeInterface(NNERuntimeRDGHlsl.Get());

		UE::NNE::UnregisterRuntime(RuntimeInterface);
		NNERuntimeRDGHlsl->RemoveFromRoot();
		NNERuntimeRDGHlsl.Reset();
	}

}

IMPLEMENT_MODULE(FNNERuntimeRDGModule, NNERuntimeRDG);
