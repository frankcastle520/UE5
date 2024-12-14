// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerViewOptionsMenu.h"
#include "Filters/Menus/SequencerMenuContext.h"
#include "Filters/SequencerTrackFilterCommands.h"
#include "Sequencer.h"
#include "SequencerCommands.h"
#include "SequencerFilterBarContext.h"
#include "SSequencer.h"
#include "ToolMenu.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "SequencerViewOptionsMenu"

TSharedRef<SWidget> FSequencerViewOptionsMenu::CreateMenu(const TWeakPtr<FSequencer>& InSequencerWeak)
{
	if (!InSequencerWeak.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	const FName FilterMenuName = TEXT("Sequencer.ViewOptionsMenu");
	if (!UToolMenus::Get()->IsMenuRegistered(FilterMenuName))
	{
		UToolMenu* const Menu = UToolMenus::Get()->RegisterMenu(FilterMenuName);
		Menu->AddDynamicSection(NAME_None, FNewToolMenuDelegate::CreateLambda([this](UToolMenu* const InMenu)
			{
				if (USequencerMenuContext* const Context = InMenu->FindContext<USequencerMenuContext>())
				{
					Context->OnPopulateFilterBarMenu.ExecuteIfBound(InMenu);
				}
			}));
	}

	USequencerMenuContext* const ContextObject = NewObject<USequencerMenuContext>();
	ContextObject->Init(InSequencerWeak);
	ContextObject->OnPopulateFilterBarMenu = FOnPopulateFilterBarMenu::CreateSP(this, &FSequencerViewOptionsMenu::PopulateMenu);

	const FToolMenuContext MenuContext(InSequencerWeak.Pin()->GetFilterInterface()->GetCommandList(), nullptr, ContextObject);
	return UToolMenus::Get()->GenerateWidget(FilterMenuName, MenuContext);
}

void FSequencerViewOptionsMenu::PopulateMenu(UToolMenu* const InMenu)
{
	if (!IsValid(InMenu))
	{
		return;
	}

	USequencerMenuContext* const Context = InMenu->FindContext<USequencerMenuContext>();
	if (!IsValid(Context))
	{
		return;
	}

	WeakSequencer = Context->GetSequencer();

	UToolMenu& Menu = *InMenu;

	PopulateFiltersSection(Menu);
	PopulateSortAndOrganizeSection(Menu);
	PopulateFilterOptionsSection(Menu);
	PopulateLayoutSection(Menu);
}

void FSequencerViewOptionsMenu::PopulateFiltersSection(UToolMenu& InMenu)
{
	const FSequencerTrackFilterCommands& TrackFilterCommands = FSequencerTrackFilterCommands::Get();

	FToolMenuSection& HiddenTracksSection = InMenu.FindOrAddSection(TEXT("HiddenTracks"), LOCTEXT("HiddenTracksHeading", "Hidden Tracks"));

	HiddenTracksSection.AddMenuEntry(TrackFilterCommands.HideSelectedTracks);
	HiddenTracksSection.AddMenuEntry(TrackFilterCommands.ClearHiddenTracks);

	FToolMenuSection& IsolateTracksSection = InMenu.FindOrAddSection(TEXT("IsolatedTracks"), LOCTEXT("IsolatedTracksHeading", "Isolated Tracks"));

	IsolateTracksSection.AddMenuEntry(TrackFilterCommands.IsolateSelectedTracks);
	IsolateTracksSection.AddMenuEntry(TrackFilterCommands.ClearIsolatedTracks);

	FToolMenuSection& ShowTracksSection = InMenu.FindOrAddSection(TEXT("ShowTracks"), LOCTEXT("ShowTracksHeading", "Show Tracks"));

	ShowTracksSection.AddMenuEntry(TrackFilterCommands.ShowAllTracks);
	ShowTracksSection.AddSeparator(NAME_None);
	ShowTracksSection.AddMenuEntry(TrackFilterCommands.ShowLocationCategoryGroups);
	ShowTracksSection.AddMenuEntry(TrackFilterCommands.ShowRotationCategoryGroups);
	ShowTracksSection.AddMenuEntry(TrackFilterCommands.ShowScaleCategoryGroups);
}

void FSequencerViewOptionsMenu::PopulateSortAndOrganizeSection(UToolMenu& InMenu)
{
	const TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
	if (!Sequencer.IsValid())
	{
		return;
	}

	const TSharedPtr<FUICommandList> SequencerBindings = Sequencer->GetCommandBindings();
	const FSequencerCommands& SequencerCommands = FSequencerCommands::Get();

	FToolMenuSection& Section = InMenu.FindOrAddSection(TEXT("OrganizeAndSort"), LOCTEXT("OrganizeAndSortHeader", "Organize and Sort"));

	Section.AddMenuEntryWithCommandList(SequencerCommands.ToggleAutoExpandNodesOnSelection, SequencerBindings);
	Section.AddMenuEntryWithCommandList(SequencerCommands.ToggleExpandCollapseNodes, SequencerBindings);
	Section.AddMenuEntryWithCommandList(SequencerCommands.ToggleExpandCollapseNodesAndDescendants, SequencerBindings);
	Section.AddMenuEntryWithCommandList(SequencerCommands.ExpandAllNodes, SequencerBindings);
	Section.AddMenuEntryWithCommandList(SequencerCommands.CollapseAllNodes, SequencerBindings);
	Section.AddMenuEntryWithCommandList(SequencerCommands.SortAllNodesAndDescendants, SequencerBindings);
}

void FSequencerViewOptionsMenu::PopulateFilterOptionsSection(UToolMenu& InMenu)
{
	FToolMenuSection& OptionsSection = InMenu.FindOrAddSection(TEXT("FilterOptions"), LOCTEXT("FilterOptionsHeading", "Filter Options"));

	OptionsSection.AddMenuEntry(TEXT("FilterPinned"),
		LOCTEXT("FilterPinned", "Filter Pinned"),
		LOCTEXT("FilterPinnedToolTip", "Toggle inclusion of pinned items when filtering"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FSequencerViewOptionsMenu::ToggleIncludePinnedInFilter),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FSequencerViewOptionsMenu::IsIncludePinnedInFilter)
		),
		EUserInterfaceActionType::ToggleButton);

	OptionsSection.AddMenuEntry(TEXT("AutoExpandPassedFilterNodes"),
		LOCTEXT("AutoExpandPassedFilterNodes", "Auto Expand Filtered Items"),
		LOCTEXT("AutoExpandPassedFilterNodesToolTip", "Toggle expansion of items when a filter is passed"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FSequencerViewOptionsMenu::ToggleAutoExpandPassedFilterNodes),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FSequencerViewOptionsMenu::IsAutoExpandPassedFilterNodes)
		),
		EUserInterfaceActionType::ToggleButton);
}

void FSequencerViewOptionsMenu::PopulateLayoutSection(UToolMenu& InMenu)
{
	const FSequencerTrackFilterCommands& TrackFilterCommands = FSequencerTrackFilterCommands::Get();

	FToolMenuSection& VisibilitySection = InMenu.FindOrAddSection(TEXT("Visibility"), LOCTEXT("VisibilityHeading", "Filter Bar"));

	VisibilitySection.AddMenuEntry(TrackFilterCommands.ToggleFilterBarVisibility);

	FToolMenuSection& LayoutSection = InMenu.FindOrAddSection(TEXT("Layout"), LOCTEXT("LayoutHeading", "Filter Bar Layout"));

	LayoutSection.AddMenuEntry(
		TEXT("VerticalLayout"),
		LOCTEXT("FilterListVerticalLayout", "Vertical"),
		LOCTEXT("FilterListVerticalLayoutToolTip", "Swap to a vertical layout for the filter bar"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FSequencerViewOptionsMenu::SetFilterLayout, EFilterBarLayout::Vertical),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FSequencerViewOptionsMenu::IsFilterLayout, EFilterBarLayout::Vertical)
		),
		EUserInterfaceActionType::RadioButton);

	LayoutSection.AddMenuEntry(
		TEXT("HorizontalLayout"),
		LOCTEXT("FilterListHorizontalLayout", "Horizontal"),
		LOCTEXT("FilterListHorizontalLayoutToolTip", "Swap to a Horizontal layout for the filter bar"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FSequencerViewOptionsMenu::SetFilterLayout, EFilterBarLayout::Horizontal),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FSequencerViewOptionsMenu::IsFilterLayout, EFilterBarLayout::Horizontal)
		),
		EUserInterfaceActionType::RadioButton);
}

bool FSequencerViewOptionsMenu::IsFilterLayout(const EFilterBarLayout InLayout) const
{
	const TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
	if (!Sequencer.IsValid())
	{
		return false;
	}

	const TSharedRef<SSequencer> SequencerWidget = StaticCastSharedRef<SSequencer>(Sequencer->GetSequencerWidget());
	return InLayout == SequencerWidget->GetFilterBarLayout();
}

void FSequencerViewOptionsMenu::SetFilterLayout(const EFilterBarLayout InLayout)
{
	const TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
	if (!Sequencer.IsValid())
	{
		return;
	}

	const TSharedRef<SSequencer> SequencerWidget = StaticCastSharedRef<SSequencer>(Sequencer->GetSequencerWidget());
	SequencerWidget->SetFilterBarLayout(InLayout);
}

bool FSequencerViewOptionsMenu::IsIncludePinnedInFilter() const
{
	const TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
	if (!Sequencer.IsValid())
	{
		return false;
	}

	const USequencerSettings* const SequencerSettings = Sequencer->GetSequencerSettings();
	if (!IsValid(SequencerSettings))
	{
		return false;
	}

	return SequencerSettings->GetIncludePinnedInFilter();
}

void FSequencerViewOptionsMenu::ToggleIncludePinnedInFilter()
{
	const TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
	if (!Sequencer.IsValid())
	{
		return;
	}

	USequencerSettings* const SequencerSettings = Sequencer->GetSequencerSettings();
	if (!IsValid(SequencerSettings))
	{
		return;
	}

	SequencerSettings->SetIncludePinnedInFilter(!SequencerSettings->GetIncludePinnedInFilter());

	Sequencer->GetFilterInterface()->RequestFilterUpdate();
}

bool FSequencerViewOptionsMenu::IsAutoExpandPassedFilterNodes() const
{
	const TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
	if (!Sequencer.IsValid())
	{
		return false;
	}

	const USequencerSettings* const SequencerSettings = Sequencer->GetSequencerSettings();
	if (!IsValid(SequencerSettings))
	{
		return false;
	}

	return SequencerSettings->GetAutoExpandNodesOnFilterPass();
}

void FSequencerViewOptionsMenu::ToggleAutoExpandPassedFilterNodes()
{
	const TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
	if (!Sequencer.IsValid())
	{
		return;
	}

	USequencerSettings* const SequencerSettings = Sequencer->GetSequencerSettings();
	if (!IsValid(SequencerSettings))
	{
		return;
	}

	SequencerSettings->SetAutoExpandNodesOnFilterPass(!SequencerSettings->GetAutoExpandNodesOnFilterPass());

	Sequencer->GetFilterInterface()->RequestFilterUpdate();
}

TSharedPtr<SSequencer> FSequencerViewOptionsMenu::GetSequencerWidget() const
{
	const TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
	if (Sequencer.IsValid())
	{
		return StaticCastSharedRef<SSequencer>(Sequencer->GetSequencerWidget());
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
