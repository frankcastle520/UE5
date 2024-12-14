// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoldoutCompositeModule.h"

#include "Interfaces/IPluginManager.h"
#include "HDRHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogHoldoutComposite);

#define LOCTEXT_NAMESPACE "HoldoutComposite"

void FHoldoutCompositeModule::StartupModule()
{
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/HoldoutComposite"), FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("HoldoutComposite"))->GetBaseDir(), TEXT("Shaders")));

	if (IsHDREnabled())
	{
		UE_LOG(LogHoldoutComposite, Warning, TEXT("Holdout composite disabled: HDR mode is not currently supported."));
	}
}

void FHoldoutCompositeModule::ShutdownModule()
{
}

IMPLEMENT_MODULE( FHoldoutCompositeModule, HoldoutComposite )

#undef LOCTEXT_NAMESPACE
