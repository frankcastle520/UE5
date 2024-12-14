// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerTrackFilter_Selected.h"
#include "EditorModeManager.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Filters/SequencerFilterBar.h"
#include "Filters/SequencerTrackFilterCommands.h"
#include "Selection.h"
#include "Sequencer.h"

#define LOCTEXT_NAMESPACE "SequencerTrackFilter_Selected"

FSequencerTrackFilter_Selected::FSequencerTrackFilter_Selected(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory)
	: FSequencerTrackFilter(InFilterInterface, MoveTemp(InCategory))
{
}

FSequencerTrackFilter_Selected::~FSequencerTrackFilter_Selected()
{
	UnbindSelectionChanged();
}

void FSequencerTrackFilter_Selected::BindSelectionChanged()
{
	if (!OnSelectionChangedHandle.IsValid())
	{
		OnSelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(this, &FSequencerTrackFilter_Selected::OnSelectionChanged);
	}
}

void FSequencerTrackFilter_Selected::UnbindSelectionChanged()
{
	if (OnSelectionChangedHandle.IsValid())
	{
		USelection::SelectionChangedEvent.Remove(OnSelectionChangedHandle);
		OnSelectionChangedHandle.Reset();
	}
}

TSharedPtr<ILevelEditor> FSequencerTrackFilter_Selected::GetLevelEditor() const
{
	const FLevelEditorModule* const LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
	if (!LevelEditorModule)
	{
		return nullptr;
	}

	return LevelEditorModule->GetLevelEditorInstance().Pin();
}

FEditorModeTools* FSequencerTrackFilter_Selected::GetEditorModeManager() const
{
	const TSharedPtr<ILevelEditor> LevelEditor = GetLevelEditor();
	if (!LevelEditor.IsValid())
	{
		return nullptr;
	}

	return &LevelEditor->GetEditorModeManager();
}

bool FSequencerTrackFilter_Selected::ShouldUpdateOnTrackValueChanged() const
{
	return true;
}

FText FSequencerTrackFilter_Selected::GetDefaultToolTipText() const
{
	return LOCTEXT("SequencerTrackFilter_SelectedToolTip", "Show only track selected in the viewport");
}

TSharedPtr<FUICommandInfo> FSequencerTrackFilter_Selected::GetToggleCommand() const
{
	return FSequencerTrackFilterCommands::Get().ToggleFilter_Selected;
}

void FSequencerTrackFilter_Selected::ActiveStateChanged(const bool bInActive)
{
	FSequencerTrackFilter::ActiveStateChanged(bInActive);

	if (bInActive)
	{
		BindSelectionChanged();
	}
	else
	{
		UnbindSelectionChanged();
	}
}

FText FSequencerTrackFilter_Selected::GetDisplayName() const
{
	return LOCTEXT("SequencerTrackFilter_Selected", "Selected");
}

FSlateIcon FSequencerTrackFilter_Selected::GetIcon() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.SelectInViewport"));
}

FString FSequencerTrackFilter_Selected::GetName() const
{
	return TEXT("Selected");
}

bool FSequencerTrackFilter_Selected::PassesFilter(FSequencerTrackFilterType InItem) const
{
	const TSharedPtr<ILevelEditor> LevelEditor = GetLevelEditor();
	if (!LevelEditor.IsValid())
	{
		return false;
	}

	const UTypedElementSelectionSet* const Selection = LevelEditor->GetElementSelectionSet();
	if (!IsValid(Selection))
	{
		return false;
	}

	FSequencerFilterData& FilterData = FilterInterface.GetFilterData();

	const UObject* const TrackObject = FilterData.ResolveTrackBoundObject(GetSequencer(), InItem);
	if (!IsValid(TrackObject))
	{
		return false;
	}

	const TArray<UObject*> SelectedObjects = Selection->GetSelectedObjects<UObject>();

	if (SelectedObjects.Contains(TrackObject))
	{
		return true;
	}

	const USceneComponent* const Component = TrackObject->GetTypedOuter<USceneComponent>();
	if (IsValid(Component) && SelectedObjects.Contains(Component))
	{
		return true;
	}

	const AActor* const Actor = TrackObject->GetTypedOuter<AActor>();
	if (IsValid(Actor) && SelectedObjects.Contains(Actor))
	{
		return true;
	}

	return false;
}

void FSequencerTrackFilter_Selected::OnSelectionChanged(UObject* const InObject)
{
	FilterInterface.RequestFilterUpdate();
}

void FSequencerTrackFilter_Selected::ToggleShowOnlySelectedTracks()
{
	SetActive(!IsActive());
}

#undef LOCTEXT_NAMESPACE
