// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SBasicFilterBar.h"
#include "Filters/SequencerTrackFilterBase.h"

class FSequencer;
class FSequencerFilterBarContextMenu;
class FSequencerFilterBar;
class FSequencerTrackFilter_CustomText;
class SFilterBarClippingHorizontalBox;
class SFilterExpressionHelpDialog;
class SSequencerFilter;
class UMovieSceneNodeGroup;
enum class ESequencerFilterChange : uint8;

class SSequencerFilterBar : public SCompoundWidget
{
public:
	/** Delegate for when filters have changed */
	DECLARE_DELEGATE(FOnFilterChanged);

	/** Delegate to create a TTextFilter used to compare FilterType with text queries */
	DECLARE_DELEGATE_RetVal(TSharedPtr<FSequencerTrackFilter_CustomText>, FCreateTextFilter);

	DECLARE_DELEGATE_OneParam(FOnFilterBarLayoutChanging, EFilterBarLayout /*InNewLayout*/);

	DECLARE_EVENT(FSequencerFilterBar, FSequencerFiltersChanged);

	DECLARE_EVENT_OneParam(FSequencerFilterBar, FExternalCustomTextFilterEvent, const TSharedRef<FSequencerFilterBar>& /*InBroadcastingFilterBar*/);

	SLATE_BEGIN_ARGS(SSequencerFilterBar)
		: _FilterBarLayout(EFilterBarLayout::Vertical)
		, _CanChangeOrientation(true)
		, _FilterPillStyle(EFilterPillStyle::Default)
		, _UseSectionsForCategories(true)
	{}
		/** An SFilterSearchBox that can be attached to this filter bar. When provided along with a CreateTextFilter
		 *  delegate, allows the user to save searches from the Search Box as text filters for the filter bar.
		 *	NOTE: Will bind a delegate to SFilterSearchBox::OnClickedAddSearchHistoryButton */
		SLATE_ARGUMENT(TSharedPtr<SFilterSearchBox>, FilterSearchBox)

		/** The layout that determines how the filters are laid out */
		SLATE_ARGUMENT(EFilterBarLayout, FilterBarLayout)

		/** If true, allow dynamically changing the orientation and saving in the config */
		SLATE_ARGUMENT(bool, CanChangeOrientation)

		/** Determines how each individual filter pill looks like */
		SLATE_ARGUMENT(EFilterPillStyle, FilterPillStyle)

		/** Whether to use submenus or sections for categories in the filter menu */
		SLATE_ARGUMENT(bool, UseSectionsForCategories)

	SLATE_END_ARGS()

	virtual ~SSequencerFilterBar() override;

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, const TSharedRef<FSequencerFilterBar>& InFilterBar);

	//~ Begin SWidget
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	//~ End SWidget

	TSharedPtr<FSequencerFilterBar> GetFilterBar() const;

	void SetTextFilterString(const FString& InFilterString);

	FText GetFilterErrorText() const;

	EFilterBarLayout GetLayout() const;
	void SetLayout(const EFilterBarLayout InFilterBarLayout);

	void AttachFilterSearchBox(const TSharedPtr<SFilterSearchBox>& InFilterSearchBox);

	bool HasAnyFilterWidgets() const;

	void CreateAddCustomTextFilterWindowFromSearch(const FText& InSearchText);

	void OnOpenTextExpressionHelp();

	void SaveCurrentFilterSetAsCustomTextFilter();

	TWeakPtr<SFilterSearchBox> GetSearchBox() const;

	// Set the state of the filter bar. Muted means that the filters are muted, but the context menu is still enabled and accessible.
	void SetMuted(bool bInMuted);

protected:
	void AddWidgetToLayout(const TSharedRef<SWidget>& InWidget);
	void RemoveWidgetFromLayout(const TSharedRef<SWidget>& InWidget);

	TSharedPtr<SSequencerFilter> FindFilterWidget(const TSharedRef<FSequencerTrackFilter>& InFilter) const;

	void CreateAndAddFilterWidget(const TSharedRef<FSequencerTrackFilter>& InFilter);
	void AddFilterWidget(const TSharedRef<SSequencerFilter>& InFilterWidget);

	void RemoveFilterWidget(const TSharedRef<FSequencerTrackFilter>& InFilter, const bool ExecuteOnFilterChanged = true);
	void RemoveFilterWidget(const TSharedRef<SSequencerFilter>& InFilterWidget);

	UWorld* GetWorld() const;

	void RemoveAllFilterWidgets();
	void RemoveAllFilterWidgetsButThis(const TSharedRef<SSequencerFilter>& InFilterWidget);
	void RemoveFilterWidgetAndUpdate(const TSharedRef<SSequencerFilter>& InFilterWidget);

	void OnEnableAllGroupFilters(bool bEnableAll);
	void OnNodeGroupFilterClicked(UMovieSceneNodeGroup* NodeGroup);

	void OnFiltersChanged(const ESequencerFilterChange InChangeType, const TSharedRef<FSequencerTrackFilter>& InFilter);

	void CreateFilterWidgetsFromConfig();

	TSharedRef<SWidget> OnWrapButtonClicked();

	TWeakPtr<FSequencerFilterBar> WeakFilterBar;

	TWeakPtr<SFilterSearchBox> WeakSearchBox;

	TSharedPtr<SWidgetSwitcher> FilterBoxWidget;
	TSharedPtr<SFilterBarClippingHorizontalBox> HorizontalContainerWidget;
	TSharedPtr<SScrollBox> VerticalContainerWidget;

	EFilterBarLayout FilterBarLayout = EFilterBarLayout::Vertical;
	bool bCanChangeOrientation = true;
	EFilterPillStyle FilterPillStyle = EFilterPillStyle::Default;

	TArray<TSharedRef<SSequencerFilter>> FilterWidgets;

	TSharedPtr<SFilterExpressionHelpDialog> TextExpressionHelpDialog;

	TSharedPtr<FSequencerFilterBarContextMenu> ContextMenu;
};
