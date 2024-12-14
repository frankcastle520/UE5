// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshEditorViewportToolbarSections.h"

#include "SStaticMeshEditorViewport.h"
#include "StaticMeshEditorActions.h"
#include "StaticMeshViewportLODCommands.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "ViewportToolbar/UnrealEdViewportToolbarContext.h"

#define LOCTEXT_NAMESPACE "StaticMeshEditorViewportToolbarSections"

FText UE::StaticMeshEditor::GetLODMenuLabel(const TSharedPtr<SStaticMeshEditorViewport>& InStaticMeshEditorViewport)
{
	FText Label = LOCTEXT("LODMenu_AutoLabel", "LOD Auto");

	if (InStaticMeshEditorViewport)
	{
		int32 LODSelectionType = InStaticMeshEditorViewport->GetLODSelection();

		if (LODSelectionType > 0)
		{
			FString TitleLabel = FString::Printf(TEXT("LOD %d"), LODSelectionType - 1);
			Label = FText::FromString(TitleLabel);
		}
	}

	return Label;
}

FToolMenuEntry UE::StaticMeshEditor::CreateLODSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicLODOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				if (UUnrealEdViewportToolbarContext* const EditorViewportContext =
						InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>())
				{
					TWeakPtr<SStaticMeshEditorViewport> StaticMeshEditorViewportWeak =
						StaticCastSharedPtr<SStaticMeshEditorViewport>(EditorViewportContext->Viewport.Pin());

					// Label updates based on currently selected LOD
					const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
						[StaticMeshEditorViewportWeak]()
						{
							if (TSharedPtr<SStaticMeshEditorViewport> Viewport = StaticMeshEditorViewportWeak.Pin())
							{
								return GetLODMenuLabel(Viewport);
							}

							return LOCTEXT("LODSubmenuLabel", "LOD");
						}
					);

					InDynamicSection.AddSubMenu(
						"LOD",
						Label,
						FText(),
						FNewToolMenuDelegate::CreateLambda(
							[StaticMeshEditorViewportWeak](UToolMenu* Submenu) -> void
							{
								if (TSharedPtr<SStaticMeshEditorViewport> Viewport = StaticMeshEditorViewportWeak.Pin())
								{
									FToolMenuSection& UnnamedSection = Submenu->FindOrAddSection(NAME_None, FText());
									TSharedRef<SWidget> LODMenuWidget =
										UE::StaticMeshEditor::GenerateLODMenuWidget(Viewport);
									FToolMenuEntry LODSubmenu = FToolMenuEntry::InitWidget("LOD", LODMenuWidget, FText());

									UnnamedSection.AddEntry(LODSubmenu);
								}
							}
						)
					);
				}
			}
		)
	);
}

FToolMenuEntry UE::StaticMeshEditor::CreateShowSubmenu()
{
	return FToolMenuEntry::InitSubMenu(
		"Show",
		LOCTEXT("ShowSubmenuLabel", "Show"),
		FText(),
		FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* Submenu) -> void
			{
				FillShowSubmenu(Submenu);
			}
		)
	);
}

TSharedRef<SWidget> UE::StaticMeshEditor::GenerateLODMenuWidget(const TSharedPtr<SStaticMeshEditorViewport>& InStaticMeshEditorViewport
)
{
	if (!InStaticMeshEditorViewport)
	{
		return SNullWidget::NullWidget;
	}

	const FStaticMeshViewportLODCommands& Actions = FStaticMeshViewportLODCommands::Get();

	TSharedPtr<FExtender> MenuExtender = InStaticMeshEditorViewport->GetExtenders();

	constexpr bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder InMenuBuilder(
		bInShouldCloseWindowAfterMenuSelection, InStaticMeshEditorViewport->GetCommandList(), MenuExtender
	);

	InMenuBuilder.PushCommandList(InStaticMeshEditorViewport->GetCommandList().ToSharedRef());
	if (MenuExtender.IsValid())
	{
		InMenuBuilder.PushExtender(MenuExtender.ToSharedRef());
	}

	{
		// LOD Models
		InMenuBuilder.BeginSection("StaticMeshViewportPreviewLODs", LOCTEXT("ShowLOD_PreviewLabel", "Preview LODs"));
		{
			InMenuBuilder.AddMenuEntry(Actions.LODAuto);
			InMenuBuilder.AddMenuEntry(Actions.LOD0);

			int32 LODCount = InStaticMeshEditorViewport->GetLODModelCount();
			for (int32 LODId = 1; LODId < LODCount; ++LODId)
			{
				FString TitleLabel = FString::Printf(TEXT(" LOD %d"), LODId);

				FUIAction Action(
					FExecuteAction::CreateSP(
						InStaticMeshEditorViewport.ToSharedRef(), &SStaticMeshEditorViewport::OnSetLODModel, LODId + 1
					),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(
						InStaticMeshEditorViewport.ToSharedRef(), &SStaticMeshEditorViewport::IsLODModelSelected, LODId + 1
					)
				);

				InMenuBuilder.AddMenuEntry(
					FText::FromString(TitleLabel), FText::GetEmpty(), FSlateIcon(), Action, NAME_None, EUserInterfaceActionType::RadioButton
				);
			}
		}
		InMenuBuilder.EndSection();
	}

	InMenuBuilder.PopCommandList();
	if (MenuExtender.IsValid())
	{
		InMenuBuilder.PopExtender();
	}

	return InMenuBuilder.MakeWidget();
}

TSharedRef<SWidget> UE::StaticMeshEditor::GenerateShowMenuWidget(const TSharedPtr<SStaticMeshEditorViewport>& InStaticMeshEditorViewport
)
{
	if (!InStaticMeshEditorViewport)
	{
		return SNullWidget::NullWidget;
	}

	InStaticMeshEditorViewport->OnFloatingButtonClicked();

	// We generate a menu via UToolMenus, so we can use FillShowSubmenu call from both old and new toolbar
	FName OldShowMenuName = "StaticMesh.OldViewportToolbar.Show";

	if (!UToolMenus::Get()->IsMenuRegistered(OldShowMenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(OldShowMenuName, NAME_None, EMultiBoxType::Menu, false);
		Menu->AddDynamicSection(
			"BaseSection",
			FNewToolMenuDelegate::CreateLambda(
				[](UToolMenu* InMenu)
				{
					FillShowSubmenu(InMenu);
				}
			)
		);
	}

	FToolMenuContext MenuContext;
	{
		MenuContext.AppendCommandList(InStaticMeshEditorViewport->GetCommandList());

		// Add the UnrealEd viewport toolbar context.
		{
			UUnrealEdViewportToolbarContext* const ContextObject =
				UE::UnrealEd::CreateViewportToolbarDefaultContext(InStaticMeshEditorViewport);

			MenuContext.AddObject(ContextObject);
		}
	}

	return UToolMenus::Get()->GenerateWidget(OldShowMenuName, MenuContext);
}

void UE::StaticMeshEditor::FillShowSubmenu(UToolMenu* InMenu)
{
	if (UUnrealEdViewportToolbarContext* const EditorViewportContext =
			InMenu->FindContext<UUnrealEdViewportToolbarContext>())
	{
		if (TSharedPtr<SStaticMeshEditorViewport> StaticMeshEditorViewport =
				StaticCastSharedPtr<SStaticMeshEditorViewport>(EditorViewportContext->Viewport.Pin()))
		{
			FToolMenuSection& UnnamedSection = InMenu->FindOrAddSection(NAME_None);
			const FStaticMeshEditorCommands& Commands = FStaticMeshEditorCommands::Get();

			UnnamedSection.AddMenuEntry(Commands.SetShowNaniteFallback);
			UnnamedSection.AddMenuEntry(Commands.SetShowDistanceField);

			FToolMenuSection& MeshComponentsSection =
				InMenu->FindOrAddSection("MeshComponents", LOCTEXT("MeshComponments", "Mesh Components"));

			MeshComponentsSection.AddMenuEntry(Commands.SetShowSockets);
			MeshComponentsSection.AddMenuEntry(Commands.SetShowVertices);
			MeshComponentsSection.AddMenuEntry(Commands.SetShowVertexColor);
			MeshComponentsSection.AddMenuEntry(Commands.SetShowNormals);
			MeshComponentsSection.AddMenuEntry(Commands.SetShowTangents);
			MeshComponentsSection.AddMenuEntry(Commands.SetShowBinormals);

			MeshComponentsSection.AddSeparator(NAME_None);

			MeshComponentsSection.AddMenuEntry(Commands.SetShowPivot);
			MeshComponentsSection.AddMenuEntry(Commands.SetShowGrid);
			MeshComponentsSection.AddMenuEntry(Commands.SetShowBounds);
			MeshComponentsSection.AddMenuEntry(Commands.SetShowSimpleCollision);
			MeshComponentsSection.AddMenuEntry(Commands.SetShowComplexCollision);
			MeshComponentsSection.AddMenuEntry(Commands.SetShowPhysicalMaterialMasks);
		}
	}
}

#undef LOCTEXT_NAMESPACE
