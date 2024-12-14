// Copyright Epic Games, Inc. All Rights Reserved.

#include "HierarchyTableEditorModule.h"
#include "Modules/ModuleManager.h"

void FHierarchyTableEditorModule::StartupModule()
{
}

void FHierarchyTableEditorModule::ShutdownModule()
{
}

void FHierarchyTableEditorModule::RegisterTableType(const UScriptStruct* HierarchyTableType, const UHierarchyTableTypeHandler_Base* Handler)
{
	Handlers.Add(HierarchyTableType, Handler);
}

void FHierarchyTableEditorModule::UnregisterTableType(const UScriptStruct* HierarchyTableType)
{
	Handlers.Remove(HierarchyTableType);
}

const UHierarchyTableTypeHandler_Base* FHierarchyTableEditorModule::FindHandler(const UScriptStruct* HierarchyTableType) const
{
	const UHierarchyTableTypeHandler_Base* const* Result = Handlers.Find(HierarchyTableType);
	if (Result)
	{
		return *Result;
	}
	return nullptr;
}

IMPLEMENT_MODULE(FHierarchyTableEditorModule, HierarchyTableEditor)
