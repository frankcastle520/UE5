// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/CustomTextFilters.h"
#include "Filters/ISequencerTrackFilters.h"
#include "Filters/SequencerFilterData.h"
#include "Filters/SequencerTrackFilterBase.h"

class FSequencer;
class FSequencerTrackFilter;
class FSequencerTrackFilter_CustomText;
class FSequencerTrackFilter_Group;
class FSequencerTrackFilter_HideIsolate;
class FSequencerTrackFilter_Level;
class FSequencerTrackFilter_Modified;
class FSequencerTrackFilter_Selected;
class FSequencerTrackFilter_Text;
class FSequencerTrackFilterCollection;
class FSequencerTrackFilterMenu;
class FUICommandList;
class SComboButton;
class SFilterBarIsolateHideShow;
class SFilterSearchBox;
class SSequencerFilterBar;
enum class EFilterBarLayout : uint8;

namespace UE::Sequencer
{
	class IOutlinerExtension;
}

enum class ESequencerFilterChange : uint8
{
	Enable,
	Disable,
	Activate,
	Deactivate
};

/** Holds the Sequencer track filter collection, the current text filter, and hidden/isolated lists. */
class FSequencerFilterBar : public TSharedFromThis<FSequencerFilterBar>, public ISequencerTrackFilters
{
public:
	/** An identifier shared by all filter bars, used to save and load settings common to every instance */
	static const FName SharedIdentifier;
	
	DECLARE_EVENT_TwoParams(FSequencerFilterBar, FSequencerFiltersChanged, const ESequencerFilterChange /*InChangeType*/, const TSharedRef<FSequencerTrackFilter>& /*InFilter*/);
	DECLARE_EVENT_TwoParams(FSequencerFilterBar, FSequencerCustomTextFiltersChanged, const ESequencerFilterChange /*InChangeType*/, const TSharedRef<FSequencerTrackFilter_CustomText>& /*InFilter*/);

	FSequencerFilterBar(FSequencer& InSequencer);
	virtual ~FSequencerFilterBar() override;

	void BindCommands();

	void CreateCustomTextFiltersFromConfig();

	TSharedPtr<ICustomTextFilter<FSequencerTrackFilterType>> CreateTextFilter();

	TSharedRef<SSequencerFilterBar> GenerateWidget(const TSharedPtr<SFilterSearchBox>& InSearchBox, const EFilterBarLayout InLayout);

	bool AreFiltersMuted() const;
	void MuteFilters(const bool bInMute);
	void ToggleMuteFilters();

	void ResetFilters();
	bool CanResetFilters() const;

	FSequencerFiltersChanged& GetOnFiltersChanged() { return FiltersChangedEvent; }

	TSharedRef<FSequencerTrackFilter_Text> GetTextFilter() const;
	FText GetFilterErrorText() const;

	/** Hide/Isolate/Show Filter Functions */

	TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>> GetHiddenTracks() const;
	TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>> GetIsolatedTracks() const;

	void HideTracks(const TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>>& InTracks, const bool bInAddToExisting);
	void UnhideTracks(const TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>>& InTracks);

	void IsolateTracks(const TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>>& InTracks, const bool bInAddToExisting);
	void UnisolateTracks(const TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>>& InTracks);

	void ShowAllTracks();

	bool HasHiddenTracks() const;
	bool HasIsolatedTracks() const;

	void EmptyHiddenTracks();
	void EmptyIsolatedTracks();

	TSharedPtr<FSequencerTrackFilter> FindFilterByDisplayName(const FString& InFilterName) const;
	TSharedPtr<FSequencerTrackFilter_CustomText> FindCustomTextFilterByDisplayName(const FString& InFilterName) const;

	bool HasAnyFiltersEnabled() const;

	//~ Begin ISequencerTrackFilters

	virtual FName GetIdentifier() const override;

	virtual FSequencer& GetSequencer() const override;

	virtual TSharedPtr<FUICommandList> GetCommandList() const override;

	virtual FString GetTextFilterString() const override;
    virtual void SetTextFilterString(const FString& InText) override;

	virtual bool DoesTextFilterStringContainExpressionPair(const FSequencerTextFilterExpressionContext& InExpression) const override;

	virtual void RequestFilterUpdate() override;

	virtual void EnableAllFilters(const bool bInEnable, const TArray<FString> InExceptionFilterNames) override;

	virtual void ActivateCommonFilters(const bool bInActivate, const TArray<FString> InExceptionFilterNames) override;

	virtual bool AreAllEnabledFiltersActive(const bool bInActive, const TArray<FString> InExceptionFilterNames) const;
	virtual void ActivateAllEnabledFilters(const bool bInActivate, const TArray<FString> InExceptionFilterNames) override;
	virtual void ToggleActivateAllEnabledFilters();

	virtual bool IsFilterActiveByDisplayName(const FString InFilterName) const override;
	virtual bool IsFilterEnabledByDisplayName(const FString InFilterName) const override;
	virtual bool SetFilterActiveByDisplayName(const FString InFilterName, const bool bInActive, const bool bInRequestFilterUpdate = true) override;
	virtual bool SetFilterEnabledByDisplayName(const FString InFilterName, const bool bInEnabled, const bool bInRequestFilterUpdate = true) override;

	virtual TArray<FText> GetFilterDisplayNames() const override;
	virtual TArray<FText> GetCustomTextFilterNames() const override;

	virtual int32 GetTotalDisplayNodeCount() const override;
	virtual int32 GetFilteredDisplayNodeCount() const override;

	virtual void HideSelectedTracks() override;
	virtual void IsolateSelectedTracks() override;

	virtual void ShowOnlyLocationCategoryGroups() override;
	virtual void ShowOnlyRotationCategoryGroups() override;
	virtual void ShowOnlyScaleCategoryGroups() override;

	virtual bool HasSelectedTracks() const override;

	virtual FSequencerFilterData& GetFilterData() override;

	//~ End ISequencerTrackFilters

	/** Active Filter Functions */

	bool AnyCommonFilterActive() const;
	bool AnyInternalFilterActive() const;
	virtual bool HasAnyFilterActive(const bool bCheckTextFilter = true
		, const bool bInCheckHideIsolateFilter = true
		, const bool bInCheckCommonFilters = true
		, const bool bInCheckInternalFilters = true
		, const bool bInCheckCustomTextFilters = true) const override;
	bool IsFilterActive(const TSharedRef<FSequencerTrackFilter> InFilter) const;
	bool SetFilterActive(const TSharedRef<FSequencerTrackFilter>& InFilter, const bool bInActive, const bool bInRequestFilterUpdate = true);
	void ActivateCommonFilters(const bool bInActivate
		, const TArray<TSharedRef<FFilterCategory>> InMatchCategories
		, const TArray<TSharedRef<FSequencerTrackFilter>>& InExceptions);
	TArray<TSharedRef<FSequencerTrackFilter>> GetActiveFilters() const;

	/** Enabled Filter Functions */

	bool HasEnabledCommonFilters() const;
	bool HasEnabledFilter(const TArray<TSharedRef<FSequencerTrackFilter>>& InFilters = {}) const;
	virtual bool HasAnyFilterEnabled() const override;
	bool IsFilterEnabled(TSharedRef<FSequencerTrackFilter> InFilter) const;
	bool SetFilterEnabled(const TSharedRef<FSequencerTrackFilter> InFilter, const bool bInEnabled, const bool bInRequestFilterUpdate = true);
	void EnableFilters(const bool bInEnable
		, const TArray<TSharedRef<FFilterCategory>> InMatchCategories = {}
		, const TArray<TSharedRef<FSequencerTrackFilter>> InExceptions = {});
	void ToggleFilterEnabled(const TSharedRef<FSequencerTrackFilter> InFilter);
	TArray<TSharedRef<FSequencerTrackFilter>> GetEnabledFilters() const;

	/** Filter Functions */

	bool HasAnyCommonFilters() const;
	bool AddFilter(const TSharedRef<FSequencerTrackFilter>& InFilter);
	bool RemoveFilter(const TSharedRef<FSequencerTrackFilter>& InFilter);

	TArray<TSharedRef<FSequencerTrackFilter>> GetCommonFilters(const TArray<TSharedRef<FFilterCategory>>& InCategories = {}) const;

	/** Custom Text Filter Functions */

	bool AnyCustomTextFilterActive() const;
	bool HasEnabledCustomTextFilters() const;
	TArray<TSharedRef<FSequencerTrackFilter_CustomText>> GetAllCustomTextFilters() const;
	virtual bool AddCustomTextFilter(const TSharedRef<FSequencerTrackFilter_CustomText>& InFilter, const bool bInAddToConfig) override;
	virtual bool RemoveCustomTextFilter(const TSharedRef<FSequencerTrackFilter_CustomText>& InFilter, const bool bInAddToConfig) override;
	void ActivateCustomTextFilters(const bool bInActivate, const TArray<TSharedRef<FSequencerTrackFilter_CustomText>> InExceptions = {});
	void EnableCustomTextFilters(const bool bInEnable, const TArray<TSharedRef<FSequencerTrackFilter_CustomText>> InExceptions = {});
	TArray<TSharedRef<FSequencerTrackFilter_CustomText>> GetEnabledCustomTextFilters() const;

	/** Filter Category Functions */

	TSet<TSharedRef<FFilterCategory>> GetFilterCategories(const TSet<TSharedRef<FSequencerTrackFilter>>* InFilters = nullptr) const;
	TSet<TSharedRef<FFilterCategory>> GetConfigCategories() const;
	TSharedRef<FFilterCategory> GetClassTypeCategory() const;
	TSharedRef<FFilterCategory> GetComponentTypeCategory() const;
	TSharedRef<FFilterCategory> GetMiscCategory() const;

	void ForEachFilter(const TFunctionRef<bool(const TSharedRef<FSequencerTrackFilter>&)>& InFunction
		, const bool bInCheckSupportsSequence
		, const TArray<TSharedRef<FFilterCategory>>& InCategories = {}) const;

	/** Level Filter Functions */

	bool HasActiveLevelFilter() const;
	bool HasAllLevelFiltersActive() const;
	const TSet<FString>& GetActiveLevelFilters() const;
	void ActivateLevelFilter(const FString& InLevelName, const bool bInActivate);
	bool IsLevelFilterActive(const FString InLevelName) const;
	void EnableAllLevelFilters(const bool bInEnable);
	bool CanEnableAllLevelFilters(const bool bInEnable);

	/** Group Filter Functions */

	void EnableAllGroupFilters(const bool bInEnable);
	bool IsGroupFilterActive(const FString& InGroupName) const;

	/** Misc Functions */

	TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>> GetSelectedTracksOrAll() const;

	void SetTrackParentsExpanded(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InNode, const bool bInExpanded);

	UWorld* GetWorld() const;

	const FSequencerFilterData& FilterNodes();

	FString GenerateTextFilterStringFromEnabledFilters() const;

	bool ShouldUpdateOnTrackValueChanged() const;

	TSharedRef<SFilterBarIsolateHideShow> MakeIsolateHideShowPanel();
	TSharedRef<SComboButton> MakeAddFilterButton();

	/** Attempts to get the filter bar widget from the Sequencer widget */
	TSharedPtr<SSequencerFilterBar> GetWidget() const;

protected:
	void CreateDefaultFilters();

	bool PassesAnyCommonFilter(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InNode);
	bool PassesAllInternalFilters(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InNode);
	bool PassesAllCustomTextFilters(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InNode);

	TArray<TSharedRef<FSequencerTrackFilter>> GetFilterList(const bool bInIncludeCustomTextFilters = false) const;

	/** The Sequencer this filter bar is interacting with */
	FSequencer& Sequencer;

	TSharedRef<FUICommandList> CommandList;

	/** Global override to enable/disable all filters */
	bool bFiltersMuted = false;

	TSharedRef<FFilterCategory> ClassTypeCategory;
	TSharedRef<FFilterCategory> ComponentTypeCategory;
	TSharedRef<FFilterCategory> MiscCategory;
	TSharedRef<FFilterCategory> TransientCategory;

	TSharedPtr<FSequencerTrackFilterCollection> CommonFilters;
	TSharedPtr<FSequencerTrackFilterCollection> InternalFilters;

	TSharedRef<FSequencerTrackFilter_Text> TextFilter;
	TSharedRef<FSequencerTrackFilter_HideIsolate> HideIsolateFilter;
	TSharedRef<FSequencerTrackFilter_Level> LevelFilter;
	TSharedRef<FSequencerTrackFilter_Group> GroupFilter;
	TSharedRef<FSequencerTrackFilter_Selected> SelectedFilter;
	TSharedRef<FSequencerTrackFilter_Modified> ModifiedFilter;
	
	TArray<TSharedRef<FSequencerTrackFilter_CustomText>> CustomTextFilters;

	TSharedPtr<FSequencerTrackFilterMenu> FilterMenu;

	FSequencerFiltersChanged FiltersChangedEvent;

	FSequencerFilterData FilterData;

private:
	static int32 InstanceCount;

	/** Do not call directly! Should only be called by FilterNodes(). */
	bool FilterNodesRecursive(const bool bInHasActiveFilter, const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InStartNode);
};
