// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextAssetItemDetails.h"

#include "ClassIconFinder.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "Entries/AnimNextAnimationGraphEntry.h"
#include "Entries/AnimNextEventGraphEntry.h"
#include "InstancedStruct.h"
#include "AnimNextAssetWorkspaceAssetUserData.h"
#include "RigVMModel/RigVMGraph.h"
#include "WorkspaceItemMenuContext.h"
#include "IWorkspaceEditor.h"
#include "ScopedTransaction.h"
#include "RigVMModel/RigVMClient.h"
#include "ToolMenus.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Framework/Commands/GenericCommands.h"
#include "Module/AnimNextModule_EditorData.h"
#include "Module/RigUnit_AnimNextModuleEvents.h"
#include "UObject/UObjectIterator.h"
#include "Variables/SAddVariablesDialog.h"

#define LOCTEXT_NAMESPACE "FAnimNextGraphItemDetails"

namespace UE::AnimNext::Editor
{

const FSlateBrush* FAnimNextAssetItemDetails::GetItemIcon(const FWorkspaceOutlinerItemExport& Export) const
{
	return FAppStyle::GetBrush(TEXT("LevelEditor.Tabs.Outliner"));
}

void FAnimNextAssetItemDetails::RegisterToolMenuExtensions()
{
	FToolMenuOwnerScoped OwnerScoped(TEXT("FAnimNextModuleItemDetails"));
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("WorkspaceOutliner.ItemContextMenu");
	if (Menu == nullptr)
	{
		return;
	}

	Menu->AddDynamicSection(TEXT("AnimNextModuleItem"), FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
	{
		UWorkspaceItemMenuContext* WorkspaceItemContext = InMenu->FindContext<UWorkspaceItemMenuContext>();
		const UAssetEditorToolkitMenuContext* AssetEditorContext = InMenu->FindContext<UAssetEditorToolkitMenuContext>();
		if (WorkspaceItemContext == nullptr || AssetEditorContext == nullptr)
		{
			return;
		}

		const TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = StaticCastSharedPtr<UE::Workspace::IWorkspaceEditor>(AssetEditorContext->Toolkit.Pin());
		if(!WorkspaceEditor.IsValid())
		{
			return;
		}

		if (WorkspaceItemContext->SelectedExports.Num() != 1)
		{
			return;
		}

		TInstancedStruct<FWorkspaceOutlinerItemData>& Data = WorkspaceItemContext->SelectedExports[0].GetData();
		if (!Data.IsValid() || !Data.GetScriptStruct()->IsChildOf(FAnimNextRigVMAssetOutlinerData::StaticStruct()))
		{
			return;
		}

		const FAnimNextRigVMAssetOutlinerData& OutlinerData = Data.Get<FAnimNextRigVMAssetOutlinerData>();
		UAnimNextRigVMAsset* Asset = OutlinerData.Asset;
		if(Asset == nullptr)
		{
			return;
		}

		UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
		if(EditorData == nullptr)
		{
			return;
		}

		FToolMenuSection& AssetSection = InMenu->AddSection("AnimNextAsset", LOCTEXT("AnimNextAssetSectionLabel", "AnimNext Asset"));

		// Per sub-item type addition
		TConstArrayView<TSubclassOf<UAnimNextRigVMAssetEntry>> AllEntryClasses = EditorData->GetEntryClasses();
		for(UClass* SubEntryClass : AllEntryClasses)
		{
			static FTextFormat AddEntryLabelFormat(LOCTEXT("AddEntryLabelFormat", "Add {0}"));
			static FTextFormat AddEntryTooltipFormat(LOCTEXT("AddEntryTooltipFormat", "Adds a new {0} to this asset"));

			if(!EditorData->CanAddNewEntry(SubEntryClass))
			{
				continue;
			}

			if(SubEntryClass == UAnimNextEventGraphEntry::StaticClass())
			{
				AssetSection.AddSubMenu(
					SubEntryClass->GetFName(),
					FText::Format(AddEntryLabelFormat, SubEntryClass->GetDisplayNameText()),
					FText::Format(AddEntryTooltipFormat, SubEntryClass->GetDisplayNameText()),
					FNewToolMenuDelegate::CreateLambda([SubEntryClass, WorkspaceItemContext, EditorData, Asset](UToolMenu* InToolMenu)
					{
						FToolMenuSection& Section = InToolMenu->AddSection(SubEntryClass->GetFName());

						static FTextFormat AddEventTooltipFormat(LOCTEXT("AddEventGraphTooltipFormat", "Adds a {0} event graph to this asset"));
						
						for(TObjectIterator<UScriptStruct> It; It; ++It)
						{
							UScriptStruct* Struct = *It;
							if(!Struct->IsChildOf(FRigUnit_AnimNextModuleEventBase::StaticStruct()) || Struct == FRigUnit_AnimNextModuleEventBase::StaticStruct())
							{
								continue;
							}
							if(Struct->HasMetaData(FRigVMStruct::HiddenMetaName) || Struct->HasMetaData(FRigVMStruct::AbstractMetaName))
							{
								continue;
							}
							
							TInstancedStruct<FRigUnit_AnimNextModuleEventBase> StructInstance;
							StructInstance.InitializeAsScriptStruct(Struct);
							FName EventName = StructInstance.Get<FRigUnit_AnimNextModuleEventBase>().GetEventName();

							Section.AddMenuEntry(
								StructInstance.Get<FRigUnit_AnimNextModuleEventBase>().GetEventName(),
								FText::FromName(EventName),
								FText::Format(AddEventTooltipFormat, FText::FromName(EventName)),
								FSlateIconFinder::FindIconForClass(SubEntryClass, "ClassIcon.Object"),
								FUIAction(
									FExecuteAction::CreateWeakLambda(WorkspaceItemContext, [EventName, SubEntryClass, EditorData, Struct]()
									{
										FScopedTransaction Transaction(LOCTEXT("AddEventGraph", "Add Event Graph"));
										EditorData->AddEventGraph(EventName, Struct);
									}),
									FCanExecuteAction::CreateWeakLambda(WorkspaceItemContext, [Asset, EventName]()
									{
										return !Asset->GetVM()->ContainsEntry(EventName);
									})
								));
						}
					}),
					false,
					FSlateIconFinder::FindIconForClass(SubEntryClass, "ClassIcon.Object"));
			}
			else
			{
				AssetSection.AddMenuEntry(
					SubEntryClass->GetFName(),
					FText::Format(AddEntryLabelFormat, SubEntryClass->GetDisplayNameText()),
					FText::Format(AddEntryTooltipFormat, SubEntryClass->GetDisplayNameText()),
					FSlateIconFinder::FindIconForClass(SubEntryClass, "ClassIcon.Object"),
					FUIAction(
						FExecuteAction::CreateWeakLambda(WorkspaceItemContext, [&OutlinerData, SubEntryClass, EditorData]()
						{
							if(SubEntryClass == UAnimNextVariableEntry::StaticClass())
							{
								TSharedRef<SAddVariablesDialog> AddVariablesDialog =
									SNew(SAddVariablesDialog, TArray<UAnimNextRigVMAssetEditorData*>({ UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(OutlinerData.Asset.Get()) }));

								TArray<FVariableToAdd> VariablesToAdd;
								TArray<FDataInterfaceToAdd> DataInterfacesToAdd;
								if(AddVariablesDialog->ShowModal(VariablesToAdd, DataInterfacesToAdd))
								{
									FScopedTransaction Transaction(LOCTEXT("AddVariables", "Add Variable(s)"));
									for (const FVariableToAdd& VariableToAdd : VariablesToAdd)
									{
										EditorData->AddVariable(VariableToAdd.Name, VariableToAdd.Type);
									}
									for (const FDataInterfaceToAdd& DataInterfaceToAdd : DataInterfacesToAdd)
									{
										EditorData->AddDataInterface(DataInterfaceToAdd.DataInterface);
									}
								}
							}
							else if(SubEntryClass == UAnimNextDataInterfaceEntry::StaticClass())
							{
								TSharedRef<SAddVariablesDialog> AddVariablesDialog =
									SNew(SAddVariablesDialog, TArray<UAnimNextRigVMAssetEditorData*>({ UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(OutlinerData.Asset.Get()) }))
									.ShouldAddInitialVariable(false);

								TArray<FVariableToAdd> VariablesToAdd;
								TArray<FDataInterfaceToAdd> DataInterfacesToAdd;
								if(AddVariablesDialog->ShowModal(VariablesToAdd, DataInterfacesToAdd))
								{
									FScopedTransaction Transaction(LOCTEXT("AddVariables", "Add Variable(s)"));
									for (const FVariableToAdd& VariableToAdd : VariablesToAdd)
									{
										EditorData->AddVariable(VariableToAdd.Name, VariableToAdd.Type);
									}
									for (const FDataInterfaceToAdd& DataInterfaceToAdd : DataInterfacesToAdd)
									{
										EditorData->AddDataInterface(DataInterfaceToAdd.DataInterface);
									}
								}
							}
							else if(SubEntryClass == UAnimNextAnimationGraphEntry::StaticClass())
							{
								FScopedTransaction Transaction(LOCTEXT("AddAnimationGraph", "Add Animation Graph"));
								EditorData->AddAnimationGraph(TEXT("Root"));
							}
						})
					));
			}
		}
	}));
}

void FAnimNextAssetItemDetails::UnregisterToolMenuExtensions()
{
	if(UToolMenus* ToolMenus = UToolMenus::Get())
	{
		ToolMenus->UnregisterOwnerByName("FAnimNextAssetItemDetails");
	}
}

}

#undef LOCTEXT_NAMESPACE // "FAnimNextGraphItemDetails"