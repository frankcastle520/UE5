// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeIREEModule.h"

#include "Modules/ModuleManager.h"

#ifdef WITH_NNE_RUNTIME_IREE

#include "CoreMinimal.h"
#include "NNE.h"

#if WITH_EDITOR
#include "HAL/FileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/ConfigCacheIni.h"
#endif // WITH_EDITOR

#endif // WITH_NNE_RUNTIME_IREE

#include "NNERuntimeIREELog.h"

DEFINE_LOG_CATEGORY(LogNNERuntimeIREE);

void FNNERuntimeIREEModule::StartupModule()
{
#ifdef WITH_NNE_RUNTIME_IREE
	NNERuntimeIREECpu = NewObject<UNNERuntimeIREECpu>();
	if (NNERuntimeIREECpu.IsValid())
	{
		NNERuntimeIREECpu->AddToRoot();
		UE::NNE::RegisterRuntime(NNERuntimeIREECpu.Get());
	}

	NNERuntimeIREECuda = NewObject<UNNERuntimeIREECuda>();
	if (NNERuntimeIREECuda.IsValid())
	{
		if (NNERuntimeIREECuda->IsAvailable())
		{
			NNERuntimeIREECuda->AddToRoot();
			UE::NNE::RegisterRuntime(NNERuntimeIREECuda.Get());
		}
		else
		{
			NNERuntimeIREECuda.Reset();
		}
	}

	NNERuntimeIREEVulkan = NewObject<UNNERuntimeIREEVulkan>();
	if (NNERuntimeIREEVulkan.IsValid())
	{
		if (NNERuntimeIREEVulkan->IsAvailable())
		{
			NNERuntimeIREEVulkan->AddToRoot();
			UE::NNE::RegisterRuntime(NNERuntimeIREEVulkan.Get());
		}
		else
		{
			NNERuntimeIREEVulkan.Reset();
		}
	}

	NNERuntimeIREERdg = NewObject<UNNERuntimeIREERdg>();
	if (NNERuntimeIREERdg.IsValid())
	{
		if (NNERuntimeIREERdg->IsAvailable())
		{
			NNERuntimeIREERdg->AddToRoot();
			UE::NNE::RegisterRuntime(NNERuntimeIREERdg.Get());
		}
		else
		{
			NNERuntimeIREERdg.Reset();
		}
	}
#endif // WITH_NNE_RUNTIME_IREE
}

void FNNERuntimeIREEModule::ShutdownModule()
{
#ifdef WITH_NNE_RUNTIME_IREE
	if (NNERuntimeIREECpu.IsValid())
	{
		UE::NNE::UnregisterRuntime(NNERuntimeIREECpu.Get());
		NNERuntimeIREECpu->RemoveFromRoot();
		NNERuntimeIREECpu.Reset();
	}

	if (NNERuntimeIREECuda.IsValid())
	{
		UE::NNE::UnregisterRuntime(NNERuntimeIREECuda.Get());
		NNERuntimeIREECuda->RemoveFromRoot();
		NNERuntimeIREECuda.Reset();
	}

	if (NNERuntimeIREEVulkan.IsValid())
	{
		UE::NNE::UnregisterRuntime(NNERuntimeIREEVulkan.Get());
		NNERuntimeIREEVulkan->RemoveFromRoot();
		NNERuntimeIREEVulkan.Reset();
	}

	if (NNERuntimeIREERdg.IsValid())
	{
		UE::NNE::UnregisterRuntime(NNERuntimeIREERdg.Get());
		NNERuntimeIREERdg->RemoveFromRoot();
		NNERuntimeIREERdg.Reset();
	}
#endif // WITH_NNE_RUNTIME_IREE
}
	
IMPLEMENT_MODULE(FNNERuntimeIREEModule, NNERuntimeIREE)