// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HierarchyTableTypeHandler.h"
#include "HierarchyTableType.h"
#include "Modules/ModuleInterface.h"

class FHierarchyTableEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	
	virtual void ShutdownModule() override;

	void HIERARCHYTABLEEDITOR_API RegisterTableType(const UScriptStruct* HierarchyTableType, const UHierarchyTableTypeHandler_Base* Handler);

	void HIERARCHYTABLEEDITOR_API UnregisterTableType(const UScriptStruct* HierarchyTableType);

	const UHierarchyTableTypeHandler_Base* FindHandler(const UScriptStruct* HierarchyTableType) const;

private:
	TMap<const UScriptStruct*, const UHierarchyTableTypeHandler_Base*> Handlers;
};