// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"
#include "Widgets/Docking/SDockTab.h"

class FObjectMixerEditorList;
class ISequencer;

class OBJECTMIXEREDITOR_API FObjectMixerEditorModule : public IModuleInterface
{
public:

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	virtual UWorld* GetWorld();

	virtual void Initialize();
	virtual void Teardown();

	static FObjectMixerEditorModule& Get();

	static void OpenProjectSettings();

	virtual FName GetModuleName() const;
	
	virtual TSharedPtr<SWidget> MakeObjectMixerDialog(
		TSubclassOf<UObjectMixerObjectFilter> InDefaultFilterClass = nullptr);

	/** Get a list of sequencers that are currently active in the editor. */
	virtual TArray<TWeakPtr<ISequencer>> GetSequencers() const;

	/**
	 * Tries to find the nomad tab assigned to this instance of Object Mixer.
	 * If DockTab is not set, will try to find the tab using GetTabSpawnerId().
	 */
	virtual TSharedPtr<SDockTab> FindNomadTab();
	
	/**
	 * Build the List widget from scratch. If DockTab is not set, will try to find the tab using GetTabSpawnerId().
	 * @return True if the widget was regenerated. False if the DockTab was invalid and could not be found.
	 */
	bool RegenerateListWidget();

	/** Called when the Rename command is executed from the UI or hotkey. */
	virtual void OnRenameCommand();
	
	void RegisterMenuGroup();
	void UnregisterMenuGroup();
	virtual void SetupMenuItemVariables();
	virtual void RegisterTabSpawner();
	virtual FName GetTabSpawnerId();
	
	/**
	 * Add a tab spawner to the Object Mixer menu group.
	 * @return If adding the item to the menu was successful
	 */
	bool RegisterItemInMenuGroup(FWorkspaceItem& InItem);
	
	virtual void UnregisterTabSpawner();
	virtual void RegisterSettings() const;
	virtual void UnregisterSettings() const;

	virtual TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	TSharedPtr<FWorkspaceItem> GetWorkspaceGroup();

	/**
	 * This is the filter class used to initialize the ListModel.
	 * This filter class cannot be turned off by the end user.
	 */
	const TSubclassOf<UObjectMixerObjectFilter>& GetDefaultFilterClass() const;

	const static FName BaseObjectMixerModuleName;
	
	DECLARE_MULTICAST_DELEGATE(FOnBlueprintFilterCompiled);
	FOnBlueprintFilterCompiled& OnBlueprintFilterCompiled()
	{
		return OnBlueprintFilterCompiledDelegate;
	}

protected:

	/** Lives for as long as the module is loaded. */
	TSharedPtr<FObjectMixerEditorList> ListModel;

	/** The text that appears on the spawned nomad tab */
	FText TabLabel;

	/** The actual spawned nomad tab */
	TWeakPtr<SDockTab> DockTab;

	/** Menu Item variables */
	FText MenuItemName;
	FSlateIcon MenuItemIcon;
	FText MenuItemTooltip;
	ETabSpawnerMenuType::Type TabSpawnerType = ETabSpawnerMenuType::Enabled;

	/**
	 * If set, this is the filter class used to initialize the ListModel.
	 * This filter class cannot be turned off by the end user.
	 */
	TSubclassOf<UObjectMixerObjectFilter> DefaultFilterClass;

	FOnBlueprintFilterCompiled OnBlueprintFilterCompiledDelegate;

private:

	TSharedPtr<FWorkspaceItem> WorkspaceGroup;
};
