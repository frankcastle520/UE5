// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class FSequencerFilterBar;
class SWidget;
class UToolMenu;

class FSequencerFilterBarContextMenu : public TSharedFromThis<FSequencerFilterBarContextMenu>
{
public:
	TSharedRef<SWidget> CreateMenu(const TSharedRef<FSequencerFilterBar>& InFilterBar);

protected:
	void PopulateMenu(UToolMenu* const InMenu);

	void PopulateOptionsSection(UToolMenu& InMenu);
	void PopulateFilterBulkOptionsSection(UToolMenu& InMenu);

	void OnActivateAllFilters(const bool bInActivate);
	void OnResetFilters();

	TWeakPtr<FSequencerFilterBar> WeakFilterBar;
};
