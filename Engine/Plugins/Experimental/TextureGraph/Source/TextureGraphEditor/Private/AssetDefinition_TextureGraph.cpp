// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_TextureGraph.h"
#include "TextureGraphEditorModule.h"
#include "TextureGraph.h"

#include "AssetRegistry/AssetIdentifier.h"
#include "ContentBrowserMenuContexts.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "IAssetTools.h"
#include "ToolMenuSection.h"
#include "AssetRegistry/AssetRegistryHelpers.h"

#define LOCTEXT_NAMESPACE "UAssetDefinition_TextureGraph"

EAssetCommandResult UAssetDefinition_TextureGraph::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UTextureGraph* TextureGraph : OpenArgs.LoadObjects<UTextureGraph>())
	{
		FTextureGraphEditorModule* TextureEditorModule = &FModuleManager::LoadModuleChecked<FTextureGraphEditorModule>("TextureGraphEditor");
		TextureEditorModule->CreateTextureGraphEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, TextureGraph);
	}
	
	return EAssetCommandResult::Handled;
}

FAssetOpenSupport UAssetDefinition_TextureGraph::GetAssetOpenSupport(const FAssetOpenSupportArgs& OpenSupportArgs) const
{
	return FAssetOpenSupport(OpenSupportArgs.OpenMethod,OpenSupportArgs.OpenMethod == EAssetOpenMethod::Edit || OpenSupportArgs.OpenMethod == EAssetOpenMethod::View); 
}

// Menu Extensions
//--------------------------------------------------------------------

namespace MenuExtension_TextureGraph
{

	static void ExecuteExportTextureGraph(const FToolMenuContext& InContext)
	{
		if (const UContentBrowserAssetContextMenuContext* CBContext = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext))
		{
			const TArray<UTextureGraph*> &TextureGraphs = CBContext->LoadSelectedObjects<UTextureGraph>();
			const FTextureGraphEditorModule& TextureGraphEditorModule = FModuleManager::Get().LoadModuleChecked<FTextureGraphEditorModule>(TEXT("TextureGraphEditor"));
			
			for (auto TextureGraphIt = TextureGraphs.CreateConstIterator(); TextureGraphIt; ++TextureGraphIt)
			{
				TextureGraphEditorModule.GetTextureExporter()->SetTextureGraphToExport(*TextureGraphIt);
			}
			
		}
	}

	static FDelayedAutoRegisterHelper DelayedAutoRegister(EDelayedRegisterRunPhase::EndOfEngineInit, []{ 
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			FToolMenuOwnerScoped OwnerScoped(UE_MODULE_NAME);

			{
				UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UTextureGraph::StaticClass());
		
				FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
				Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
				{
					if (const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InSection))
					{
						{
							const TAttribute<FText> Label = LOCTEXT("TextureGraph_Export", "Export Texture Graph");
							const TAttribute<FText> ToolTip = LOCTEXT("Texture_ExportTextureGraphTooltip", "Allows Exporting Texture Graph with parameter changes.");
							const FSlateIcon Icon = FSlateIcon();
							const FToolMenuExecuteAction UIAction = FToolMenuExecuteAction::CreateStatic(&ExecuteExportTextureGraph);
							InSection.AddMenuEntry("Texture_Export", Label, ToolTip, Icon, UIAction);
						}
					}
				}));
			}

			
		}));
	});
}

#undef LOCTEXT_NAMESPACE
