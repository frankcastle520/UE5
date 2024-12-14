// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextRigVMAssetEditorData.h"
#include "ISceneOutlinerMode.h"

class UAnimNextRigVMAssetEditorData;

namespace UE::Workspace
{
	class IWorkspaceEditor;
}

namespace UE::AnimNext::Editor
{
	class SVariablesOutliner;
}

namespace UE::AnimNext::Editor
{
class FVariablesOutlinerMode : public ISceneOutlinerMode
{
public:
	FVariablesOutlinerMode(SVariablesOutliner* InVariablesOutliner, const TSharedRef<UE::Workspace::IWorkspaceEditor>& InWorkspaceEditor);

	// Begin ISceneOutlinerMode overrides
	virtual void Rebuild() override;
	virtual TSharedPtr<SWidget> CreateContextMenu() override;
	virtual FReply OnKeyDown(const FKeyEvent& InKeyEvent) override;
	virtual void OnItemClicked(FSceneOutlinerTreeItemPtr Item) override;
	void HandleItemSelection(const FSceneOutlinerItemSelection& Selection);
	virtual void OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection) override;
	virtual bool CanCustomizeToolbar() const override { return true; }
	virtual bool CanRenameItem(const ISceneOutlinerTreeItem& Item) const override;
	virtual ESelectionMode::Type GetSelectionMode() const override { return ESelectionMode::Multi; }
	virtual void BindCommands(const TSharedRef<FUICommandList>& OutCommandList) override;
	virtual TSharedPtr<FDragDropOperation> CreateDragDropOperation(const FPointerEvent& MouseEvent, const TArray<FSceneOutlinerTreeItemPtr>& InTreeItems) const override;

protected:
	virtual TUniquePtr<ISceneOutlinerHierarchy> CreateHierarchy() override;
	// End ISceneOutlinerMode overrides

	void ResetOutlinerSelection();

	SVariablesOutliner* GetOutliner() const;

	void Rename();

	bool CanRename() const;

	void Delete();

	bool CanDelete() const;

private:
	friend class FVariablesOutlinerHierarchy;

	TWeakPtr<UE::Workspace::IWorkspaceEditor> WeakWorkspaceEditor;

	TSharedPtr<FUICommandList> CommandList;
};

}