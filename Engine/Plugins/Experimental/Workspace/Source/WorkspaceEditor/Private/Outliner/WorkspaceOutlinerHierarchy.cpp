// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorkspaceOutlinerHierarchy.h"

#include "ISceneOutlinerMode.h"
#include "Workspace.h"
#include "WorkspaceAssetRegistryInfo.h"
#include "WorkspaceOutlinerTreeItem.h"

namespace UE::Workspace
{
	FWorkspaceOutlinerHierarchy::FWorkspaceOutlinerHierarchy(ISceneOutlinerMode* Mode, const TWeakObjectPtr<UWorkspace>& InWorkspace) : ISceneOutlinerHierarchy(Mode), WeakWorkspace(InWorkspace)
	{
	}

	void FWorkspaceOutlinerHierarchy::CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
	{
		if (const UWorkspace* Workspace = WeakWorkspace.Get())
		{
			TArray<FAssetData> AssetDataEntries; 
			Workspace->GetAssetDataEntries(AssetDataEntries);

			for (const FAssetData& AssetData : AssetDataEntries)
			{
				FString TagValue;
				if(AssetData.GetTagValue(UE::Workspace::ExportsWorkspaceItemsRegistryTag, TagValue))
				{
					FWorkspaceOutlinerItemExports Exports;
					FWorkspaceOutlinerItemExports::StaticStruct()->ImportText(*TagValue, &Exports, nullptr, 0, nullptr, FWorkspaceOutlinerItemExports::StaticStruct()->GetName());
					for (const FWorkspaceOutlinerItemExport& Export : Exports.Exports)
					{
						if (FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FWorkspaceOutlinerTreeItem>(FWorkspaceOutlinerTreeItem::FItemData{Export}))
						{
							OutItems.Add(Item);
						}
					}
				}
			}
		}		
	}

	FSceneOutlinerTreeItemPtr FWorkspaceOutlinerHierarchy::FindOrCreateParentItem(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items, bool bCreate)
	{
		if (const FWorkspaceOutlinerTreeItem* TreeItem = Item.CastTo<FWorkspaceOutlinerTreeItem>())
		{
			const uint32 ParentHash = TreeItem->Export.GetParentHash();
			if (ParentHash != INDEX_NONE)
			{
				if (const FSceneOutlinerTreeItemPtr* ParentItem = Items.Find(ParentHash))
				{
					return *ParentItem;
				}
				else if(bCreate)
				{
					if (const UWorkspace* Workspace = WeakWorkspace.Get())
					{
						TArray<FAssetData> AssetDataEntries; 
						Workspace->GetAssetDataEntries(AssetDataEntries);

						if(const FAssetData* AssetDataPtr = AssetDataEntries.FindByPredicate([AssetPath = TreeItem->Export.GetAssetPath()](const FAssetData& AssetData) { return AssetData.GetSoftObjectPath() == AssetPath; }))
						{
							FString TagValue;
							if(AssetDataPtr->GetTagValue(UE::Workspace::ExportsWorkspaceItemsRegistryTag, TagValue))
							{
								FWorkspaceOutlinerItemExports Exports;
								FWorkspaceOutlinerItemExports::StaticStruct()->ImportText(*TagValue, &Exports, nullptr, 0, nullptr, FWorkspaceOutlinerItemExports::StaticStruct()->GetName());
								
								const FName ParentIdentifier = TreeItem->Export.GetParentIdentifier();
								if (const FWorkspaceOutlinerItemExport* ExportPtr = Exports.Exports.FindByPredicate([ParentIdentifier](const FWorkspaceOutlinerItemExport& ItemExport)
								{
									return ItemExport.GetIdentifier() == ParentIdentifier;
								}))
								{
									Mode->CreateItemFor<FWorkspaceOutlinerTreeItem>(FWorkspaceOutlinerTreeItem::FItemData{*ExportPtr}, true);
								}
							}
						}
					}
				}
			}
		}
		
		return nullptr;
	}
}
