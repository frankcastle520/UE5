// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class SWidget;
class IMessageLogListing;
struct FTopLevelAssetPath;

namespace UE::AnimNext::Editor
{

const FLazyName CompilerResultsTabName("CompilerResultsTab");
const FLazyName LogListingName("AnimNextCompilerResults");

struct FVariablePickerArgs;

class IAnimNextEditorModule : public IModuleInterface
{
public:
	// Register a valid fragment type name to be used with parameter UOLs
	// @param InLocatorFragmentEditorName The name of the locator fragment editor
	virtual void RegisterLocatorFragmentEditorType(FName InLocatorFragmentEditorName) = 0;

	// Unregister a valid fragment type name to be used with parameter UOLs
	// @param InLocatorFragmentEditorName The name of the locator fragment editor
	virtual void UnregisterLocatorFragmentEditorType(FName InLocatorFragmentEditorName) = 0;

	// Add a UClass path to the set of classes which can be opened within an AnimNext Workspace
	// @param InClassAssetPath Asset path for to-be-registered Class 
	virtual void AddWorkspaceSupportedAssetClass(const FTopLevelAssetPath& InClassAssetPath) = 0;
	
	// Remove a UClass path to the set of classes which can be opened within an AnimNext Workspace
	// @param InClassAssetPath Asset path for to-be-unregistered Class 
	virtual void RemoveWorkspaceSupportedAssetClass(const FTopLevelAssetPath& InClassAssetPath) = 0;
};

}
