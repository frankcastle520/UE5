// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceControlViewportMenu.h"
#include "RevisionControlStyle/RevisionControlStyle.h"
#include "Widgets/Input/SSpinBox.h"
#include "EngineAnalytics.h"
#include "LevelEditorMenuContext.h"
#include "LevelEditorViewport.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "SourceControlViewportMenu"

static const FName MenuName("LevelEditor.LevelViewportToolbar.Show");
static const FName SectionName("LevelViewportEditorShow");
static const FName SubMenuName("ShowRevisionControlMenu");

FSourceControlViewportMenu::FSourceControlViewportMenu()
{
}

FSourceControlViewportMenu::~FSourceControlViewportMenu()
{
	RemoveViewportMenu();
}

void FSourceControlViewportMenu::Init()
{
}

void FSourceControlViewportMenu::SetEnabled(bool bInEnabled)
{
	if (bInEnabled)
	{
		InsertViewportMenu();
	}
	else
	{
		RemoveViewportMenu();
	}
}

void FSourceControlViewportMenu::InsertViewportMenu()
{
	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		UToolMenu* Menu = ToolMenus->ExtendMenu(MenuName);
		if (Menu != nullptr)
		{
			Menu->AddDynamicSection(SectionName,
				FNewToolMenuDelegate::CreateSP(this, &FSourceControlViewportMenu::PopulateViewportMenu)
			);
		}
	}
}

void FSourceControlViewportMenu::PopulateViewportMenu(UToolMenu* InMenu)
{
	check(InMenu);

	InMenu->AddDynamicSection(SectionName, FNewToolMenuDelegate::CreateLambda(
		[this](UToolMenu* InMenu)
		{
			ULevelViewportToolBarContext* Context = InMenu->FindContext<ULevelViewportToolBarContext>();
			if (!Context)
			{
				return;
			}

			FLevelEditorViewportClient* ViewportClient = Context->GetLevelViewportClient();
			if (!ViewportClient || !ViewportClient->IsPerspective())
			{
				return;
			}

			OpacityWidget = SNew(SSpinBox<uint8>)
				.ClearKeyboardFocusOnCommit(true)
				.OnValueChanged_Lambda(
					[this, ViewportClient](uint8 InNewValue)
					{
						SetOpacityValue(ViewportClient, InNewValue);
					}
				)
				.OnValueCommitted_Lambda(
					[this, ViewportClient](uint8 InNewValue, ETextCommit::Type InCommitType)
					{
						SetOpacityValue(ViewportClient, InNewValue);
					}
				)
				.Value_Lambda(
					[this, ViewportClient]()
					{
						return GetOpacityValue(ViewportClient);
					}
				)
				.MinValue(0)
				.MinSliderValue(0)
				.MaxValue(100)
				.MaxSliderValue(100);

			FToolMenuSection& RevisionControlSection = InMenu->FindOrAddSection(TEXT("LevelViewportEditorShow"));
			RevisionControlSection.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda(
				[this, ViewportClient](FToolMenuSection& InSection)
				{
					InSection.AddSubMenu(
						SubMenuName,
						LOCTEXT("RevisionControlSubMenu", "Revision Control"),
						LOCTEXT("RevisionControlSubMenu_ToolTip", "Toggle revision control viewport options on or off."),
						FNewToolMenuDelegate::CreateLambda(
							[this, ViewportClient](UToolMenu* InSubMenu)
							{
								FToolMenuSection& DefaultSection = InSubMenu->AddSection(NAME_None, LOCTEXT("RevisionControlSectionStatus", "Status Highlights"));

								DefaultSection.AddMenuEntry(
									NAME_None,
									LOCTEXT("ShowAll", "Show All"),
									LOCTEXT("ShowAll_ToolTip", "Enable highlighting for all statuses"),
									FSlateIcon(),
									FUIAction(
										FExecuteAction::CreateSP(this, &FSourceControlViewportMenu::ShowAll, ViewportClient)
									),
									EUserInterfaceActionType::Button
								);

								DefaultSection.AddMenuEntry(
									NAME_None,
									LOCTEXT("HideAll", "Hide All"),
									LOCTEXT("HideAll_ToolTip", "Disable highlighting for all statuses"),
									FSlateIcon(),
									FUIAction(
										FExecuteAction::CreateSP(this, &FSourceControlViewportMenu::HideAll, ViewportClient)
									),
									EUserInterfaceActionType::Button
								);

								DefaultSection.AddSeparator(NAME_None);

								DefaultSection.AddMenuEntry(
									NAME_None,
									LOCTEXT("HighlightCheckedOutByOtherUser", "Checked Out by Others"),
									LOCTEXT("HighlightCheckedOutByOtherUser_ToolTip", "Highlight objects that are checked out by someone else."),
									FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.ShowMenu.CheckedOutByOtherUser"),
									FUIAction(
										FExecuteAction::CreateSP(this, &FSourceControlViewportMenu::ToggleHighlight, ViewportClient, ESourceControlStatus::CheckedOutByOtherUser),
										FCanExecuteAction(),
										FIsActionChecked::CreateSP(this, &FSourceControlViewportMenu::IsHighlighted, ViewportClient, ESourceControlStatus::CheckedOutByOtherUser)
									),
									EUserInterfaceActionType::ToggleButton
								);

								DefaultSection.AddMenuEntry(
									NAME_None,
									LOCTEXT("HighlightNotAtHeadRevision", "Out of Date"),
									LOCTEXT("HighlightNotAtHeadRevision_ToolTip", "Highlight objects that are not at the latest revision."),
									FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.ShowMenu.NotAtHeadRevision"),
									FUIAction(
										FExecuteAction::CreateSP(this, &FSourceControlViewportMenu::ToggleHighlight, ViewportClient, ESourceControlStatus::NotAtHeadRevision),
										FCanExecuteAction(),
										FIsActionChecked::CreateSP(this, &FSourceControlViewportMenu::IsHighlighted, ViewportClient, ESourceControlStatus::NotAtHeadRevision)
									),
									EUserInterfaceActionType::ToggleButton
								);

								DefaultSection.AddMenuEntry(
									NAME_None,
									LOCTEXT("HighlightCheckedOut", "Checked Out by Me"),
									LOCTEXT("HighlightCheckedOut_ToolTip", "Highlight objects that are checked out by me."),
									FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.ShowMenu.CheckedOut"),
									FUIAction(
										FExecuteAction::CreateSP(this, &FSourceControlViewportMenu::ToggleHighlight, ViewportClient, ESourceControlStatus::CheckedOut),
										FCanExecuteAction(),
										FIsActionChecked::CreateSP(this, &FSourceControlViewportMenu::IsHighlighted, ViewportClient, ESourceControlStatus::CheckedOut)
									),
									EUserInterfaceActionType::ToggleButton
								);

								DefaultSection.AddMenuEntry(
									NAME_None,
									LOCTEXT("HighlightOpenForAdd", "Newly Added"),
									LOCTEXT("HighlightOpenForAdd_ToolTip", "Highlight objects that have been added by me."),
									FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.ShowMenu.OpenForAdd"),
									FUIAction(
										FExecuteAction::CreateSP(this, &FSourceControlViewportMenu::ToggleHighlight, ViewportClient, ESourceControlStatus::OpenForAdd),
										FCanExecuteAction(),
										FIsActionChecked::CreateSP(this, &FSourceControlViewportMenu::IsHighlighted, ViewportClient, ESourceControlStatus::OpenForAdd)
									),
									EUserInterfaceActionType::ToggleButton
								);

								DefaultSection.AddEntry(FToolMenuEntry::InitWidget(
									NAME_None,
									OpacityWidget.ToSharedRef(),
									LOCTEXT("Opacity", "Opacity")
									)
								);
							}
						),
						false,
						FSlateIcon(FAppStyle::Get().GetStyleSetName(), "ShowFlagsMenu.SubMenu.RevisionControl")
					);
				}));
		}
	)
	);
}

void FSourceControlViewportMenu::RemoveViewportMenu()
{
	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		UToolMenu* Menu = ToolMenus->ExtendMenu(MenuName);
		if (Menu != nullptr)
		{
			Menu->RemoveSection(SectionName);
		}
	}
}

void FSourceControlViewportMenu::ShowAll(FLevelEditorViewportClient* ViewportClient)
{
	ensure(ViewportClient);

	SourceControlViewportUtils::SetFeedbackEnabled(ViewportClient, ESourceControlStatus::CheckedOutByOtherUser, /*bEnabled=*/true);
	SourceControlViewportUtils::SetFeedbackEnabled(ViewportClient, ESourceControlStatus::NotAtHeadRevision, /*bEnabled=*/true);
	SourceControlViewportUtils::SetFeedbackEnabled(ViewportClient, ESourceControlStatus::CheckedOut, /*bEnabled=*/true);
	SourceControlViewportUtils::SetFeedbackEnabled(ViewportClient, ESourceControlStatus::OpenForAdd, /*bEnabled=*/true);
	
	RecordToggleEvent(TEXT("All"), /*bEnabled=*/true);
}

void FSourceControlViewportMenu::HideAll(FLevelEditorViewportClient* ViewportClient)
{
	ensure(ViewportClient);

	SourceControlViewportUtils::SetFeedbackEnabled(ViewportClient, ESourceControlStatus::CheckedOutByOtherUser, /*bEnabled=*/false);
	SourceControlViewportUtils::SetFeedbackEnabled(ViewportClient, ESourceControlStatus::NotAtHeadRevision, /*bEnabled=*/false);
	SourceControlViewportUtils::SetFeedbackEnabled(ViewportClient, ESourceControlStatus::CheckedOut, /*bEnabled=*/false);
	SourceControlViewportUtils::SetFeedbackEnabled(ViewportClient, ESourceControlStatus::OpenForAdd, /*bEnabled=*/false);

	RecordToggleEvent(TEXT("All"), /*bEnabled=*/false);
}

void FSourceControlViewportMenu::ToggleHighlight(FLevelEditorViewportClient* ViewportClient, ESourceControlStatus Status)
{
	ensure(ViewportClient);

	bool bOld = SourceControlViewportUtils::GetFeedbackEnabled(ViewportClient, Status);
	bool bNew = bOld ? false : true;
	SourceControlViewportUtils::SetFeedbackEnabled(ViewportClient, Status, bNew);

	FString EnumValueWithoutType = UEnum::GetValueAsString(Status)
		.Replace(TEXT("ESourceControlStatus::"), TEXT(""));
	RecordToggleEvent(EnumValueWithoutType, bNew);
}

bool FSourceControlViewportMenu::IsHighlighted(FLevelEditorViewportClient* ViewportClient, ESourceControlStatus Status) const
{
	ensure(ViewportClient);

	return SourceControlViewportUtils::GetFeedbackEnabled(ViewportClient, Status);
}

void FSourceControlViewportMenu::SetOpacityValue(FLevelEditorViewportClient* ViewportClient, uint8 InNewValue)
{
	SourceControlViewportUtils::SetFeedbackOpacity(ViewportClient, InNewValue);
}

uint8 FSourceControlViewportMenu::GetOpacityValue(FLevelEditorViewportClient* ViewportClient) const
{
	return SourceControlViewportUtils::GetFeedbackOpacity(ViewportClient);
}

void FSourceControlViewportMenu::RecordToggleEvent(const FString& Param, bool bEnabled) const
{
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(
			TEXT("Editor.Usage.SourceControl.Settings"), Param, bEnabled ? TEXT("True") : TEXT("False")
		);
	}
}

#undef LOCTEXT_NAMESPACE