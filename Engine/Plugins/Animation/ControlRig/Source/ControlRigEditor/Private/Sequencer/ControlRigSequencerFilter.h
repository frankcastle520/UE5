// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SequencerTrackFilterExtension.h"
#include "ControlRigSequencerFilter.generated.h"

UCLASS()
class UControlRigTrackFilter : public USequencerTrackFilterExtension
{
public:
	GENERATED_BODY()

	//~ Begin USequencerTrackFilterExtension
	virtual void AddTrackFilterExtensions(ISequencerTrackFilters& InOutFilterInterface
		, const TSharedRef<FFilterCategory>& InPreferredCategory
		, TArray<TSharedRef<FSequencerTrackFilter>>& InOutFilterList) const override;
	//~ End USequencerTrackFilterExtension
};