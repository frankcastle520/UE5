// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemEditorViewportToolbarSections.h"

#include "EditorViewportCommands.h"
#include "NiagaraEditorCommands.h"
#include "SNiagaraSystemViewport.h"
#include "ToolMenu.h"
#include "ViewportToolbar/UnrealEdViewportToolbarContext.h"
#include "Widgets/Input/SSpinBox.h"

#define LOCTEXT_NAMESPACE "NiagaraSystemEditorViewportToolbarSections"

TSharedRef<SWidget> UE::NiagaraSystemEditor::CreateShowMenuWidget(
	const TSharedRef<SNiagaraSystemViewport>& InNiagaraSystemEditorViewport, bool bInShowViewportStatsToggle
)
{
	InNiagaraSystemEditorViewport->OnFloatingButtonClicked();
	TSharedRef<SEditorViewport> ViewportRef = InNiagaraSystemEditorViewport->GetViewportWidget();

	constexpr bool bInShouldCloseWindowAfterMenuSelection = true;

	// TODO move to UToolMenu*
	FMenuBuilder ShowMenuBuilder(bInShouldCloseWindowAfterMenuSelection, ViewportRef->GetCommandList());
	{
		FNiagaraEditorCommands Commands = FNiagaraEditorCommands::Get();

		if (bInShowViewportStatsToggle)
		{
			ShowMenuBuilder.AddSubMenu(
				LOCTEXT("ViewportStatsSubMenu", "Viewport Stats"),
				LOCTEXT("ViewportStatsSubMenu_ToolTip", "Viewport Stats settings"),
				FNewMenuDelegate::CreateLambda(
					[](FMenuBuilder& InMenuBuilder)
					{
						InMenuBuilder.BeginSection("CommonStats", LOCTEXT("CommonStatsLabel", "Common Stats"));

						InMenuBuilder.AddMenuEntry(
							FEditorViewportCommands::Get().ToggleStats,
							"ViewportStats",
							LOCTEXT("ViewportStatsLabel", "Show Stats")
						);

						InMenuBuilder.AddMenuEntry(
							FEditorViewportCommands::Get().ToggleFPS, "ViewportFPS", LOCTEXT("ViewportFPSLabel", "Show FPS")
						);

						InMenuBuilder.AddMenuSeparator();
						InMenuBuilder.EndSection();
					}
				),
				false,
				FSlateIcon()
			);
		}

		ShowMenuBuilder.BeginSection("CommonShowFlags", LOCTEXT("CommonShowFlagsLabel", "Common Show Flags"));

		ShowMenuBuilder.AddMenuEntry(Commands.TogglePreviewGrid);
		ShowMenuBuilder.AddMenuSeparator();
		ShowMenuBuilder.AddMenuEntry(Commands.ToggleEmitterExecutionOrder);
		ShowMenuBuilder.AddMenuEntry(Commands.ToggleGpuTickInformation);
		ShowMenuBuilder.AddMenuEntry(Commands.ToggleInstructionCounts);
		ShowMenuBuilder.AddMenuEntry(Commands.ToggleMemoryInfo);
		ShowMenuBuilder.AddMenuEntry(Commands.ToggleParticleCounts);
		ShowMenuBuilder.AddMenuEntry(Commands.ToggleStatelessInfo);
	}

	return ShowMenuBuilder.MakeWidget();
}

FToolMenuEntry UE::NiagaraSystemEditor::CreateShowSubmenu()
{
	return FToolMenuEntry::InitSubMenu(
		"Show",
		LOCTEXT("ShowSubmenuLabel", "Show"),
		LOCTEXT("ShowSubmenuTooltip", "Show options"),
		FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* Submenu) -> void
			{
				UUnrealEdViewportToolbarContext* ViewportToolbarContext =
					Submenu->FindContext<UUnrealEdViewportToolbarContext>();
				if (!ViewportToolbarContext)
				{
					return;
				}

				TSharedPtr<SNiagaraSystemViewport> NiagaraSystemViewport =
					StaticCastSharedPtr<SNiagaraSystemViewport>(ViewportToolbarContext->Viewport.Pin());
				if (!NiagaraSystemViewport)
				{
					return;
				}

				FToolMenuSection& UnnamedSection = Submenu->FindOrAddSection(NAME_None);

				UnnamedSection.AddEntry(FToolMenuEntry::InitWidget(
					"ShowMenuItems",
					UE::NiagaraSystemEditor::CreateShowMenuWidget(NiagaraSystemViewport.ToSharedRef(), true),
					FText(),
					true
				));
			}
		)
	);
}

FToolMenuEntry UE::NiagaraSystemEditor::CreateSettingsSubmenu()
{
	return FToolMenuEntry::InitSubMenu(
		"Settings",
		LOCTEXT("SettingsSubmenuLabel", "Settings"),
		LOCTEXT("SettingsSubmenuTooltip", "Settings options"),
		FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* Submenu) -> void
			{
				FToolMenuSection& ViewportControlsSection =
					Submenu->FindOrAddSection("ViewportControls", LOCTEXT("ViewportControlsLabel", "Viewport Controls"));

				ViewportControlsSection.AddSubMenu(
					"MotionOptions",
					LOCTEXT("MotionOptionsSubMenu", "Motion Options"),
					LOCTEXT("MotionOptionsSubMenu_ToolTip", "Set Motion Options for the Niagara Component"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* InMenu)
						{
							UUnrealEdViewportToolbarContext* ViewportToolbarContext =
								InMenu->FindContext<UUnrealEdViewportToolbarContext>();
							if (!ViewportToolbarContext)
							{
								return;
							}

							TSharedPtr<SNiagaraSystemViewport> NiagaraSystemViewport =
								StaticCastSharedPtr<SNiagaraSystemViewport>(ViewportToolbarContext->Viewport.Pin());
							if (!NiagaraSystemViewport)
							{
								return;
							}

							InMenu->AddMenuEntry(
								"MotionOptions",
								FToolMenuEntry::InitWidget(
									"MotionOptions",
									UE::NiagaraSystemEditor::CreateMotionMenuWidget(NiagaraSystemViewport.ToSharedRef()),
									FText(),
									true
								)
							);
						}
					),
					false,
					FSlateIcon()
				);
			}
		)
	);
}

TSharedRef<SWidget> UE::NiagaraSystemEditor::CreateMotionMenuWidget(const TSharedRef<SNiagaraSystemViewport>& InNiagaraSystemEditorViewport
)
{
	constexpr bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MotionMenuBuilder(bInShouldCloseWindowAfterMenuSelection, InNiagaraSystemEditorViewport->GetCommandList());

	MotionMenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleMotion);

	MotionMenuBuilder.AddWidget(
		SNew(SSpinBox<float>)
			.IsEnabled(InNiagaraSystemEditorViewport, &SNiagaraSystemViewport::IsMotionEnabled)
			.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
			.MinSliderValue(0.0f)
			.MaxSliderValue(360.0f)
			.Value(InNiagaraSystemEditorViewport, &SNiagaraSystemViewport::GetMotionRate)
			.OnValueChanged(InNiagaraSystemEditorViewport, &SNiagaraSystemViewport::SetMotionRate),
		LOCTEXT("MotionSpeed", "Motion Speed")
	);

	MotionMenuBuilder.AddWidget(
		SNew(SSpinBox<float>)
			.IsEnabled(InNiagaraSystemEditorViewport, &SNiagaraSystemViewport::IsMotionEnabled)
			.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
			.MinSliderValue(0.0f)
			.MaxSliderValue(1000.0f)
			.Value(InNiagaraSystemEditorViewport, &SNiagaraSystemViewport::GetMotionRadius)
			.OnValueChanged(InNiagaraSystemEditorViewport, &SNiagaraSystemViewport::SetMotionRadius),
		LOCTEXT("MotionRadius", "Motion Radius")
	);

	return MotionMenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
