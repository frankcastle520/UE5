// Copyright Epic Games, Inc. All Rights Reserved.

#include "HierarchyTableBuiltinEditorModule.h"
#include "TimeProfile/HierarchyTableTypeTime.h"
#include "TimeProfile/TimeProfileTypeHandler.h"
#include "MaskProfile/HierarchyTableTypeMask.h"
#include "MaskProfile/MaskProfileTypeHandler.h"
#include "Modules/ModuleManager.h"
#include "HierarchyTableEditorModule.h"

void FHierarchyTableBuiltinEditorModule::StartupModule()
{
	FHierarchyTableEditorModule& HierarchyTableModule = FModuleManager::LoadModuleChecked<FHierarchyTableEditorModule>("HierarchyTableEditor");
	HierarchyTableModule.RegisterTableType(FHierarchyTableType_Mask::StaticStruct(), GetDefault<UHierarchyTableTypeHandler_Mask>());
	HierarchyTableModule.RegisterTableType(FHierarchyTableType_Time::StaticStruct(), GetDefault<UHierarchyTableTypeHandler_Time>());
}

void FHierarchyTableBuiltinEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("HierarchyTableEditor"))
	{
		FHierarchyTableEditorModule& HierarchyTableModule = FModuleManager::GetModuleChecked<FHierarchyTableEditorModule>("HierarchyTableEditor");
		HierarchyTableModule.UnregisterTableType(FHierarchyTableType_Mask::StaticStruct());
		HierarchyTableModule.UnregisterTableType(FHierarchyTableType_Time::StaticStruct());
	}
}

IMPLEMENT_MODULE(FHierarchyTableBuiltinEditorModule, HierarchyTableBuiltinEditor)
