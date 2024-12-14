// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModels/OutlinerIndicators/IOutlinerIndicatorBuilder.h"

namespace UE::Sequencer
{

class FOutlinerIndicatorBuilderBase
	: public IOutlinerIndicatorBuilder
{
public:

	FOutlinerIndicatorBuilderBase()
	{
	}
	
public:

	bool IsItemCompatibleWithIndicator(const FCreateOutlinerColumnParams& InParams) const override { return false; }
	TSharedPtr<SWidget> CreateIndicatorWidget(const FCreateOutlinerColumnParams& InParams, const TSharedRef<ISequencerTreeViewRow>& TreeViewRow, const TSharedRef<IOutlinerColumn>& OutlinerColumn, const int32 NumCompatibleIndicators) override { return nullptr; }
};

} // namespace UE::Sequencer