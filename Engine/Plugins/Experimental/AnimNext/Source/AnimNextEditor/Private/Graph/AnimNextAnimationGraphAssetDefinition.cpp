// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextAnimationGraphAssetDefinition.h"
#include "ContentBrowserMenuContexts.h"
#include "FileHelpers.h"
#include "IWorkspaceEditorModule.h"
#include "ToolMenus.h"
#include "Workspace/AnimNextWorkspaceFactory.h"

#define LOCTEXT_NAMESPACE "AnimNextAssetDefinitions"

EAssetCommandResult UAssetDefinition_AnimNextAnimationGraph::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	using namespace UE::AnimNext::Editor;
	using namespace UE::Workspace;

	for (UAnimNextAnimationGraph* Asset : OpenArgs.LoadObjects<UAnimNextAnimationGraph>())
	{
		IWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::Get().LoadModuleChecked<IWorkspaceEditorModule>("WorkspaceEditor");
		WorkspaceEditorModule.OpenWorkspaceForObject(Asset, EOpenWorkspaceMethod::Default, UAnimNextWorkspaceFactory::StaticClass());
	}

	return EAssetCommandResult::Handled;
}

namespace UE::AnimNext::Editor
{

static FDelayedAutoRegisterHelper AutoRegisterGraphMenuItems(EDelayedRegisterRunPhase::EndOfEngineInit, []
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		FToolMenuOwnerScoped OwnerScoped(UE_MODULE_NAME);
		UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UAnimNextAnimationGraph::StaticClass());
		 
		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
		Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			InSection.AddMenuEntry(
				"ForceSave",
				LOCTEXT("ForceSaveLabel", "Force Save"),
				LOCTEXT("ForceSaveTooltip", "Force the save of this item and all its subobjects."),
				FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Save"),
				FToolUIAction(FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& InContext)
				{
					if(UContentBrowserAssetContextMenuContext* AssetContext = InContext.FindContext<UContentBrowserAssetContextMenuContext>())
					{
						TArray<UPackage*> PackagesToSave;
						for (const FAssetData& AssetData : AssetContext->SelectedAssets)
						{
							if (UPackage* Package = AssetData.GetPackage())
							{
								PackagesToSave.Add(Package);
								PackagesToSave.Append(Package->GetExternalPackages());
							}
						}

						if(PackagesToSave.Num() > 0)
						{
							FEditorFileUtils::FPromptForCheckoutAndSaveParams SaveParams;
							SaveParams.bCheckDirty = false;
							SaveParams.bPromptToSave = false;
							SaveParams.bIsExplicitSave = true;

							FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, SaveParams);
						}
					}
				})));
		}));
	}));
});

}

#undef LOCTEXT_NAMESPACE