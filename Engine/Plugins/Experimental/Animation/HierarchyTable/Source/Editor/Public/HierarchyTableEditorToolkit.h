// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HierarchyTable.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Elements/Common/TypedElementHandles.h"

class FHierarchyTableEditorToolkit : public FAssetEditorToolkit
{
public:
	void InitEditor(const TArray<UObject*>& InObjects);
	virtual void OnClose() override;

	void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;

	FName GetToolkitFName() const override { return "HierarchyTableEditor"; }
	FText GetBaseToolkitName() const override { return INVTEXT("Hierarchy Table Editor"); }
	FString GetWorldCentricTabPrefix() const override { return "Hierarchy Table "; }
	FLinearColor GetWorldCentricTabColorScale() const override { return {}; }

	void ExtendToolbar();

private:
	void AddEntry(const FName Identifier, const EHierarchyTableEntryType EntryType);

	TSharedRef<SWidget> CreateTedsOutliner();

	UHierarchyTable* HierarchyTable;

	TMap<int32, UE::Editor::DataStorage::RowHandle> EntryIndexToHandleMap;

	UE::Editor::DataStorage::QueryHandle InitialColumnQuery;

	TSharedPtr<class ISceneOutliner> TedsOutlinerPtr;
};
