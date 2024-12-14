// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace UE::Insights
{

class FFilterContext;

class TRACEINSIGHTSCORE_API IFilterExecutor
{
public:
	virtual bool ApplyFilters(const class FFilterContext& Context) const = 0;
};

} // namespace UE::Insights
