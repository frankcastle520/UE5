// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// TraceInsightsCore
#include "InsightsCore/Filter/ViewModels/Filters.h"

namespace UE::Insights::MemoryProfiler
{

class FThreadFilterValueConverter : public IFilterValueConverter
{
public:
	virtual bool Convert(const FString& Input, int64& Output, FText& OutError) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetHintText() const override;
};

} // namespace UE::Insights::MemoryProfiler
