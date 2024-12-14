// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaOutlinerDefines.h"
#include "Containers/ArrayView.h"
#include "IAvaEditorExtension.h"
#include "IAvaOutlinerModule.h"

class AActor;
class FUICommandList;
class IAvaEditor;
class UAvaOutlinerItemsContext;
class UAvaSceneState;
class UToolMenu;

class FAvaSceneRigExtension : public FAvaEditorExtension
{
public:
	UE_AVA_INHERITS(FAvaSceneRigExtension, FAvaEditorExtension);

	static UAvaSceneState* GetSceneState(UWorld* const InWorld);

	void PromptToSaveSceneRigFromOutlinerItems();
	bool CanSaveSceneRigFromOutlinerItems() const;

	void AddOutlinerItemsToSceneRig();
	bool CanAddOutlinerItemsToSceneRig() const;

	void RemoveOutlinerItemsFromSceneRig();
	bool CanRemoveOutlinerItemsFromSceneRig() const;

	//~ Begin IAvaEditorExtension
	virtual void Activate() override;
	virtual void Deactivate() override;
	virtual void BindCommands(const TSharedRef<FUICommandList>& InCommandList) override;
	//~ End IAvaEditorExtension

protected:
	static TArray<AActor*> OutlinerItemsToActors(const TConstArrayView<FAvaOutlinerItemWeakPtr>& InOutlinerItems
		, const bool bInIncludeLocked = false);

	void ExtendOutlinerItemContextMenu(UToolMenu* const InToolMenu);

	void CreateSubMenu(UToolMenu* const InToolMenu);

	FDelegateHandle OutlinerItemContextMenuDelegate;

	TWeakPtr<FUICommandList> CommandListWeak;

	TWeakObjectPtr<UAvaOutlinerItemsContext> ItemsContextWeak;
};
