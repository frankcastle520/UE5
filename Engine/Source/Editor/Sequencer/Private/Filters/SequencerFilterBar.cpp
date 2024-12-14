// Copyright Epic Games, Inc. All Rights Reserved.

#include "Filters/SequencerFilterBar.h"
#include "CurveEditor.h"
#include "Filters/Filters/SequencerTrackFilter_Keyed.h"
#include "Filters/Filters/SequencerTrackFilter_Condition.h"
#include "Filters/Filters/SequencerTrackFilter_CustomText.h"
#include "Filters/Filters/SequencerTrackFilter_Group.h"
#include "Filters/Filters/SequencerTrackFilter_HideIsolate.h"
#include "Filters/Filters/SequencerTrackFilter_Level.h"
#include "Filters/Filters/SequencerTrackFilter_Modified.h"
#include "Filters/Filters/SequencerTrackFilter_Selected.h"
#include "Filters/Filters/SequencerTrackFilter_Text.h"
#include "Filters/Filters/SequencerTrackFilter_TimeWarp.h"
#include "Filters/Filters/SequencerTrackFilter_Unbound.h"
#include "Filters/Filters/SequencerTrackFilters.h"
#include "Filters/SequencerFilterBarConfig.h"
#include "Filters/SequencerTrackFilterCollection.h"
#include "Filters/SequencerTrackFilterCommands.h"
#include "Filters/SequencerTrackFilterExtension.h"
#include "Filters/Widgets/SSequencerFilterBar.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/SToolBarButtonBlock.h"
#include "Menus/SequencerTrackFilterMenu.h"
#include "MovieScene.h"
#include "MVVM/CurveEditorExtension.h"
#include "MVVM/Selection/Selection.h"
#include "MVVM/ViewModels/CategoryModel.h"
#include "MVVM/Views/SOutlinerView.h"
#include "Sequencer.h"
#include "SequencerLog.h"
#include "SSequencer.h"
#include "Stats/StatsMisc.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SFilterBarIsolateHideShow.h"

using namespace UE::Sequencer;

#define LOCTEXT_NAMESPACE "SequencerFilterBar"

const FName FSequencerFilterBar::SharedIdentifier = TEXT("SharedSequencerTrackFilter");

int32 FSequencerFilterBar::InstanceCount = 0;

FSequencerFilterBar::FSequencerFilterBar(FSequencer& InSequencer)
	: Sequencer(InSequencer)
	, CommandList(MakeShared<FUICommandList>())
	, ClassTypeCategory(MakeShared<FFilterCategory>(LOCTEXT("ActorTypeFilterCategory", "Actor Type Filters"), FText()))
	, ComponentTypeCategory(MakeShared<FFilterCategory>(LOCTEXT("ObjectTypeFilterCategory", "Object Type Filters"), FText()))
	, MiscCategory(MakeShared<FFilterCategory>(LOCTEXT("MiscFilterCategory", "Misc Filters"), FText()))
	, TransientCategory(MakeShared<FFilterCategory>(LOCTEXT("TransientFilterCategory", "Transient Filters"), FText()))
	, CommonFilters(MakeShared<FSequencerTrackFilterCollection>(*this))
	, InternalFilters(MakeShared<FSequencerTrackFilterCollection>(*this))
	, TextFilter(MakeShared<FSequencerTrackFilter_CustomText>(*this))
	, HideIsolateFilter(MakeShared<FSequencerTrackFilter_HideIsolate>(*this))
	, LevelFilter(MakeShared<FSequencerTrackFilter_Level>(*this, TransientCategory))
	, GroupFilter(MakeShared<FSequencerTrackFilter_Group>(*this, TransientCategory))
	, SelectedFilter(MakeShared<FSequencerTrackFilter_Selected>(*this, MiscCategory))
	, ModifiedFilter(MakeShared<FSequencerTrackFilter_Modified>(*this, MiscCategory))
	, FilterMenu(MakeShared<FSequencerTrackFilterMenu>())
	, FilterData(FString())
{
	InstanceCount++;

	FSequencerTrackFilterCommands::Register();

	CommonFilters->OnChanged().AddRaw(this, &FSequencerFilterBar::RequestFilterUpdate);
	InternalFilters->OnChanged().AddRaw(this, &FSequencerFilterBar::RequestFilterUpdate);
	TextFilter->OnChanged().AddRaw(this, &FSequencerFilterBar::RequestFilterUpdate);
	LevelFilter->OnChanged().AddRaw(this, &FSequencerFilterBar::RequestFilterUpdate);
	HideIsolateFilter->OnChanged().AddRaw(this, &FSequencerFilterBar::RequestFilterUpdate);
	SelectedFilter->OnChanged().AddRaw(this, &FSequencerFilterBar::RequestFilterUpdate);

	CreateDefaultFilters();
}

FSequencerFilterBar::~FSequencerFilterBar()
{
	InstanceCount--;

	if (InstanceCount == 0)
	{
		FSequencerTrackFilterCommands::Unregister();
	}

	CommonFilters->OnChanged().RemoveAll(this);
    InternalFilters->OnChanged().RemoveAll(this);
    TextFilter->OnChanged().RemoveAll(this);
    LevelFilter->OnChanged().RemoveAll(this);
    HideIsolateFilter->OnChanged().RemoveAll(this);
    SelectedFilter->OnChanged().RemoveAll(this);

	CommonFilters.Reset();
	InternalFilters.Reset();
}

TSharedPtr<ICustomTextFilter<FSequencerTrackFilterType>> FSequencerFilterBar::CreateTextFilter()
{
	return MakeShared<FSequencerTrackFilter_CustomText>(*this);
}

void FSequencerFilterBar::CreateDefaultFilters()
{
	// Add internal filters that won't be saved to config
	InternalFilters->RemoveAll();

	InternalFilters->Add(LevelFilter);
	InternalFilters->Add(GroupFilter);

	// Add class type category filters
	CommonFilters->RemoveAll();

	CommonFilters->Add(MakeShared<FSequencerTrackFilter_Audio>(*this, ClassTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_CameraCut>(*this, ClassTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_DataLayer>(*this, ClassTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_Event>(*this, ClassTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_Fade>(*this, ClassTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_Folder>(*this, ClassTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_LevelVisibility>(*this, ClassTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_Particle>(*this, ClassTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_CinematicShot>(*this, ClassTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_Subsequence>(*this, ClassTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_TimeDilation>(*this, ClassTypeCategory));

	// Add component type category filters
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_Camera>(*this, ComponentTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_Light>(*this, ComponentTypeCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_SkeletalMesh>(*this, ComponentTypeCategory));

	// Add misc category filters
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_Keyed>(*this, MiscCategory));
	//CommonFilters->Add(ModifiedFilter); // Disabling until clear direction on what this should do
	CommonFilters->Add(SelectedFilter);
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_Unbound>(*this, MiscCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_Condition>(*this, MiscCategory));
	CommonFilters->Add(MakeShared<FSequencerTrackFilter_TimeWarp>(*this, MiscCategory));

	// Add global user-defined filters
	for (TObjectIterator<USequencerTrackFilterExtension> ExtensionIt(RF_NoFlags); ExtensionIt; ++ExtensionIt)
	{
		const USequencerTrackFilterExtension* const PotentialExtension = *ExtensionIt;
		if (IsValid(PotentialExtension)
			&& PotentialExtension->HasAnyFlags(RF_ClassDefaultObject)
			&& !PotentialExtension->GetClass()->HasAnyClassFlags(CLASS_Deprecated | CLASS_Abstract))
		{
			TArray<TSharedRef<FSequencerTrackFilter>> ExtendedTrackFilters;
			PotentialExtension->AddTrackFilterExtensions(*this, ClassTypeCategory, ExtendedTrackFilters);

			for (const TSharedRef<FSequencerTrackFilter>& ExtendedTrackFilter : ExtendedTrackFilters)
			{
				CommonFilters->Add(ExtendedTrackFilter);
			}
		}
	}

	CommonFilters->Sort();
}

void FSequencerFilterBar::BindCommands()
{
	const FSequencerTrackFilterCommands& TrackFilterCommands = FSequencerTrackFilterCommands::Get();

	const TSharedRef<SSequencer> SequencerWidget = StaticCastSharedRef<SSequencer>(GetSequencer().GetSequencerWidget());

	CommandList->MapAction(TrackFilterCommands.ToggleFilterBarVisibility,
		FUIAction(
			FExecuteAction::CreateSP(SequencerWidget, &SSequencer::ToggleFilterBarVisibility),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(SequencerWidget, &SSequencer::IsFilterBarVisible)
		));

	CommandList->MapAction(TrackFilterCommands.ResetFilters,
		FUIAction(
			FExecuteAction::CreateSP(this, &FSequencerFilterBar::ResetFilters),
			FCanExecuteAction::CreateSP(this, &FSequencerFilterBar::CanResetFilters)
		));

	CommandList->MapAction(TrackFilterCommands.ToggleMuteFilters,
		FUIAction(
			FExecuteAction::CreateSP(this, &FSequencerFilterBar::ToggleMuteFilters),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FSequencerFilterBar::AreFiltersMuted)
		));

	CommandList->MapAction(TrackFilterCommands.DisableAllFilters,
		FUIAction(
			FExecuteAction::CreateSP(this, &FSequencerFilterBar::EnableAllFilters, false, TArray<FString>()),
			FCanExecuteAction::CreateSP(this, &FSequencerFilterBar::HasAnyFilterEnabled)
		));

	CommandList->MapAction(TrackFilterCommands.ToggleActivateEnabledFilters,
		FUIAction(
			FExecuteAction::CreateSP(this, &FSequencerFilterBar::ToggleActivateAllEnabledFilters),
			FCanExecuteAction::CreateSP(this, &FSequencerFilterBar::HasAnyFilterEnabled)
		));

	// Bind all filter actions
	UMovieSceneSequence* const FocusedSequence = Sequencer.GetFocusedMovieSceneSequence();
	if (!IsValid(FocusedSequence))
	{
		return;
	}

	const TArray<TSharedRef<FSequencerTrackFilter>> AllFilters = GetFilterList(true);
	for (const TSharedRef<FSequencerTrackFilter>& Filter : AllFilters)
	{
		if (Filter->SupportsSequence(FocusedSequence))
		{
			Filter->BindCommands();
		}
	}

	// Add bindings for curve editor if supported
	FCurveEditorExtension* const CurveEditorExtension = Sequencer.GetViewModel()->CastDynamic<FCurveEditorExtension>();
	if (CurveEditorExtension)
	{
		const TSharedPtr<FCurveEditor> CurveEditor = CurveEditorExtension->GetCurveEditor();
		if (ensure(CurveEditor.IsValid()))
		{
			const TSharedPtr<FUICommandList> CurveEditorCommands = CurveEditor->GetCommands();
			if (ensure(CurveEditorCommands.IsValid()))
			{
				const TSharedPtr<FUICommandList> CurveEditorSharedBindings = Sequencer.GetCommandBindings(ESequencerCommandBindings::CurveEditor);

				// Add the general track filter commands
				for (const TSharedPtr<FUICommandInfo>& Command : TrackFilterCommands.GetAllCommands())
				{
					if (Command.IsValid() && CommandList->IsActionMapped(Command))
					{
						CurveEditorSharedBindings->MapAction(Command, *CommandList->GetActionForCommand(Command));
					}
				}

				// Add the specific track filter toggle commands
				for (const TSharedRef<FSequencerTrackFilter>& Filter : AllFilters)
				{
					if (Filter->SupportsSequence(FocusedSequence))
					{
						const TSharedPtr<FUICommandList>& FilterCommandList = Filter->GetFilterInterface().GetCommandList();
						const TSharedPtr<FUICommandInfo>& FilterCommand = Filter->GetToggleCommand();

						if (FilterCommand.IsValid() && FilterCommandList->IsActionMapped(FilterCommand))
						{
							CurveEditorSharedBindings->MapAction(FilterCommand, *FilterCommandList->GetActionForCommand(FilterCommand));
						}
					}
				}

				CurveEditorCommands->Append(CurveEditorSharedBindings.ToSharedRef());

			}
		}
	}
}

void FSequencerFilterBar::CreateCustomTextFiltersFromConfig()
{
	USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
	check(IsValid(SequencerSettings));

	CustomTextFilters.Empty();

	FSequencerFilterBarConfig& Config = SequencerSettings->FindOrAddTrackFilterBar(GetIdentifier(), false);

	for (const FCustomTextFilterData& CustomTextFilterData : Config.GetCustomTextFilters())
	{
		const TSharedRef<FSequencerTrackFilter_CustomText> NewCustomTextFilter = MakeShared<FSequencerTrackFilter_CustomText>(*this);
		NewCustomTextFilter->SetFromCustomTextFilterData(CustomTextFilterData);
		CustomTextFilters.Add(NewCustomTextFilter);
	}
}

FSequencer& FSequencerFilterBar::GetSequencer() const
{
	return Sequencer;
}

TSharedPtr<FUICommandList> FSequencerFilterBar::GetCommandList() const
{
	return CommandList;
}

FName FSequencerFilterBar::GetIdentifier() const
{
	USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
	if (IsValid(SequencerSettings))
	{
		return *SequencerSettings->GetName();
	}
	return TEXT("SequencerMain");
}

TSharedRef<SSequencerFilterBar> FSequencerFilterBar::GenerateWidget(const TSharedPtr<SFilterSearchBox>& InSearchBox, const EFilterBarLayout InLayout)
{
	return SNew(SSequencerFilterBar, SharedThis(this))
		.FilterBarLayout(InLayout)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("SequencerTrackFilters")))
		.FilterSearchBox(InSearchBox);
}

bool FSequencerFilterBar::AreFiltersMuted() const
{
	return bFiltersMuted;
}

void FSequencerFilterBar::MuteFilters(const bool bInMute)
{
	bFiltersMuted = bInMute;

	if (const TSharedPtr<SSequencerFilterBar> FilterBarWidget = GetWidget())
	{
		FilterBarWidget->SetMuted(bFiltersMuted);
	}

	RequestFilterUpdate();
}

void FSequencerFilterBar::ToggleMuteFilters()
{
	MuteFilters(!AreFiltersMuted());
}

void FSequencerFilterBar::ResetFilters()
{
	EnableAllFilters(false, {});
	EnableCustomTextFilters(false);
}

bool FSequencerFilterBar::CanResetFilters() const
{
	const TArray<TSharedRef<FFilterCategory>> Categories = { GetClassTypeCategory(), GetComponentTypeCategory(), GetMiscCategory() };
	const TArray<TSharedRef<FSequencerTrackFilter>> ClassAndCompFilters = GetCommonFilters(Categories);
	return HasEnabledFilter(ClassAndCompFilters);
}

FString FSequencerFilterBar::GetTextFilterString() const
{
	return TextFilter->GetRawFilterText().ToString();
}

void FSequencerFilterBar::SetTextFilterString(const FString& InText)
{
	TextFilter->SetRawFilterText(FText::FromString(InText));

	if (const TSharedPtr<SSequencerFilterBar> FilterBarWidget = GetWidget())
	{
		FilterBarWidget->SetTextFilterString(InText);
	}
}

bool FSequencerFilterBar::DoesTextFilterStringContainExpressionPair(const FSequencerTextFilterExpressionContext& InExpression) const
{
	return TextFilter->DoesTextFilterStringContainExpressionPair(InExpression);
}

TSharedRef<FSequencerTrackFilter_Text> FSequencerFilterBar::GetTextFilter() const
{
	return TextFilter;
}

FText FSequencerFilterBar::GetFilterErrorText() const
{
	return TextFilter->GetFilterErrorText();
}

TSet<TWeakViewModelPtr<IOutlinerExtension>> FSequencerFilterBar::GetHiddenTracks() const
{
	return HideIsolateFilter->GetHiddenTracks();
}

TSet<TWeakViewModelPtr<IOutlinerExtension>> FSequencerFilterBar::GetIsolatedTracks() const
{
	return HideIsolateFilter->GetIsolatedTracks();
}

void FSequencerFilterBar::HideTracks(const TSet<TWeakViewModelPtr<IOutlinerExtension>>& InTracks, const bool bInAddToExisting)
{
	HideIsolateFilter->HideTracks(InTracks, bInAddToExisting);
}

void FSequencerFilterBar::UnhideTracks(const TSet<TWeakViewModelPtr<IOutlinerExtension>>& InTracks)
{
	HideIsolateFilter->UnhideTracks(InTracks);
}

void FSequencerFilterBar::IsolateTracks(const TSet<TWeakViewModelPtr<IOutlinerExtension>>& InTracks, const bool bInAddToExisting)
{
	HideIsolateFilter->IsolateTracks(InTracks, bInAddToExisting);
}

void FSequencerFilterBar::UnisolateTracks(const TSet<TWeakViewModelPtr<IOutlinerExtension>>& InTracks)
{
	HideIsolateFilter->UnisolateTracks(InTracks);
}

void FSequencerFilterBar::ShowAllTracks()
{
	HideIsolateFilter->ShowAllTracks();

	if (const TSharedPtr<SSequencer> SequencerWidget = StaticCastSharedRef<SSequencer>(Sequencer.GetSequencerWidget()))
	{
		const TSharedPtr<FSequencerSelection> Selection = Sequencer.GetViewModel()->GetSelection();
		const TArray<TWeakViewModelPtr<IOutlinerExtension>> SelectedTracks = Selection->Outliner.GetSelected().Array();
		if (SelectedTracks.Num() > 0)
		{
			SequencerWidget->GetTreeView()->RequestScrollIntoView(SelectedTracks[0]);
		}
	}

	RequestFilterUpdate();
}

bool FSequencerFilterBar::HasHiddenTracks() const
{
	return HideIsolateFilter->HasHiddenTracks();
}

bool FSequencerFilterBar::HasIsolatedTracks() const
{
	return HideIsolateFilter->HasIsolatedTracks();
}

void FSequencerFilterBar::EmptyHiddenTracks()
{
	HideIsolateFilter->EmptyHiddenTracks();

	RequestFilterUpdate();
}

void FSequencerFilterBar::EmptyIsolatedTracks()
{
	HideIsolateFilter->EmptyIsolatedTracks();

	RequestFilterUpdate();
}

void FSequencerFilterBar::RequestFilterUpdate()
{
	Sequencer.GetNodeTree()->RequestFilterUpdate();
}

TSharedPtr<FSequencerTrackFilter> FSequencerFilterBar::FindFilterByDisplayName(const FString& InFilterName) const
{
	TSharedPtr<FSequencerTrackFilter> OutFilter;

	CommonFilters->ForEachFilter([&InFilterName, &OutFilter]
		(const TSharedRef<FSequencerTrackFilter>& InFilter)
		{
			const FString FilterName = InFilter->GetDisplayName().ToString();
			if (FilterName.Equals(InFilterName, ESearchCase::IgnoreCase))
			{
				OutFilter = InFilter;
				return false;
			}
			return true;
		}
		, false);

	return OutFilter;
}

TSharedPtr<FSequencerTrackFilter_CustomText> FSequencerFilterBar::FindCustomTextFilterByDisplayName(const FString& InFilterName) const
{
	TSharedPtr<FSequencerTrackFilter_CustomText> OutFilter;

	for (const TSharedRef<FSequencerTrackFilter_CustomText>& CustomTextFilter : CustomTextFilters)
	{
		const FString FilterName = CustomTextFilter->GetDisplayName().ToString();
		if (FilterName.Equals(InFilterName, ESearchCase::IgnoreCase))
		{
			OutFilter = CustomTextFilter;
			break;
		}
	}

	return OutFilter;
}

bool FSequencerFilterBar::HasAnyFiltersEnabled() const
{
	return HasEnabledCommonFilters() || HasEnabledCustomTextFilters();
}

bool FSequencerFilterBar::IsFilterActiveByDisplayName(const FString InFilterName) const
{
	if (const TSharedPtr<FSequencerTrackFilter> Filter = FindFilterByDisplayName(InFilterName))
	{
		return IsFilterActive(Filter.ToSharedRef());
	}
	return false;
}

bool FSequencerFilterBar::IsFilterEnabledByDisplayName(const FString InFilterName) const
{
	if (const TSharedPtr<FSequencerTrackFilter> Filter = FindFilterByDisplayName(InFilterName))
	{
		return IsFilterEnabled(Filter.ToSharedRef());
	}
	return false;
}

bool FSequencerFilterBar::SetFilterActiveByDisplayName(const FString InFilterName, const bool bInActive, const bool bInRequestFilterUpdate)
{
	if (const TSharedPtr<FSequencerTrackFilter> Filter = FindFilterByDisplayName(InFilterName))
	{
		return SetFilterActive(Filter.ToSharedRef(), bInActive, bInRequestFilterUpdate);
	}

	if (const TSharedPtr<FSequencerTrackFilter> Filter = FindCustomTextFilterByDisplayName(InFilterName))
	{
		return SetFilterActive(Filter.ToSharedRef(), bInActive, bInRequestFilterUpdate);
	}

	return false;
}

bool FSequencerFilterBar::SetFilterEnabledByDisplayName(const FString InFilterName, const bool bInEnabled, const bool bInRequestFilterUpdate)
{
	if (const TSharedPtr<FSequencerTrackFilter> Filter = FindFilterByDisplayName(InFilterName))
	{
		return SetFilterEnabled(Filter.ToSharedRef(), bInEnabled, bInRequestFilterUpdate);
	}

	if (const TSharedPtr<FSequencerTrackFilter> Filter = FindCustomTextFilterByDisplayName(InFilterName))
	{
		return SetFilterEnabled(Filter.ToSharedRef(), bInEnabled, bInRequestFilterUpdate);
	}

	return false;
}

bool FSequencerFilterBar::AnyCommonFilterActive() const
{
	bool bOutActiveFilter = false;

	CommonFilters->ForEachFilter([this, &bOutActiveFilter]
		(const TSharedRef<FSequencerTrackFilter>& InFilter)
		{
			if (IsFilterActive(InFilter))
			{
				bOutActiveFilter = true;
				return false;
			}
			return true;
		}
		, false);

	return bOutActiveFilter;
}

bool FSequencerFilterBar::AnyInternalFilterActive() const
{
	const bool bLevelFilterActive = LevelFilter->HasHiddenLevels();
	const bool bGroupFilterActive = GroupFilter->HasActiveGroupFilter();
	return bLevelFilterActive || bGroupFilterActive;
}

bool FSequencerFilterBar::HasAnyFilterActive(const bool bCheckTextFilter
	, const bool bInCheckHideIsolateFilter
	, const bool bInCheckCommonFilters
	, const bool bInCheckInternalFilters
	, const bool bInCheckCustomTextFilters) const
{
	if (bFiltersMuted)
	{
		return false;
	}

	const bool bTextFilterActive = bCheckTextFilter && TextFilter->IsActive();
	const bool bHideIsolateFilterActive = bInCheckHideIsolateFilter && HideIsolateFilter->IsActive();
	const bool bCommonFilterActive = bInCheckCommonFilters && AnyCommonFilterActive();
	const bool bInternalFilterActive = bInCheckInternalFilters && AnyInternalFilterActive();
	const bool bCustomTextFilterActive = bInCheckCustomTextFilters && AnyCustomTextFilterActive();

	return bTextFilterActive
		|| bHideIsolateFilterActive
		|| bCommonFilterActive
		|| bInternalFilterActive
		|| bCustomTextFilterActive;
}

bool FSequencerFilterBar::IsFilterActive(const TSharedRef<FSequencerTrackFilter> InFilter) const
{
	USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
	check(IsValid(SequencerSettings));

	const FSequencerFilterBarConfig& Config = SequencerSettings->FindOrAddTrackFilterBar(GetIdentifier(), false);

	const FString FilterName = InFilter->GetDisplayName().ToString();
	return Config.IsFilterActive(FilterName);
}

bool FSequencerFilterBar::SetFilterActive(const TSharedRef<FSequencerTrackFilter>& InFilter, const bool bInActive, const bool bInRequestFilterUpdate)
{
	USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
	check(IsValid(SequencerSettings));

	const bool bNewActive = InFilter->IsInverseFilter() ? !bInActive : bInActive;

	FSequencerFilterBarConfig& Config = SequencerSettings->FindOrAddTrackFilterBar(GetIdentifier(), true);

	const FString FilterName = InFilter->GetDisplayName().ToString();
	const bool bSuccess = Config.SetFilterActive(FilterName, bNewActive);

	if (bSuccess)
	{
		SequencerSettings->SaveConfig();

		InFilter->SetActive(bNewActive);
		InFilter->ActiveStateChanged(bNewActive);

		const ESequencerFilterChange FilterChangeType = bNewActive ? ESequencerFilterChange::Activate : ESequencerFilterChange::Deactivate;
		FiltersChangedEvent.Broadcast(FilterChangeType, InFilter);

		if (bInRequestFilterUpdate)
		{
			RequestFilterUpdate();
		}
	}

	return bSuccess;
}

void FSequencerFilterBar::EnableAllFilters(const bool bInEnable, const TArray<FString> InExceptionFilterNames)
{
	TArray<TSharedRef<FSequencerTrackFilter>> ExceptionFilters;
	TArray<TSharedRef<FSequencerTrackFilter_CustomText>> ExceptionCustomTextFilters;

	for (const FString& FilterName : InExceptionFilterNames)
	{
		if (const TSharedPtr<FSequencerTrackFilter> Filter = FindFilterByDisplayName(FilterName))
		{
			ExceptionFilters.Add(Filter.ToSharedRef());
		}
		else if (const TSharedPtr<FSequencerTrackFilter_CustomText> CustomTextFilter = FindCustomTextFilterByDisplayName(FilterName))
		{
			ExceptionCustomTextFilters.Add(CustomTextFilter.ToSharedRef());
		}
	}

	EnableFilters(bInEnable, {}, ExceptionFilters);
	EnableCustomTextFilters(bInEnable, ExceptionCustomTextFilters);
}

void FSequencerFilterBar::ActivateCommonFilters(const bool bInActivate, const TArray<FString> InExceptionFilterNames)
{
	TArray<TSharedRef<FSequencerTrackFilter>> ExceptionFilters;

	for (const FString& FilterName : InExceptionFilterNames)
	{
		if (const TSharedPtr<FSequencerTrackFilter> Filter = FindFilterByDisplayName(FilterName))
		{
			ExceptionFilters.Add(Filter.ToSharedRef());
		}
	}

	return ActivateCommonFilters(bInActivate, {}, ExceptionFilters);
}

void FSequencerFilterBar::ActivateCommonFilters(const bool bInActivate
    , const TArray<TSharedRef<FFilterCategory>> InMatchCategories
    , const TArray<TSharedRef<FSequencerTrackFilter>>& InExceptions)
{
	USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
	check(IsValid(SequencerSettings));

	FSequencerFilterBarConfig& Config = SequencerSettings->FindOrAddTrackFilterBar(GetIdentifier(), false);

	bool bNeedsSave = false;

	CommonFilters->ForEachFilter([this, bInActivate, &InExceptions, &Config, &bNeedsSave]
		(const TSharedRef<FSequencerTrackFilter>& InFilter)
		{
			if (InExceptions.Contains(InFilter))
			{
				return true;
			}

			const FString FilterName = InFilter->GetDisplayName().ToString();
			if (Config.SetFilterActive(FilterName, bInActivate))
			{
				const ESequencerFilterChange FilterChangeType = bInActivate
					? ESequencerFilterChange::Activate
					: ESequencerFilterChange::Deactivate;
				FiltersChangedEvent.Broadcast(FilterChangeType, InFilter);

				InFilter->SetActive(bInActivate);
				InFilter->ActiveStateChanged(bInActivate);

				bNeedsSave = true;
			}

			return true;
		}
		, true
		, InMatchCategories);

	if (bNeedsSave)
	{
		SequencerSettings->SaveConfig();
	}

	RequestFilterUpdate();
}

bool FSequencerFilterBar::AreAllEnabledFiltersActive(const bool bInActive, const TArray<FString> InExceptionFilterNames) const
{
	const TArray<TSharedRef<FSequencerTrackFilter>> EnabledFilters = GetEnabledFilters();
	for (const TSharedRef<FSequencerTrackFilter>& Filter : EnabledFilters)
	{
		const FString FilterName = Filter->GetDisplayName().ToString();
		if (InExceptionFilterNames.Contains(FilterName))
		{
			continue;
		}

		if (IsFilterActive(Filter) != bInActive)
		{
			return false;
		}
	}

	const TArray<TSharedRef<FSequencerTrackFilter_CustomText>> EnabledCustomTextFilters = GetEnabledCustomTextFilters();
	for (const TSharedRef<FSequencerTrackFilter_CustomText>& CustomTextFilter : EnabledCustomTextFilters)
	{
		const FString FilterName = CustomTextFilter->GetDisplayName().ToString();
		if (InExceptionFilterNames.Contains(FilterName))
		{
			continue;
		}

		if (IsFilterActive(CustomTextFilter) != bInActive)
		{
			return false;
		}
	}

	return true;
}

void FSequencerFilterBar::ActivateAllEnabledFilters(const bool bInActivate, const TArray<FString> InExceptionFilterNames)
{
	const TArray<TSharedRef<FSequencerTrackFilter>> EnabledFilters = GetEnabledFilters();
	for (const TSharedRef<FSequencerTrackFilter>& Filter : EnabledFilters)
	{
		const FString FilterName = Filter->GetDisplayName().ToString();
		if (InExceptionFilterNames.Contains(FilterName))
        {
        	continue;
        }

		if (IsFilterActive(Filter) != bInActivate)
		{
			SetFilterActive(Filter, bInActivate);
		}
	}

	const TArray<TSharedRef<FSequencerTrackFilter_CustomText>> EnabledCustomTextFilters = GetEnabledCustomTextFilters();
	for (const TSharedRef<FSequencerTrackFilter_CustomText>& CustomTextFilter : EnabledCustomTextFilters)
	{
		const FString FilterName = CustomTextFilter->GetDisplayName().ToString();
		if (InExceptionFilterNames.Contains(FilterName))
		{
			continue;
		}

		if (IsFilterActive(CustomTextFilter) != bInActivate)
		{
			SetFilterActive(CustomTextFilter, bInActivate);
		}
	}
}

void FSequencerFilterBar::ToggleActivateAllEnabledFilters()
{
	const bool bNewActive = !AreAllEnabledFiltersActive(true, {});
	ActivateAllEnabledFilters(bNewActive, {});
}

TArray<TSharedRef<FSequencerTrackFilter>> FSequencerFilterBar::GetActiveFilters() const
{
	TArray<TSharedRef<FSequencerTrackFilter>> OutFilters;

	CommonFilters->ForEachFilter([this, &OutFilters]
		(const TSharedRef<FSequencerTrackFilter>& InFilter)
		{
			if (IsFilterActive(InFilter))
			{
				OutFilters.Add(InFilter);
			}
			return true;
		}
		, true);

	return OutFilters;
}

bool FSequencerFilterBar::HasEnabledCommonFilters() const
{
	bool bOutReturn = false;

	CommonFilters->ForEachFilter([this, &bOutReturn]
		(const TSharedRef<FSequencerTrackFilter>& InFilter)
		{
			if (IsFilterEnabled(InFilter))
			{
				bOutReturn = true;
				return false;
			}
			return true;
		}
		, true);

	if (bOutReturn)
	{
		return true;
	}

	InternalFilters->ForEachFilter([this, &bOutReturn]
		(const TSharedRef<FSequencerTrackFilter>& InFilter)
		{
			if (IsFilterEnabled(InFilter))
			{
				bOutReturn = true;
				return false;
			}
			return true;
		}
		, false);

	return bOutReturn;
}

bool FSequencerFilterBar::HasEnabledFilter(const TArray<TSharedRef<FSequencerTrackFilter>>& InFilters) const
{
	const TArray<TSharedRef<FSequencerTrackFilter>>& Filters = InFilters.IsEmpty() ? GetCommonFilters() : InFilters;

	for (const TSharedRef<FSequencerTrackFilter>& Filter : Filters)
	{
		if (IsFilterEnabled(Filter))
		{
			return true;
		}
	}

	return false;
}

bool FSequencerFilterBar::HasAnyFilterEnabled() const
{
	const bool bCommonFilterEnabled = HasEnabledCommonFilters();
	const bool bCustomTextFilterEnabled = HasEnabledCustomTextFilters();

	return bCommonFilterEnabled
		|| bCustomTextFilterEnabled;
}

bool FSequencerFilterBar::IsFilterEnabled(TSharedRef<FSequencerTrackFilter> InFilter) const
{
	USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
	check(IsValid(SequencerSettings));

	FSequencerFilterBarConfig& Config = SequencerSettings->FindOrAddTrackFilterBar(GetIdentifier(), false);

	const FString FilterName = InFilter->GetDisplayName().ToString();
	return Config.IsFilterEnabled(FilterName);
}

bool FSequencerFilterBar::SetFilterEnabled(const TSharedRef<FSequencerTrackFilter> InFilter, const bool bInEnabled, const bool bInRequestFilterUpdate)
{
	USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
	check(IsValid(SequencerSettings));

	FSequencerFilterBarConfig& Config = SequencerSettings->FindOrAddTrackFilterBar(GetIdentifier(), true);

	const FString FilterName = InFilter->GetDisplayName().ToString();
	const bool bSuccess = Config.SetFilterEnabled(FilterName, bInEnabled);

	if (bSuccess)
	{
		SequencerSettings->SaveConfig();

		const ESequencerFilterChange FilterChangeType = bInEnabled ? ESequencerFilterChange::Enable : ESequencerFilterChange::Disable;
		FiltersChangedEvent.Broadcast(FilterChangeType, InFilter);

		if (!bInEnabled && IsFilterActive(InFilter))
		{
			InFilter->SetActive(false);
			InFilter->ActiveStateChanged(false);
		}

		if (bInRequestFilterUpdate)
		{
			RequestFilterUpdate();
		}
	}

	return bSuccess;
}

void FSequencerFilterBar::EnableFilters(const bool bInEnable
	, const TArray<TSharedRef<FFilterCategory>> InMatchCategories
	, const TArray<TSharedRef<FSequencerTrackFilter>> InExceptions)
{
	USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
	check(IsValid(SequencerSettings));

	FSequencerFilterBarConfig& Config = SequencerSettings->FindOrAddTrackFilterBar(GetIdentifier(), true);

	CommonFilters->ForEachFilter([this, bInEnable, &InExceptions, &Config]
		(const TSharedRef<FSequencerTrackFilter>& InFilter)
		{
			if (InExceptions.IsEmpty() || !InExceptions.Contains(InFilter))
			{
				const FString FilterName = InFilter->GetDisplayName().ToString();
				if (Config.SetFilterEnabled(FilterName, bInEnable))
				{
					const ESequencerFilterChange FilterChangeType = bInEnable ? ESequencerFilterChange::Enable : ESequencerFilterChange::Disable;
					FiltersChangedEvent.Broadcast(FilterChangeType, InFilter);

					if (!bInEnable && IsFilterActive(InFilter))
					{
						InFilter->SetActive(false);
						InFilter->ActiveStateChanged(false);
					}
				}
			}
			return true;
		}
		, true
		, InMatchCategories);

	SequencerSettings->SaveConfig();

	RequestFilterUpdate();
}

void FSequencerFilterBar::ToggleFilterEnabled(const TSharedRef<FSequencerTrackFilter> InFilter)
{
	SetFilterEnabled(InFilter, !IsFilterEnabled(InFilter), true);
}

TArray<TSharedRef<FSequencerTrackFilter>> FSequencerFilterBar::GetEnabledFilters() const
{
	TArray<TSharedRef<FSequencerTrackFilter>> OutFilters;

	CommonFilters->ForEachFilter([this, &OutFilters]
		(const TSharedRef<FSequencerTrackFilter>& InFilter)
		{
			if (IsFilterEnabled(InFilter))
			{
				OutFilters.Add(InFilter);
			}
			return true;
		}
		, true);

	return OutFilters;
}

bool FSequencerFilterBar::HasAnyCommonFilters() const
{
	return !CommonFilters->IsEmpty();
}

bool FSequencerFilterBar::AddFilter(const TSharedRef<FSequencerTrackFilter>& InFilter)
{
	const bool bSuccess = CommonFilters->Add(InFilter) == 1;

	return bSuccess;
}

bool FSequencerFilterBar::RemoveFilter(const TSharedRef<FSequencerTrackFilter>& InFilter)
{
	const bool bSuccess = CommonFilters->Remove(InFilter) == 1;

	if (bSuccess)
	{
		FiltersChangedEvent.Broadcast(ESequencerFilterChange::Disable, InFilter);
	}

	return bSuccess;
}

TArray<FText> FSequencerFilterBar::GetFilterDisplayNames() const
{
	return CommonFilters->GetFilterDisplayNames();
}

TArray<FText> FSequencerFilterBar::GetCustomTextFilterNames() const
{
	TArray<FText> OutLabels;

	for (const TSharedRef<FSequencerTrackFilter_CustomText>& CustomTextFilter : CustomTextFilters)
	{
		const FCustomTextFilterData TextFilterData = CustomTextFilter->CreateCustomTextFilterData();
		OutLabels.Add(TextFilterData.FilterLabel);
	}

	return OutLabels;
}

int32 FSequencerFilterBar::GetTotalDisplayNodeCount() const
{
	return FilterData.GetTotalNodeCount();
}

int32 FSequencerFilterBar::GetFilteredDisplayNodeCount() const
{
	return FilterData.GetDisplayNodeCount();
}

TArray<TSharedRef<FSequencerTrackFilter>> FSequencerFilterBar::GetCommonFilters(const TArray<TSharedRef<FFilterCategory>>& InCategories) const
{
	return CommonFilters->GetAllFilters(InCategories);
}

bool FSequencerFilterBar::AnyCustomTextFilterActive() const
{
	for (const TSharedRef<FSequencerTrackFilter_CustomText>& Filter : CustomTextFilters)
	{
		if (IsFilterActive(Filter))
		{
			return true;
		}
	}

	return false;
}

bool FSequencerFilterBar::HasEnabledCustomTextFilters() const
{
	for (const TSharedRef<FSequencerTrackFilter_CustomText>& Filter : CustomTextFilters)
	{
		if (IsFilterEnabled(Filter))
		{
			return true;
		}
	}
	return false;
}

TArray<TSharedRef<FSequencerTrackFilter_CustomText>> FSequencerFilterBar::GetAllCustomTextFilters() const
{
	return CustomTextFilters;
}

bool FSequencerFilterBar::AddCustomTextFilter(const TSharedRef<FSequencerTrackFilter_CustomText>& InFilter, const bool bInAddToConfig)
{
	if (CustomTextFilters.Add(InFilter) != 1)
	{
		return false;
	}

	if (bInAddToConfig)
	{
		USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
		if (ensure(IsValid(SequencerSettings)))
		{
			FSequencerFilterBarConfig& Config = SequencerSettings->FindOrAddTrackFilterBar(GetIdentifier(), false);

			if (Config.AddCustomTextFilter(InFilter->CreateCustomTextFilterData()))
			{
				SequencerSettings->SaveConfig();
			}
		}
	}

	FiltersChangedEvent.Broadcast(ESequencerFilterChange::Activate, InFilter);

	return true;
}

bool FSequencerFilterBar::RemoveCustomTextFilter(const TSharedRef<FSequencerTrackFilter_CustomText>& InFilter, const bool bInAddToConfig)
{
	if (CustomTextFilters.Remove(InFilter) != 1)
	{
		return false;
	}

	if (bInAddToConfig)
	{
		USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
		if (ensure(IsValid(SequencerSettings)))
		{
			FSequencerFilterBarConfig& Config = SequencerSettings->FindOrAddTrackFilterBar(GetIdentifier(), false);

			const FString FilterName = InFilter->GetDisplayName().ToString();
			if (Config.RemoveCustomTextFilter(FilterName))
			{
				SequencerSettings->SaveConfig();
			}
		}
	}

	FiltersChangedEvent.Broadcast(ESequencerFilterChange::Disable, InFilter);

	return true;
}

void FSequencerFilterBar::ActivateCustomTextFilters(const bool bInActivate, const TArray<TSharedRef<FSequencerTrackFilter_CustomText>> InExceptions)
{
	USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
	check(IsValid(SequencerSettings));

	FSequencerFilterBarConfig& Config = SequencerSettings->FindOrAddTrackFilterBar(GetIdentifier(), false);

	for (const TSharedRef<FSequencerTrackFilter_CustomText>& CustomTextFilter : CustomTextFilters)
	{
		if (InExceptions.IsEmpty() || !InExceptions.Contains(CustomTextFilter))
		{
			const FString FilterName = CustomTextFilter->GetDisplayName().ToString();
			if (Config.SetFilterActive(FilterName, bInActivate))
			{
				if (!bInActivate && IsFilterActive(CustomTextFilter))
				{
					CustomTextFilter->SetActive(false);
					CustomTextFilter->ActiveStateChanged(false);
				}

				const ESequencerFilterChange FilterChangeType = bInActivate ? ESequencerFilterChange::Activate : ESequencerFilterChange::Deactivate;
				FiltersChangedEvent.Broadcast(FilterChangeType, CustomTextFilter);
			}
		}
	}

	SequencerSettings->SaveConfig();

	RequestFilterUpdate();
}

void FSequencerFilterBar::EnableCustomTextFilters(const bool bInEnable, const TArray<TSharedRef<FSequencerTrackFilter_CustomText>> InExceptions)
{
	USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
	check(IsValid(SequencerSettings));

	FSequencerFilterBarConfig& Config = SequencerSettings->FindOrAddTrackFilterBar(GetIdentifier(), false);

	for (const TSharedRef<FSequencerTrackFilter_CustomText>& CustomTextFilter : CustomTextFilters)
	{
		if (InExceptions.IsEmpty() || !InExceptions.Contains(CustomTextFilter))
		{
			const FString FilterName = CustomTextFilter->GetDisplayName().ToString();
			if (Config.SetFilterEnabled(FilterName, bInEnable))
			{
				if (!bInEnable && IsFilterActive(CustomTextFilter))
				{
					CustomTextFilter->SetActive(false);
					CustomTextFilter->ActiveStateChanged(false);
				}

				const ESequencerFilterChange FilterChangeType = bInEnable ? ESequencerFilterChange::Enable : ESequencerFilterChange::Disable;
				FiltersChangedEvent.Broadcast(FilterChangeType, CustomTextFilter);
			}
		}
	}

	SequencerSettings->SaveConfig();

	RequestFilterUpdate();
}

TArray<TSharedRef<FSequencerTrackFilter_CustomText>> FSequencerFilterBar::GetEnabledCustomTextFilters() const
{
	TArray<TSharedRef<FSequencerTrackFilter_CustomText>> OutFilters;

	for (const TSharedRef<FSequencerTrackFilter_CustomText>& CustomTextFilter : CustomTextFilters)
	{
		if (IsFilterEnabled(CustomTextFilter))
		{
			OutFilters.Add(CustomTextFilter);
		}
	}

	return OutFilters;
}

TSet<TSharedRef<FFilterCategory>> FSequencerFilterBar::GetFilterCategories(const TSet<TSharedRef<FSequencerTrackFilter>>* InFilters) const
{
	return CommonFilters->GetCategories(InFilters);
}

TSet<TSharedRef<FFilterCategory>> FSequencerFilterBar::GetConfigCategories() const
{
	return { ClassTypeCategory, ComponentTypeCategory, MiscCategory };
}

TSharedRef<FFilterCategory> FSequencerFilterBar::GetClassTypeCategory() const
{
	return ClassTypeCategory;
}

TSharedRef<FFilterCategory> FSequencerFilterBar::GetComponentTypeCategory() const
{
	return ComponentTypeCategory;
}

TSharedRef<FFilterCategory> FSequencerFilterBar::GetMiscCategory() const
{
	return MiscCategory;
}

void FSequencerFilterBar::ForEachFilter(const TFunctionRef<bool(const TSharedRef<FSequencerTrackFilter>&)>& InFunction
	, const bool bInCheckSupportsSequence
	, const TArray<TSharedRef<FFilterCategory>>& InCategories) const
{
	CommonFilters->ForEachFilter(InFunction, bInCheckSupportsSequence, InCategories);
}

bool FSequencerFilterBar::HasActiveLevelFilter() const
{
	return LevelFilter->HasHiddenLevels();
}

bool FSequencerFilterBar::HasAllLevelFiltersActive() const
{
	return LevelFilter->HasAllLevelsHidden();
}

const TSet<FString>& FSequencerFilterBar::GetActiveLevelFilters() const
{
	return LevelFilter->GetHiddenLevels();
}

void FSequencerFilterBar::ActivateLevelFilter(const FString& InLevelName, const bool bInActivate)
{
	if (bInActivate)
	{
		LevelFilter->UnhideLevel(InLevelName);
	}
	else
	{
		LevelFilter->HideLevel(InLevelName);
	}
}

bool FSequencerFilterBar::IsLevelFilterActive(const FString InLevelName) const
{
	return !LevelFilter->IsLevelHidden(InLevelName);
}

void FSequencerFilterBar::EnableAllLevelFilters(const bool bInEnable)
{
	LevelFilter->HideAllLevels(!bInEnable);
}

bool FSequencerFilterBar::CanEnableAllLevelFilters(const bool bInEnable)
{
	return LevelFilter->CanHideAllLevels(!bInEnable);
}

void FSequencerFilterBar::EnableAllGroupFilters(const bool bInEnable)
{
	UMovieSceneSequence* const FocusedMovieSequence = Sequencer.GetFocusedMovieSceneSequence();
	if (!IsValid(FocusedMovieSequence))
	{
		return;
	}

	UMovieScene* const FocusedMovieScene = FocusedMovieSequence->GetMovieScene();
	if (!IsValid(FocusedMovieScene))
	{
		return;
	}

	for (UMovieSceneNodeGroup* const NodeGroup : FocusedMovieScene->GetNodeGroups())
	{
		NodeGroup->SetEnableFilter(bInEnable);
	}
}

bool FSequencerFilterBar::IsGroupFilterActive(const FString& InGroupName) const
{
	return GroupFilter->HasActiveGroupFilter();
}

bool FSequencerFilterBar::PassesAnyCommonFilter(const TViewModelPtr<IOutlinerExtension>& InNode)
{
	bool bPassedAnyFilters = false;
	bool bAnyFilterActive = false;

	// Only one common filter needs to pass for this node to be included in the filtered set
	CommonFilters->ForEachFilter([this, &InNode, &bPassedAnyFilters, &bAnyFilterActive]
		(const TSharedRef<FSequencerTrackFilter>& InFilter)
		{
			const FString FilterName = InFilter->GetDisplayName().ToString();
			if (IsFilterActive(InFilter))
			{
				bAnyFilterActive = true;
				
				if (InFilter->PassesFilter(InNode))
				{
					bPassedAnyFilters = true;
					return false; // Stop processing filters
				}
			}

			return true;
		}
		, true);

	if (!bAnyFilterActive)
	{
		return true;
	}

	return bPassedAnyFilters;
}

bool FSequencerFilterBar::PassesAllInternalFilters(const TViewModelPtr<IOutlinerExtension>& InNode)
{
	bool bPassedAllFilters = true;

	InternalFilters->ForEachFilter([this, &InNode, &bPassedAllFilters]
		(const TSharedRef<FSequencerTrackFilter>& InFilter)
		{
			if (!InFilter->PassesFilter(InNode))
			{
				bPassedAllFilters = false;
				return false; // Stop processing filters
			}
			return true;
		}
		, true);

	return bPassedAllFilters;
}

bool FSequencerFilterBar::PassesAllCustomTextFilters(const TViewModelPtr<IOutlinerExtension>& InNode)
{
	for (const TSharedRef<FSequencerTrackFilter_CustomText>& Filter : CustomTextFilters)
	{
		if (IsFilterActive(Filter))
		{
			if (!Filter->PassesFilter(InNode))
			{
				return false;
			}
		}
	}

	return true;
}

UWorld* FSequencerFilterBar::GetWorld() const
{
	UObject* const PlaybackContext = Sequencer.GetPlaybackContext();
	if (IsValid(PlaybackContext))
	{
		return PlaybackContext->GetWorld();
	}
	return nullptr;
}

const FSequencerFilterData& FSequencerFilterBar::FilterNodes()
{
	//SCOPE_LOG_TIME_IN_SECONDS(TEXT("FSequencerFilterBar::FilterNodes()"), nullptr);

	// Update the world for the level filter
	const UObject* const PlaybackContext = Sequencer.GetPlaybackContext();
	UWorld* const World = PlaybackContext ? PlaybackContext->GetWorld() : nullptr;
	LevelFilter->UpdateWorld(World);

	// Update the group filter
	const UMovieSceneSequence* const FocusedMovieSceneSequence = Sequencer.GetFocusedMovieSceneSequence();
	if (IsValid(FocusedMovieSceneSequence))
	{
		GroupFilter->UpdateMovieScene(FocusedMovieSceneSequence->GetMovieScene());
	}

	// Reset all filter data
	FilterData.Reset();

	// Always include the bottom spacer
	TViewModelPtr<IOutlinerExtension> SpacerNode;
	if (const FSequenceModel* const SequenceModel = Sequencer.GetNodeTree()->GetRootNode()->CastThis<FSequenceModel>())
	{
		SpacerNode = CastViewModelChecked<IOutlinerExtension>(SequenceModel->GetBottomSpacer());
	}

	// Loop through all nodes and filter recursively
	const bool bHasActiveFilter = HasAnyFilterActive();
	for (const TViewModelPtr<IOutlinerExtension>& RootNode : Sequencer.GetNodeTree()->GetRootNodes())
	{
		FilterNodesRecursive(bHasActiveFilter, RootNode);
	}

	// Always filter in spacer node
	SpacerNode->SetFilteredOut(false);

	return FilterData;
}

FSequencerFilterData& FSequencerFilterBar::GetFilterData()
{
	return FilterData;
}

bool FSequencerFilterBar::FilterNodesRecursive(const bool bInHasActiveFilter, const TViewModelPtr<IOutlinerExtension>& InStartNode)
{
	/**
	 * Main Filtering Logic
	 *
	 * - Pinning overrides all other filters
	 * - Hidden/Isolated tracks will take precedence over common filters
	 * - Can hide sub tracks of isolated tracks
	 */

	bool bAnyChildPassed = false;

	// Child nodes should always be processed, as they may force their parents to pass
	for (const TViewModelPtr<IOutlinerExtension>& Node : InStartNode.AsModel()->GetChildrenOfType<IOutlinerExtension>())
	{
		if (FilterNodesRecursive(bInHasActiveFilter, Node))
		{
			bAnyChildPassed = true;
		}
	}

	// Increment the total node count so we can remove the code to loop again just to count
	FilterData.IncrementTotalNodeCount();

	// Early out if no filter
	if (!bInHasActiveFilter)
	{
		FilterData.FilterInNode(InStartNode);
		return false;
	}

	const USequencerSettings* const SequencerSettings = Sequencer.GetSequencerSettings();
    check(IsValid(SequencerSettings));

	// Pinning overrides all other filters
	if (!SequencerSettings->GetIncludePinnedInFilter())
	{
		const TSharedPtr<IPinnableExtension> Pinnable = InStartNode.AsModel()->FindAncestorOfType<IPinnableExtension>(true);
		if (Pinnable.IsValid() && Pinnable->IsPinned())
		{
			FilterData.FilterInParentChildNodes(InStartNode, true, true, true);
			return true;
		}
	}

	const bool bPassedTextFilter = !TextFilter->IsActive() || TextFilter->PassesFilter(InStartNode);
	const bool bPassedHideIsolateFilter = !HideIsolateFilter->IsActive() || HideIsolateFilter->PassesFilter(InStartNode);
	const bool bPassedAnyCommonFilters = PassesAnyCommonFilter(InStartNode);
	const bool bPassedInternalFilters = !AnyInternalFilterActive() || PassesAllInternalFilters(InStartNode);
	const bool bPassedAnyCustomTextFilters = PassesAllCustomTextFilters(InStartNode);

	const bool bAllFiltersPassed = bPassedTextFilter
		&& bPassedHideIsolateFilter
		&& bPassedAnyCommonFilters
		&& bPassedInternalFilters
		&& bPassedAnyCustomTextFilters;

	if (bAllFiltersPassed || bAnyChildPassed)
	{
		if (SequencerSettings->GetAutoExpandNodesOnFilterPass())
		{
			SetTrackParentsExpanded(InStartNode.ImplicitCast(), true);
		}

		FilterData.FilterInNodeWithAncestors(InStartNode);
		return true;
	}

	// After child nodes are processed, fail anything that didn't pass
	FilterData.FilterOutNode(InStartNode);
	return false;
}

TSet<TWeakViewModelPtr<IOutlinerExtension>> FSequencerFilterBar::GetSelectedTracksOrAll() const
{
	TSharedPtr<FSequencerEditorViewModel> SequencerViewModel = GetSequencer().GetViewModel();
	if (!SequencerViewModel.IsValid())
	{
		return {};
	}

	const TSharedPtr<FSequencerSelection> Selection = SequencerViewModel->GetSelection();
	if (!Selection.IsValid())
	{
		return {};
	}

	const TSet<TWeakViewModelPtr<IOutlinerExtension>> SelectedSet = Selection->Outliner.GetSelected();
	if (SelectedSet.IsEmpty())
	{
		TSet<TWeakViewModelPtr<IOutlinerExtension>> OutTracks;
		for (const TViewModelPtr<IOutlinerExtension>& TrackModel : SequencerViewModel->GetRootModel()->GetDescendantsOfType<IOutlinerExtension>())
		{
			OutTracks.Add(TrackModel);
		}
		return OutTracks;
	}

	return SelectedSet;
}

bool FSequencerFilterBar::HasSelectedTracks() const
{
	return !GetSelectedTracksOrAll().IsEmpty();
}

void FSequencerFilterBar::HideSelectedTracks()
{
	const bool bAddToExisting = !FSlateApplication::Get().GetModifierKeys().AreModifersDown(EModifierKey::Shift);
	const TSet<TWeakViewModelPtr<IOutlinerExtension>> TracksToHide = GetSelectedTracksOrAll();
	HideIsolateFilter->HideTracks(TracksToHide, bAddToExisting);
}

void FSequencerFilterBar::IsolateSelectedTracks()
{
	const bool bAddToExisting = FSlateApplication::Get().GetModifierKeys().AreModifersDown(EModifierKey::Shift);
	const TSet<TWeakViewModelPtr<IOutlinerExtension>> TracksToIsolate = GetSelectedTracksOrAll();
	HideIsolateFilter->IsolateTracks(TracksToIsolate, bAddToExisting);
}

void FSequencerFilterBar::ShowOnlyLocationCategoryGroups()
{
	HideIsolateFilter->IsolateCategoryGroupTracks(GetSelectedTracksOrAll(), { TEXT("Location") }, false);
}

void FSequencerFilterBar::ShowOnlyRotationCategoryGroups()
{
	HideIsolateFilter->IsolateCategoryGroupTracks(GetSelectedTracksOrAll(), { TEXT("Rotation") }, false);
}

void FSequencerFilterBar::ShowOnlyScaleCategoryGroups()
{
	HideIsolateFilter->IsolateCategoryGroupTracks(GetSelectedTracksOrAll(), { TEXT("Scale") }, false);
}

void FSequencerFilterBar::SetTrackParentsExpanded(const TViewModelPtr<IOutlinerExtension>& InNode, const bool bInExpanded)
{
	for (const TViewModelPtr<IOutlinerExtension>& ParentNode : InNode.AsModel()->GetAncestorsOfType<IOutlinerExtension>())
	{
		if (!ParentNode->IsExpanded())
		{
			ParentNode->SetExpansion(true);
		}
	}
}

FString FSequencerFilterBar::GenerateTextFilterStringFromEnabledFilters() const
{
	FString GeneratedFilterString = TextFilter->GetRawFilterText().ToString();

	for (const TSharedRef<FSequencerTrackFilter>& Filter : GetCommonFilters())
	{
		if (IsFilterActive(Filter) && IsFilterEnabled(Filter))
		{
			const FString AndAddString = GeneratedFilterString.IsEmpty() ? TEXT("") : TEXT(" AND ");
			const FString ThisFilterGeneratedString = FString::Format(TEXT("{0}{1}==TRUE"), { AndAddString, *Filter->GetName() });
			GeneratedFilterString.Append(ThisFilterGeneratedString);
		}
	}

	return GeneratedFilterString;
}

TArray<TSharedRef<FSequencerTrackFilter>> FSequencerFilterBar::GetFilterList(const bool bInIncludeCustomTextFilters) const
{
	TArray<TSharedRef<FSequencerTrackFilter>> AllFilters;

	AllFilters.Append(CommonFilters->GetAllFilters());
	AllFilters.Append(InternalFilters->GetAllFilters());

	AllFilters.Add(TextFilter);
	AllFilters.Add(HideIsolateFilter);

	if (bInIncludeCustomTextFilters)
	{
		for (const TSharedRef<FSequencerTrackFilter_CustomText>& Filter : CustomTextFilters)
		{
			AllFilters.Add(Filter);
		}
	}

	return AllFilters;
}

bool FSequencerFilterBar::ShouldUpdateOnTrackValueChanged() const
{
	if (bFiltersMuted)
	{
		return false;
	}

	const TArray<TSharedRef<FSequencerTrackFilter>> AllFilters = GetFilterList();

	for (const TSharedRef<FSequencerTrackFilter>& Filter : AllFilters)
	{
		if (Filter->ShouldUpdateOnTrackValueChanged() && IsFilterActive(Filter))
		{
			return true;
		}
	}

	return false;
}

TSharedRef<SFilterBarIsolateHideShow> FSequencerFilterBar::MakeIsolateHideShowPanel()
{
	return SNew(SFilterBarIsolateHideShow, SharedThis(this));
}

TSharedRef<SComboButton> FSequencerFilterBar::MakeAddFilterButton()
{
	const TSharedPtr<SLayeredImage> FilterImage = SNew(SLayeredImage)
		.Image(FAppStyle::Get().GetBrush(TEXT("Icons.Filter")))
		.ColorAndOpacity_Lambda([this]()
			{
				return AreFiltersMuted() ? FLinearColor(1.f, 1.f, 1.f, 0.2f) : FSlateColor::UseForeground();
			});

	// Badge the filter icon if there are filters enabled or active
	FilterImage->AddLayer(TAttribute<const FSlateBrush*>::CreateLambda([this]() -> const FSlateBrush*
		{
			if (AreFiltersMuted() || !HasAnyFilterEnabled())
			{
				return nullptr;
			}

			if (HasAnyFilterActive(false, false))
			{
				return FAppStyle::Get().GetBrush(TEXT("Icons.BadgeModified"));
			}

			return FAppStyle::Get().GetBrush(TEXT("Icons.Badge"));
		}));

	const TSharedRef<SToolBarButtonBlock> asdf = SNew(SToolBarButtonBlock);
	const TSharedRef<SComboButton> ComboButton = SNew(SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>(TEXT("SimpleComboButtonWithIcon")))
		.ForegroundColor(FSlateColor::UseStyle())
		.ToolTipText_Lambda([this]()
			{
				return FText::Format(LOCTEXT("AddFilterToolTip", "Open the Add Filter Menu to add or manage filters\n\n"
					"Shift + Click to temporarily mute all active filters\n\n{0}")
					, SFilterBarIsolateHideShow::MakeLongDisplaySummaryText(*this));
			})
		.OnComboBoxOpened_Lambda([this]()
			{
				// Don't allow opening the menu if filters are muted or we are toggling the filter mute state
				if (AreFiltersMuted() || FSlateApplication::Get().GetModifierKeys().IsShiftDown())
				{
					FSlateApplication::Get().DismissAllMenus();
				}
			})
		.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
			{
				if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
				{
					MuteFilters(!AreFiltersMuted());
					FSlateApplication::Get().DismissAllMenus();
					return SNullWidget::NullWidget;
				}
				return FilterMenu->CreateMenu(SharedThis(this));
			})
		.ContentPadding(FMargin(1, 0))
		.ButtonContent()
		[
			FilterImage.ToSharedRef()
		];
	ComboButton->AddMetadata(MakeShared<FTagMetaData>(TEXT("SequencerTrackFiltersCombo")));

	return ComboButton;
}

TSharedPtr<SSequencerFilterBar> FSequencerFilterBar::GetWidget() const
{
	const TSharedPtr<SSequencer> SequencerWidget = StaticCastSharedRef<SSequencer>(GetSequencer().GetSequencerWidget());
	if (!SequencerWidget.IsValid())
	{
		return nullptr;
	}
	return SequencerWidget->GetFilterBarWidget();
}

#undef LOCTEXT_NAMESPACE
