// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMaterialTextureSetEditorModule.h"

#include "DMTextureSetContentBrowserIntegrationPrivate.h"
#include "DMTextureSetStyle.h"
#include "Modules/ModuleManager.h"

void FDynamicMaterialTextureSetEditorModule::StartupModule()
{
	FDMTextureSetStyle::Get();
	FDMTextureSetContentBrowserIntegrationPrivate::Integrate();
}

void FDynamicMaterialTextureSetEditorModule::ShutdownModule()
{
	FDMTextureSetContentBrowserIntegrationPrivate::Disintegrate();
}

IMPLEMENT_MODULE(FDynamicMaterialTextureSetEditorModule, DynamicMaterialTextureSetEditor)
