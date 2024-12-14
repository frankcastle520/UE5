// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"

#include "InsightsCore/Filter/ViewModels/Filters.h"

namespace UE::Insights
{

class TRACEINSIGHTSCORE_API FTimeFilterValueConverter : public IFilterValueConverter
{
public:
	virtual bool Convert(const FString& Input, double& Output, FText& OutError) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetHintText() const override;
};

} // namespace UE::Insights
