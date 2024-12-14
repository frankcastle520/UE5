// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeCategories.h"
#include "IDMXControlConsoleEditorModule.h"
#include "Misc/AssetCategoryPath.h"

class FMenuBuilder;
class FSpawnTabArgs;
class SDockTab;
class UDMXControlConsole;


/** Editor Module for DMXControlConsole */
class FDMXControlConsoleEditorModule
	: public IDMXControlConsoleEditorModule
{
public:
	/** Constructor */
	FDMXControlConsoleEditorModule();

	//~ Begin IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~End IModuleInterface interface

	//~ Begin IDMXControlConsoleEditorModule interface
	virtual FAssetCategoryPath GetControlConsoleCategory() const override { return ControlConsoleCategory; }
	//~End IDMXControlConsoleEditorModule interface

	/** Gets the DMX Editor asset category */
	static EAssetTypeCategories::Type GetDMXEditorAssetCategory() { return DMXEditorAssetCategory; }

	/** Opens the ControlConsole */
	static void OpenControlConsole();

	/** Returns the compact editor tab, or nullptr if the compact editor tab is closed */
	TSharedPtr<SDockTab> GetCompactEditorTab() const { return CompactEditorTab; }

	/** Name identifier for the Control Console Editor app */
	static const FName ControlConsoleEditorAppIdentifier;

	/** Tab Id for the compact editor tab */
	static const FName CompactEditorTabId;

private:
	/** Registers Control Console commands in the Level Editor */
	void RegisterLevelEditorCommands();

	/** Registers an extender for the Level Editor Toolbar DMX Menu */
	static void RegisterDMXMenuExtender();

	/** Extends the Level Editor Toolbar DMX Menu */
	static void ExtendDMXMenu(FMenuBuilder& MenuBuilder);

	/** Registers a tab spawner for the compact editor */
	void RegisterCompactEditorTabSpawner();

	/** Called when the compact editor tab is spawned */
	static TSharedRef<SDockTab> OnSpawnCompactEditorTab(const FSpawnTabArgs& InSpawnTabArgs);

	/** Called when the compact editor tab was closed */
	static void OnCompactEditorTabClosed(TSharedRef<SDockTab> Tab);

	// Called at the end of UEngine::Init, right before loading PostEngineInit modules for both normal execution and commandlets
	void OnPostEnginInit();

	/** 
	 * The compact control console editor tab, or nullptr if the tab is closed.
	 * Mind we explicitly hold the active tab here, as it may be assigned to any tab manager,
	 * not just the global or the level editor tab manager.
	 */
	TSharedPtr<SDockTab> CompactEditorTab;

	/** The category path under which Control Console assets are nested. */
	FAssetCategoryPath ControlConsoleCategory;

	/** The DMX Editor asset category */
	static EAssetTypeCategories::Type DMXEditorAssetCategory;

	/** Name of the Control Console Editor Tab  */
	static const FName ControlConsoleEditorTabName;
};
