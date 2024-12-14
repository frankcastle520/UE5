// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMaterialShadersEditorModule.h"
#include "DMAlphaOneMinusPS.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"

void FDynamicMaterialShadersEditorModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME);
	check(Plugin.IsValid());

	const FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(UE::DynamicMaterialShadersEditor::Internal::VirtualShaderMountPoint, PluginShaderDir);

	// Force init
	FDMAlphaOneMinusPS::GetStaticType();
}

IMPLEMENT_MODULE(FDynamicMaterialShadersEditorModule, DynamicMaterialShadersEditor)
