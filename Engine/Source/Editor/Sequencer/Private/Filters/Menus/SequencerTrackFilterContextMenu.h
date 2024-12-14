// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class FSequencerTrackFilter;
class SSequencerFilter;
class SWidget;
class UToolMenu;

class FSequencerTrackFilterContextMenu : public TSharedFromThis<FSequencerTrackFilterContextMenu>
{
public:
	TSharedRef<SWidget> CreateMenuWidget(const TSharedRef<SSequencerFilter>& InFilterWidget);

protected:
	void PopulateMenu(UToolMenu* const InMenu);

	void PopulateFilterOptionsSection(UToolMenu& InMenu);
	void PopulateCustomFilterOptionsSection(UToolMenu& InMenu);
	void PopulateBulkOptionsSection(UToolMenu& InMenu);

	FText GetFilterDisplayName() const;

	void OnDisableFilter();
	void OnResetFilters();

	void OnActivateWithFilterException();

	void OnActivateAllFilters(const bool bInActivate);

	void OnEditFilter();
	void OnDeleteFilter();

	const TSharedPtr<FSequencerTrackFilter> GetFilter() const;

	TWeakPtr<SSequencerFilter> WeakFilterWidget;
};
