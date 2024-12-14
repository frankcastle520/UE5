// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SequencerTrackFilterBase.h"
#include "Filters/Widgets/SSequencerFilter.h"
#include "SequencerFilterBarContext.h"
#include "Templates/SharedPointer.h"
#include "SequencerFilterMenuContext.generated.h"

UCLASS()
class USequencerFilterMenuContext : public UObject
{
	GENERATED_BODY()

public:
	void Init(const TWeakPtr<SSequencerFilter>& InWeakFilterWidget)
	{
		WeakFilterWidget = InWeakFilterWidget;
	}

	TSharedPtr<SSequencerFilter> GetFilterWidget() const
	{
		return WeakFilterWidget.Pin();
	}

	TSharedPtr<FSequencerTrackFilter> GetFilter() const
	{
		return WeakFilterWidget.IsValid() ? WeakFilterWidget.Pin()->GetFilter() : nullptr;
	}

	FOnPopulateFilterBarMenu OnPopulateFilterBarMenu;

protected:
	TWeakPtr<SSequencerFilter> WeakFilterWidget;
};
