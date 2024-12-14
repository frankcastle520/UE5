// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerMode.h"

#include "ISourceControlModule.h"
#include "WorkspaceItemMenuContext.h"
#include "VariablesOutlinerHierarchy.h"
#include "VariablesOutlinerEntryItem.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "IWorkspaceEditor.h"
#include "AnimNextEditorModule.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "Framework/Commands/GenericCommands.h"
#include "Entries/AnimNextRigVMAssetEntry.h"
#include "Variables/SAddVariablesDialog.h"
#include "Variables/AnimNextVariableItemMenuContext.h"
#include "AnimNextRigVMAsset.h"
#include "VariablesOutlinerAssetItem.h"
#include "VariablesOutlinerDataInterfaceItem.h"
#include "VariablesOutlinerDragDrop.h"
#include "Common/GraphEditorSchemaActions.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Variables/SVariablesView.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Variables/AnimNextVariableEntryProxy.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerMode"

namespace UE::AnimNext::Editor
{

FVariablesOutlinerMode::FVariablesOutlinerMode(SVariablesOutliner* InVariablesOutliner, const TSharedRef<UE::Workspace::IWorkspaceEditor>& InWorkspaceEditor)
	: ISceneOutlinerMode(InVariablesOutliner)
	, WeakWorkspaceEditor(InWorkspaceEditor)
{
	CommandList = MakeShared<FUICommandList>();
}

void FVariablesOutlinerMode::Rebuild()
{
	Hierarchy = CreateHierarchy();
}

TSharedPtr<SWidget> FVariablesOutlinerMode::CreateContextMenu()
{
	static const FName MenuName("VariablesOutliner.ItemContextMenu");
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus->IsMenuRegistered(MenuName))
	{
		FToolMenuOwnerScoped ToolMenuOwnerScope(this);
		if (UToolMenu* Menu = ToolMenus->RegisterMenu(MenuName))
		{
			Menu->AddDynamicSection(TEXT("Assets"), FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
			{
				const UAssetEditorToolkitMenuContext* EditorContext = InMenu->FindContext<UAssetEditorToolkitMenuContext>();
				const UAnimNextVariableItemMenuContext* MenuContext = InMenu->FindContext<UAnimNextVariableItemMenuContext>();
				if(EditorContext == nullptr || MenuContext == nullptr)
				{
					return;
				}

				FToolMenuSection& VariablesSection = InMenu->AddSection("Variables", LOCTEXT("VariablesSectionLabel", "Variables"));

				VariablesSection.AddMenuEntry(TEXT("AddVariables"),
					LOCTEXT("AddVariablesMenuItem", "Add Variable(s)"),
					LOCTEXT("AddVariablesMenuItemTooltip", "Adds variables to assets.\nIf multiple assets are selected, then variables will be added to each.\nIf no assets are selected and there are multiple assets, variables will be added to all assets."),
					FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Plus"),
					FUIAction(FExecuteAction::CreateLambda([WeakOutliner = MenuContext->WeakOutliner]()
					{
						if (TSharedPtr<SVariablesOutliner> Outliner = WeakOutliner.Pin())
						{
							Outliner->HandleAddVariablesClicked();
						}
					}))
				);

				VariablesSection.AddMenuEntryWithCommandList(FGenericCommands::Get().Delete, MenuContext->WeakCommandList.Pin());
				VariablesSection.AddMenuEntryWithCommandList(FGenericCommands::Get().Rename, MenuContext->WeakCommandList.Pin());

				if (TSharedPtr<SVariablesOutliner> Outliner = MenuContext->WeakOutliner.Pin())
				{
					Outliner->AddSourceControlMenuOptions(InMenu);
				}
			}));
		}
	}

	{
		UAnimNextVariableItemMenuContext* MenuContext = NewObject<UAnimNextVariableItemMenuContext>();
		MenuContext->WeakWorkspaceEditor = WeakWorkspaceEditor;
		MenuContext->WeakOutliner = StaticCastSharedRef<SVariablesOutliner>(SceneOutliner->AsShared());
		MenuContext->WeakCommandList = CommandList;
		TArray<FSceneOutlinerTreeItemPtr> SelectedItems = GetOutliner()->GetSelectedItems();
		for(const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
		{
			if (const FVariablesOutlinerAssetItem* AssetItem = Item->CastTo<FVariablesOutlinerAssetItem>())
			{
				UAnimNextRigVMAsset* Asset = AssetItem->SoftAsset.Get();
				if(Asset == nullptr)
				{
					continue;
				}

				UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
				if(EditorData == nullptr)
				{
					continue;
				}
				
				MenuContext->WeakEditorDatas.Add(EditorData);
			}
			else if (const FVariablesOutlinerEntryItem* EntryItem = Item->CastTo<FVariablesOutlinerEntryItem>())
			{
				UAnimNextRigVMAssetEntry* Entry = EntryItem->WeakEntry.Get();
				if(Entry == nullptr)
				{
					continue;
				}

				MenuContext->WeakEntries.Add(Entry);
			}
		}

		FToolMenuContext Context;
		Context.AddObject(MenuContext);
		WeakWorkspaceEditor.Pin()->InitToolMenuContext(Context);
		return UToolMenus::Get()->GenerateWidget(MenuName, Context);
	}
}

FReply FVariablesOutlinerMode::OnKeyDown(const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void FVariablesOutlinerMode::OnItemClicked(FSceneOutlinerTreeItemPtr Item)
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	HandleItemSelection(Selection);
}

void FVariablesOutlinerMode::HandleItemSelection(const FSceneOutlinerItemSelection& Selection)
{
	if (TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = WeakWorkspaceEditor.Pin())
	{
		TArray<FSceneOutlinerTreeItemPtr> SelectedItems;
		Selection.Get(SelectedItems);
		TArray<UObject*> EntriesToShow;
		EntriesToShow.Reserve(SelectedItems.Num());
		for(const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
		{
			if (FVariablesOutlinerEntryItem* VariablesItem = Item->CastTo<FVariablesOutlinerEntryItem>())
			{
				if(UAnimNextVariableEntry* VariableEntry = VariablesItem->WeakEntry.Get())
				{
					if(UAnimNextDataInterfaceEntry* DataInterfaceEntry = VariablesItem->WeakDataInterfaceEntry.Get())
					{
						// Create proxy object to display in the details panel
						if(!VariablesItem->ProxyEntry.IsValid())
						{
							VariablesItem->ProxyEntry = TStrongObjectPtr(NewObject<UAnimNextVariableEntryProxy>(GetTransientPackage(), NAME_None, RF_Transient));
						}
						VariablesItem->ProxyEntry.Get()->VariableEntry = VariableEntry;
						VariablesItem->ProxyEntry.Get()->DataInterfaceEntry = DataInterfaceEntry;
						EntriesToShow.Add(VariablesItem->ProxyEntry.Get());
					}
					else
					{
						EntriesToShow.Add(VariableEntry);
					}
				}
			}
			else if(const FVariablesOutlinerDataInterfaceItem* DataInterfaceItem = Item->CastTo<FVariablesOutlinerDataInterfaceItem>())
			{
				if(UAnimNextDataInterfaceEntry* DataInterfaceEntry = DataInterfaceItem->WeakEntry.Get())
				{
					EntriesToShow.Add(DataInterfaceEntry);
				}
			}
		}

		WorkspaceEditor->SetDetailsObjects(EntriesToShow);
	}
}

void FVariablesOutlinerMode::OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection)
{
	HandleItemSelection(Selection);

	if (TSharedPtr<UE::Workspace::IWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
	{
		SharedWorkspaceEditor->SetGlobalSelection(SceneOutliner->AsShared(), UE::Workspace::FOnClearGlobalSelection::CreateRaw(this, &FVariablesOutlinerMode::ResetOutlinerSelection));
	}
}

bool FVariablesOutlinerMode::CanRenameItem(const ISceneOutlinerTreeItem& Item) const
{
	if (const FVariablesOutlinerEntryItem* EntryItem = Item.CastTo<FVariablesOutlinerEntryItem>())
	{
		return true;
	}
	else if (const FVariablesOutlinerAssetItem* AssetItem = Item.CastTo<FVariablesOutlinerAssetItem>())
	{
		return true;
	}
	return false;
}

void FVariablesOutlinerMode::BindCommands(const TSharedRef<FUICommandList>& OutCommandList)
{
	CommandList->MapAction( 
		FGenericCommands::Get().Rename, 
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::Rename),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanRename)
	);

	CommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::Delete),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanDelete));
}

TUniquePtr<ISceneOutlinerHierarchy> FVariablesOutlinerMode::CreateHierarchy()
{
	return MakeUnique<FVariablesOutlinerHierarchy>(this);
}

void FVariablesOutlinerMode::ResetOutlinerSelection()
{
	SceneOutliner->ClearSelection();
}

SVariablesOutliner* FVariablesOutlinerMode::GetOutliner() const
{
	return static_cast<SVariablesOutliner*>(SceneOutliner);
}

void FVariablesOutlinerMode::Rename()
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	if (Selection.Num() == 1)
	{
		FSceneOutlinerTreeItemPtr ItemToRename = Selection.SelectedItems[0].Pin();

		if (ItemToRename.IsValid() && CanRenameItem(*ItemToRename) && ItemToRename->CanInteract())
		{
			SceneOutliner->SetPendingRenameItem(ItemToRename);
			SceneOutliner->ScrollItemIntoView(ItemToRename);
		}
	}
}

bool FVariablesOutlinerMode::CanRename() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	if (Selection.Num() == 1)
	{
		FSceneOutlinerTreeItemPtr ItemToRename = Selection.SelectedItems[0].Pin();
		return ItemToRename.IsValid() && CanRenameItem(*ItemToRename) && ItemToRename->CanInteract();
	}
	return false;
}

void FVariablesOutlinerMode::Delete()
{
	int32 NumEntries = 0;
	TMap<UAnimNextRigVMAssetEditorData*, TArray<UAnimNextRigVMAssetEntry*>> EntriesToDeletePerAsset;
	TArray<FSceneOutlinerTreeItemPtr> SelectedItems = SceneOutliner->GetSelectedItems();
	for(const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
	{
		if (const FVariablesOutlinerEntryItem* VariablesItem = Item->CastTo<FVariablesOutlinerEntryItem>())
		{
			UAnimNextVariableEntry* VariableEntry = VariablesItem->WeakEntry.Get();
			UAnimNextDataInterfaceEntry* DataInterfaceEntry = VariablesItem->WeakDataInterfaceEntry.Get();
			if(VariableEntry == nullptr || DataInterfaceEntry != nullptr)	// Cant delete variables in other data interfaces
			{
				continue;
			}

			UAnimNextRigVMAssetEditorData* EditorData = VariableEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
			if(EditorData == nullptr)
			{
				continue;
			}

			TArray<UAnimNextRigVMAssetEntry*>& EntriesToDelete = EntriesToDeletePerAsset.FindOrAdd(EditorData);
			EntriesToDelete.Add(VariableEntry);
			NumEntries++;
		}
		else if (const FVariablesOutlinerDataInterfaceItem* DataInterfaceItem = Item->CastTo<FVariablesOutlinerDataInterfaceItem>())
		{
			UAnimNextDataInterfaceEntry* DataInterfaceEntry = DataInterfaceItem->WeakEntry.Get();
			if(DataInterfaceEntry == nullptr)
			{
				continue;
			}

			UAnimNextRigVMAssetEditorData* EditorData = DataInterfaceEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
			if(EditorData == nullptr)
			{
				continue;
			}

			TArray<UAnimNextRigVMAssetEntry*>& EntriesToDelete = EntriesToDeletePerAsset.FindOrAdd(EditorData);
			EntriesToDelete.Add(DataInterfaceEntry);
			NumEntries++;
		}
	}

	if(NumEntries > 0)
	{
		FScopedTransaction Transaction(FText::Format(LOCTEXT("DeleteVariablesFormat", "Delete {0}|plural(one=variable, other=variables)"), NumEntries));
		for(const TPair<UAnimNextRigVMAssetEditorData*, TArray<UAnimNextRigVMAssetEntry*>>& EntriesPair : EntriesToDeletePerAsset)
		{
			EntriesPair.Key->RemoveEntries(EntriesPair.Value);
		}
	}
}

bool FVariablesOutlinerMode::CanDelete() const
{
	return true;
}

TSharedPtr<FDragDropOperation> FVariablesOutlinerMode::CreateDragDropOperation(const FPointerEvent& MouseEvent, const TArray<FSceneOutlinerTreeItemPtr>& InTreeItems) const
{
	if(InTreeItems.Num() == 0)
	{
		return nullptr;
	}

	FVariablesOutlinerEntryItem* VariableItem = InTreeItems[0]->CastTo<FVariablesOutlinerEntryItem>();
	if(VariableItem == nullptr)
	{
		return nullptr;
	}

	UAnimNextVariableEntry* Entry = Cast<UAnimNextVariableEntry>(VariableItem->WeakEntry.Get());
	if(Entry == nullptr)
	{
		return nullptr;
	}

	URigVMHost* RigVMHost = Entry->GetTypedOuter<URigVMHost>();
	if(RigVMHost == nullptr)
	{
		return nullptr;
	}

	FName VariableName = Entry->GetVariableName();
	TArray<FRigVMExternalVariable> ExternalVariables = RigVMHost->GetVM()->GetExternalVariables(RigVMHost->GetRigVMExtendedExecuteContext());
	for (const FRigVMExternalVariable& ExternalVariable : ExternalVariables)
	{
		if(ExternalVariable.Name == VariableName)
		{
			const bool bIsGetter = !(MouseEvent.IsShiftDown() || MouseEvent.IsAltDown());
			TSharedPtr<FAnimNextSchemaAction_Variable> Action = MakeShared<FAnimNextSchemaAction_Variable>(ExternalVariable, bIsGetter);
			return FVariableDragDropOp::New(Action);
		}
	}

	return nullptr;
}

}

#undef LOCTEXT_NAMESPACE // "FVariablesOutlinerMode"