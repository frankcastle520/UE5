// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdMode.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"

#include "StateTreeEditorMode.generated.h"

class FStateTreeBindingExtension;
class FStateTreeBindingsChildrenCustomization;
class IDetailsView;
class IMessageLogListing;

UCLASS(Transient)
class STATETREEEDITORMODULE_API UStateTreeEditorMode : public UEdMode
{
	GENERATED_BODY()
public:
	const static FEditorModeID EM_StateTree;
	
	UStateTreeEditorMode();

	virtual void Enter() override;
	virtual void Exit() override;
	virtual void CreateToolkit() override;
	virtual void BindCommands() override;

protected:
	void OnStateTreeChanged();
	void BindToolkitCommands(const TSharedRef<FUICommandList>& ToolkitCommands);

	void OnIdentifierChanged(const UStateTree& InStateTree);
	void OnSchemaChanged(const UStateTree& InStateTree);
	void ForceRefreshDetailsView() const;
	void OnRefreshDetailsView(const UStateTree& InStateTree) const;
	void OnStateParametersChanged(const UStateTree& InStateTree, const FGuid ChangedStateID) const;

	void HandleMessageTokenClicked(const TSharedRef<IMessageToken>& InMessageToken) const;

	void OnAssetFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent) const;
	void OnSelectionFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);

	void Compile();
	bool CanCompile() const;

	bool HasValidStateTree() const;
	
	void HandleModelAssetChanged();
	void HandleModelSelectionChanged(const TArray<TWeakObjectPtr<UStateTreeState>>& SelectedStates) const;
	void HandleModelBringNodeToFocus(const UStateTreeState* State, const FGuid NodeID) const;

	void HandleStateAdded(UStateTreeState* , UStateTreeState*) { UpdateAsset(); }
	void HandleStatesRemoved(const TSet<UStateTreeState*>&) { UpdateAsset(); }
	void HandleOnStatesMoved(const TSet<UStateTreeState*>&, const TSet<UStateTreeState*>&) { UpdateAsset(); }
	/** Resolve the internal editor data and fixup the StateTree nodes. */
	void UpdateAsset();

	TSharedPtr<IDetailsView> GetDetailsView() const;
	TSharedPtr<IDetailsView> GetAssetDetailsView() const;
	
	TSharedPtr<IMessageLogListing> GetMessageLogListing() const;	
	void ShowCompilerTab() const;

	UStateTree* GetStateTree() const;
	
	friend class FStateTreeEditorModeToolkit;

protected:
	uint32 EditorDataHash = 0;
	bool bLastCompileSucceeded = true;
	bool bForceAssetDetailViewToRefresh = false;

	TWeakObjectPtr<UStateTree> CachedStateTree = nullptr;

	TSharedPtr<FStateTreeBindingExtension> DetailsViewExtensionHandler;
	TSharedPtr<FStateTreeBindingsChildrenCustomization> DetailsViewChildrenCustomizationHandler;
};

