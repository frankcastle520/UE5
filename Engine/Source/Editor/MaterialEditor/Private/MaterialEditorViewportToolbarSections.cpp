// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialEditorViewportToolbarSections.h"
#include "EditorViewportCommands.h"
#include "MaterialEditorActions.h"
#include "SEditorViewport.h"
#include "SMaterialEditorViewport.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "ViewportToolbar/UnrealEdViewportToolbarContext.h"

#define LOCTEXT_NAMESPACE "MaterialEditorViewportToolbarSections"

TSharedRef<SWidget> UE::MaterialEditor::CreateShowMenuWidget(const TSharedRef<SMaterialEditor3DPreviewViewport>& InMaterialEditorViewport, bool bInShowViewportStatsToggle
)
{
	InMaterialEditorViewport->OnFloatingButtonClicked();

	// We generate a menu via UToolMenus, so we can use FillShowSubmenu call from both old and new toolbar
	FName OldShowMenuName = "MaterialEditor.OldViewportToolbar.Show";

	if (!UToolMenus::Get()->IsMenuRegistered(OldShowMenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(OldShowMenuName, NAME_None, EMultiBoxType::Menu, false);
		Menu->AddDynamicSection(
			"BaseSection",
			FNewToolMenuDelegate::CreateLambda(
				[ViewportWeak = InMaterialEditorViewport.ToWeakPtr(), bInShowViewportStatsToggle](UToolMenu* InMenu)
				{
					if (TSharedPtr<SMaterialEditor3DPreviewViewport> Viewport = ViewportWeak.Pin())
					{
						UUnrealEdViewportToolbarContext* const ContextObject = NewObject<UUnrealEdViewportToolbarContext>();
						ContextObject->Viewport = ViewportWeak;
						InMenu->Context.AddObject(ContextObject);

						UE::MaterialEditor::FillShowSubmenu(InMenu, bInShowViewportStatsToggle);
					}
				}
			)
		);
	}

	FToolMenuContext MenuContext;
	{
		MenuContext.AppendCommandList(InMaterialEditorViewport->GetCommandList());

		// Add the UnrealEd viewport toolbar context.
		{
			UUnrealEdViewportToolbarContext* const ContextObject =
				UE::UnrealEd::CreateViewportToolbarDefaultContext(InMaterialEditorViewport);

			MenuContext.AddObject(ContextObject);
		}
	}

	return UToolMenus::Get()->GenerateWidget(OldShowMenuName, MenuContext);
}

FToolMenuEntry UE::MaterialEditor::CreateShowSubmenu()
{
	return FToolMenuEntry::InitSubMenu(
		"Show",
		LOCTEXT("ShowSubmenuLabel", "Show"),
		LOCTEXT("ShowSubmenuTooltip", "Show options"),
		FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* Submenu) -> void
			{
				UE::MaterialEditor::FillShowSubmenu(Submenu);
			}
		)
	);
}

void UE::MaterialEditor::FillShowSubmenu(UToolMenu* InMenu, bool bInShowViewportStatsToggle)
{
	if (UUnrealEdViewportToolbarContext* const EditorViewportContext =
			InMenu->FindContext<UUnrealEdViewportToolbarContext>())
	{
		if (TSharedPtr<SMaterialEditor3DPreviewViewport> StaticMeshEditorViewport =
				StaticCastSharedPtr<SMaterialEditor3DPreviewViewport>(EditorViewportContext->Viewport.Pin()))
		{
			FToolMenuSection& UnnamedSection = InMenu->FindOrAddSection(NAME_None);

			if (bInShowViewportStatsToggle)
			{
				UnnamedSection.AddMenuEntry(
					FEditorViewportCommands::Get().ToggleStats, LOCTEXT("ViewportStatsLabel", "Viewport Stats")
				);

				UnnamedSection.AddSeparator(NAME_None);
			}

			UnnamedSection.AddMenuEntry(FMaterialEditorCommands::Get().ToggleMaterialStats);

			UnnamedSection.AddSeparator(NAME_None);

			UnnamedSection.AddMenuEntry(FMaterialEditorCommands::Get().TogglePreviewBackground);
		}
	}
}

#undef LOCTEXT_NAMESPACE
