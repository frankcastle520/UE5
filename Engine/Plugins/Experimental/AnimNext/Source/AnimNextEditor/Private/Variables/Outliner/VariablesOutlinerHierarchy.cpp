// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerHierarchy.h"

#include "AnimNextRigVMAsset.h"
#include "ISceneOutlinerMode.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "VariablesOutlinerAssetItem.h"
#include "VariablesOutlinerDataInterfaceItem.h"
#include "VariablesOutlinerMode.h"
#include "WorkspaceAssetRegistryInfo.h"
#include "VariablesOutlinerEntryItem.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Entries/AnimNextRigVMAssetEntry.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Variables/SVariablesView.h"
#include "DataInterface/AnimNextDataInterface.h"

namespace UE::AnimNext::Editor
{

FVariablesOutlinerHierarchy::FVariablesOutlinerHierarchy(ISceneOutlinerMode* Mode)
	: ISceneOutlinerHierarchy(Mode)
{
}

void FVariablesOutlinerHierarchy::CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
{
	SVariablesOutliner* Outliner = static_cast<FVariablesOutlinerMode* const>(Mode)->GetOutliner();
	for(const TSoftObjectPtr<UAnimNextRigVMAsset>& SoftAsset : Outliner->Assets)
	{
		UAnimNextRigVMAsset* Asset = SoftAsset.Get();
		if(Asset == nullptr)
		{
			continue;
		}

		const UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
		if (EditorData == nullptr)
		{
			continue;
		}

		if (FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FVariablesOutlinerAssetItem>(SoftAsset))
		{
			OutItems.Add(Item);
		}

		EditorData->ForEachEntryOfType<UAnimNextVariableEntry>([this, &OutItems](UAnimNextVariableEntry* InVariable)
		{
			if (FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FVariablesOutlinerEntryItem>(CastChecked<UAnimNextVariableEntry>(InVariable)))
			{
				OutItems.Add(Item);
			}
			return true;
		});

		EditorData->ForEachEntryOfType<UAnimNextDataInterfaceEntry>([this, &OutItems](UAnimNextDataInterfaceEntry* InInterfaceEntry)
		{
			if (FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FVariablesOutlinerDataInterfaceItem>(CastChecked<UAnimNextDataInterfaceEntry>(InInterfaceEntry)))
			{
				OutItems.Add(Item);
			}

			auto AddDataInterface = [this, &OutItems](UAnimNextDataInterfaceEntry* InDataInterfaceEntry, UAnimNextDataInterfaceEntry* InRootDataInterfaceEntry, auto& InAddDataInterface) -> void
			{
				UAnimNextRigVMAssetEditorData* InterfaceEditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(InDataInterfaceEntry->DataInterface.Get());
				InterfaceEditorData->ForEachEntryOfType<UAnimNextVariableEntry>([this, &OutItems, InRootDataInterfaceEntry](UAnimNextVariableEntry* InVariable)
				{
					if(InVariable->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Public)
					{
						if (FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FVariablesOutlinerEntryItem>(CastChecked<UAnimNextVariableEntry>(InVariable)))
						{
							FVariablesOutlinerEntryItem* EntryItem = Item->CastTo<FVariablesOutlinerEntryItem>();
							EntryItem->WeakDataInterfaceEntry = InRootDataInterfaceEntry;
							OutItems.Add(Item);
						}
					}
					return true;
				});

				InterfaceEditorData->ForEachEntryOfType<UAnimNextDataInterfaceEntry>([this, &OutItems, &InAddDataInterface, InRootDataInterfaceEntry](UAnimNextDataInterfaceEntry* InSubInterfaceEntry)
				{
					InAddDataInterface(InSubInterfaceEntry, InRootDataInterfaceEntry, InAddDataInterface);
					return true;
				});
			};

			AddDataInterface(InInterfaceEntry, InInterfaceEntry, AddDataInterface);

			return true;
		});
	}
}

FSceneOutlinerTreeItemPtr FVariablesOutlinerHierarchy::FindOrCreateParentItem(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items, bool bCreate)
{
	if (const FVariablesOutlinerEntryItem* EntryItem = Item.CastTo<FVariablesOutlinerEntryItem>())
	{
		UAnimNextDataInterfaceEntry* DataInterfaceEntry = EntryItem->WeakDataInterfaceEntry.Get();
		if(DataInterfaceEntry != nullptr)
		{
			// Add as part of an imported data interface, so use that as parent
			TSoftObjectPtr<UAnimNextRigVMAssetEntry> SoftObjectPtr(DataInterfaceEntry); 
			uint32 Hash = GetTypeHash(SoftObjectPtr);

			if (const FSceneOutlinerTreeItemPtr* ParentItem = Items.Find(Hash))
			{
				return *ParentItem;
			}
		}
		else
		{
			UAnimNextRigVMAssetEntry* Entry = EntryItem->WeakEntry.Get();
			if (Entry == nullptr)
			{
				return nullptr;
			}

			UAnimNextRigVMAsset* Asset = Entry->GetTypedOuter<UAnimNextRigVMAsset>();
			if (Entry == nullptr)
			{
				return nullptr;
			}

			TSoftObjectPtr<UAnimNextRigVMAsset> SoftObjectPtr(Asset);
			uint32 Hash = GetTypeHash(SoftObjectPtr);

			if (const FSceneOutlinerTreeItemPtr* ParentItem = Items.Find(Hash))
			{
				return *ParentItem;
			}
		}
	}

	if (const FVariablesOutlinerDataInterfaceItem* InterfaceItem = Item.CastTo<FVariablesOutlinerDataInterfaceItem>())
	{
		UAnimNextRigVMAssetEntry* Entry = InterfaceItem->WeakEntry.Get();
		if (Entry == nullptr)
		{
			return nullptr;
		}

		UAnimNextRigVMAsset* Asset = Entry->GetTypedOuter<UAnimNextRigVMAsset>();
		if (Entry == nullptr)
		{
			return nullptr;
		}

		TSoftObjectPtr<UAnimNextRigVMAsset> SoftObjectPtr(Asset);
		uint32 Hash = GetTypeHash(SoftObjectPtr);

		if (const FSceneOutlinerTreeItemPtr* ParentItem = Items.Find(Hash))
		{
			return *ParentItem;
		}
	}

	return nullptr;
}

}
