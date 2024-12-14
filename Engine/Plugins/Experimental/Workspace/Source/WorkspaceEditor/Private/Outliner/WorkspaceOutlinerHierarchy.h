// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutlinerHierarchy.h"

class UWorkspace;

namespace UE::Workspace
{
	class FWorkspaceOutlinerHierarchy : public ISceneOutlinerHierarchy
	{
	public:
		FWorkspaceOutlinerHierarchy(ISceneOutlinerMode* Mode, const TWeakObjectPtr<UWorkspace>& InWorkspace);
		FWorkspaceOutlinerHierarchy(const FWorkspaceOutlinerHierarchy&) = delete;
		FWorkspaceOutlinerHierarchy& operator=(const FWorkspaceOutlinerHierarchy&) = delete;

		// Begin ISceneOutlinerHierarchy overrides
		virtual void CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const override;
		virtual void CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const override {}
		virtual FSceneOutlinerTreeItemPtr FindOrCreateParentItem(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items, bool bCreate = false) override;
		// End ISceneOutlinerHierarchy overrides
	private:
		TWeakObjectPtr<UWorkspace> WeakWorkspace;
	};
}