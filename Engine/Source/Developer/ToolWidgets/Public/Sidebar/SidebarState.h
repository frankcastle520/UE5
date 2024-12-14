// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SidebarState.generated.h"

/** Represents the state of a sidebar drawer to be saved/restored to/from config. */
USTRUCT()
struct TOOLWIDGETS_API FSidebarDrawerState
{
	GENERATED_BODY()

	FSidebarDrawerState() {}
	FSidebarDrawerState(const FName InDrawerId)
	{
		DrawerId = InDrawerId;
	}

	UPROPERTY()
	FName DrawerId = NAME_None;

	/** Names of all sections that were last selected */
	UPROPERTY()
	TSet<FName> SelectedSections;

	UPROPERTY()
	bool bIsPinned = false;

	UPROPERTY()
	bool bIsDocked = false;
};

/** Represents the state of a sidebar to be saved/restored to/from config. */
USTRUCT()
struct TOOLWIDGETS_API FSidebarState
{
	GENERATED_BODY()

public:
	static constexpr float DefaultSize = 0.25f;
	static constexpr float MinSize = 0.005f;
	static constexpr float MaxSize = 0.5f;
	static constexpr float AutoDockThresholdSize = 0.05f;

	/** @return True if any property has been changed from default */
	bool IsValid() const;

	bool IsHidden() const;
	bool IsVisible() const;

	void SetHidden(const bool bInHidden);
	void SetVisible(const bool bInVisible);

	float GetDrawerSize() const;
	void SetDrawerSize(const float InSize);

	void GetDrawerSizes(float& OutDrawerSize, float& OutContentSize) const;
	void SetDrawerSizes(const float InDrawerSize, const float InContentSize);

	const TArray<FSidebarDrawerState>& GetDrawerStates() const;

	FSidebarDrawerState& FindOrAddDrawerState(const FSidebarDrawerState& InDrawerState);
	const FSidebarDrawerState* FindDrawerState(const FSidebarDrawerState& InDrawerState);

	/** Saves the state of a drawer. If the drawers state already exists in config, it will be replaced. */
	void SaveDrawerState(const FSidebarDrawerState& InState);

protected:
	UPROPERTY()
	bool bHidden = false;

	UPROPERTY()
	float DrawerSize = 0.f;

	/** Save the other splitter slot size to exactly restore the size when a drawer is docked in a SSplitter widget. */
	UPROPERTY()
	float ContentSize = 0.f;

	UPROPERTY()
	TArray<FSidebarDrawerState> DrawerStates;
};
