// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SequencerTrackFilterBase.h"

class FEditorModeTools;
class ILevelEditor;

class FSequencerTrackFilter_Selected : public FSequencerTrackFilter
{
public:
	FSequencerTrackFilter_Selected(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr);
	virtual ~FSequencerTrackFilter_Selected() override;

	//~ Begin FSequencerTrackFilter
	virtual bool ShouldUpdateOnTrackValueChanged() const override;
	virtual FText GetDefaultToolTipText() const override;
	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override;
	virtual void ActiveStateChanged(const bool bInActive) override;
	//~ End FSequencerTrackFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override;
	virtual FSlateIcon GetIcon() const override;
	//~ End FFilterBase

	//~ Begin IFilter
	virtual FString GetName() const override;
	virtual bool PassesFilter(FSequencerTrackFilterType InItem) const override;
	//~ End IFilter

	void ToggleShowOnlySelectedTracks();

protected:
	void BindSelectionChanged();
	void UnbindSelectionChanged();

	TSharedPtr<ILevelEditor> GetLevelEditor() const;
	FEditorModeTools* GetEditorModeManager() const;

	void OnSelectionChanged(UObject* const InObject);

	FDelegateHandle OnSelectionChangedHandle;
};
