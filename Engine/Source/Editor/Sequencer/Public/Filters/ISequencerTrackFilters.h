// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/UICommandList.h"

class FSequencerTextFilterExpressionContext;
class FSequencerTrackFilter_CustomText;
class FSequencer;
class FString;
class FText;
struct FSequencerFilterData;

class ISequencerTrackFilters
{
public:
	virtual ~ISequencerTrackFilters() = default;

	SEQUENCER_API virtual FName GetIdentifier() const = 0;

	SEQUENCER_API virtual FSequencer& GetSequencer() const = 0;

	SEQUENCER_API virtual TSharedPtr<FUICommandList> GetCommandList() const = 0;

	SEQUENCER_API virtual FString GetTextFilterString() const = 0;
	SEQUENCER_API virtual void SetTextFilterString(const FString& InText) = 0;

	/** Returns true if the current filter bar text filter string contains the specified text expression.
	 * The text expression must have key, operator, and value tokens. */
	SEQUENCER_API virtual bool DoesTextFilterStringContainExpressionPair(const FSequencerTextFilterExpressionContext& InExpression) const = 0;

	SEQUENCER_API virtual bool HasAnyFilterActive(const bool bCheckTextFilter = true
		, const bool bInCheckHideIsolateFilter = true
		, const bool bInCheckCommonFilters = true
		, const bool bInCheckInternalFilters = true
		, const bool bInCheckCustomTextFilters = true) const = 0;

	SEQUENCER_API virtual bool HasAnyFilterEnabled() const = 0;

	SEQUENCER_API virtual void RequestFilterUpdate() = 0;

	SEQUENCER_API virtual bool AddCustomTextFilter(const TSharedRef<FSequencerTrackFilter_CustomText>& InFilter, const bool bInAddToConfig) = 0;
	SEQUENCER_API virtual bool RemoveCustomTextFilter(const TSharedRef<FSequencerTrackFilter_CustomText>& InFilter, const bool bInAddToConfig) = 0;

	SEQUENCER_API virtual void EnableAllFilters(const bool bInEnable, const TArray<FString> InExceptionFilterNames) = 0;

	SEQUENCER_API virtual void ActivateCommonFilters(const bool bInActivate, const TArray<FString> InExceptionFilterNames) = 0;

	SEQUENCER_API virtual void ActivateAllEnabledFilters(const bool bInActivate, const TArray<FString> InExceptionFilterNames) = 0;

	SEQUENCER_API virtual bool IsFilterActiveByDisplayName(const FString InFilterName) const = 0;
	SEQUENCER_API virtual bool IsFilterEnabledByDisplayName(const FString InFilterName) const = 0;
	SEQUENCER_API virtual bool SetFilterActiveByDisplayName(const FString InFilterName, const bool bInActive, const bool bInRequestFilterUpdate = true) = 0;
	SEQUENCER_API virtual bool SetFilterEnabledByDisplayName(const FString InFilterName, const bool bInEnabled, const bool bInRequestFilterUpdate = true) = 0;

	SEQUENCER_API virtual TArray<FText> GetFilterDisplayNames() const = 0;
	SEQUENCER_API virtual TArray<FText> GetCustomTextFilterNames() const = 0;

	SEQUENCER_API virtual int32 GetTotalDisplayNodeCount() const = 0;
	SEQUENCER_API virtual int32 GetFilteredDisplayNodeCount() const  = 0;

	SEQUENCER_API virtual void HideSelectedTracks() = 0;
	SEQUENCER_API virtual void IsolateSelectedTracks() = 0;

	SEQUENCER_API virtual void ShowOnlyLocationCategoryGroups() = 0;
	SEQUENCER_API virtual void ShowOnlyRotationCategoryGroups() = 0;
	SEQUENCER_API virtual void ShowOnlyScaleCategoryGroups() = 0;

	SEQUENCER_API virtual bool HasSelectedTracks() const = 0;

	SEQUENCER_API virtual FSequencerFilterData& GetFilterData() = 0;
};
